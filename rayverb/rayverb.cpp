#include "rayverb.h"

#include "assimp/Importer.hpp"
#include "assimp/postprocess.h"
#include "assimp/scene.h"

#define VECTORISE

#ifdef VECTORISE
#include <x86intrin.h>
#endif

#include <cmath>
#include <numeric>
#include <fstream>
#include <streambuf>
#include <iostream>

#define USE_OBJECT_MATERIALS

using namespace std;

cl_float3 fromAIVec (const aiVector3D & v)
{
    return (cl_float3) {v.x, v.y, v.z, 0};
}

struct LatestImpulse: public binary_function <Impulse, Impulse, bool>
{
    inline bool operator() (const Impulse & a, const Impulse & b) const throw()
    {
        return a.time < b.time;
    }
};

cl_float3 sum (const cl_float3 & a, const cl_float3 & b) throw()
{
    return (cl_float3) 
    {   a.s [0] + b.s [0]
    ,   a.s [1] + b.s [1]
    ,   a.s [2] + b.s [2]
    ,   0
    };
}

vector <cl_float3> flattenImpulses 
(   const vector <Impulse> & impulse
,   float samplerate
) throw()
{
    const float MAX_TIME = max_element 
    (   begin (impulse)
    ,   end (impulse)
    ,   LatestImpulse()
    )->time;
    const unsigned long MAX_SAMPLE = round (MAX_TIME * samplerate);
    
    vector <cl_float3> flattened (MAX_SAMPLE, (cl_float3) {0});

    for (const auto & i : impulse)
    {
        const unsigned long SAMPLE = round (i.time * samplerate);
        flattened [SAMPLE] = sum (flattened [SAMPLE], i.volume);
    }

    return flattened;
}

void lopass 
(   vector <cl_float3> & data
,   float cutoff
,   float sr
,   int index
) throw()
{
    const float param = 1 - exp (-2 * M_PI * cutoff / sr);
    float state = 0;

    for (auto & i : data)
        i.s [index] = state += param * (i.s [index] - state);
}

void hipass 
(   vector <cl_float3> & data
,   float cutoff
,   float sr
,   int index
) throw()
{
    const float param = 1 - exp (-2 * M_PI * cutoff / sr);
    float state = 0;

    for (auto & i : data)
        i.s [index] -= state += param * (i.s [index] - state);
}

void bandpass 
(   vector <cl_float3> & data
,   float lo
,   float hi
,   float sr
,   int index
) throw()
{
    lopass (data, hi, sr, index);
    hipass (data, lo, sr, index);
}

void filter (vector <cl_float3> & data, float lo, float hi, float sr) throw()
{
#ifdef VECTORISE
    const float loParam = 1 - exp (-2 * M_PI * lo / sr);
    const float hiParam = 1 - exp (-2 * M_PI * hi / sr);
    
    __m128 state = {0};
    __m128 params = {loParam, hiParam, loParam, hiParam};

    for (auto & i : data)
    {
        __m128 in = {i.s [0], i.s [1], i.s [1], i.s [2]};
        __m128 t0 = _mm_sub_ps (in, state);
        __m128 t1 = _mm_mul_ps (t0, params);
        state = _mm_add_ps (t1, state);
        
        i.s [0] = state [0];
        i.s [1] = state [1];
        i.s [1] -= state [2];
        i.s [2] -= state [3];
    }
#else
    lopass (data, lo, sr, 0);
    bandpass (data, lo, hi, sr, 1);
    hipass (data, hi, sr, 2);
#endif
}

void hipass (vector <float> & data, float lo, float sr) throw()
{
    const float loParam = 1 - exp (-2 * M_PI * lo / sr);
    float loState = 0;

    for (auto & i : data)
        i -= loState += loParam * (i - loState);
}

struct Sum: public unary_function <cl_float3, float>
{
    inline float operator() (const cl_float3 & i) const
    {
        return accumulate (i.s, i.s + 3, 0.0);
    }
};

vector <float> sum (const vector <cl_float3> & data) throw()
{
    vector <float> ret (data.size());
    transform (begin (data), end (data), begin (ret), Sum());
    return ret;
}

