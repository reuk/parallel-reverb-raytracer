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

struct LatestImpulse: binary_function <Impulse, Impulse, bool>
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

std::vector <float> flattenCustomReflectionLevel
(   const vector <Impulse> & impulse
,   unsigned long REFLECTION_LEVEL
,   unsigned long NUM_IMPULSES
,   float samplerate
) throw()
{
    float maxtime = 0;

    for 
    (   unsigned long i = REFLECTION_LEVEL
    ;   i < impulse.size()
    ;   i += NUM_IMPULSES
    )
        maxtime = max (maxtime, impulse [i].time);

    const unsigned long MAX_SAMPLE = round (maxtime * samplerate) + 1;

    vector <float> flattened (MAX_SAMPLE, 0);

    for 
    (   unsigned long i = REFLECTION_LEVEL
    ;   i < impulse.size()
    ;   i += NUM_IMPULSES
    )
    {
        const unsigned long SAMPLE = round (impulse [i].time * samplerate);
        const float vol = impulse [i].volume.s [0];
        flattened [SAMPLE] += vol;
    }

    return flattened;
}

std::vector <float> flattenCustomImpulses 
(   const vector <Impulse> & impulse
,   unsigned long NUM_IMPULSES
,   const vector <vector <float>> & custom
,   float samplerate
) throw()
{
    const float MAX_TIME = max_element 
    (   begin (impulse)
    ,   end (impulse)
    ,   LatestImpulse()
    )->time;

    const unsigned long MAX_SAMPLE = round (MAX_TIME * samplerate) + custom.back().size() + 1;

    vector <float> flattened (MAX_SAMPLE, 0);

    for (unsigned long i = 0; i != NUM_IMPULSES; ++i)
    {
        vector <float> refi = flattenCustomReflectionLevel
        (   impulse
        ,   i
        ,   NUM_IMPULSES
        ,   samplerate
        );

        cout << i;

        vector <float> convolved = dconvolve (custom [i], refi);

        cout << " convolved";

        for (unsigned long j = 0; j != convolved.size(); ++j)
            flattened [j] += convolved [j];

        cout << endl;
    }

    return flattened;
}

vector <float> dconvolve (const vector <float> & a, vector <float> & b)
{
    while ((b.size() % 4) != 0)
        b.push_back (0);

    vector <float> ret (a.size() + b.size() - 1, 0);

    for (unsigned long i = 0; i != a.size(); ++i)
    {
        const float in = a [i];
        if (in != 0)
        {
#ifdef VECTORISE
            __m128 four = {in, in, in, in};
#endif
            for (unsigned long j = 0; j != b.size(); j += 4)
            {
#ifdef VECTORISE
                __m128 in = {b [j + 0], b [j + 1], b [j + 2], b [j + 3]};
                __m128 t0 = _mm_mul_ps (in, four);
                __m128 t1 = {ret [i + j + 0], ret [i + j + 1], ret [i + j + 2], ret [i + j + 3]};
                __m128 t2 = _mm_add_ps (t0, t1);

                for (int k = 0; k != 4; ++k)
                    ret [i + j + k] = t2 [k];
#else
                ret [i + j] += in * b [j];
#endif
            }
        }
    }

    return ret;
}


vector <cl_float3> flattenImpulses 
(   const vector <Impulse> & impulse
,   float samplerate
) throw()
{
    const float MAX_TIME = max_element (begin (impulse), end (impulse), LatestImpulse())->time;
    const unsigned long MAX_SAMPLE = round (MAX_TIME * samplerate);
    
    vector <cl_float3> flattened (MAX_SAMPLE, (cl_float3) {0});

    for_each 
    (   begin (impulse)
    ,   end (impulse)
    ,   [&] (const Impulse & i)
        {
            const unsigned long SAMPLE = round (i.time * samplerate);
            flattened [SAMPLE] = sum (flattened [SAMPLE], i.volume);
        }
    );

    return flattened;
}

void lopass (vector <cl_float3> & data, float cutoff, float sr, int index) throw()
{
    const float param = 1 - exp (-2 * M_PI * cutoff / sr);
    float state = 0;
    
    for (auto i = begin (data); i != end (data); ++i)
    {
        i->s [index] = state += param * (i->s [index] - state);
    }
}

void hipass (vector <cl_float3> & data, float cutoff, float sr, int index) throw()
{
    const float param = 1 - exp (-2 * M_PI * cutoff / sr);
    float state = 0;
    
    for (auto i = begin (data); i != end (data); ++i)
    {
        i->s [index] -= state += param * (i->s [index] - state);
    }
}

void bandpass (vector <cl_float3> & data, float lo, float hi, float sr, int index) throw()
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
    
    for (auto i = begin (data); i != end (data); ++i)
    {
        __m128 in = {i->s [0], i->s [1], i->s [1], i->s [2]};
        __m128 t0 = _mm_sub_ps (in, state);
        __m128 t1 = _mm_mul_ps (t0, params);
        state = _mm_add_ps (t1, state);
        
        i->s [0] = state [0];
        i->s [1] = state [1];
        i->s [1] -= state [2];
        i->s [2] -= state [3];
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
    
    for (auto i = begin (data); i != end (data); ++i)
    {
        *i -= loState += loParam * (*i - loState);
    }
}

vector <float> sum (const vector <cl_float3> & data) throw()
{
    vector <float> ret (data.size());
    for (unsigned long i = 0; i != data.size(); ++i)
    {
        ret [i] = accumulate (data [i].s, data[i].s + 3, 0.0);
    }
    return ret;
}

vector <vector <float>> process (vector <vector <cl_float3>> & data, float sr) throw()
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

struct Scene::SceneData
{
public:
    SceneData (const std::string & objpath)
    {
        populate (objpath);
    }

    void populate (const aiScene * scene)
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

    void populate (const std::string & objpath)
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

    bool validSurfaces()
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

    bool validTriangles()
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

    bool valid()
    {
        if (triangles.empty() || vertices.empty() || surfaces.empty())
            return false;

        return validSurfaces() && validTriangles();
    }

    std::vector <Triangle> triangles;
    std::vector <cl_float3> vertices;
    std::vector <Surface> surfaces;
};

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
,   sceneData.triangles
,   sceneData.vertices
,   sceneData.surfaces
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

