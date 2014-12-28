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

using namespace std;
using namespace rapidjson;

cl_float3 fromAIVec (const aiVector3D & v)
{
    return (cl_float3) {v.x, v.y, v.z, 0};
}

struct LatestImpulse: public binary_function <Impulse, Impulse, bool>
{
    inline bool operator() (const Impulse & a, const Impulse & b) const
    {
        return a.time < b.time;
    }
};

template<>
struct plus <cl_float3>
:   public binary_function <cl_float3, cl_float3, cl_float3>
{
    inline cl_float3 operator() (const cl_float3 & a, const cl_float3 & b) const
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
    inline cl_float8 operator() (const cl_float8 & a, const cl_float8 & b) const
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
)
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

vector <vector <VolumeType>> flattenImpulses
(   const vector <vector <Impulse>> & attenuated
,   float samplerate
)
{
    vector <vector <VolumeType>> flattened (attenuated.size());
    transform
    (   begin (attenuated)
    ,   end (attenuated)
    ,   begin (flattened)
    ,   [&] (const vector <Impulse> & i)
        {
            return flattenImpulses (i, samplerate);
        }
    );
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
    ,   [&] (const T & i)
        {
            return accumulate (i.s, i.s + sizeof (T) / sizeof (float), 0.0);
        }
    );
    return ret;
}

