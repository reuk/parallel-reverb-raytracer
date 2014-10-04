#pragma once

#define DIAGNOSTIC

#define __CL_ENABLE_EXCEPTIONS
#include "cl.hpp"

#include <vector>

//  These definitions MUST be kept up-to-date with the defs in the cl file.

typedef struct  {
    cl_ulong surface;
    cl_ulong v0;
    cl_ulong v1;
    cl_ulong v2;
} _Triangle_unalign;

typedef _Triangle_unalign __attribute__ ((aligned(8))) Triangle;

typedef struct  {
    cl_float3 origin;
    cl_float radius;
} _Sphere_unalign;

typedef _Sphere_unalign __attribute__ ((aligned(8))) Sphere;

typedef struct  {
    cl_float3 specular;
    cl_float3 diffuse;
} _Surface_unalign;

typedef _Surface_unalign __attribute__ ((aligned(8))) Surface;

#ifdef DIAGNOSTIC
typedef struct {
    cl_ulong surface;
    cl_float3 position;
    cl_float3 normal;
    cl_float3 volume;
    cl_float distance;
} _Reflection_unalign;

typedef _Reflection_unalign __attribute__ ((aligned(8))) Reflection;
#endif

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

std::vector <cl_float3> flattenImpulses 
(   const std::vector <Impulse> & impulse
,   float samplerate
) throw();

std::vector <std::vector <float>> process 
(   std::vector <std::vector <cl_float3>> & data
,   float samplerate
) throw();

class Scene
{
public:
    Scene 
    (   cl::Context & cl_context
    ,   unsigned long nrays
    ,   unsigned long nreflections
    ,   std::vector <cl_float3> & directions
    ,   std::vector <Triangle> & triangles
    ,   std::vector <cl_float3> & vertices
    ,   std::vector <Surface> & surfaces
    ,   unsigned long nsources = 1
    );

    Scene
    (   cl::Context & cl_context
    ,   unsigned long rays
    ,   unsigned long nreflections
    ,   std::vector <cl_float3> & directions
    ,   const std::string & objpath
    ,   unsigned long nsources = 1
    );

    std::vector <std::vector <Impulse>> trace 
    (   const cl_float3 & micpos
    ,   std::vector <Sphere> & sources
    ,   const std::vector <Speaker> & speakers
    ) const;

private:
    unsigned long nrays;
    unsigned long nreflections;
    unsigned long ntriangles;

    cl::Context & cl_context;

    cl::Buffer cl_directions;
    cl::Buffer cl_triangles;
    cl::Buffer cl_vertices;
    cl::Buffer cl_surfaces;
    cl::Buffer cl_spheres;

    cl::Buffer cl_impulses;
    cl::Buffer cl_attenuated;

    cl::Program cl_program;

    struct SceneData
    {
    public:
        SceneData (const std::string & objpath);

        std::vector <Triangle> triangles;
        std::vector <cl_float3> vertices;
        std::vector <Surface> surfaces;
    };

    Scene
    (   cl::Context & cl_context
    ,   unsigned long nrays
    ,   unsigned long nreflections
    ,   std::vector <cl_float3> & directions
    ,   SceneData sceneData
    ,   unsigned long nsources = 1
    );
};
