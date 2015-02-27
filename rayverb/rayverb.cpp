#include "rayverb.h"
#include "filters.h"
#include "config.h"

#include "rapidjson/rapidjson.h"
#include "rapidjson/error/en.h"
#include "rapidjson/document.h"

#include "assimp/Importer.hpp"
#include "assimp/postprocess.h"
#include "assimp/scene.h"

#include <cmath>
#include <numeric>
#include <fstream>
#include <streambuf>
#include <sstream>
#include <map>
#include <iomanip>

using namespace std;
using namespace rapidjson;

inline cl_float3 fromAIVec (const aiVector3D & v)
{
    return (cl_float3) {{v.x, v.y, v.z, 0}};
}

vector <vector <vector <float>>> flattenImpulses
(   const vector <vector <Impulse>> & attenuated
,   float samplerate
)
{
    vector <vector <vector <float>>> flattened (attenuated.size());
    transform
    (   begin (attenuated)
    ,   end (attenuated)
    ,   begin (flattened)
    ,   [samplerate] (const auto & i)
        {
            return flattenImpulses (i, samplerate);
        }
    );
    return flattened;
}

vector <vector <float>> flattenImpulses
(   const vector <Impulse> & impulse
,   float samplerate
)
{
    float maxtime = 0;
    for (const auto & i : impulse)
        maxtime = max (maxtime, i.time);
    const auto MAX_SAMPLE = round (maxtime * samplerate) + 1;

    vector <vector <float>> flattened
    (   sizeof (VolumeType) / sizeof (float)
    ,   vector <float> (MAX_SAMPLE, 0)
    );

    for (const auto & i : impulse)
    {
        const auto SAMPLE = round (i.time * samplerate);
        for (auto j = 0; j != flattened.size(); ++j)
        {
            flattened [j] [SAMPLE] += i.volume.s [j];
        }
    }

    return flattened;
}

template <typename T>
vector <float> sum (const vector <T> & data)
{
    vector <float> ret (data.size());
    transform
    (   begin (data)
    ,   end (data)
    ,   begin (ret)
    ,   [] (const T & i)
        {
            return accumulate (i.s, i.s + sizeof (T) / sizeof (float), 0.0);
        }
    );
    return ret;
}

vector <float> mixdown (const vector <vector <float>> & data)
{
    vector <float> ret (data.front().size(), 0);
    for (auto && i : data)
        transform
        (   ret.begin()
        ,   ret.end()
        ,   i.begin()
        ,   ret.begin()
        ,   plus <float>()
        );
    return ret;
}

void trimTail (vector <vector <float>> & audioChannels, float minVol)
{
    auto len = accumulate
    (   audioChannels.begin()
    ,   audioChannels.end()
    ,   0
    ,   [minVol] (long current, const auto & i)
        {
            return max
            (   current
            ,   distance
                (   i.begin()
                ,   find_if
                    (   i.rbegin()
                    ,   i.rend()
                    ,   [minVol] (auto j) {return abs (j) >= minVol;}
                    ).base()
                ) - 1
            );
        }
    );

    for (auto && i : audioChannels)
        i.resize (len);
}

vector <vector <float>> process
(   RayverbFiltering::FilterType filtertype
,   vector <vector <vector <float>>> & data
,   float sr
,   bool do_normalize
,   bool do_hipass
,   bool do_trim_tail
,   float volume_scale
)
{
    RayverbFiltering::filter (filtertype, data, sr);
    vector <vector <float>> ret (data.size());
    transform (data.begin(), data.end(), ret.begin(), mixdown);

    if (do_hipass)
    {
        RayverbFiltering::HipassWindowedSinc hp (ret.front().size());
        hp.setParams (10, sr);
        for (auto && i : ret)
            hp.filter (i);
    }

    if (do_normalize)
        normalize (ret);

    if (volume_scale != 1)
        mul (ret, volume_scale);

    if (do_trim_tail)
        trimTail (ret, 0.00001);

    return ret;
}

ContextProvider::ContextProvider()
{
    vector <cl::Platform> platform;
    cl::Platform::get (&platform);

    cl_context_properties cps [3] = {
        CL_CONTEXT_PLATFORM,
        (cl_context_properties) (platform [0]) (),
        0
    };

    cl_context = cl::Context (CL_DEVICE_TYPE_GPU, cps);
}

