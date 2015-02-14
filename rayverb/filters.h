#pragma once

#include "fftw3.h"

#include <array>
#include <vector>
#include <cmath>
#include <algorithm>
#include <iostream>

/// This namespace houses all of the machinery for multiband crossover
/// filtering.
namespace RayverbFiltering
{
    /// Interface for a plain boring bandpass filter.
    class Bandpass
    {
    public:
        virtual ~Bandpass() {}

        /// Given a vector of data, return a bandpassed version of the data.
        virtual void filter (std::vector <float> & data) = 0;

        virtual void setParams (float l, float h, float s)
        {
            lo = l;
            hi = h;
            sr = s;
        }

        float lo, hi, sr;
    };

    class FastConvolution
    {
    public:
        FastConvolution (long FFT_LENGTH);
        virtual ~FastConvolution();

        template <typename T, typename U>
        std::vector <float> convolve
        (   const T & a
        ,   const U & b
        )
        {
            forward_fft (r2c, a, r2c_i, r2c_o, acplx);
            forward_fft (r2c, b, r2c_i, r2c_o, bcplx);

            memset (c2r_i, 0, sizeof (fftwf_complex) * CPLX_LENGTH);
            memset (c2r_o, 0, sizeof (float) * FFT_LENGTH);

            fftwf_complex * x = acplx;
            fftwf_complex * y = bcplx;
            fftwf_complex * z = c2r_i;

            for (; z != c2r_i + CPLX_LENGTH; ++x, ++y, ++z)
            {
                (*z) [0] += (*x) [0] * (*y) [0] - (*x) [1] * (*y) [1];
                (*z) [1] += (*x) [0] * (*y) [1] + (*x) [1] * (*y) [0];
            }

            fftwf_execute (c2r);

            return std::vector <float> (c2r_o, c2r_o + FFT_LENGTH);
        }

    private:
        template <typename T>
        void forward_fft
        (   fftwf_plan & plan
        ,   const T & data
        ,   float * i
        ,   fftwf_complex * o
        ,   fftwf_complex * results
        )
        {
            std::fill (i, i + FFT_LENGTH, 0);
            std::copy (data.begin(), data.end(), i);
            fftwf_execute (plan);
            memcpy (results, o, sizeof (fftwf_complex) * CPLX_LENGTH);
        }

        const long FFT_LENGTH;
        const long CPLX_LENGTH = FFT_LENGTH / 2 + 1;

        float * r2c_i;
        fftwf_complex * r2c_o;
        fftwf_complex * c2r_i;
        float * c2r_o;
        fftwf_complex * acplx;
        fftwf_complex * bcplx;

        fftwf_plan r2c;
        fftwf_plan c2r;
    };

    /// An interesting windowed-sinc bandpass filter.
    class BandpassWindowedSinc: public Bandpass, public FastConvolution
    {
    public:
        BandpassWindowedSinc (long inputLength)
        :   FastConvolution (KERNEL_LENGTH + inputLength - 1)
        {

        }

        /// Filter a vector of data.
        virtual void filter (std::vector <float> & data);
        virtual void setParams (float l, float h, float s);
    private:
        static const auto KERNEL_LENGTH = 29;

        /// Fetch a convolution kernel for a bandpass filter with the given
        /// paramters.
        static std::vector <float> bandpassKernel
        (   float sr
        ,   float lo
        ,   float hi
        );

        std::array <float, KERNEL_LENGTH> kernel;
    };

    /// Simple biquad bandpass filter.
    class BandpassBiquadOnepass: public Bandpass
    {
    public:
        /// Filter a vector of data.
        virtual void filter (std::vector <float> & data);

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
        /// Filter a vector of data.
        virtual void filter (std::vector <float> & data);
    };

    /// Enum denoting available filter types.
    enum FilterType
    {   FILTER_TYPE_WINDOWED_SINC
    ,   FILTER_TYPE_BIQUAD_ONEPASS
    ,   FILTER_TYPE_BIQUAD_TWOPASS
    };

    /// Given a filter type and a vector of vector of float, return the
    /// parallel-filtered and summed data, using the specified filtering method.
    void filter
    (   FilterType ft
    ,   std::vector <std::vector <std::vector <float>>> & data
    ,   float sr
    );
}
