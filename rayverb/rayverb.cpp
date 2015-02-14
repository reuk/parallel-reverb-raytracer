#include "rayverb.h"
#include "filters.h"

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
    ,   [samplerate] (const vector <Impulse> & i)
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
        const long SAMPLE = (long) (i.time * samplerate + 0.5f);
        flattened [0] [SAMPLE] += i.volume.s [0];
        flattened [1] [SAMPLE] += i.volume.s [1];
        flattened [2] [SAMPLE] += i.volume.s [2];
        flattened [3] [SAMPLE] += i.volume.s [3];
        flattened [4] [SAMPLE] += i.volume.s [4];
        flattened [5] [SAMPLE] += i.volume.s [5];
        flattened [6] [SAMPLE] += i.volume.s [6];
        flattened [7] [SAMPLE] += i.volume.s [7];
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
        ,   [] (float a, float b) {return a + b;}
        );
    return ret;
}

vector <vector <float>> process
(   RayverbFiltering::FilterType filtertype
,   vector <vector <vector <float>>> & data
,   float sr
)
{
    RayverbFiltering::filter (filtertype, data, sr);
    vector <vector <float>> ret (data.size());
    transform
    (   data.begin()
    ,   data.end()
    ,   ret.begin()
    ,   [] (vector <vector <float>> & i)
        {
            return mixdown (i);
        }
    );
    normalize (ret);
    return ret;
}

