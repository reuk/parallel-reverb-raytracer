#include "rayverb.h"

#include "assimp/Importer.hpp"
#include "assimp/postprocess.h"
#include "assimp/scene.h"

#include "fftw3.h"

#include <cmath>
#include <numeric>
#include <fstream>
#include <streambuf>
#include <iostream>

//#define USE_OBJECT_MATERIALS

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

vector <float> fastConvolve (const vector <float> & a, const vector <float> & b)
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

vector <float> bandpassKernel (float sr, float lo, float hi, unsigned long l)
{
    vector <float> lop = lopassKernel (sr, hi, l);
    vector <float> hip = hipassKernel (sr, lo, l);
    return fastConvolve (lop, hip);
}

cl_float3 fromAIVec (const aiVector3D & v)
{
    return (cl_float3) {v.x, v.y, v.z, 0};
}

struct LatestImpulse: public binary_function <Impulse, Impulse, bool>
{
    inline bool operator() (const Impulse & a, const Impulse & b) const throw()
    {
        return a.time < b.time;
    }
};

template<>
struct plus <cl_float3>
:   public binary_function <cl_float3, cl_float3, cl_float3>
{
    inline cl_float3 operator() (const cl_float3 & a, const cl_float3 & b)
    {
        return (cl_float3)
        {   a.s [0] + b.s [0]
        ,   a.s [1] + b.s [1]
        ,   a.s [2] + b.s [2]
        ,   a.s [3] + b.s [3]
        };
    }
};

