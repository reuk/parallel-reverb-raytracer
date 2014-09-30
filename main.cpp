//
//  main.cpp
//  parallel_raytracer
//
//  Created by Reuben Thomas on 01/09/2014.
//  Copyright (c) 2014 Reuben Thomas. All rights reserved.
//

#include <iostream>
#include <fstream>
#include <streambuf>
#include <vector>
#include <random>
#include <algorithm>
#include <functional>

#include "assimp/Importer.hpp"
#include "assimp/postprocess.h"
#include "assimp/scene.h"

#include "sndfile.hh"

#define __CL_ENABLE_EXCEPTIONS
#include "cl.hpp"

//  These definitions MUST be kept up-to-date with the defs in the cl file.

typedef struct  {
    cl_ulong surface;
    cl_ulong v0;
    cl_ulong v1;
    cl_ulong v2;
} _Triangle_unalign;

typedef _Triangle_unalign __attribute__ ((aligned(8))) Triangle;

typedef struct  {
    cl_ulong surface;
    cl_float3 origin;
    cl_float radius;
} _Sphere_unalign;

typedef _Sphere_unalign __attribute__ ((aligned(8))) Sphere;

typedef struct  {
    cl_float3 specular;
    cl_float3 diffuse;
} _Surface_unalign;

typedef _Surface_unalign __attribute__ ((aligned(8))) Surface;

typedef struct  {
    cl_float3 volume;
    cl_float time;
} _Impulse_unalign;

typedef _Impulse_unalign __attribute__ ((aligned(8))) Impulse;

typedef struct  {
    cl_float3 direction;
    cl_float coefficient;
} _Speaker_unalign;

typedef _Speaker_unalign __attribute__ ((aligned(8))) Speaker;

#define VECTORISE

#ifdef VECTORISE
#include <x86intrin.h>
#endif

//#define USE_OBJECT_MATERIALS

std::vector <cl_float3> getDirections (unsigned long num)
{
    std::vector <cl_float3> ret (num);
    std::uniform_real_distribution <float> zDist (-1, 1);
    std::uniform_real_distribution <float> thetaDist (-M_PI, M_PI);
    std::default_random_engine engine;
    
    for (auto i = std::begin (ret); i != std::end (ret); ++i)
    {
        const float z = zDist (engine);
        const float theta = thetaDist (engine);
        const float ztemp = sqrtf (1 - z * z);
        *i = (cl_float3) {ztemp * cosf (theta), ztemp * sinf (theta), z, 0};
    }
    
    return ret;
}

cl_float3 fromAIVec (const aiVector3D & v)
{
    return (cl_float3) {v.x, v.y, v.z, 0};
}

struct LatestImpulse: std::binary_function <Impulse, Impulse, bool>
{
    inline bool operator() (const Impulse & a, const Impulse & b) const
    {
        return a.time < b.time;
    }
};

cl_float3 sum (const cl_float3 & a, const cl_float3 & b)
{
    return (cl_float3) 
    {   a.s [0] + b.s [0]
    ,   a.s [1] + b.s [1]
    ,   a.s [2] + b.s [2]
    ,   0
    };
}

std::vector <cl_float3> flattenImpulses 
(   const std::vector <Impulse> & impulse
,   float samplerate
)
{
    const float MAX_TIME = std::max_element (impulse.begin(), impulse.end(), LatestImpulse())->time;
    const unsigned long MAX_SAMPLE = round (MAX_TIME * samplerate);
    
    std::vector <cl_float3> flattened (MAX_SAMPLE, (cl_float3) {0});

    std::for_each (impulse.begin(), impulse.end(), [&] (const Impulse & i)
        {
            const unsigned long SAMPLE = round (i.time * samplerate);
            flattened [SAMPLE] = sum (flattened [SAMPLE], i.volume);
        }
    );

    return flattened;
}

void lopass (std::vector <cl_float3> & data, float cutoff, float sr, int index)
{
    const float param = 1 - exp (-2 * M_PI * cutoff / sr);
    float state = 0;
    
    for (auto i = std::begin (data); i != std::end (data); ++i)
    {
        i->s [index] = state += param * (i->s [index] - state);
    }
}

