#pragma once

#include <vector>
#include <cmath>

/// This namespace houses all of the machinery for multiband crossover
/// filtering.
namespace RayverbFiltering
{
    /// Interface for a plain boring bandpass filter.
    class Bandpass
    {
    public:
        Bandpass (float lo, float hi, float sr)
        :   lo (lo)
        ,   hi (hi)
        ,   sr (sr)
        {

        }

        /// A bandpass must be able to filter stuff.
        template <typename T>
        std::vector <float> multifilter
        (   const std::vector <T> & data
        ,   int index
        ) const
        {
            return filter (extractIndex (data, index));
        }

        /// Given a vector of data, return a bandpassed version of the data.
        virtual std::vector <float> filter
        (   const std::vector <float> & data
        ) const = 0;

        /// Normal functional map.
        /// In C++11 this should use a move constructor to return, so there
        /// shouldn't be too much overhead.
        template <typename T, typename U, typename F>
        inline static T map (const U & u, F f)
        {
            T ret(u.size());
            std::transform(std::begin(u), std::end(u), std::begin(ret), f);
            return ret;
        }

        /// Given a vector of some vector type, return a vector of just the
        /// 'indexth' members of each item.
        template <typename T>
        inline static std::vector <float> extractIndex
        (   const std::vector <T> & data
        ,   int index
        )
        {
            return map <std::vector <float>>
            (   data
            ,   [&] (const T & t) {return t.s [index]; }
            );
        }

        float lo, hi, sr;
    };

    /// An interesting windowed-sinc bandpass filter.
    class BandpassWindowedSinc: public Bandpass
    {
    public:
        BandpassWindowedSinc (float lo, float hi, float sr)
        :   Bandpass (lo, hi, sr)
        {

        }

        /// Filter a vector of data.
        virtual std::vector <float> filter
        (   const std::vector <float> & data
        ) const
        {
            std::vector <float> kernel = bandpassKernel (sr, lo, hi, 31);
            return fastConvolve (data, kernel);
        }
    private:
        /// Fetch a convolution kernel for a bandpass filter with the given
        /// paramters.
        static std::vector <float> bandpassKernel
        (   float sr
        ,   float lo
        ,   float hi
        ,   unsigned long l
        );

        /// Do a blindingly fast convolution.
        static std::vector <float> fastConvolve
        (   const std::vector <float> & a
        ,   const std::vector <float> & b
        );
    };

    /// Simple biquad bandpass filter.
    class BandpassBiquadOnepass: public Bandpass
    {
    public:
        BandpassBiquadOnepass (float lo, float hi, float sr)
        :   Bandpass (lo, hi, sr)
        {

        }

        /// Filter a vector of data.
        virtual std::vector <float> filter
        (   const std::vector <float> & data
        ) const;

        /// Implements a simple biquad filter.
        static std::vector <float> biquad
        (   const std::vector <float> & input
        ,   double b0
        ,   double b1
        ,   double b2
        ,   double a1
        ,   double a2
        );
    };

    /// Twopass (definitely offline) linear-phase biquad bandpass filter.
    class BandpassBiquadTwopass: public BandpassBiquadOnepass
    {
    public:
        BandpassBiquadTwopass (float lo, float hi, float sr)
        :   BandpassBiquadOnepass (lo, hi, sr)
        {

        }

        /// Filter a vector of data.
        virtual std::vector <float> filter
        (   const std::vector <float> & data
        ) const;
    };

    /// Enum denoting available filter types.
    enum FilterType
    {   FILTER_TYPE_WINDOWED_SINC
    ,   FILTER_TYPE_BIQUAD_ONEPASS
    ,   FILTER_TYPE_BIQUAD_TWOPASS
    };

    /// Given a vector of cl_floatx, return a bandpassed version of the
    /// 'indexth' item of the cl_floatx.
    template <typename T>
    std::vector <float> bandpass
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
            return BandpassWindowedSinc (lo, hi, sr).multifilter (data, index);
        case FILTER_TYPE_BIQUAD_ONEPASS:
            return BandpassBiquadOnepass (lo, hi, sr).multifilter (data, index);
        case FILTER_TYPE_BIQUAD_TWOPASS:
            return BandpassBiquadTwopass (lo, hi, sr).multifilter(data, index);
        }
    }

    /// Given a filter type and a vector of cl_floatx, return the parallel-
    /// filtered and summed data, using the specified filtering method.
    template <typename T>
    std::vector <float> filter
    (   FilterType ft
    ,   std::vector <T> & data
    ,   float sr
    )
    {
        //  This is ugly.
        //  In python, I would do something like zip(edges[:-1], edges[1:])
        //  to get a list of tuples of bandedges, which could then be mapped
        //  across.
        //  But whatever.
        std::vector <float> edges =
            {1, 190, 380, 760, 1520, 3040, 6080, 12160, 20000};
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