vector <vector <float>> process 
(   vector <vector <cl_float3>> & data
,   float sr
) throw()
{
    vector <vector <float>> ret (data.size());
    
    for (int i = 0; i != data.size(); ++i)
    {
        filter (data [i], 200, 2000, sr);
        ret [i] = sum (data [i]);
        hipass (ret [i], 20, sr);
    }
    
    normalize (ret);
    
    return ret;
}

Scene::Scene 
(   cl::Context & cl_context
,   unsigned long nreflections
,   vector <cl_float3> & directions
,   vector <Triangle> & triangles
,   vector <cl_float3> & vertices
,   vector <Surface> & surfaces
,   bool verbose 
)
:   nrays (directions.size())
,   nreflections (nreflections)
,   ntriangles (triangles.size())
,   cl_context (cl_context)
,   cl_directions (cl_context, begin (directions), end (directions), false)
,   cl_triangles  (cl_context, begin (triangles),  end (triangles),  false)
,   cl_vertices   (cl_context, begin (vertices),   end (vertices),   false)
,   cl_surfaces   (cl_context, begin (surfaces),   end (surfaces),   false)
,   cl_sphere     (cl_context, CL_MEM_READ_WRITE, sizeof (Sphere))
,   cl_impulses   
    (   cl_context
    ,   CL_MEM_READ_WRITE
    ,   directions.size() * nreflections * sizeof (Impulse)
    )
,   cl_attenuated 
    (   cl_context
    ,   CL_MEM_READ_WRITE
    ,   directions.size() * nreflections * sizeof (Impulse)
    )
{
    ifstream cl_source_file ("kernel.cl");
    string cl_source_string 
    (   (istreambuf_iterator <char> (cl_source_file))
    ,   istreambuf_iterator <char> ()
    );

    cl_program = cl::Program (cl_context, cl_source_string, true);

    vector <cl::Device> device = cl_context.getInfo <CL_CONTEXT_DEVICES>();

    cl::Device used_device = device.back();
    
    cerr 
    <<  cl_program.getBuildInfo <CL_PROGRAM_BUILD_LOG> (used_device) 
    << endl;

    queue = cl::CommandQueue (cl_context, used_device);
}

SceneData::SceneData (const std::string & objpath)
{
    populate (objpath);
}

void SceneData::populate (const aiScene * scene)
{
    if (! scene)
        throw runtime_error ("Failed to load object file.");
    
    for (unsigned long i = 0; i != scene->mNumMeshes; ++i)
    {
        const aiMesh * mesh = scene->mMeshes [i];
        
        vector <cl_float3> meshVertices (mesh->mNumVertices);
        
        for (unsigned long j = 0; j != mesh->mNumVertices; ++j)
        {
            meshVertices [j] = fromAIVec (mesh->mVertices [j]);
        }
        
#ifdef USE_OBJECT_MATERIALS
        const aiMaterial * material = scene->mMaterials 
        [   mesh->mMaterialIndex
        ];

        aiColor3D diffuse (0.9, 0.8, 0.7);
        material->Get(AI_MATKEY_COLOR_DIFFUSE, diffuse);
        
        aiColor3D specular (0.9, 0.8, 0.7);
        material->Get(AI_MATKEY_COLOR_SPECULAR, specular);
        
        Surface surface = {
            (cl_float3) {specular.r, specular.g, specular.b, 0},
            (cl_float3) {diffuse.r, diffuse.g, diffuse.b, 0}
        };
#else
        Surface surface = {
            (cl_float3) {0.95, 0.85, 0.75, 0},
            (cl_float3) {0.95, 0.85, 0.75, 0}
        };
#endif
        
        surfaces.push_back (surface);
        
        vector <Triangle> meshTriangles (mesh->mNumFaces);
        
        for (unsigned long j = 0; j != mesh->mNumFaces; ++j)
        {
            const aiFace face = mesh->mFaces [j];
            
            meshTriangles [j] = (Triangle) {
                surfaces.size() - 1,
                vertices.size() + face.mIndices [0],
                vertices.size() + face.mIndices [1],
                vertices.size() + face.mIndices [2]
            };
        }
            
        vertices.insert 
        (   vertices.end()
        ,   begin (meshVertices)
        ,   end (meshVertices)
        );

        triangles.insert 
        (   triangles.end()
        ,   begin (meshTriangles)
        ,   end (meshTriangles)
        );
    }
}

