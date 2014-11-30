#include "filters.h"

#include "fftw3.h"

#include <cmath>
#include <numeric>

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
    //memset (o, 0, sizeof (fftwf_complex) * CPLX_LENGTH);
    memcpy (i, data.data(), sizeof (float) * data.size());
    fftwf_execute (plan);
    memcpy (results, o, sizeof (fftwf_complex) * CPLX_LENGTH);
}

std::vector <float> RayverbFiltering::fastConvolve
(   const std::vector <float> & a
,   const std::vector <float> & b
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

vector <float> RayverbFiltering::bandpassKernel
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