vector <vector <float>> process
(   RayverbFiltering::FilterType filtertype
,   vector <vector <VolumeType>> & data
,   float sr
)
{
    vector <vector <float>> ret (data.size());

    for (int i = 0; i != data.size(); ++i)
    {
        ret [i] = RayverbFiltering::filter
        (   filtertype
        ,   data[i]
        ,   sr
        );
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

        constexpr unsigned bands = sizeof (VolumeType) / sizeof (float);
        if (json [key.c_str()].Size() != bands)
        {
            stringstream ss;
            ss << "Length of material description array must be " << bands;
            throw runtime_error (ss.str());
        }

        for (unsigned i = 0; i != bands; ++i)
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

        constexpr unsigned bands = sizeof (VolumeType) / sizeof (float);

        Surface ret;
        for (unsigned i = 0; i != bands; ++i)
        {
            ret.specular.s [i] = json [specular_string.c_str()] [i].GetDouble();
            ret.diffuse.s [i] = json [diffuse_string.c_str()] [i].GetDouble();
        }

        for (unsigned i = 0; i != bands; ++i)
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
            (VolumeType) {0.02, 0.02, 0.03, 0.03, 0.04, 0.05, 0.05, 0.05},
            (VolumeType) {0.50, 0.90, 0.95, 0.95, 0.95, 0.95, 0.95, 0.95}
        };

        surfaces.push_back (surface);

        Document document;
        attemptJsonParse (materialFileName, document);
        if (! document.IsObject())
            throw runtime_error ("Materials must be stored in a JSON object");

        map <string, int> materialIndices;
        for
        (   Value::ConstMemberIterator i = document.MemberBegin()
        ;   i != document.MemberEnd()
        ;   ++i
        )
        {
            string name = i->name.GetString();
            surfaces.push_back (jsonToSurface (i->value));
            materialIndices [name] = surfaces.size() - 1;
        }

        for (unsigned long i = 0; i != scene->mNumMeshes; ++i)
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

            for (unsigned long j = 0; j != mesh->mNumVertices; ++j)
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

            for (unsigned long j = 0; j != mesh->mNumFaces; ++j)
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
        cerr << "Model bounds: " << endl;

        cl_float3 mini, maxi;
        mini = maxi = vertices.front();

        for (const auto & i : vertices)
        {
            for (int j = 0; j != 3; ++j)
            {
                mini.s [j] = min (mini.s [j], i.s [j]);
                maxi.s [j] = max (maxi.s [j], i.s [j]);
            }
        }

        cerr
        <<  " mini ["
        <<  mini.s [0]
        <<  ", "
        <<  mini.s [1]
        <<  ", "
        <<  mini.s [2]
        <<  "]"
        <<  endl;
        cerr
        <<  " maxi ["
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
,   const string & objpath
,   const string & materialFileName
,   bool verbose
)
:   Scene
(   cl_context
,   nreflections
,   directions
,   SceneData (objpath, materialFileName)
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

Hrtf jsonToHrtfData (const Value & json)
{
    if (! json.IsArray())
        throw runtime_error ("Hrtf must be a JSON array");

    if (json.Size() != 2)
        throw runtime_error ("Hrtf array length must be 2");

    if (! json [(SizeType) 0].IsObject())
        throw runtime_error ("Hrtf array first item must be a descriptor object");
    if (! json [(SizeType) 1].IsArray())
        throw runtime_error ("Hrtf array second item must be an array of arrays of coefficients");

    if (! json [(SizeType) 0].HasMember ("r"))
        throw runtime_error ("Hrtf descriptor object must contain radius 'r'");
    if (! json [(SizeType) 0].HasMember ("a"))
        throw runtime_error ("Hrtf descriptor object must contain azimuth 'a'");
    if (! json [(SizeType) 0].HasMember ("e"))
        throw runtime_error ("Hrtf descriptor object must contain elevation 'e'");

    if (! json [(SizeType) 0] ["r"].IsNumber())
        throw runtime_error ("Hrtf radius must be numerical");
    if (! json [(SizeType) 0] ["a"].IsNumber())
        throw runtime_error ("Hrtf azimuth must be numerical");
    if (! json [(SizeType) 0] ["e"].IsNumber())
        throw runtime_error ("Hrtf elevation must be numerical");

    constexpr unsigned bands = sizeof (VolumeType) / sizeof (float);

    if (json [(SizeType) 1].Size() != 2)
        throw runtime_error ("Hrtf array second item must contain data for two channels");

    if
    (   !
        (   json [(SizeType) 1] [(SizeType) 0].IsArray()
        &&  json [(SizeType) 1] [(SizeType) 1].IsArray()
        )
    )
        throw runtime_error ("Hrtf coefficients must be stored in json arrays");

    if
    (   json [(SizeType) 1] [(SizeType) 0].Size() != bands
    ||  json [(SizeType) 1] [(SizeType) 1].Size() != bands
    )
        throw runtime_error ("Hrtf coefficient array has incorrect length");

    VolumeType vt_l;
    VolumeType vt_r;
    for (int i = 0; i != bands; ++i)
    {
        vt_l.s [i] = json [(SizeType) 1] [(SizeType) 0] [i].GetDouble();
        vt_r.s [i] = json [(SizeType) 1] [(SizeType) 1] [i].GetDouble();
    }

    const cl_int a = json [(SizeType) 0] ["a"].GetInt();
    const cl_int e = json [(SizeType) 0] ["e"].GetInt();

    return (Hrtf) {a, e, vt_l, vt_r};
}

vector <Hrtf> readHrtfFile (const string & file)
{
    Document document;
    attemptJsonParse (file, document);
    if (! document.IsArray())
        throw runtime_error ("Hrtf data must be stored in a JSON array");

    vector <Hrtf> fields;

    for
    (   Value::ConstValueIterator i = document.Begin()
    ;   i != document.End()
    ;   ++i
    )
        fields.push_back (jsonToHrtfData (*i));

    sort
    (   begin (fields)
    ,   end (fields)
    ,   [&] (const Hrtf & i, const Hrtf & j)
        {
            if (i.azimuth == j.azimuth)
                return i.elevation < j.elevation;
            else
                return i.azimuth < j.azimuth;
        }
    );

    return fields;
}

vector <vector <Impulse>> Scene::hrtf
(   const string & file
,   const cl_float3 & facing
,   const cl_float3 & up
)
{
    vector <Hrtf> hrtf_vec = readHrtfFile (file);

    cl_hrtf = cl::Buffer
    (   cl_context
    ,   CL_MEM_READ_WRITE
    ,   hrtf_vec.size() * sizeof (Hrtf)
    );

    cl::copy
    (   queue
    ,   begin (hrtf_vec)
    ,   end (hrtf_vec)
    ,   cl_hrtf
    );

    nhrtf = hrtf_vec.size();

    auto channels = {0, 1};
    vector <vector <Impulse>> attenuated (channels.size());
    transform
    (   begin (channels)
    ,   end (channels)
    ,   begin (attenuated)
    ,   [&] (unsigned long i) {return hrtf (i, facing, up);}
    );
    return attenuated;
}

vector <Impulse> Scene::hrtf
(   unsigned long channel
,   const cl_float3 & facing
,   const cl_float3 & up
)
{
    //  pass data to opencl
    //  for each direction, look up the hrtf coefficients
    //  multiply the impulse at that direction by the coefficients
    //  return
    auto hrtf = cl::make_kernel
    <   cl::Buffer
    ,   cl::Buffer
    ,   cl_ulong
    ,   cl::Buffer
    ,   cl_ulong
    ,   cl_float3
    ,   cl_float3
    ,   cl_ulong
    > (cl_program, "hrtf");

    //  remember to keep pointing and up normalized!

    hrtf
    (   cl::EnqueueArgs (queue, cl::NDRange (nrays))
    ,   cl_impulses
    ,   cl_attenuated
    ,   nreflections
    ,   cl_hrtf
    ,   nhrtf
    ,   facing
    ,   up
    ,   channel
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
