#include "filters.h"

#include "fftw3.h"

#include <numeric>
#include <iostream>

using namespace std;

template <typename T>
T sinc (const T & t)
{
    T pit = M_PI * t;
    return sin (pit) / pit;
}

template <typename T>
vector <T> sincKernel (double cutoff, unsigned long length)
{
    if (! (length % 2))
        throw runtime_error ("Length of sinc filter kernel must be odd.");

    vector <T> ret (length);
    for (unsigned long i = 0; i != length; ++i)
    {
        if (i == ((length - 1) / 2))
            ret [i] = 1;
        else
            ret [i] = sinc (2 * cutoff * (i - (length - 1) / 2.0));
    }
    return ret;
}

template <typename T>
vector <T> blackman (unsigned long length)
{
    const double a0 = 7938.0 / 18608.0;
    const double a1 = 9240.0 / 18608.0;
    const double a2 = 1430.0 / 18608.0;

    vector <T> ret (length);
    for (unsigned long i = 0; i != length; ++i)
    {
        const double offset = i / (length - 1.0);
        ret [i] =
        (   a0
        -   a1 * cos (2 * M_PI * offset)
        +   a2 * cos (4 * M_PI * offset)
        );
    }
    return ret;
}

vector <float> lopassKernel (float sr, float cutoff, unsigned long length)
{
    vector <float> window = blackman <float> (length);
    vector <float> kernel = sincKernel <float> (cutoff / sr, length);
    vector <float> ret (length);
    transform
    (   begin (window)
    ,   end (window)
    ,   begin (kernel)
    ,   begin (ret)
    ,   [&] (float i, float j) { return i * j; }
    );
    float sum = accumulate (begin (ret), end (ret), 0.0);
    for (auto & i : ret) i /= sum;
    return ret;
}

vector <float> hipassKernel (float sr, float cutoff, unsigned long length)
{
    vector <float> kernel = lopassKernel (sr, cutoff, length);
    for (auto & i : kernel) i = -i;
    kernel [(length - 1) / 2] += 1;
    return kernel;
}

void forward_fft
(   fftwf_plan & plan
,   const vector <float> & data
,   float * i
,   fftwf_complex * o
,   unsigned long FFT_LENGTH
,   fftwf_complex * results
)
{
    const unsigned long CPLX_LENGTH = FFT_LENGTH / 2 + 1;

    memset (i, 0, sizeof (float) * FFT_LENGTH);
    memcpy (i, data.data(), sizeof (float) * data.size());
    fftwf_execute (plan);
    memcpy (results, o, sizeof (fftwf_complex) * CPLX_LENGTH);
}

vector <float> RayverbFiltering::BandpassWindowedSinc::fastConvolve
(   const vector <float> & a
,   const vector <float> & b
)
{
    const unsigned long FFT_LENGTH = a.size() + b.size() - 1;
    const unsigned long CPLX_LENGTH = FFT_LENGTH / 2 + 1;

    float * r2c_i = fftwf_alloc_real (FFT_LENGTH);
    fftwf_complex * r2c_o = fftwf_alloc_complex (CPLX_LENGTH);

    fftwf_complex * acplx = fftwf_alloc_complex (CPLX_LENGTH);
    fftwf_complex * bcplx = fftwf_alloc_complex (CPLX_LENGTH);

    fftwf_plan r2c = fftwf_plan_dft_r2c_1d
    (   FFT_LENGTH
    ,   r2c_i
    ,   r2c_o
    ,   FFTW_ESTIMATE
    );

    forward_fft (r2c, a, r2c_i, r2c_o, FFT_LENGTH, acplx);
    forward_fft (r2c, b, r2c_i, r2c_o, FFT_LENGTH, bcplx);

    fftwf_destroy_plan (r2c);
    fftwf_free (r2c_i);
    fftwf_free (r2c_o);

    fftwf_complex * c2r_i = fftwf_alloc_complex (CPLX_LENGTH);
    float * c2r_o = fftwf_alloc_real (FFT_LENGTH);

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

    fftwf_free (acplx);
    fftwf_free (bcplx);

    fftwf_plan c2r = fftwf_plan_dft_c2r_1d
    (   FFT_LENGTH
    ,   c2r_i
    ,   c2r_o
    ,   FFTW_ESTIMATE
    );

    fftwf_execute (c2r);
    fftwf_destroy_plan (c2r);

    vector <float> ret (c2r_o, c2r_o + FFT_LENGTH);

    fftwf_free (c2r_i);
    fftwf_free (c2r_o);

    return ret;
}

vector <float> RayverbFiltering::BandpassWindowedSinc::bandpassKernel
(   float sr
,   float lo
,   float hi
,   unsigned long l
)
{
    vector <float> lop = lopassKernel (sr, hi, l);
    vector <float> hip = hipassKernel (sr, lo, l);
    return fastConvolve (lop, hip);
}

vector <float> RayverbFiltering::BandpassBiquadOnepass::biquad
(   const vector <float> & input
,   double b0
,   double b1
,   double b2
,   double a1
,   double a2
)
{
    double z1 = 0, z2 = 0;

    return map <vector <float>>
    (   input
    ,   [&] (float i)
        {
            double out = i * b0 + z1;
            z1 = i * b1 + z2 - a1 * out;
            z2 = i * b2 - a2 * out;
            return out;
        }
    );
}

vector <float> RayverbFiltering::BandpassBiquadOnepass::filter
(   const vector <float> & data
) const
{
    const double c = sqrt (lo * hi);
    const double omega = 2 * M_PI * c / sr;
    const double cs = cos (omega);
    const double sn = sin (omega);
    const double bandwidth = log2 (hi / lo);
    const double Q = sn / (log(2) * bandwidth * omega);
    const double alpha = sn * sinh (1 / (2 * Q));

    double b0 = alpha;
    double b1 = 0;
    double b2 = -alpha;
    double a0 = 1 + alpha;
    double a1 = -2 * cs;
    double a2 = 1 - alpha;

    const double nrm = 1 / a0;

    b0 *= nrm;
    b1 *= nrm;
    b2 *= nrm;
    a0 *= nrm;
    a1 *= nrm;
    a2 *= nrm;

    return biquad
    (   data
    ,   b0
    ,   b1
    ,   b2
    ,   a1
    ,   a2
    );
}

vector <float> RayverbFiltering::BandpassBiquadTwopass::filter
(   const std::vector <float> & data
) const
{
    BandpassBiquadOnepass b (lo, hi, sr);
    vector <float> p1 = b.filter (data);
    reverse (std::begin (p1), std::end (p1));
    vector <float> p2 = b.filter (p1);
    reverse (std::begin (p2), std::end (p2));
    return p2;
}