template<>
struct plus <cl_float8>
:   public binary_function <cl_float8, cl_float8, cl_float8>
{
    inline cl_float8 operator() (const cl_float8 & a, const cl_float8 & b)
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

template <typename T>
inline T sum (const T & a, const T & b)
{
    return plus <T>() (a, b);
}

vector <VolumeType> flattenImpulses
(   const vector <Impulse> & impulse
,   float samplerate
) throw()
{
    const float MAX_TIME = max_element
    (   begin (impulse)
    ,   end (impulse)
    ,   LatestImpulse()
    )->time;
    const unsigned long MAX_SAMPLE = round (MAX_TIME * samplerate) + 1;

    vector <VolumeType> flattened (MAX_SAMPLE, (VolumeType) {0});

    for (const auto & i : impulse)
    {
        const unsigned long SAMPLE = round (i.time * samplerate);
        flattened [SAMPLE] = sum (flattened [SAMPLE], i.volume);
    }

    return flattened;
}

template <typename T>
vector <float> bandpass (const vector <T> & data, float lo, float hi, float sr, int index) throw()
{
    vector <float> input (data.size());
    transform (begin (data), end (data), begin (input), [&] (const T & t) { return t.s [index]; });
    vector <float> kernel = bandpassKernel (sr, lo, hi, 127);
    return fastConvolve (input, kernel);
}

vector <float> filter
(   vector <cl_float8> & data
,   float b0
,   float b1
,   float b2
,   float b3
,   float b4
,   float b5
,   float b6
,   float b7
,   float sr
)   throw()
{
    vector <float> x0 = bandpass (data, b0, b1, sr, 0);
    vector <float> x1 = bandpass (data, b1, b2, sr, 1);
    vector <float> x2 = bandpass (data, b2, b3, sr, 2);
    vector <float> x3 = bandpass (data, b3, b4, sr, 3);
    vector <float> x4 = bandpass (data, b4, b5, sr, 4);
    vector <float> x5 = bandpass (data, b5, b6, sr, 5);
    vector <float> x6 = bandpass (data, b6, b7, sr, 6);

    vector <float> ret (x0.size());
    for (unsigned long i = 0; i != x0.size(); ++i)
    {
        ret [i] = x0 [i] + x1 [i] + x2 [i] + x3 [i] + x4 [i] + x5 [i] + x6 [i];
    }

    return ret;
}

void hipass (vector <float> & data, float lo, float sr) throw()
{
    const float loParam = 1 - exp (-2 * M_PI * lo / sr);
    float loState = 0;

    for (auto & i : data)
        i -= loState += loParam * (i - loState);
}

template <typename T>
vector <float> sum (const vector <T> & data) throw()
{
    vector <float> ret (data.size());
    transform
    (   begin (data)
    ,   end (data)
    ,   begin (ret)
    ,   [&] (const T & i)
        {
            return accumulate (i.s, i.s + sizeof (T) / sizeof (float), 0.0);
        }
    );
    return ret;
}

vector <vector <float>> process
(   vector <vector <VolumeType>> & data
,   float sr
) throw()
{
    vector <vector <float>> ret (data.size());

    for (int i = 0; i != data.size(); ++i)
    {
        //filter (data [i], 200, 2000, sr);
        //filter (data [i], 125, 250, 500, 1000, 2000, 4000, 8000, sr);
        ret [i] = filter (data [i], 20, 200, 400, 800, 1600, 3200, 6400, 12800, sr);
        //ret [i] = sum (data [i]);
        hipass (ret [i], 20, sr);
    }

    normalize (ret);

    return ret;
}

Scene::Scene
(   cl::Context & cl_context
,   unsigned long nreflections
,   vector <cl_float3> & directions
,   vector <Triangle> & triangles
,   vector <cl_float3> & vertices
,   vector <Surface> & surfaces
,   bool verbose
)
:   nrays (directions.size())
,   nreflections (nreflections)
,   ntriangles (triangles.size())
,   cl_context (cl_context)
,   cl_directions (cl_context, begin (directions), end (directions), false)
,   cl_triangles  (cl_context, begin (triangles),  end (triangles),  false)
,   cl_vertices   (cl_context, begin (vertices),   end (vertices),   false)
,   cl_surfaces   (cl_context, begin (surfaces),   end (surfaces),   false)
,   cl_impulses
    (   cl_context
    ,   CL_MEM_READ_WRITE
    ,   directions.size() * nreflections * sizeof (Impulse)
    )
,   cl_attenuated
    (   cl_context
    ,   CL_MEM_READ_WRITE
    ,   directions.size() * nreflections * sizeof (Impulse)
    )
{
    ifstream cl_source_file ("kernel.cl");
    string cl_source_string
    (   (istreambuf_iterator <char> (cl_source_file))
    ,   istreambuf_iterator <char> ()
    );

    cl_program = cl::Program (cl_context, cl_source_string, false);

    vector <cl::Device> device = cl_context.getInfo <CL_CONTEXT_DEVICES>();
    vector <cl::Device> used_devices (device.end() - 1, device.end());

    cl_program.build (used_devices);
    cl::Device used_device = used_devices.front();

    cerr
    <<  cl_program.getBuildInfo <CL_PROGRAM_BUILD_LOG> (used_device)
    <<  endl;

    queue = cl::CommandQueue (cl_context, used_device);
}

struct Scene::SceneData
{
public:
    SceneData (const std::string & objpath)
    {
        populate (objpath);
    }

    void populate (const aiScene * scene)
    {
        if (! scene)
            throw runtime_error ("Failed to load object file.");

        for (unsigned long i = 0; i != scene->mNumMeshes; ++i)
        {
            const aiMesh * mesh = scene->mMeshes [i];

            vector <cl_float3> meshVertices (mesh->mNumVertices);

            for (unsigned long j = 0; j != mesh->mNumVertices; ++j)
            {
                meshVertices [j] = fromAIVec (mesh->mVertices [j]);
            }

#ifdef USE_OBJECT_MATERIALS
            const aiMaterial * material = scene->mMaterials
            [   mesh->mMaterialIndex
            ];

            aiColor3D diffuse (0.9, 0.8, 0.7);
            material->Get(AI_MATKEY_COLOR_DIFFUSE, diffuse);

            aiColor3D specular (0.9, 0.8, 0.7);
            material->Get(AI_MATKEY_COLOR_SPECULAR, specular);

            Surface surface = {
                (cl_float3) {specular.r, specular.g, specular.b, 0},
                (cl_float3) {diffuse.r, diffuse.g, diffuse.b, 0}
            };
#else
            Surface surface = {
                (VolumeType) {0.98, 0.98, 0.97, 0.97, 0.96, 0.95, 0.95, 0.00},
                (VolumeType) {0.50, 0.90, 0.95, 0.95, 0.95, 0.95, 0.95, 0.00}
            };
#endif

            surfaces.push_back (surface);

            vector <Triangle> meshTriangles (mesh->mNumFaces);

            for (unsigned long j = 0; j != mesh->mNumFaces; ++j)
            {
                const aiFace face = mesh->mFaces [j];

                meshTriangles [j] = (Triangle) {
                    surfaces.size() - 1,
                    vertices.size() + face.mIndices [0],
                    vertices.size() + face.mIndices [1],
                    vertices.size() + face.mIndices [2]
                };
            }

            vertices.insert
            (   vertices.end()
            ,   begin (meshVertices)
            ,   end (meshVertices)
            );

            triangles.insert
            (   triangles.end()
            ,   begin (meshTriangles)
            ,   end (meshTriangles)
            );
        }
    }

    void populate (const std::string & objpath)
    {
        Assimp::Importer importer;
        populate
        (   importer.ReadFile
            (   objpath
            ,   (   aiProcess_Triangulate
                |   aiProcess_GenSmoothNormals
                |   aiProcess_FlipUVs
                )
            )
        );
    }

    bool validSurfaces()
    {
        for (const Surface & s : surfaces)
        {
            for (int i = 0; i != 3; ++i)
            {
                if
                (   s.specular.s [i] < 0 || 1 < s.specular.s [i]
                ||  s.diffuse.s [i] < 0 || 1 < s.diffuse.s [i]
                )
                    return false;
            }
        }

        return true;
    }

    bool validTriangles()
    {
        for (const Triangle & t : triangles)
        {
            if
            (   surfaces.size() <= t.surface
            ||  vertices.size() <= t.v0
            ||  vertices.size() <= t.v1
            ||  vertices.size() <= t.v2
            )
                return false;
        }

        return true;
    }

    bool valid()
    {
        if (triangles.empty() || vertices.empty() || surfaces.empty())
            return false;

        return validSurfaces() && validTriangles();
    }

    std::vector <Triangle> triangles;
    std::vector <cl_float3> vertices;
    std::vector <Surface> surfaces;
};

Scene::Scene
(   cl::Context & cl_context
,   unsigned long nreflections
,   std::vector <cl_float3> & directions
,   SceneData sceneData
,   bool verbose
)
:   Scene
(   cl_context
,   nreflections
,   directions
,   sceneData.triangles
,   sceneData.vertices
,   sceneData.surfaces
)
{
}

Scene::Scene
(   cl::Context & cl_context
,   unsigned long nreflections
,   std::vector <cl_float3> & directions
,   const std::string & objpath
,   bool verbose
)
:   Scene
(   cl_context
,   nreflections
,   directions
,   SceneData (objpath)
)
{
}

#ifdef DIAGNOSTIC
vector <Reflection> Scene::test
(   const cl_float3 & micpos
,   const cl_float3 & source
)
{
    auto test = cl::make_kernel
    <   cl::Buffer
    ,   cl_float3
    ,   cl::Buffer
    ,   unsigned long
    ,   cl::Buffer
    ,   cl::Buffer
    ,   cl::Buffer
    ,   cl::Buffer
    ,   unsigned long
    > (cl_program, "test");

    cl_reflections = cl::Buffer
    (   cl_context
    ,   CL_MEM_READ_WRITE
    ,   nrays * nreflections * sizeof (Reflection)
    );

    test
    (   cl::EnqueueArgs (queue, cl::NDRange (nrays))
    ,   cl_directions
    ,   micpos
    ,   cl_triangles
    ,   ntriangles
    ,   cl_vertices
    ,   source
    ,   cl_surfaces
    ,   cl_reflections
    ,   nreflections
    );

    vector <Reflection> reflections (nrays * nreflections);

    cl::copy
    (   queue
    ,   cl_reflections
    ,   begin (reflections)
    ,   end (reflections)
    );

    return reflections;
}
#endif

void Scene::trace (const cl_float3 & micpos, const cl_float3 & source)
{
    auto raytrace = cl::make_kernel
    <   cl::Buffer
    ,   cl_float3
    ,   cl::Buffer
    ,   cl_ulong
    ,   cl::Buffer
    ,   cl_float3
    ,   cl::Buffer
    ,   cl::Buffer
    ,   cl_ulong
    > (cl_program, "raytrace");

    raytrace
    (   cl::EnqueueArgs (queue, cl::NDRange (nrays))
    ,   cl_directions
    ,   micpos
    ,   cl_triangles
    ,   ntriangles
    ,   cl_vertices
    ,   source
    ,   cl_surfaces
    ,   cl_impulses
    ,   nreflections
    );
}

vector <Impulse> Scene::attenuate (const Speaker & speaker)
{
    auto attenuate = cl::make_kernel
    <   cl::Buffer
    ,   cl::Buffer
    ,   cl_ulong
    ,   Speaker
    > (cl_program, "attenuate");

    attenuate
    (   cl::EnqueueArgs (queue, cl::NDRange (nrays))
    ,   cl_impulses
    ,   cl_attenuated
    ,   nreflections
    ,   speaker
    );

    vector <Impulse> attenuated (nreflections * nrays);

    cl::copy
    (   queue
    ,   cl_attenuated
    ,   begin (attenuated)
    ,   end (attenuated)
    );

    return attenuated;
}

vector <vector <Impulse>> Scene::attenuate (const vector <Speaker> & speakers)
{
    vector <vector <Impulse>> attenuated (speakers.size());
    transform
    (   begin (speakers)
    ,   end (speakers)
    ,   begin (attenuated)
    ,   [&] (const Speaker & i) {return attenuate (i);}
    );
    return attenuated;
}