void hipass (std::vector <cl_float3> & data, float cutoff, float sr, int index)
{
    const float param = 1 - exp (-2 * M_PI * cutoff / sr);
    float state = 0;
    
    for (auto i = std::begin (data); i != std::end (data); ++i)
    {
        i->s [index] -= state += param * (i->s [index] - state);
    }
}

void bandpass (std::vector <cl_float3> & data, float lo, float hi, float sr, int index)
{
    lopass (data, hi, sr, index);
    hipass (data, lo, sr, index);
}

void filter (std::vector <cl_float3> & data, float lo, float hi, float sr)
{
#ifdef VECTORISE
    const float loParam = 1 - exp (-2 * M_PI * lo / sr);
    const float hiParam = 1 - exp (-2 * M_PI * hi / sr);
    
    __m128 state = {0};
    __m128 params = {loParam, hiParam, loParam, hiParam};
    
    for (auto i = std::begin (data); i != std::end (data); ++i)
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

void hipass (std::vector <float> & data, float lo, float sr)
{
    const float loParam = 1 - exp (-2 * M_PI * lo / sr);
    float loState = 0;
    
    for (auto i = std::begin (data); i != std::end (data); ++i)
    {
        *i -= loState += loParam * (*i - loState);
    }
}

std::vector <float> sum (const std::vector <cl_float3> & data)
{
    std::vector <float> ret (data.size());
    for (unsigned long i = 0; i != data.size(); ++i)
    {
        ret [i] = std::accumulate (data [i].s, data[i].s + 3, 0.0);
    }
    return ret;
}

std::vector <std::vector <float>> process (std::vector <std::vector <cl_float3>> & data, float sr)
{
    std::vector <std::vector <float>> ret (data.size());
    
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






int main(int argc, const char * argv[])
{
    Assimp::Importer importer;
    const aiScene * scene = importer.ReadFile ("/Users/reuben/Desktop/basic.obj",
                                               aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_FlipUVs);
    
    if (! scene)
    {
        std::cout << "Failed to load object file." << std::endl;
        return 1;
    }
    
    std::vector <Triangle> triangles;
    std::vector <cl_float3> vertices;
    std::vector <Surface> surfaces;
    
    for (unsigned long i = 0; i != scene->mNumMeshes; ++i)
    {
        const aiMesh * mesh = scene->mMeshes [i];
        
        std::vector <cl_float3> meshVertices (mesh->mNumVertices);
        
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
        
        std::vector <Triangle> meshTriangles (mesh->mNumFaces);
        
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
    
    const unsigned long NUM_RAYS = 1024;
    
    std::vector <cl_float3> directions = getDirections (NUM_RAYS);
    
    Surface surface = {
        (cl_float3) {1, 1, 1, 0},
        (cl_float3) {1, 1, 1, 0}
    };
    
    surfaces.push_back (surface);
    
    std::vector <Sphere> spheres;
    spheres.push_back ((Sphere) {surfaces.size() - 1, (cl_float3) {5, 5, 5, 0}, 1});
    
    std::vector <Speaker> speakers;
    speakers.push_back ((Speaker) {(cl_float3) {1, 0, 0}, 0.5});
    speakers.push_back ((Speaker) {(cl_float3) {0, 1, 0}, 0.5});
    
    const unsigned long NUM_IMPULSES = 128;

    //  results go in here
    //  so that they can escape from the exception-handling scope
    std::vector <std::vector <Impulse>> attenuated_impulses
    (   speakers.size()
    ,   std::vector <Impulse> (NUM_IMPULSES * NUM_RAYS)
    );
        
#ifdef __CL_ENABLE_EXCEPTIONS
    try
    {
#endif
        std::vector <cl::Platform> platform;
        cl::Platform::get (&platform);
        
        cl_context_properties cps [3] = {
            CL_CONTEXT_PLATFORM,
            (cl_context_properties) (platform [0]) (),
            0
        };
        
        cl::Context context (CL_DEVICE_TYPE_GPU, cps);
        
        std::vector <cl::Device> device = context.getInfo <CL_CONTEXT_DEVICES>();

        cl::Device used_device = device [1];

        std::cout << "device used: " << used_device.getInfo <CL_DEVICE_NAME>() << std::endl;
        
        cl::CommandQueue queue (context, used_device);
        
        cl::Buffer cl_directions    (context, std::begin (directions),  std::end (directions),  false);
        cl::Buffer cl_triangles     (context, std::begin (triangles),   std::end (triangles),   false);
        cl::Buffer cl_vertices      (context, std::begin (vertices),    std::end (vertices),    false);
        cl::Buffer cl_spheres       (context, std::begin (spheres),     std::end (spheres),     false);
        cl::Buffer cl_surfaces      (context, std::begin (surfaces),    std::end (surfaces),    false);
        
        const unsigned long IMPULSES_BYTES = NUM_IMPULSES * NUM_RAYS * sizeof (Impulse);

        cl::Buffer cl_impulses 
        (   context
        ,   CL_MEM_READ_WRITE
        ,   IMPULSES_BYTES
        );

        cl::Buffer cl_attenuated_impulses 
        (   context
        ,   CL_MEM_WRITE_ONLY
        ,   IMPULSES_BYTES
        );
        
        std::ifstream cl_source_file ("kernel.cl");
        std::string cl_source_string 
        (   (std::istreambuf_iterator <char> (cl_source_file))
        ,   std::istreambuf_iterator <char> ()
        );
        
        cl::Program program (context, cl_source_string, true);
        
        auto info = program.getBuildInfo <CL_PROGRAM_BUILD_LOG> (used_device);
        std::cout << "program build info:" << std::endl << info << std::endl;
        
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
        > (program, "raytrace");
        
        auto attenuate = cl::make_kernel 
        <   cl::Buffer
        ,   cl::Buffer
        ,   unsigned long
        ,   cl::Buffer
        ,   Speaker
        > (program, "attenuate");

        cl::EnqueueArgs (queue, cl::NDRange (NUM_RAYS));

        raytrace 
        (   cl::EnqueueArgs (queue, cl::NDRange (NUM_RAYS))
        ,   cl_directions
        ,   (cl_float3) {-5, -5, -5, 0}
        ,   cl_triangles
        ,   triangles.size()
        ,   cl_vertices
        ,   cl_spheres
        ,   cl_surfaces
        ,   cl_impulses
        ,   NUM_IMPULSES
        );

        for (int i = 0; i != speakers.size(); ++i)
        {
            attenuate 
            (   cl::EnqueueArgs (queue, cl::NDRange (NUM_RAYS))
            ,   cl_impulses
            ,   cl_attenuated_impulses
            ,   NUM_IMPULSES
            ,   cl_directions
            ,   speakers [i]
            );

            cl::copy 
            (   queue
            ,   cl_attenuated_impulses
            ,   attenuated_impulses [i].begin()
            ,   attenuated_impulses [i].end()
            );
        }
        
#ifdef __CL_ENABLE_EXCEPTIONS
    }
    catch (cl::Error error)
    {
        std::cout << "encountered opencl error:" << std::endl;
        std::cout << error.what() << std::endl;
        std::cout << error.err() << std::endl;
    }
#endif
    
    for (auto i = attenuated_impulses [0].begin(); i != attenuated_impulses [0].begin() + NUM_IMPULSES; ++i)
    {
        std::cout << i->time << std::endl;
    }

    const float SAMPLE_RATE = 44100;
    std::vector <std::vector <cl_float3>> flattened;
    for (int i = 0; i != speakers.size(); ++i)
    {
        flattened.push_back 
        (   flattenImpulses 
            (   attenuated_impulses [i]
            ,   SAMPLE_RATE
            )
        );
    }
    
    std::vector <std::vector <float>> outdata = process 
    (   flattened
    ,   SAMPLE_RATE
    );
    
    std::vector <float> interleaved (outdata.size() * outdata [0].size());
    
    for (int i = 0; i != outdata.size(); ++i)
    {
        for (int j = 0; j != outdata [i].size(); ++j)
        {
            interleaved [j * outdata.size() + i] = outdata [i] [j];
        }
    }
    
    SndfileHandle outfile ("para.aiff", SFM_WRITE, SF_FORMAT_AIFF | SF_FORMAT_PCM_16, outdata.size(), SAMPLE_RATE);
    outfile.write (interleaved.data(), interleaved.size());
    
    return 0;
}

