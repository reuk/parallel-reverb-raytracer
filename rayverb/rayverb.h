#pragma once

#include "filters.h"
#include "clstructs.h"

#include "rapidjson/document.h"

#define __CL_ENABLE_EXCEPTIONS
#include "cl.hpp"

#include <vector>
#include <cmath>
#include <numeric>
#include <iostream>
#include <array>
#include <map>

//#define DIAGNOSTIC

/// Sum impulses ocurring at the same (sampled) time and return a vector in
/// which each subsequent item refers to the next sample of an impulse
/// response.
std::vector <std::vector <float>> flattenImpulses
(   const std::vector <Impulse> & impulse
,   float samplerate
);

/// Maps flattenImpulses over a vector of input vectors.
std::vector <std::vector <std::vector <float>>> flattenImpulses
(   const std::vector <std::vector <Impulse>> & impulse
,   float samplerate
);

/// Filter each channel of the input data, then normalize all channels.
std::vector <std::vector <float>> process
(   RayverbFiltering::FilterType filtertype
,   std::vector <std::vector <std::vector <float>>> & data
,   float sr
,   bool do_normalize
,   bool do_hipass
,   bool do_trim_tail
,   float volumme_scale
);

/// You can call max_amp on an arbitrarily nested vector and get back the
/// magnitude of the value with the greatest magnitude in the vector.
template <typename T>
inline float max_amp (const T & t)
{
    return std::accumulate
    (   t.begin()
    ,   t.end()
    ,   0.0
    ,   [] (float a, const auto & b) {return std::max (a, max_amp (b));}
    );
}

template<>
inline float max_amp (const float & t)
{
    return std::fabs (t);
}

/// Recursively divide by reference.
template <typename T>
inline void div (T & ret, float f)
{
    for (auto && i : ret)
        div (i, f);
}

template<>
inline void div (float & ret, float f)
{
    ret /= f;
}

/// Recursively multiply by reference.
template <typename T>
inline void mul (T & ret, float f)
{
    for (auto && i : ret)
        mul (i, f);
}

template<>
inline void mul (float & ret, float f)
{
    ret *= f;
}

/// Find the largest absolute value in an arbitarily nested vector, then
/// divide every item in the vector by that value.
template <typename T>
inline void normalize (std::vector <T> & ret)
{
    mul (ret, 1.0 / max_amp (ret));
}

template <typename T, typename U>
inline T elementwise (const T & a, const T & b, const U & u)
{
    T ret;
    std::transform (std::begin (a.s), std::end (a.s), std::begin (b.s), std::begin (ret.s), u);
    return ret;
}

template <typename T>
inline float findPredelay (const T & ret)
{
    return accumulate
    (   ret.begin() + 1
    ,   ret.end()
    ,   findPredelay (ret.front())
    ,   [] (auto a, const auto & b)
        {
            auto pd = findPredelay (b);
            if (a == 0)
                return pd;
            if (pd == 0)
                return a;
            return std::min (a, pd);
        }
    );
}

template<>
inline float findPredelay (const Impulse & i)
{
    return i.time;
}

template <typename T>
inline void fixPredelay (T & ret, float seconds)
{
    for (auto && i : ret)
        fixPredelay (i, seconds);
}

template<>
inline void fixPredelay (Impulse & ret, float seconds)
{
    ret.time = ret.time > seconds ? ret.time - seconds : 0;
}

template <typename T>
inline void fixPredelay (T & ret)
{
    auto predelay = findPredelay (ret);
    fixPredelay (ret, predelay);
}


class ContextProvider
{
public:
    ContextProvider();

    cl::Context cl_context;
};

class KernelLoader: public ContextProvider
{
public:
    KernelLoader();

    cl::Program cl_program;
    cl::CommandQueue queue;
    static const std::string KERNEL_STRING;
};

struct RaytracerResults
{
    RaytracerResults() {}
    RaytracerResults (const std::vector <Impulse> impulses, const cl_float3 & c)
    :   impulses (impulses)
    ,   mic (c)
    {}

    std::vector <Impulse> impulses;
    cl_float3 mic;
};