void SceneData::populate (const std::string & objpath)
{
    Assimp::Importer importer;
    populate 
    (   importer.ReadFile 
        (   objpath
        ,   (   aiProcess_Triangulate 
            |   aiProcess_GenSmoothNormals 
            |   aiProcess_FlipUVs
            )
        )
    );
}

bool SceneData::validSurfaces() const
{
    for (const Surface & s : surfaces)
    {
        for (int i = 0; i != 3; ++i)
        {
            if 
            (   s.specular.s [i] < 0 || 1 < s.specular.s [i]
            ||  s.diffuse.s [i] < 0 || 1 < s.diffuse.s [i]
            )
                return false;
        }
    }

    return true;
}

bool SceneData::validTriangles() const
{
    for (const Triangle & t : triangles)
    {
        if 
        (   surfaces.size() <= t.surface
        ||  vertices.size() <= t.v0
        ||  vertices.size() <= t.v1
        ||  vertices.size() <= t.v2
        )
            return false;
    }

    return true;
}

bool SceneData::valid() const
{
    if (triangles.empty() || vertices.empty() || surfaces.empty())
        return false;

    return validSurfaces() && validTriangles();
}

inline std::vector <Triangle> & SceneData::getTriangles() { return triangles; }
inline std::vector <cl_float3> & SceneData::getVertices() { return vertices; }
inline std::vector <Surface> & SceneData::getSurfaces() { return surfaces; }

Scene::Scene
(   cl::Context & cl_context
,   unsigned long nreflections
,   std::vector <cl_float3> & directions
,   SceneData sceneData
,   bool verbose 
)
:   Scene
(   cl_context
,   nreflections
,   directions
,   sceneData.getTriangles()
,   sceneData.getVertices()
,   sceneData.getSurfaces()
)
{
}

Scene::Scene
(   cl::Context & cl_context
,   unsigned long nreflections
,   std::vector <cl_float3> & directions
,   const std::string & objpath
,   bool verbose 
)
:   Scene
(   cl_context
,   nreflections
,   directions
,   SceneData (objpath)
)
{
}

#ifdef DIAGNOSTIC
vector <Reflection> Scene::test (const cl_float3 & micpos, Sphere source)
{
    auto test = cl::make_kernel 
    <   cl::Buffer
    ,   cl_float3
    ,   cl::Buffer
    ,   unsigned long
    ,   cl::Buffer
    ,   cl::Buffer
    ,   cl::Buffer
    ,   cl::Buffer
    ,   unsigned long
    > (cl_program, "test");

    Sphere * sp = &source;

    cl::copy
    (   queue
    ,   sp
    ,   sp + 1
    ,   cl_sphere
    );

    cl_reflections = cl::Buffer 
    (   cl_context
    ,   CL_MEM_READ_WRITE
    ,   nrays * nreflections * sizeof (Reflection)
    );

    test
    (   cl::EnqueueArgs (queue, cl::NDRange (nrays))
    ,   cl_directions
    ,   micpos
    ,   cl_triangles
    ,   ntriangles
    ,   cl_vertices
    ,   cl_sphere
    ,   cl_surfaces
    ,   cl_reflections
    ,   nreflections
    );

    vector <Reflection> reflections (nrays * nreflections);

    cl::copy 
    (   queue
    ,   cl_reflections
    ,   begin (reflections)
    ,   end (reflections)
    );

    return reflections;
}
#endif

void Scene::trace (const cl_float3 & micpos, Sphere source)
{
    auto raytrace = cl::make_kernel 
    <   cl::Buffer
    ,   cl_float3
    ,   cl::Buffer
    ,   unsigned long
    ,   cl::Buffer
    ,   cl::Buffer
    ,   cl::Buffer
    ,   cl::Buffer
    ,   unsigned long
    > (cl_program, "raytrace");

    Sphere * sp = &source;

    cl::copy
    (   queue
    ,   sp
    ,   sp + 1
    ,   cl_sphere
    );

    raytrace 
    (   cl::EnqueueArgs (queue, cl::NDRange (nrays))
    ,   cl_directions
    ,   micpos
    ,   cl_triangles
    ,   ntriangles
    ,   cl_vertices
    ,   cl_sphere
    ,   cl_surfaces
    ,   cl_impulses
    ,   nreflections
    );
}

