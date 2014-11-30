#pragma once

#include <vector>
#include <cmath>

namespace RayverbFiltering
{
    template <typename T, typename U, typename F>
    inline T map (const U & u, F f)
    {
        T ret(u.size());
        std::transform(std::begin(u), std::end(u), std::begin(ret), f);
        return ret;
    }

    enum FilterType
    {   FILTER_TYPE_WINDOWED_SINC
    ,   FILTER_TYPE_BIQUAD_ONEPASS
    ,   FILTER_TYPE_BIQUAD_TWOPASS
    };

    std::vector <float> bandpassKernel
    (   float sr
    ,   float lo
    ,   float hi
    ,   unsigned long l
    );
    std::vector <float> fastConvolve
    (   const std::vector <float> & a
    ,   const std::vector <float> & b
    );

    template <typename T>
    inline std::vector <float> extractIndex
    (   const std::vector <T> & data
    ,   int index
    )
    {
        return map <std::vector <float>>
        (   data
        ,   [&] (const T & t) {return t.s [index]; }
        );
    }

    template <typename T>
    inline std::vector <float> bandpassWindowedSinc
    (   const std::vector <T> & data
    ,   float lo
    ,   float hi
    ,   float sr
    ,   int index
    )
    {
        std::vector <float> kernel = bandpassKernel (sr, lo, hi, 31);
        return fastConvolve (extractIndex (data, index), kernel);
    }

    std::vector <float> biquad
    (   const std::vector <float> & input
    ,   double b0
    ,   double b1
    ,   double b2
    ,   double a1
    ,   double a2
    );

    std::vector <float> bandpassBiquad
    (   const std::vector <float> & data
    ,   float lo
    ,   float hi
    ,   float sr
    );

    template <typename T>
    inline std::vector <float> bandpassBiquadOnepass
    (   const std::vector <T> & data
    ,   float lo
    ,   float hi
    ,   float sr
    ,   int index
    )
    {
        return bandpassBiquad
        (   extractIndex (data, index)
        ,   lo
        ,   hi
        ,   sr
        );
    }

    template <typename T>
    inline std::vector <float> bandpassBiquadTwopass
    (   const std::vector <T> & data
    ,   float lo
    ,   float hi
    ,   float sr
    ,   int index
    )
    {
        std::vector <float> p0 = extractIndex (data, index);
        std::vector <float> p1 = bandpassBiquad(p0, lo, hi, sr);
        std::reverse (std::begin (p1), std::end (p1));
        std::vector <float> p2 = bandpassBiquad(p1, lo, hi, sr);
        std::reverse (std::begin (p2), std::end (p2));
        return p2;
    }

    //  given a vector of cl_floatx, return a bandpassed version of the
    //  'indexth' item of the cl_floatx

    template <typename T>
    inline std::vector <float> bandpass
    (   const FilterType filterType
    ,   const std::vector <T> & data
    ,   float lo
    ,   float hi
    ,   float sr
    ,   int index
    )
    {
        switch (filterType)
        {
        case FILTER_TYPE_WINDOWED_SINC:
            return bandpassWindowedSinc(data, lo, hi, sr, index);
        case FILTER_TYPE_BIQUAD_ONEPASS:
            return bandpassBiquadOnepass(data, lo, hi, sr, index);
        case FILTER_TYPE_BIQUAD_TWOPASS:
            return bandpassBiquadTwopass(data, lo, hi, sr, index);
        }
    }

    //  given a filter type and a vector of cl_floatx, return the parallel-
    //  filtered and summed data, using the specified filtering method

    template <typename T>
    inline std::vector <float> filter
    (   FilterType ft
    ,   std::vector <T> & data
    ,   float sr
    )
    {
        std::vector <float> edges =
            {20, 190, 380, 760, 1520, 3040, 6080, 12160, 20000};
        std::vector <std::vector <float>> out (edges.size() - 1);
        for (unsigned long i = 0; i != out.size(); ++i)
        {
            out[i] = bandpass (ft, data, edges [i], edges [i + 1], sr, i);
        }

        std::vector <float> ret (out.front().size(), 0);
        for (unsigned long i = 0; i != ret.size(); ++i)
        {
            for (unsigned long j = 0; j != out.size(); ++j)
                ret [i] += out [j] [i];
        }

        return ret;
    }
}