class Raytracer: public KernelLoader
{
public:
    Raytracer
    (   unsigned long nreflections
    ,   std::vector <Triangle> & triangles
    ,   std::vector <cl_float3> & vertices
    ,   std::vector <Surface> & surfaces
    );

    Raytracer
    (   unsigned long nreflections
    ,   const std::string & objpath
    ,   const std::string & materialFileName
    );

    void raytrace
    (   const cl_float3 & micpos
    ,   const cl_float3 & source
    ,   const std::vector <cl_float3> & directions
    );

    /// Get raw, unprocessed diffuse impulses.
    RaytracerResults getRawDiffuse();

    /// Get raw, unprocessed image-source impulses.
    RaytracerResults getRawImages (bool removeDirect);

    /// Get all raw, unprocessed impulses.
    RaytracerResults getAllRaw (bool removeDirect);

private:
    const unsigned long nreflections;
    const unsigned long ntriangles;

    cl::Buffer cl_directions;
    cl::Buffer cl_triangles;
    cl::Buffer cl_vertices;
    cl::Buffer cl_surfaces;
    cl::Buffer cl_impulses;
    cl::Buffer cl_image_source;
    cl::Buffer cl_image_source_index;

    std::pair <cl_float3, cl_float3> bounds;

    cl_float3 storedMicpos;

    struct SceneData;

    Raytracer
    (   unsigned long nreflections
    ,   SceneData sceneData
    );

    static const unsigned long RAY_GROUP_SIZE = 4096;

    decltype
    (   cl::make_kernel
        <   cl::Buffer
        ,   cl_float3
        ,   cl::Buffer
        ,   cl_ulong
        ,   cl::Buffer
        ,   cl_float3
        ,   cl::Buffer
        ,   cl::Buffer
        ,   cl::Buffer
        ,   cl::Buffer
        ,   cl_ulong
        > (cl_program, "raytrace")
    ) raytrace_kernel;

    std::vector <Impulse> storedDiffuse;
    std::map <std::vector <unsigned long>, Impulse> imageSourceTally;
};

struct HrtfConfig
{
    cl_float3 facing;
    cl_float3 up;
};

struct Attenuator: public KernelLoader
{
    cl::Buffer cl_in;
    cl::Buffer cl_out;
};

class HrtfAttenuator: public Attenuator
{
public:
    HrtfAttenuator();

    std::vector <std::vector <Impulse>> attenuate
    (   const RaytracerResults & results
    ,   const HrtfConfig & config
    );
    std::vector <std::vector <Impulse>> attenuate
    (   const RaytracerResults & results
    ,   const cl_float3 & facing
    ,   const cl_float3 & up
    );

    virtual const std::array <std::array <std::array <cl_float8, 180>, 360>, 2> & getHrtfData() const;
private:
    static const std::array <std::array <std::array <cl_float8, 180>, 360>, 2> HRTF_DATA;
    std::vector <Impulse> attenuate
    (   const cl_float3 & mic_pos
    ,   unsigned long channel
    ,   const cl_float3 & facing
    ,   const cl_float3 & up
    ,   const std::vector <Impulse> & impulses
    );

    cl::Buffer cl_hrtf;

    decltype
    (   cl::make_kernel
        <   cl_float3
        ,   cl::Buffer
        ,   cl::Buffer
        ,   cl::Buffer
        ,   cl_float3
        ,   cl_float3
        ,   cl_ulong
        > (cl_program, "hrtf")
    ) attenuate_kernel;
};

class SpeakerAttenuator: public Attenuator
{
public:
    SpeakerAttenuator();
    std::vector <std::vector <Impulse>> attenuate
    (   const RaytracerResults & results
    ,   const std::vector <Speaker> & speakers
    );
private:
    std::vector <Impulse> attenuate
    (   const cl_float3 & mic_pos
    ,   const Speaker & speaker
    ,   const std::vector <Impulse> & impulses
    );
    decltype
    (   cl::make_kernel
        <   cl_float3
        ,   cl::Buffer
        ,   cl::Buffer
        ,   Speaker
        > (cl_program, "attenuate")
    ) attenuate_kernel;
};

/// Try to open and parse a json file.
void attemptJsonParse
(   const std::string & fname
,   rapidjson::Document & doc
);