KernelLoader::KernelLoader (cl::Context & cl_context)
:   cl_program (cl_context, KERNEL_STRING, false)
{
    vector <cl::Device> device = cl_context.getInfo <CL_CONTEXT_DEVICES>();
    vector <cl::Device> used_devices (device.end() - 1, device.end());

    cl_program.build (used_devices);
    cl::Device used_device = used_devices.front();

    cerr
    <<  cl_program.getBuildInfo <CL_PROGRAM_BUILD_LOG> (used_device)
    <<  endl;

    queue = cl::CommandQueue (cl_context, used_device);
}

pair <cl_float3, cl_float3> Scene::getBounds
(   const vector <cl_float3> & vertices
)
{
    return pair <cl_float3, cl_float3>
    (   accumulate
        (   vertices.begin() + 1
        ,   vertices.end()
        ,   vertices.front()
        ,   [] (const auto & a, const auto & b)
            {
                return elementwise (a, b, [] (auto i, auto j) {return min (i, j);});
            }
        )
    ,   accumulate
        (   vertices.begin() + 1
        ,   vertices.end()
        ,   vertices.front()
        ,   [] (const auto & a, const auto & b)
            {
                return elementwise (a, b, [] (auto i, auto j) {return max (i, j);});
            }
        )
    );
}

Scene::Scene
(   unsigned long nreflections
,   vector <Triangle> & triangles
,   vector <cl_float3> & vertices
,   vector <Surface> & surfaces
)
:   KernelLoader (cl_context)
,   ngroups (0)
,   nreflections (nreflections)
,   ntriangles (triangles.size())
,   cl_directions
    (   cl_context
    ,   CL_MEM_READ_WRITE
    ,   RAY_GROUP_SIZE * sizeof (cl_float3)
    )
,   cl_triangles  (cl_context, begin (triangles),  end (triangles),  false)
,   cl_vertices   (cl_context, begin (vertices),   end (vertices),   false)
,   cl_surfaces   (cl_context, begin (surfaces),   end (surfaces),   false)
,   cl_impulses
    (   cl_context
    ,   CL_MEM_READ_WRITE
    ,   RAY_GROUP_SIZE * nreflections * sizeof (Impulse)
    )
,   cl_attenuated
    (   cl_context
    ,   CL_MEM_READ_WRITE
    ,   RAY_GROUP_SIZE * nreflections * sizeof (Impulse)
    )
,   cl_image_source
    (   cl_context
    ,   CL_MEM_READ_WRITE
    ,   RAY_GROUP_SIZE * NUM_IMAGE_SOURCE * sizeof (Impulse)
    )
,   cl_image_source_index
    (   cl_context
    ,   CL_MEM_READ_WRITE
    ,   RAY_GROUP_SIZE * NUM_IMAGE_SOURCE * sizeof (cl_ulong)
    )
,   bounds (getBounds (vertices))
,   raytrace_kernel
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
    )
,   attenuate_kernel
    (   cl::make_kernel
        <   cl_float3
        ,   cl::Buffer
        ,   cl::Buffer
        ,   cl_ulong
        ,   Speaker
        > (cl_program, "attenuate")
    )
,   hrtf_kernel
    (   cl::make_kernel
        <   cl_float3
        ,   cl::Buffer
        ,   cl::Buffer
        ,   cl_ulong
        ,   cl::Buffer
        ,   cl_float3
        ,   cl_float3
        ,   cl_ulong
        > (cl_program, "hrtf")
    )
{
}

void attemptJsonParse (const string & fname, Document & doc)
{
    ifstream in (fname);
    string file
    (   (istreambuf_iterator <char> (in))
    ,   istreambuf_iterator <char>()
    );

    doc.Parse(file.c_str());
}

struct Scene::SceneData
{
public:
    SceneData (const string & objpath, const string & materialFileName)
    {
        populate (objpath, materialFileName);
    }

