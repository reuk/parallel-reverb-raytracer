#include "rayverb_lib.h"

#include <cmath>
#include <numeric>

#define VECTORISE

#ifdef VECTORISE
#include <x86intrin.h>
#endif

//#define USE_OBJECT_MATERIALS

using namespace std;

struct LatestImpulse: binary_function <Impulse, Impulse, bool>
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

vector <cl_float3> flattenImpulses 
(   const vector <Impulse> & impulse
,   float samplerate
)
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

void lopass (vector <cl_float3> & data, float cutoff, float sr, int index)
{
    const float param = 1 - exp (-2 * M_PI * cutoff / sr);
    float state = 0;
    
    for (auto i = begin (data); i != end (data); ++i)
    {
        i->s [index] = state += param * (i->s [index] - state);
    }
}

void hipass (vector <cl_float3> & data, float cutoff, float sr, int index)
{
    const float param = 1 - exp (-2 * M_PI * cutoff / sr);
    float state = 0;
    
    for (auto i = begin (data); i != end (data); ++i)
    {
        i->s [index] -= state += param * (i->s [index] - state);
    }
}

void bandpass (vector <cl_float3> & data, float lo, float hi, float sr, int index)
{
    lopass (data, hi, sr, index);
    hipass (data, lo, sr, index);
}

void filter (vector <cl_float3> & data, float lo, float hi, float sr)
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

void hipass (vector <float> & data, float lo, float sr)
{
    const float loParam = 1 - exp (-2 * M_PI * lo / sr);
    float loState = 0;
    
    for (auto i = begin (data); i != end (data); ++i)
    {
        *i -= loState += loParam * (*i - loState);
    }
}

vector <float> sum (const vector <cl_float3> & data)
{
    vector <float> ret (data.size());
    for (unsigned long i = 0; i != data.size(); ++i)
    {
        ret [i] = accumulate (data [i].s, data[i].s + 3, 0.0);
    }
    return ret;
}

vector <vector <float>> process (vector <vector <cl_float3>> & data, float sr)
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

