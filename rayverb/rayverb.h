#pragma once

#include "filters.h"

#include "rapidjson/document.h"

#define __CL_ENABLE_EXCEPTIONS
#include "cl.hpp"

#include <vector>
#include <cmath>
#include <numeric>
#include <iostream>
#include <array>

//#define DIAGNOSTIC

#define NUM_IMAGE_SOURCE 10
#define SPEED_OF_SOUND (340.0f)

//  These definitions MUST be kept up-to-date with the defs in the cl file.
//  It might make sense to nest them inside the Scene because I don't think
//  other classes will need the same data formats.
//
//  Unless I have a new class/kernel for constructing BVHs?

/// Type used for storing multiband volumes.
/// Higher values of 'x' in cl_floatx = higher numbers of parallel bands.
typedef cl_float8 VolumeType;

/// A Triangle contains an offset into an array of Surface, and three offsets
/// into an array of cl_float3.
typedef struct  {
    cl_ulong surface;
    cl_ulong v0;
    cl_ulong v1;
    cl_ulong v2;
} _Triangle_unalign;

typedef _Triangle_unalign __attribute__ ((aligned(8))) Triangle;

/// Surfaces describe their specular and diffuse coefficients per-band.
typedef struct  {
    VolumeType specular;
    VolumeType diffuse;
} _Surface_unalign;

typedef _Surface_unalign __attribute__ ((aligned(8))) Surface;

/// An impulse contains a volume, a time in seconds, and the direction from
/// which it came (useful for attenuation/hrtf stuff).
typedef struct  {
    VolumeType volume;
    cl_float3 direction;
    cl_float time;
#ifdef DIAGNOSTIC
    cl_float3 position;
#endif
} _Impulse_unalign;

typedef _Impulse_unalign __attribute__ ((aligned(8))) Impulse;

/// Each speaker has a (normalized-unit) direction, and a coefficient in the
/// range 0-1 which describes its polar pattern from omni to bidirectional.
typedef struct  {
    cl_float3 direction;
    cl_float coefficient;
} _Speaker_unalign;

typedef _Speaker_unalign __attribute__ ((aligned(8))) Speaker;

/// Sum impulses ocurring at the same (sampled) time and return a vector in
/// which each subsequent item refers to the next sample of an impulse
/// response.
std::vector <VolumeType> flattenImpulses
(   const std::vector <Impulse> & impulse
,   float samplerate
);

/// Maps flattenImpulses over a vector of input vectors.
std::vector <std::vector <VolumeType>> flattenImpulses
(   const std::vector <std::vector <Impulse>> & impulse
,   float samplerate
);

/// Filter each channel of the input data, then normalize all channels.
std::vector <std::vector <float>> process
(   RayverbFiltering::FilterType filtertype
,   std::vector <std::vector <VolumeType>> & data
,   float samplerate
);

//  These next few functions are recursive template metaprogramming magic
//  and I've forgotten how they work since I wrote them but it's REALLY CLEVER
//  I promise.

/// You can call max_amp on an arbitrarily nested vector and get back the
/// magnitude of the value with the greatest magnitude in the vector.
template <typename T>
inline float max_amp (const std::vector <T> & ret);

template <typename T>
struct FabsMax: public std::binary_function <float, T, float>
{
    inline float operator() (float a, T b) const
        { return std::max (a, max_amp (b)); }
};

template<>
struct FabsMax <float>: public std::binary_function <float, float, float>
{
    inline float operator() (float a, float b) const
        { return std::max (a, std::fabs (b)); }
};

template <typename T>
inline float max_amp (const std::vector <T> & ret)
{
    return std::accumulate (begin (ret), end (ret), 0.0f, FabsMax <T>());
}

/// Recursively divide by reference.
template <typename T>
inline void div (T & ret, float f)
{
    for (auto & i : ret)
        div (i, f);
}

template<>
inline void div (float & ret, float f)
{
    ret /= f;
}

/// Find the largest absolute value in an arbitarily nested vector, then
/// divide every item in the vector by that value.
template <typename T>
inline void normalize (std::vector <T> & ret)
{
    div (ret, max_amp (ret));
}

/// Overloads std::plus for cl_float3.
template<>
struct std::plus <cl_float3>
:   public binary_function <cl_float3, cl_float3, cl_float3>
{
    inline cl_float3 operator() (const cl_float3 & a, const cl_float3 & b) const
    {
        return (cl_float3)
        {   a.s [0] + b.s [0]
        ,   a.s [1] + b.s [1]
        ,   a.s [2] + b.s [2]
        ,   a.s [3] + b.s [3]
        };
    }
};

/// Overloads std::plus for cl_float8.
template<>
struct std::plus <cl_float8>
:   public binary_function <cl_float8, cl_float8, cl_float8>
{
    inline cl_float8 operator() (const cl_float8 & a, const cl_float8 & b) const
    {
        return (cl_float8)
        {   a.s [0] + b.s [0]
        ,   a.s [1] + b.s [1]
        ,   a.s [2] + b.s [2]
        ,   a.s [3] + b.s [3]
        ,   a.s [4] + b.s [4]
        ,   a.s [5] + b.s [5]
        ,   a.s [6] + b.s [6]
        ,   a.s [7] + b.s [7]
        };
    }
};