    void populate (const aiScene * scene, const string & materialFileName)
    {
        if (! scene)
            throw runtime_error ("Failed to load object file.");

        Surface surface = {
            (VolumeType) {{0.02, 0.02, 0.03, 0.03, 0.04, 0.05, 0.05, 0.05}},
            (VolumeType) {{0.50, 0.90, 0.95, 0.95, 0.95, 0.95, 0.95, 0.95}}
        };

        surfaces.push_back (surface);

        Document document;
        attemptJsonParse (materialFileName, document);
        if (! document.IsObject())
            throw runtime_error ("Materials must be stored in a JSON object");

        map <string, int> materialIndices;
        for
        (   auto i = document.MemberBegin()
        ;   i != document.MemberEnd()
        ;   ++i
        )
        {
            string name = i->name.GetString();
            Surface surface;
            JsonGetter <Surface> getter (surface);
            if (! getter.check (i->value))
            {
                throw std::runtime_error ("invalid surface");
            }
            getter.get (i->value);
            surfaces.push_back (surface);
            materialIndices [name] = surfaces.size() - 1;
        }

        for (auto i = 0; i != scene->mNumMeshes; ++i)
        {
            const aiMesh * mesh = scene->mMeshes [i];

            aiString name = mesh->mName;
            cerr << "Found mesh: " << name.C_Str() << endl;

            unsigned long mat_index = 0;
            auto nameIterator = materialIndices.find (name.C_Str());
            if (nameIterator != materialIndices.end())
                mat_index = nameIterator->second;

            Surface surface = surfaces [mat_index];

            vector <cl_float3> meshVertices (mesh->mNumVertices);

            for (auto j = 0; j != mesh->mNumVertices; ++j)
            {
                meshVertices [j] = fromAIVec (mesh->mVertices [j]);
            }

            const aiMaterial * material =
                scene->mMaterials [mesh->mMaterialIndex];
            material->Get (AI_MATKEY_NAME, name);

            cerr << "    Material name: " << name.C_Str() << endl;

            cerr << "    Material properties: " << endl;
            cerr << "        specular: ["
                 << surface.specular.s [0] << ", "
                 << surface.specular.s [1] << ", "
                 << surface.specular.s [2] << ", "
                 << surface.specular.s [3] << ", "
                 << surface.specular.s [4] << ", "
                 << surface.specular.s [5] << ", "
                 << surface.specular.s [6] << ", "
                 << surface.specular.s [7] << "]"
                 << endl;
            cerr << "        diffuse: ["
                 << surface.diffuse.s [0] << ", "
                 << surface.diffuse.s [1] << ", "
                 << surface.diffuse.s [2] << ", "
                 << surface.diffuse.s [3] << ", "
                 << surface.diffuse.s [4] << ", "
                 << surface.diffuse.s [5] << ", "
                 << surface.diffuse.s [6] << ", "
                 << surface.diffuse.s [7] << "]"
                 << endl;

            vector <Triangle> meshTriangles (mesh->mNumFaces);

            for (auto j = 0; j != mesh->mNumFaces; ++j)
            {
                const aiFace face = mesh->mFaces [j];

                meshTriangles [j] = (Triangle) {
                    mat_index,
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

        cerr
        <<  "Loaded 3D model with "
        <<  triangles.size()
        <<  " triangles"
        <<  endl;
    }

    void populate (const string & objpath, const string & materialFileName)
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
        ,   materialFileName
        );
    }

    bool validSurfaces()
    {
        for (const auto & s : surfaces)
        {
            for (auto i = 0; i != 3; ++i)
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
        for (const auto & t : triangles)
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

    vector <Triangle> triangles;
    vector <cl_float3> vertices;
    vector <Surface> surfaces;
};

Scene::Scene
(   unsigned long nreflections
,   SceneData sceneData
)
:   Scene
(   nreflections
,   sceneData.triangles
,   sceneData.vertices
,   sceneData.surfaces
)
{
}

Scene::Scene
(   unsigned long nreflections
,   const string & objpath
,   const string & materialFileName
)
:   Scene
(   nreflections
,   SceneData (objpath, materialFileName)
)
{
}

bool inside (const std::pair <cl_float3, cl_float3> & bounds, const cl_float3 & point)
{
    for (auto i = 0; i != sizeof (cl_float3) / sizeof (float); ++i)
        if (! (bounds.first.s [i] <= point.s [i] && point.s [i] <= bounds.second.s [i]))
            return false;
    return true;
}

void Scene::trace
(   const cl_float3 & micpos
,   const cl_float3 & source
,   const vector <cl_float3> & directions
)
{
    //  check that mic and source are inside model bounds
    bool micinside = inside (bounds, micpos);
    bool srcinside = inside (bounds, source);
    if (! (micinside && srcinside))
    {
        cerr
        <<  "model bounds: ["
        <<  bounds.first.s [0] << ", "
        <<  bounds.first.s [1] << ", "
        <<  bounds.first.s [2] << "], ["
        <<  bounds.second.s [0] << ", "
        <<  bounds.second.s [1] << ", "
        <<  bounds.second.s [2] << "]"
        <<  endl;

        if (! micinside)
        {
            cerr << "WARNING: microphone position may be outside model" << endl;
            cerr
            <<  "mic position: ["
            <<  micpos.s [0] << ", "
            <<  micpos.s [1] << ", "
            <<  micpos.s [2] << "]"
            <<  endl;
        }

        if (! srcinside)
        {
            cerr << "WARNING: source position may be outside model" << endl;
            cerr
            <<  "src position: ["
            <<  source.s [0] << ", "
            <<  source.s [1] << ", "
            <<  source.s [2] << "]"
            <<  endl;
        }
    }

    ngroups = directions.size() / RAY_GROUP_SIZE;

    storedDiffuse.resize (ngroups * RAY_GROUP_SIZE * nreflections);

    map <vector <unsigned long>, Impulse> imageSourceTally;

    for (auto i = 0; i != ngroups; ++i)
    {
        cl::copy
        (   queue
        ,   directions.begin() + (i + 0) * RAY_GROUP_SIZE
        ,   directions.begin() + (i + 1) * RAY_GROUP_SIZE
        ,   cl_directions
        );

        vector <Impulse> diffuse
            (RAY_GROUP_SIZE * nreflections, (Impulse) {{{0}}});
        cl::copy (queue, begin (diffuse), end (diffuse), cl_impulses);

        vector <Impulse> image
            (RAY_GROUP_SIZE * NUM_IMAGE_SOURCE, (Impulse) {{{0}}});
        cl::copy (queue, begin (image), end (image), cl_image_source);

        vector <unsigned long> image_source_index
            (RAY_GROUP_SIZE * NUM_IMAGE_SOURCE, 0);
        cl::copy
        (   queue
        ,   begin (image_source_index)
        ,   end (image_source_index)
        ,   cl_image_source_index
        );

        raytrace_kernel
        (   cl::EnqueueArgs (queue, cl::NDRange (RAY_GROUP_SIZE))
        ,   cl_directions
        ,   micpos
        ,   cl_triangles
        ,   ntriangles
        ,   cl_vertices
        ,   source
        ,   cl_surfaces
        ,   cl_impulses
        ,   cl_image_source
        ,   cl_image_source_index
        ,   nreflections
        );

        cl::copy
        (   queue
        ,   cl_image_source_index
        ,   begin (image_source_index)
        ,   end (image_source_index)
        );
        cl::copy (queue, cl_image_source, begin (image), end (image));

        for
        (   auto j = 0
        ;   j != RAY_GROUP_SIZE * NUM_IMAGE_SOURCE
        ;   j += NUM_IMAGE_SOURCE
        )
        {
            for (auto k = 1; k != NUM_IMAGE_SOURCE + 1; ++k)
            {
                vector <unsigned long> surfaces
                (   image_source_index.begin() + j
                ,   image_source_index.begin() + j + k
                );

                if (k == 1 || surfaces.back() != 0)
                {
                    auto it = imageSourceTally.find (surfaces);
                    if (it == imageSourceTally.end())
                    {
                        imageSourceTally [surfaces] = image [j + k - 1];
                    }
                }
            }
        }

        cl::copy
        (   queue
        ,   cl_impulses
        ,   storedDiffuse.begin() + (i + 0) * RAY_GROUP_SIZE * nreflections
        ,   storedDiffuse.begin() + (i + 1) * RAY_GROUP_SIZE * nreflections
        );
    }

    storedImage.resize (imageSourceTally.size());
    transform
    (   begin (imageSourceTally)
    ,   end (imageSourceTally)
    ,   begin (storedImage)
    ,   [] (const auto & i) {return i.second;}
    );

    const auto MULTIPLIER = RAY_GROUP_SIZE * NUM_IMAGE_SOURCE;
    storedImage.resize
    (   MULTIPLIER * ceil (storedImage.size() / float (MULTIPLIER))
    ,   (Impulse) {{{0}}}
    );
}

vector <vector <Impulse>> Scene::attenuate
(   const cl_float3 & mic_pos
,   const vector <Speaker> & speakers
)
{
    vector <vector <Impulse>> attenuated (speakers.size());
    transform
    (   begin (speakers)
    ,   end (speakers)
    ,   begin (attenuated)
    ,   [this, mic_pos] (const auto & i) {return attenuate (mic_pos, i);}
    );
    return attenuated;
}

vector <Impulse> Scene::attenuate
(   const cl_float3 & mic_pos
,   const Speaker & speaker
,   const vector <Impulse> & impulses
,   const unsigned long jump_size
)
{
    cout << "image size " << storedImage.size() << endl;
    cout << "diff size  " << storedDiffuse.size() << endl;

    const auto chunk_size = RAY_GROUP_SIZE * jump_size;
    vector <Impulse> ret (impulses.size());
    for (auto i = 0; i != impulses.size() / chunk_size; ++i)
    {
        cl::copy
        (   queue
        ,   impulses.begin() + (i + 0) * chunk_size
        ,   impulses.begin() + (i + 1) * chunk_size
        ,   cl_impulses
        );
        attenuate_kernel
        (   cl::EnqueueArgs (queue, cl::NDRange (RAY_GROUP_SIZE))
        ,   mic_pos
        ,   cl_impulses
        ,   cl_attenuated
        ,   jump_size
        ,   speaker
        );
        cl::copy
        (   queue
        ,   cl_attenuated
        ,   ret.begin() + (i + 0) * chunk_size
        ,   ret.begin() + (i + 1) * chunk_size
        );
    }
    return ret;
}

vector <Impulse> Scene::attenuate
(   const cl_float3 & mic_pos
,   const Speaker & speaker
)
{
    auto retDiffuse = attenuate (mic_pos, speaker, storedDiffuse, nreflections);
    auto retImage = attenuate (mic_pos, speaker, storedImage, NUM_IMAGE_SOURCE);
    retDiffuse.insert (retDiffuse.end(), retImage.begin(), retImage.end());
    return retDiffuse;
}

vector <Impulse> Scene::getRawDiffuse()
{
    return storedDiffuse;
}

vector <Impulse> Scene::getRawImages()
{
    return storedImage;
}

vector <vector <Impulse>> Scene::hrtf
(   const cl_float3 & mic_pos
,   const cl_float3 & facing
,   const cl_float3 & up
)
{
    cl_hrtf = cl::Buffer
    (   cl_context
    ,   CL_MEM_READ_WRITE
    ,   sizeof (VolumeType) * 360 * 180
    );

    auto channels = {0, 1};
    vector <vector <Impulse>> attenuated (channels.size());
    transform
    (   begin (channels)
    ,   end (channels)
    ,   begin (attenuated)
    ,   [this, mic_pos, facing, up] (auto i)
        {
            return hrtf (mic_pos, i, facing, up);
        }
    );
    return attenuated;
}

std::vector <Impulse> Scene::hrtf
(   const cl_float3 & mic_pos
,   unsigned long channel
,   const cl_float3 & facing
,   const cl_float3 & up
,   const std::vector <Impulse> & impulses
,   const unsigned long jump_size
)
{
    const auto chunk_size = RAY_GROUP_SIZE * jump_size;
    vector <Impulse> ret (impulses.size());
    for (auto i = 0; i != impulses.size() / chunk_size; ++i)
    {
        cl::copy
        (   queue
        ,   impulses.begin() + (i + 0) * chunk_size
        ,   impulses.begin() + (i + 1) * chunk_size
        ,   cl_impulses
        );
        hrtf_kernel
        (   cl::EnqueueArgs (queue, cl::NDRange (RAY_GROUP_SIZE))
        ,   mic_pos
        ,   cl_impulses
        ,   cl_attenuated
        ,   jump_size
        ,   cl_hrtf
        ,   facing
        ,   up
        ,   channel
        );
        cl::copy
        (   queue
        ,   cl_attenuated
        ,   ret.begin() + (i + 0) * chunk_size
        ,   ret.begin() + (i + 1) * chunk_size
        );
    }
    return ret;
}

vector <Impulse> Scene::hrtf
(   const cl_float3 & mic_pos
,   unsigned long channel
,   const cl_float3 & facing
,   const cl_float3 & up
)
{
    vector <VolumeType> hrtfChannelData (360 * 180);

    auto offset = 0;
    for (auto && i : HRTF_DATA [channel])
    {
        copy (begin (i), end (i), hrtfChannelData.begin() + offset);
        offset += i.size();
    }

    cl::copy
    (   queue
    ,   begin (hrtfChannelData)
    ,   end (hrtfChannelData)
    ,   cl_hrtf
    );

    auto retDiffuse = hrtf (mic_pos, channel, facing, up, storedDiffuse, nreflections);
    auto retImage = hrtf (mic_pos, channel, facing, up, storedImage, NUM_IMAGE_SOURCE);
    retDiffuse.insert (retDiffuse.end(), retImage.begin(), retImage.end());
    return retDiffuse;
}
