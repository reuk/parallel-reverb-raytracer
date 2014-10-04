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

//#define USE_OBJECT_MATERIALS

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

vector <cl_float3> flattenImpulses 
(   const vector <Impulse> & impulse
,   float samplerate
) throw()
{
    const float MAX_TIME = max_element (impulse.begin(), impulse.end(), LatestImpulse())->time;
    const unsigned long MAX_SAMPLE = round (MAX_TIME * samplerate);
    
    vector <cl_float3> flattened (MAX_SAMPLE, (cl_float3) {0});

    for_each 
    (   impulse.begin()
    ,   impulse.end()
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
    
    float max = 0;
    for (int i = 0; i != ret.size(); ++i)
    {
        for (int j = 0; j != ret [i].size(); ++j)
        {
            const float F = fabs (ret [i] [j]);
            if (F > max)
            {
                max = F;
            }
        }
    }
    
    for (int i = 0; i != ret.size(); ++i)
    {
        for (int j = 0; j != ret [i].size(); ++j)
        {
            ret [i] [j] = (ret [i] [j] / max) * 0.99;
        }
    }
    
    return ret;
}

Scene::Scene 
(   cl::Context & cl_context
,   unsigned long nrays
,   unsigned long nreflections
,   vector <cl_float3> & directions
,   vector <Triangle> & triangles
,   vector <cl_float3> & vertices
,   vector <Surface> & surfaces
,   unsigned long nsources
)
:   nrays (nrays)
,   nreflections (nreflections)
,   ntriangles (triangles.size())
,   cl_context (cl_context)
,   cl_directions (cl_context, begin (directions), end (directions), false)
,   cl_triangles  (cl_context, begin (triangles),  end (triangles),  false)
,   cl_vertices   (cl_context, begin (vertices),   end (vertices),   false)
,   cl_surfaces   (cl_context, begin (surfaces),   end (surfaces),   false)
,   cl_spheres    (cl_context, CL_MEM_READ_WRITE, nsources * sizeof (Sphere))
,   cl_impulses   (cl_context, CL_MEM_READ_WRITE, nrays * nreflections * sizeof (Impulse))
,   cl_attenuated (cl_context, CL_MEM_READ_WRITE, nrays * nreflections * sizeof (Impulse))
{
    ifstream cl_source_file ("kernel.cl");
    string cl_source_string 
    (   (istreambuf_iterator <char> (cl_source_file))
    ,   istreambuf_iterator <char> ()
    );
    
    cl_program = cl::Program (cl_context, cl_source_string, true);
}

Scene::Scene
(   cl::Context & cl_context
,   unsigned long nrays
,   unsigned long nreflections
,   std::vector <cl_float3> & directions
,   SceneData sceneData
,   unsigned long nsources 
)
:   Scene
(   cl_context
,   nrays
,   nreflections
,   directions
,   sceneData.triangles
,   sceneData.vertices
,   sceneData.surfaces
,   nsources
)
{

}

Scene::SceneData::SceneData (const std::string & objpath)
{
    Assimp::Importer importer;
    const aiScene * scene = importer.ReadFile 
    (   objpath
    ,   aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_FlipUVs
    );
    
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
        const aiMaterial * material = scene->mMaterials [mesh->mMaterialIndex];

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
            (cl_float3) {0.95, 0.8, 0.75, 0},
            (cl_float3) {0.95, 0.8, 0.75, 0}
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
            
        vertices.insert (vertices.end(), meshVertices.begin(), meshVertices.end());
        triangles.insert (triangles.end(), meshTriangles.begin(), meshTriangles.end());
    }
}

Scene::Scene
(   cl::Context & cl_context
,   unsigned long nrays
,   unsigned long nreflections
,   std::vector <cl_float3> & directions
,   const std::string & objpath
,   unsigned long nsources
)
:   Scene
(   cl_context
,   nrays   
,   nreflections
,   directions
,   SceneData (objpath)
,   nsources
)
{

}

vector <vector <Impulse>> Scene::trace 
(   const cl_float3 & micpos
,   vector <Sphere> & spheres
,   const vector <Speaker> & speakers
) const
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
    
    auto attenuate = cl::make_kernel 
    <   cl::Buffer
    ,   cl::Buffer
    ,   unsigned long
    ,   cl::Buffer
    ,   Speaker
    > (cl_program, "attenuate");

    vector <cl::Device> device = cl_context.getInfo <CL_CONTEXT_DEVICES>();
    cl::Device used_device = device.back();
    cl::CommandQueue queue (cl_context, used_device);

    raytrace 
    (   cl::EnqueueArgs (queue, cl::NDRange (nrays))
    ,   cl_directions
    ,   micpos
    ,   cl_triangles
    ,   ntriangles
    ,   cl_vertices
    ,   cl_spheres
    ,   cl_surfaces
    ,   cl_impulses
    ,   nreflections
    );

    vector <vector <Impulse>> attenuated
    (   speakers.size()
    ,   vector <Impulse> (nreflections * nrays)
    );

    for (int i = 0; i != speakers.size(); ++i)
    {
        attenuate 
        (   cl::EnqueueArgs (queue, cl::NDRange (nrays))
        ,   cl_impulses
        ,   cl_attenuated
        ,   nreflections
        ,   cl_directions
        ,   speakers [i]
        );

        cl::copy 
        (   queue
        ,   cl_attenuated
        ,   attenuated [i].begin()
        ,   attenuated [i].end()
        );
    }

    return attenuated;
}
