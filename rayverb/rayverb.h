#pragma once

//#define DIAGNOSTIC

#define __CL_ENABLE_EXCEPTIONS
#include "cl.hpp"

#include <vector>
#include <cmath>
#include <numeric>
#include <iostream>

//  These definitions MUST be kept up-to-date with the defs in the cl file.
//  It might make sense to nest them inside the Scene because I don't think
//  other classes will need the same data formats.
//
//  Unless I have a new class/kernel for constructing BVHs?

typedef cl_float8 VolumeType;

typedef struct  {
    cl_ulong surface;
    cl_ulong v0;
    cl_ulong v1;
    cl_ulong v2;
} _Triangle_unalign;

typedef _Triangle_unalign __attribute__ ((aligned(8))) Triangle;

typedef struct  {
    VolumeType specular;
    VolumeType diffuse;
} _Surface_unalign;

typedef _Surface_unalign __attribute__ ((aligned(8))) Surface;

#ifdef DIAGNOSTIC
typedef struct {
    cl_ulong surface;
    cl_float3 position;
    cl_float3 normal;
    VolumeType volume;
    cl_float distance;
} _Reflection_unalign;

typedef _Reflection_unalign __attribute__ ((aligned(8))) Reflection;
#endif

typedef struct  {
    VolumeType volume;
    cl_float3 direction;
    cl_float time;
} _Impulse_unalign;

typedef _Impulse_unalign __attribute__ ((aligned(8))) Impulse;

typedef struct  {
    cl_float3 direction;
    cl_float coefficient;
} _Speaker_unalign;

typedef _Speaker_unalign __attribute__ ((aligned(8))) Speaker;

std::vector <VolumeType> flattenImpulses
(   const std::vector <Impulse> & impulse
,   float samplerate
) throw();

std::vector <std::vector <float>> process
(   std::vector <std::vector <VolumeType>> & data
,   float samplerate
) throw();

template <typename T>
inline float max_amp (const std::vector <T> & ret) throw();

template <typename T>
struct FabsMax: public std::binary_function <float, T, float>
{
    inline float operator() (float a, T b) const throw()
        { return std::max (a, max_amp (b)); }
};

template<>
struct FabsMax <float>: public std::binary_function <float, float, float>
{
    inline float operator() (float a, float b) const throw()
        { return std::max (a, std::fabs (b)); }
};

template <typename T>
inline float max_amp (const std::vector <T> & ret) throw()
{
    return std::accumulate (begin (ret), end (ret), 0.0f, FabsMax <T>());
}

template <typename T>
inline void div (T & ret, float f) throw()
{
    for (auto & i : ret)
        div (i, f);
}

template<>
inline void div (float & ret, float f) throw()
{
    ret /= f;
}

template <typename T>
inline void normalize (std::vector <T> & ret) throw()
{
    div (ret, max_amp (ret));
}


//  Scene is imagined to be an 'initialize-once, use-many' kind of class.
//  It's initialized with a certain set of geometry, and then it keeps that
//  geometry throughout its lifespan.
//
//  If I were to allow the geometry to be updated, there's a chance the buffers
//  would be reinitialized WHILE a kernel was running, and I'm not sure what
//  would happen in that case (though I'm sure it would be bad). Unlike copies,
//  reinitializing the buffer on the host is not queued.

class Scene
{
public:
    Scene
    (   cl::Context & cl_context
    ,   unsigned long nreflections
    ,   std::vector <cl_float3> & directions
    ,   std::vector <Triangle> & triangles
    ,   std::vector <cl_float3> & vertices
    ,   std::vector <Surface> & surfaces
    ,   bool verbose = false
    );

    Scene
    (   cl::Context & cl_context
    ,   unsigned long nreflections
    ,   std::vector <cl_float3> & directions
    ,   const std::string & objpath
    ,   bool verbose = false
    );

#ifdef DIAGNOSTIC
    std::vector <Reflection> test
    (   const cl_float3 & micpos
    ,   const cl_float3 & source
    );
#endif

    void trace
    (   const cl_float3 & micpos
    ,   const cl_float3 & source
    );

    std::vector <Impulse> attenuate (const Speaker & speaker);

    std::vector <std::vector <Impulse>>  attenuate
    (   const std::vector <Speaker> & speakers
    );

private:
    const unsigned long nrays;
    const unsigned long nreflections;
    const unsigned long ntriangles;

    cl::Context & cl_context;

    cl::Buffer cl_directions;
    cl::Buffer cl_triangles;
    cl::Buffer cl_vertices;
    cl::Buffer cl_surfaces;

#ifdef DIAGNOSTIC
    cl::Buffer cl_reflections;
#endif

    cl::Buffer cl_impulses;
    cl::Buffer cl_attenuated;

    cl::Program cl_program;

    cl::CommandQueue queue;

    struct SceneData;

    Scene
    (   cl::Context & cl_context
    ,   unsigned long nreflections
    ,   std::vector <cl_float3> & directions
    ,   SceneData sceneData
    ,   bool verbose = false
    );
};