/// Overloads std::negate for cl_float3.
template<>
struct std::negate <cl_float3>
:   public unary_function <cl_float3, cl_float3>
{
    inline cl_float3 operator() (const cl_float3 & a) const
    {
        return (cl_float3) {-a.s [0], -a.s [1], -a.s [2], -a.s [3]};
    }
};

/// Basically std::plus but without having to manually specify type info.
template <typename T>
inline T sum (const T & a, const T & b)
{
    return std::plus <T>() (a, b);
}

/// Basically std::negate but without having to manually specify type info.
template <typename T>
inline T doNegate (const T & a)
{
    return std::negate <T>() (a);
}

/// Subtract a time in seconds from a nested vector of impulses.
template <typename T>
inline void fixPredelay (T & ret, float predelay_seconds)
{
    for (auto & i : ret)
        fixPredelay (i, predelay_seconds);
}

/// Subtract a time in seconds from the 'time' field of an impulse.
template<>
inline void fixPredelay (Impulse & ret, float predelay_seconds)
{
    ret.time = ret.time > predelay_seconds ? ret.time - predelay_seconds : 0;
}

/// Calculate the time taken for sound to travel between a source and mic,
/// then subtract that time from all impulses.
template <typename T>
inline void fixPredelay
(   T & ret
,   const cl_float3 & micpos
,   const cl_float3 & source
)
{
    const cl_float3 diff = sum (micpos, doNegate (source));
    const float length = sqrt
    (   pow (diff.s [0], 2)
    +   pow (diff.s [1], 2)
    +   pow (diff.s [2], 2)
    );
    fixPredelay (ret, length / SPEED_OF_SOUND);
}

/// This is where the magic happens.
/// Scene is imagined to be an 'initialize-once, use-many' kind of class.
/// It's initialized with a certain set of geometry, and then it keeps that
/// geometry throughout its lifespan.
///
/// If I were to allow the geometry to be updated, there's a chance the buffers
/// would be reinitialized WHILE a kernel was running, and I'm not sure what
/// would happen in that case (though I'm sure it would be bad). Unlike copies,
/// reinitializing the buffer on the host is not queued.
class Scene
{
public:
    /// Set up a scene with an openCL context and a bunch of geometry.
    /// This reserves a whole bunch of space on the GPU.
    /// Probably don't have more than one instance of this class alive at a
    /// time.
    Scene
    (   cl::Context & cl_context
    ,   unsigned long nreflections
    ,   std::vector <Triangle> & triangles
    ,   std::vector <cl_float3> & vertices
    ,   std::vector <Surface> & surfaces
    ,   bool verbose = false
    );

    /// Init from a 3D model file.
    Scene
    (   cl::Context & cl_context
    ,   unsigned long nreflections
    ,   const std::string & objpath
    ,   const std::string & materialFileName
    ,   bool verbose = false
    );

    /// Do a raytrace.
    /// Results are stored internally.
    /// To get stuff out, call attenuate or hrtf.
    void trace
    (   const cl_float3 & micpos
    ,   const cl_float3 & source
    ,   const std::vector <cl_float3> & directions
    );

    /// Get raw, unprocessed diffuse impulses from raytrace.
    std::vector <Impulse> getRawDiffuse();

    /// Get raw, unprocessed image-source impulses from raytrace.
    std::vector <Impulse> getRawImages();

    /// Get attenuated impulses for a particular speaker.
    std::vector <Impulse> attenuate (const Speaker & speaker);

    /// Get attenuated impulses for a vector of speakers.
    std::vector <std::vector <Impulse>>  attenuate
    (   const std::vector <Speaker> & speakers
    );

    /// Get attenuated impulses for two channels of hrtf.
    std::vector <std::vector <Impulse>> hrtf
    (   const cl_float3 & facing
    ,   const cl_float3 & up
    );

private:
    std::vector <Impulse> hrtf
    (   unsigned long channel
    ,   const cl_float3 & facing
    ,   const cl_float3 & up
    );

    unsigned long ngroups;
    const unsigned long nreflections;
    const unsigned long ntriangles;

    unsigned long nhrtf;

    cl::Context & cl_context;

    cl::Buffer cl_directions;
    cl::Buffer cl_triangles;
    cl::Buffer cl_vertices;
    cl::Buffer cl_surfaces;
    cl::Buffer cl_impulses;
    cl::Buffer cl_attenuated;

    cl::Buffer cl_hrtf;

    cl::Program cl_program;

    cl::CommandQueue queue;

    struct SceneData;

    Scene
    (   cl::Context & cl_context
    ,   unsigned long nreflections
    ,   SceneData sceneData
    ,   bool verbose = false
    );

    static const int RAY_GROUP_SIZE = 1024;
    static const std::string KERNEL_STRING;
    static const std::array <std::array <std::array <cl_float8, 180>, 360>, 2> HRTF_DATA;

    cl::Buffer cl_image_source;

    std::vector <Impulse> storedDiffuse;
    std::vector <Impulse> storedImage;
};

/// Try to open and parse a json file.
void attemptJsonParse
(   const std::string & fname
,   rapidjson::Document & doc
);