vector <Impulse> Scene::attenuate (const Speaker & speaker)
{
    auto attenuate = cl::make_kernel 
    <   cl::Buffer
    ,   cl::Buffer
    ,   unsigned long
    ,   cl::Buffer
    ,   Speaker
    > (cl_program, "attenuate");

    attenuate 
    (   cl::EnqueueArgs (queue, cl::NDRange (nrays))
    ,   cl_impulses
    ,   cl_attenuated
    ,   nreflections
    ,   cl_directions
    ,   speaker
    );

    vector <Impulse> attenuated (nreflections * nrays);

    cl::copy 
    (   queue
    ,   cl_attenuated
    ,   begin (attenuated)
    ,   end (attenuated)
    );

    return attenuated;
}

vector <vector <Impulse>> Scene::attenuate (const vector <Speaker> & speakers)
{
    vector <vector <Impulse>> attenuated;
    for (const Speaker & s : speakers)
        attenuated.push_back (attenuate (s));
    return attenuated;
}

BVH::BVH (SceneData sceneData)
:   triangles (sceneData.getTriangles())
,   vertices (sceneData.getVertices())
,   surfaces (sceneData.getSurfaces())
{
}

BVH::BVH
(   std::vector <Triangle> & triangles
,   std::vector <cl_float3> & vertices
,   std::vector <Surface> & surfaces
)
:   triangles (triangles)
,   vertices (vertices)
,   surfaces (surfaces)
{
}

inline void minMaxFloat3 (AABB & aabb, const cl_float3 & f)
{
    for (int i = 0; i != 4; ++i)
    {
        aabb.min.s [i] = min (aabb.min.s [i], f.s [i]);
        aabb.max.s [i] = max (aabb.max.s [i], f.s [i]);
    }
}

struct MinMaxFloat3: public binary_function <AABB, cl_float3, AABB>
{
    inline AABB operator() (const AABB & aabb, const cl_float3 & f) const
    {
        AABB ret = aabb;
        minMaxFloat3 (ret, f);
        return ret;
    }
};

struct MinMaxAABB: public binary_function <AABB, AABB, AABB>
{
    inline AABB operator() (const AABB & a, const AABB & b) const
    {
        AABB ret;
        for (int i = 0; i != 4; ++i)
        {
            ret.min.s [i] = min (a.min.s [i], b.min.s [i]);
            ret.max.s [i] = max (a.max.s [i], b.max.s [i]);
        }
        return ret;
    }
};

template <typename T>
struct GetAABB: public unary_function <T, AABB>
{
    GetAABB (const vector <cl_float3> & vertices)
    :   vertices (vertices)
    {
    }

    inline AABB operator() (const T & t) const
    {
        return getAABB (t, vertices);
    }

private:
    const vector <cl_float3> & vertices;
};

template <typename T>
inline AABB getAABB 
(   const T & t
,   const vector <cl_float3> & vertices
)
{
    vector <AABB> aabb (t.size());
    transform (t.begin(), t.end(), aabb.begin(), GetAABB <decltype(t.front())> (vertices));

    return accumulate 
    (   aabb.begin() + 1
    ,   aabb.end()
    ,   aabb.front()
    ,   MinMaxAABB()
    );
}

template<>
inline AABB getAABB 
(   const Triangle & t
,   const vector <cl_float3> & vertices
)
{
    std::vector <cl_float3> v = 
    {   vertices [t.v0]
    ,   vertices [t.v1]
    ,   vertices [t.v2]
    };

    return accumulate 
    (   v.begin() + 1
    ,   v.end()
    ,   (AABB) {v.front(), v.front()}
    ,   MinMaxFloat3()
    );
}

template<>
inline AABB getAABB
(   const AABB & aabb
,   const vector <cl_float3> & vertices
)
{
    return aabb;
}

