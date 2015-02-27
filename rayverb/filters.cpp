#include "filters.h"

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
    for (auto i = 0; i != length; ++i)
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
    const auto a0 = 7938.0 / 18608.0;
    const auto a1 = 9240.0 / 18608.0;
    const auto a2 = 1430.0 / 18608.0;

    vector <T> ret (length);
    for (auto i = 0; i != length; ++i)
    {
        const auto offset = i / (length - 1.0);
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
    auto window = blackman <float> (length);
    auto kernel = sincKernel <float> (cutoff / sr, length);
    vector <float> ret (length);
    transform
    (   begin (window)
    ,   end (window)
    ,   begin (kernel)
    ,   begin (ret)
    ,   [] (auto i, auto j) { return i * j; }
    );
    auto sum = accumulate (begin (ret), end (ret), 0.0);
    for (auto && i : ret) i /= sum;
    return ret;
}

vector <float> hipassKernel (float sr, float cutoff, unsigned long length)
{
    auto kernel = lopassKernel (sr, cutoff, length);
    for (auto && i : kernel) i = -i;
    kernel [(length - 1) / 2] += 1;
    return kernel;
}

void RayverbFiltering::Hipass::setParams (float co, float s)
{
    cutoff = co;
    sr = s;
}

void RayverbFiltering::Bandpass::setParams (float l, float h, float s)
{
    lo = l;
    hi = h;
    sr = s;
}

RayverbFiltering::HipassWindowedSinc::HipassWindowedSinc (unsigned long inputLength)
:   FastConvolution (KERNEL_LENGTH + inputLength - 1)
{

}

void RayverbFiltering::HipassWindowedSinc::filter (vector <float> & data)
{
    data = convolve (kernel, data);
}

void RayverbFiltering::HipassWindowedSinc::setParams
(   float co
,   float s
)
{
    auto i = hipassKernel (s, co, KERNEL_LENGTH);
    copy (i.begin(), i.end(), kernel.begin());
}

RayverbFiltering::BandpassWindowedSinc::BandpassWindowedSinc (unsigned long inputLength)
:   FastConvolution (KERNEL_LENGTH + inputLength - 1)
{

}

vector <float> RayverbFiltering::BandpassWindowedSinc::bandpassKernel
(   float sr
,   float lo
,   float hi
)
{
    auto lop = lopassKernel (sr, hi, 1 + KERNEL_LENGTH / 2);
    auto hip = hipassKernel (sr, lo, 1 + KERNEL_LENGTH / 2);

    FastConvolution fc (KERNEL_LENGTH);
    return fc.convolve (lop, hip);
}

void RayverbFiltering::BandpassWindowedSinc::filter (vector <float> & data)
{
    data = convolve (kernel, data);
}

void RayverbFiltering::BandpassWindowedSinc::setParams
(   float l
,   float h
,   float s
)
{
    auto i = bandpassKernel (s, l, h);
    copy (i.begin(), i.end(), kernel.begin());
}

void RayverbFiltering::BandpassBiquadOnepass::biquad
(   vector <float> & input
,   double b0
,   double b1
,   double b2
,   double a1
,   double a2
)
{
    double z1 = 0;
    double z2 = 0;

    for (auto && i : input)
    {
        double out = i * b0 + z1;
        z1 = i * b1 + z2 - a1 * out;
        z2 = i * b2 - a2 * out;
        i = out;
    }
}

void RayverbFiltering::BandpassBiquadOnepass::filter
(   vector <float> & data
)
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

    biquad (data, b0, b1, b2, a1, a2);
}

void RayverbFiltering::BandpassBiquadTwopass::filter (vector <float> & data)
{
    BandpassBiquadOnepass b;
    b.setParams (lo, hi, sr);
    b.filter (data);
    reverse (begin (data), end (data));
    b.filter (data);
    reverse (begin (data), end (data));
}

void RayverbFiltering::filter
(   FilterType ft
,   vector <vector <vector <float>>> & data
,   float sr
)
{
    unique_ptr <Bandpass> bp;

    switch (ft)
    {
    case FILTER_TYPE_WINDOWED_SINC:
        bp = unique_ptr <Bandpass> (new BandpassWindowedSinc (data.front().front().size()));
        break;
    case FILTER_TYPE_BIQUAD_ONEPASS:
        bp = unique_ptr <Bandpass> (new BandpassBiquadOnepass());
        break;
    case FILTER_TYPE_BIQUAD_TWOPASS:
        bp = unique_ptr <Bandpass> (new BandpassBiquadTwopass());
        break;
    }

    for (auto && channel : data)
    {
        const vector <float> EDGES
            ({90, 175, 350, 700, 1400, 2800, 5600, 11200, 20000});

        for (auto i = 0; i != channel.size(); ++i)
        {
            bp->setParams (EDGES [i], EDGES [i + 1], sr);
            bp->filter (channel [i]);
        }
    }
}


RayverbFiltering::FastConvolution::FastConvolution (unsigned long FFT_LENGTH)
:   FFT_LENGTH (FFT_LENGTH)
,   r2c_i (fftwf_alloc_real (FFT_LENGTH))
,   r2c_o (fftwf_alloc_complex (CPLX_LENGTH))
,   c2r_i (fftwf_alloc_complex (CPLX_LENGTH))
,   c2r_o (fftwf_alloc_real (FFT_LENGTH))
,   acplx (fftwf_alloc_complex (CPLX_LENGTH))
,   bcplx (fftwf_alloc_complex (CPLX_LENGTH))
,   r2c
    (   fftwf_plan_dft_r2c_1d
        (   FFT_LENGTH
        ,   r2c_i
        ,   r2c_o
        ,   FFTW_ESTIMATE
        )
    )
,   c2r
    (   fftwf_plan_dft_c2r_1d
        (   FFT_LENGTH
        ,   c2r_i
        ,   c2r_o
        ,   FFTW_ESTIMATE
        )
    )
{

}

RayverbFiltering::FastConvolution::~FastConvolution()
{
    fftwf_destroy_plan (r2c);
    fftwf_destroy_plan (c2r);
    fftwf_free (r2c_i);
    fftwf_free (r2c_o);
    fftwf_free (acplx);
    fftwf_free (bcplx);
    fftwf_free (c2r_i);
    fftwf_free (c2r_o);
}