Scene::Scene
(   cl::Context & cl_context
,   long nreflections
,   vector <Triangle> & triangles
,   vector <cl_float3> & vertices
,   vector <Surface> & surfaces
,   bool verbose
)
:   ngroups (0)
,   nreflections (nreflections)
,   ntriangles (triangles.size())
,   cl_context (cl_context)
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
    ,   RAY_GROUP_SIZE * NUM_IMAGE_SOURCE * sizeof (cl_long)
    )
{
    cl_program = cl::Program (cl_context, KERNEL_STRING, false);

    vector <cl::Device> device = cl_context.getInfo <CL_CONTEXT_DEVICES>();
    vector <cl::Device> used_devices (device.end() - 1, device.end());

    cl_program.build (used_devices);
    cl::Device used_device = used_devices.front();

    cerr
    <<  cl_program.getBuildInfo <CL_PROGRAM_BUILD_LOG> (used_device)
    <<  endl;

    queue = cl::CommandQueue (cl_context, used_device);
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

    void checkJsonSurfaceKey (const Value & json, const string & key)
    {
        if (! json.HasMember (key.c_str()))
            throw runtime_error
            (   string ("Surface must contain '") + key + "' key"
            );

        if (! json [key.c_str()].IsArray())
            throw runtime_error
            (   string ("Type of ") + key + " must be 'Array'"
            );

        constexpr auto bands = sizeof (VolumeType) / sizeof (float);
        if (json [key.c_str()].Size() != bands)
        {
            stringstream ss;
            ss << "Length of material description array must be " << bands;
            throw runtime_error (ss.str());
        }

        for (auto i = 0; i != bands; ++i)
            if (! json [key.c_str()] [i].IsNumber())
                throw runtime_error
                (   "Elements of material description array must be numerical"
                );
    }

    Surface jsonToSurface (const Value & json)
    {
        if (! json.IsObject())
            throw runtime_error ("Surface must be a JSON object");

        string specular_string ("specular");
        string diffuse_string ("diffuse");

        checkJsonSurfaceKey (json, specular_string);
        checkJsonSurfaceKey (json, diffuse_string);

        constexpr auto bands = sizeof (VolumeType) / sizeof (float);

        Surface ret;
        for (auto i = 0; i != bands; ++i)
        {
            ret.specular.s [i] = json [specular_string.c_str()] [i].GetDouble();
            ret.diffuse.s [i] = json [diffuse_string.c_str()] [i].GetDouble();
        }

        for (auto i = 0; i != bands; ++i)
        {
            if (ret.specular.s [i] < 0 || 1 < ret.specular.s [i])
            {
                stringstream ss;
                ss
                <<  "Invalid specular value '"
                <<  ret.specular.s [i]
                <<  "' found"
                <<  endl;
                ss << "Specular values must be in the range (0, 1)" << endl;
                throw runtime_error (ss.str());
            }
            if (ret.diffuse.s [i] < 0 || 1 < ret.diffuse.s [i])
            {
                stringstream ss;
                ss
                <<  "Invalid diffuse value '"
                <<  ret.diffuse.s [i]
                <<  "' found"
                <<  endl;
                ss << "Diffuse values must be in the range (0, 1)" << endl;
                throw runtime_error (ss.str());
            }
        }

        return ret;
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
            surfaces.push_back (jsonToSurface (i->value));
            materialIndices [name] = surfaces.size() - 1;
        }

        for (auto i = 0; i != scene->mNumMeshes; ++i)
        {
            const aiMesh * mesh = scene->mMeshes [i];

            aiString name = mesh->mName;
            cerr << "Found mesh: " << name.C_Str() << endl;

            long mat_index = 0;
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
                    static_cast <cl_long> (vertices.size() + face.mIndices [0]),
                    static_cast <cl_long> (vertices.size() + face.mIndices [1]),
                    static_cast <cl_long> (vertices.size() + face.mIndices [2])
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
        cerr << "Model bounds: " << endl;

        cl_float3 mini, maxi;
        mini = maxi = vertices.front();

        for (auto && i : vertices)
        {
            for (auto j = 0; j != 3; ++j)
            {
                mini.s [j] = min (mini.s [j], i.s [j]);
                maxi.s [j] = max (maxi.s [j], i.s [j]);
            }
        }

        cerr
        <<  " min ["
        <<  mini.s [0]
        <<  ", "
        <<  mini.s [1]
        <<  ", "
        <<  mini.s [2]
        <<  "]"
        <<  endl;
        cerr
        <<  " max ["
        <<  maxi.s [0]
        <<  ", "
        <<  maxi.s [1]
        <<  ", "
        <<  maxi.s [2]
        <<  "]"
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

    std::vector <Triangle> triangles;
    std::vector <cl_float3> vertices;
    std::vector <Surface> surfaces;
};

Scene::Scene
(   cl::Context & cl_context
,   long nreflections
,   SceneData sceneData
,   bool verbose
)
:   Scene
(   cl_context
,   nreflections
,   sceneData.triangles
,   sceneData.vertices
,   sceneData.surfaces
)
{
}

Scene::Scene
(   cl::Context & cl_context
,   long nreflections
,   const string & objpath
,   const string & materialFileName
,   bool verbose
)
:   Scene
(   cl_context
,   nreflections
,   SceneData (objpath, materialFileName)
)
{
}

void Scene::trace
(   const cl_float3 & micpos
,   const cl_float3 & source
,   const vector <cl_float3> & directions
)
{
    auto raytrace = cl::make_kernel
    <   cl::Buffer
    ,   cl_float3
    ,   cl::Buffer
    ,   cl_long
    ,   cl::Buffer
    ,   cl_float3
    ,   cl::Buffer
    ,   cl::Buffer
    ,   cl::Buffer
    ,   cl::Buffer
    ,   cl_long
    > (cl_program, "raytrace");

    ngroups = directions.size() / RAY_GROUP_SIZE;

    storedDiffuse.resize (ngroups * RAY_GROUP_SIZE * nreflections);
    storedImage.resize (ngroups * RAY_GROUP_SIZE * NUM_IMAGE_SOURCE);

    map <vector <long>, Impulse> imageSourceTally;

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

        vector <long> image_source_index
            (RAY_GROUP_SIZE * NUM_IMAGE_SOURCE, 0);
        cl::copy
        (   queue
        ,   begin (image_source_index)
        ,   end (image_source_index)
        ,   cl_image_source_index
        );

        raytrace
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
                vector <long> surfaces
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
    ,   [] (const pair <vector <long>, Impulse> & i) {return i.second;}
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
    ,   [this, mic_pos] (const Speaker & i) {return attenuate (mic_pos, i);}
    );
    return attenuated;
}