cl_float3 centroid (const Triangle & t, const vector <cl_float3> & v)
{
    cl_float3 f = sum (v [t.v0], sum (v [t.v1], v [t.v2]));
    for (int i = 0; i != 3; ++i)
        f.s [i] /= 3;
    return f;
}

uint32_t BVH::keyForCell (const cl_uint3 & outer, const cl_uint3 & inner)
{
    uint32_t outer_key = 0;
    uint32_t inner_key = 0;
    for (int i = 0; i != 3; ++i)
    {
        outer_key |= (outer.s [i] & (OUT_DIVISIONS - 1)) << (OUT_BITS * i);
        inner_key |= (inner.s [i] & (IN_DIVISIONS - 1)) << (IN_BITS * i);
    }

    return (outer_key << (IN_BITS * 3)) | inner_key;
}

cl_uint3 BVH::outerCellForKey (uint32_t key)
{
    key >>= IN_BITS * 3;
    cl_uint3 ret;
    for (int i = 0; i != 3; ++i)
        ret.s [i] = (OUT_DIVISIONS - 1) & (key >> (OUT_BITS * i));
    return ret;
}

cl_uint3 BVH::innerCellForKey (uint32_t key)
{
    cl_uint3 ret;
    for (int i = 0; i != 3; ++i)
        ret.s [i] = (IN_DIVISIONS - 1) & (key >> (IN_BITS * i));
    return ret;
}

template <typename T>
void printvec (const string & title, const T & t)
{
    cout 
    <<  title 
    <<  ": " 
    <<  t.s [0] 
    <<  ", " 
    <<  t.s [1] 
    <<  ", " 
    <<  t.s [2] 
    <<  endl;
}

cl_uint3 BVH::grid0cell (const cl_float3 & pt, const AABB & aabb)
{
    uint32_t divisions = 1 << (IN_BITS + OUT_BITS);
    cl_uint3 ret;
    for (int i = 0; i != 3; ++i)
    {
        float diff = aabb.max.s [i] - aabb.min.s [i];
        float p = pt.s [i];
        ret.s [i] = floor (((p - aabb.min.s [i]) * divisions) / diff);
    }
    return ret;
}

struct CellRefPair
{
    uint32_t refID, cellID;
};

void BVH::build()
{
    AABB aabb = getAABB (triangles, vertices);
    printvec ("min", aabb.min);
    printvec ("max", aabb.max);

    //  categorise

    vector <CellRefPair> cellrefpair;
    for (uint32_t i = 0; i != triangles.size(); ++i)
    {
        cl_float3 c = centroid (triangles [i], vertices);
        cl_uint3 k = grid0cell (c, aabb);

        cl_uint3 outer, inner;
        for (int j = 0; j != 3; ++j)
        {
            outer.s [j] = k.s [j] / IN_DIVISIONS;
            inner.s [j] = k.s [j] % IN_DIVISIONS;
        }

        cellrefpair.push_back ((CellRefPair) {i, keyForCell (outer, inner)});
    }

    //  sort

    sort 
    (   begin (cellrefpair)
    ,   end (cellrefpair)
    ,   [&] 
        (   const CellRefPair & a
        ,   const CellRefPair & b
        ) 
        { 
            return a.cellID < b.cellID; 
        }
    );

    vector <uint32_t> refids (cellrefpair.size());
    transform 
    (   begin (cellrefpair)
    ,   end (cellrefpair)
    ,   begin (refids)
    ,   [&] (const CellRefPair & i)
        {
            return i.refID;
        }
    );

    const uint32_t outermask = ((1 << (OUT_BITS * 3)) - 1) << (IN_BITS * 3);

    vector <uint32_t> regionStarts (1, 0);
    uint32_t currentcell = cellrefpair.front().cellID & outermask;
    for (uint32_t i = 0; i != cellrefpair.size(); ++i)
    {
        uint32_t outercell = cellrefpair [i].cellID & outermask;
        if (outercell != currentcell)
        {
            currentcell = outercell;
            regionStarts.push_back (i);
        }
    }

    //  At this point I got to the end of page 699 in the Garanzha paper and 
    //  completely lost what was happening
}
