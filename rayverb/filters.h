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

        /// Given a vector of data, return a bandpassed version of the data.
        virtual void filter (std::vector <float> & data) const = 0;

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
        virtual void filter (std::vector <float> & data) const;
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
        virtual void filter (std::vector <float> & data) const;

        /// Implements a simple biquad filter.
        inline static void biquad
        (   std::vector <float> & input
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
        virtual void filter (std::vector <float> & data) const;
    };

    /// Enum denoting available filter types.
    enum FilterType
    {   FILTER_TYPE_WINDOWED_SINC
    ,   FILTER_TYPE_BIQUAD_ONEPASS
    ,   FILTER_TYPE_BIQUAD_TWOPASS
    };

    /// Given a vector of float, return a bandpassed version of the
    /// 'indexth' item of the cl_floatx.
    void bandpass
    (   const FilterType filterType
    ,   std::vector <float> & data
    ,   float lo
    ,   float hi
    ,   float sr
    );

    /// Given a filter type and a vector of vector of float, return the
    /// parallel-filtered and summed data, using the specified filtering method.
    void filter
    (   FilterType ft
    ,   std::vector <std::vector <float>> & data
    ,   float sr
    );
}