vector <Impulse> Scene::attenuate
(   const cl_float3 & mic_pos
,   const Speaker & speaker
)
{
    auto attenuate = cl::make_kernel
    <   cl_float3
    ,   cl::Buffer
    ,   cl::Buffer
    ,   cl_long
    ,   Speaker
    > (cl_program, "attenuate");

    vector <Impulse> retDiffuse (ngroups * RAY_GROUP_SIZE * nreflections);
    vector <Impulse> retImage (ngroups * RAY_GROUP_SIZE * NUM_IMAGE_SOURCE);

    for (auto i = 0; i != ngroups; ++i)
    {
        cl::copy
        (   queue
        ,   storedDiffuse.begin() + (i + 0) * RAY_GROUP_SIZE * nreflections
        ,   storedDiffuse.begin() + (i + 1) * RAY_GROUP_SIZE * nreflections
        ,   cl_impulses
        );
        cl::copy
        (   queue
        ,   storedImage.begin() + (i + 0) * RAY_GROUP_SIZE * NUM_IMAGE_SOURCE
        ,   storedImage.begin() + (i + 1) * RAY_GROUP_SIZE * NUM_IMAGE_SOURCE
        ,   cl_image_source
        );

        attenuate
        (   cl::EnqueueArgs (queue, cl::NDRange (RAY_GROUP_SIZE))
        ,   mic_pos
        ,   cl_impulses
        ,   cl_attenuated
        ,   nreflections
        ,   speaker
        );
        cl::copy
        (   queue
        ,   cl_attenuated
        ,   retDiffuse.begin() + (i + 0) * RAY_GROUP_SIZE * nreflections
        ,   retDiffuse.begin() + (i + 1) * RAY_GROUP_SIZE * nreflections
        );

        attenuate
        (   cl::EnqueueArgs (queue, cl::NDRange (RAY_GROUP_SIZE))
        ,   mic_pos
        ,   cl_image_source
        ,   cl_attenuated
        ,   NUM_IMAGE_SOURCE
        ,   speaker
        );
        cl::copy
        (   queue
        ,   cl_attenuated
        ,   retImage.begin() + (i + 0) * RAY_GROUP_SIZE * NUM_IMAGE_SOURCE
        ,   retImage.begin() + (i + 1) * RAY_GROUP_SIZE * NUM_IMAGE_SOURCE
        );
    }

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
    ,   [this, mic_pos, facing, up] (long i)
        {
            return hrtf (mic_pos, i, facing, up);
        }
    );
    return attenuated;
}

vector <Impulse> Scene::hrtf
(   const cl_float3 & mic_pos
,   long channel
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

    auto hrtf = cl::make_kernel
    <   cl_float3
    ,   cl::Buffer
    ,   cl::Buffer
    ,   cl_long
    ,   cl::Buffer
    ,   cl_float3
    ,   cl_float3
    ,   cl_long
    > (cl_program, "hrtf");

    vector <Impulse> retDiffuse (ngroups * RAY_GROUP_SIZE * nreflections);
    vector <Impulse> retImage (ngroups * RAY_GROUP_SIZE * NUM_IMAGE_SOURCE);

    for (auto i = 0; i != ngroups; ++i)
    {
        cl::copy
        (   queue
        ,   storedDiffuse.begin() + (i + 0) * RAY_GROUP_SIZE * nreflections
        ,   storedDiffuse.begin() + (i + 1) * RAY_GROUP_SIZE * nreflections
        ,   cl_impulses
        );
        cl::copy
        (   queue
        ,   storedImage.begin() + (i + 0) * RAY_GROUP_SIZE * NUM_IMAGE_SOURCE
        ,   storedImage.begin() + (i + 1) * RAY_GROUP_SIZE * NUM_IMAGE_SOURCE
        ,   cl_image_source
        );

        hrtf
        (   cl::EnqueueArgs (queue, cl::NDRange (RAY_GROUP_SIZE))
        ,   mic_pos
        ,   cl_impulses
        ,   cl_attenuated
        ,   nreflections
        ,   cl_hrtf
        ,   facing
        ,   up
        ,   channel
        );
        cl::copy
        (   queue
        ,   cl_attenuated
        ,   retDiffuse.begin() + (i + 0) * RAY_GROUP_SIZE * nreflections
        ,   retDiffuse.begin() + (i + 1) * RAY_GROUP_SIZE * nreflections
        );

        hrtf
        (   cl::EnqueueArgs (queue, cl::NDRange (RAY_GROUP_SIZE))
        ,   mic_pos
        ,   cl_image_source
        ,   cl_attenuated
        ,   NUM_IMAGE_SOURCE
        ,   cl_hrtf
        ,   facing
        ,   up
        ,   channel
        );
        cl::copy
        (   queue
        ,   cl_attenuated
        ,   retImage.begin() + (i + 0) * RAY_GROUP_SIZE * NUM_IMAGE_SOURCE
        ,   retImage.begin() + (i + 1) * RAY_GROUP_SIZE * NUM_IMAGE_SOURCE
        );
    }

    retDiffuse.insert (retDiffuse.end(), retImage.begin(), retImage.end());
    return retDiffuse;
}
