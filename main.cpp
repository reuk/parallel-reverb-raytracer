#include "rayverb_lib.h"

#include <iostream>
#include <fstream>
#include <streambuf>
#include <vector>
#include <random>
#include <algorithm>
#include <functional>

#include "assimp/Importer.hpp"
#include "assimp/postprocess.h"
#include "assimp/scene.h"

#include "sndfile.hh"

#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"

//#define DIAGNOSTIC

//#define VERBOSE

using namespace rapidjson;
using namespace std;

vector <cl_float3> getDirections (unsigned long num)
{
    vector <cl_float3> ret (num);
    uniform_real_distribution <float> zDist (-1, 1);
    uniform_real_distribution <float> thetaDist (-M_PI, M_PI);
    default_random_engine engine;

    for_each (begin (ret), end (ret), [&] (cl_float3 & i)
        {
            const float z = zDist (engine);
            const float theta = thetaDist (engine);
            const float ztemp = sqrtf (1 - z * z);
            i = (cl_float3) {ztemp * cosf (theta), ztemp * sinf (theta), z, 0};
        }
    );
    
    return ret;
}

cl_float3 fromAIVec (const aiVector3D & v)
{
    return (cl_float3) {v.x, v.y, v.z, 0};
}

int main(int argc, const char * argv[])
{
    Assimp::Importer importer;
    const aiScene * scene = importer.ReadFile 
    (   "/Users/reuben/Desktop/basic.obj"
    ,   aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_FlipUVs
    );
    
    if (! scene)
    {
        cerr << "Failed to load object file." << endl;
        return 1;
    }
    
    vector <Triangle> triangles;
    vector <cl_float3> vertices;
    vector <Surface> surfaces;
    
    for (unsigned long i = 0; i != scene->mNumMeshes; ++i)
    {
        const aiMesh * mesh = scene->mMeshes [i];
        
        vector <cl_float3> meshVertices (mesh->mNumVertices);
        
        for (unsigned long j = 0; j != mesh->mNumVertices; ++j)
        {
            meshVertices [j] = fromAIVec (mesh->mVertices [j]);
        }
        
#ifdef USE_OBJECT_MATERIALS
        const aiMaterial * material = scene->mMaterials [mesh->mMaterialIndex];

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
            (cl_float3) {0.95, 0.8, 0.75, 0},
            (cl_float3) {0.95, 0.8, 0.75, 0}
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
            
        vertices.insert (vertices.end(), meshVertices.begin(), meshVertices.end());
        triangles.insert (triangles.end(), meshTriangles.begin(), meshTriangles.end());
    }
    
    const unsigned long NUM_RAYS = 32768;
    
    vector <cl_float3> directions = getDirections (NUM_RAYS);
    
    Surface surface = {
        (cl_float3) {1, 1, 1, 0},
        (cl_float3) {1, 1, 1, 0}
    };
    
    surfaces.push_back (surface);
    
    vector <Sphere> spheres;
    spheres.push_back ((Sphere) {surfaces.size() - 1, (cl_float3) {5, 5, 5, 0}, 1});
    
    vector <Speaker> speakers;
    speakers.push_back ((Speaker) {(cl_float3) {1, 0, 0}, 0.5});
    speakers.push_back ((Speaker) {(cl_float3) {0, 1, 0}, 0.5});
    
    const unsigned long NUM_IMPULSES = 128;

    //  results go in here
    //  so that they can escape from the exception-handling scope
    vector <vector <Impulse>> attenuated_impulses
    (   speakers.size()
    ,   vector <Impulse> (NUM_IMPULSES * NUM_RAYS)
    );
#ifdef DIAGNOSTIC
    vector <Reflection> reflections (NUM_IMPULSES * NUM_RAYS);
#endif
        
#ifdef __CL_ENABLE_EXCEPTIONS
    try
    {
#endif
        vector <cl::Platform> platform;
        cl::Platform::get (&platform);
        
        cl_context_properties cps [3] = {
            CL_CONTEXT_PLATFORM,
            (cl_context_properties) (platform [0]) (),
            0
        };
        
        cl::Context context (CL_DEVICE_TYPE_GPU, cps);
        
        vector <cl::Device> device = context.getInfo <CL_CONTEXT_DEVICES>();

        cl::Device used_device = device [1];

#ifdef VERBOSE
        cerr << "device used: " << used_device.getInfo <CL_DEVICE_NAME>() << endl;
#endif
        
        cl::CommandQueue queue (context, used_device);
        
        cl::Buffer cl_directions    (context, begin (directions),  end (directions),  false);
        cl::Buffer cl_triangles     (context, begin (triangles),   end (triangles),   false);
        cl::Buffer cl_vertices      (context, begin (vertices),    end (vertices),    false);
        cl::Buffer cl_spheres       (context, begin (spheres),     end (spheres),     false);
        cl::Buffer cl_surfaces      (context, begin (surfaces),    end (surfaces),    false);
        
        const unsigned long IMPULSES_BYTES = NUM_IMPULSES * NUM_RAYS * sizeof (Impulse);

        cl::Buffer cl_impulses 
        (   context
        ,   CL_MEM_READ_WRITE
        ,   IMPULSES_BYTES
        );

#ifdef DIAGNOSTIC
        cl::Buffer cl_reflections
        (   context
        ,   CL_MEM_READ_WRITE
        ,   NUM_IMPULSES * NUM_RAYS * sizeof (Reflection)
        );
#endif

        cl::Buffer cl_attenuated_impulses 
        (   context
        ,   CL_MEM_WRITE_ONLY
        ,   IMPULSES_BYTES
        );
        
        ifstream cl_source_file ("kernel.cl");
        string cl_source_string 
        (   (istreambuf_iterator <char> (cl_source_file))
        ,   istreambuf_iterator <char> ()
        );
        
        cl::Program program (context, cl_source_string, true);
        
        auto info = program.getBuildInfo <CL_PROGRAM_BUILD_LOG> (used_device);
#ifdef VERBOSE
        cerr << "program build info:" << endl << info << endl;
#endif

#ifdef DIAGNOSTIC
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
        > (program, "test");
#endif
        
        auto raytrace = cl::make_kernel 
        <   cl::Buffer
        ,   cl_float3
        ,   cl::Buffer
        ,   unsigned long
        ,   cl::Buffer
        ,   cl::Buffer
        ,   cl::Buffer
        ,   cl::Buffer
        ,   unsigned long
        > (program, "raytrace");
        
        auto attenuate = cl::make_kernel 
        <   cl::Buffer
        ,   cl::Buffer
        ,   unsigned long
        ,   cl::Buffer
        ,   Speaker
        > (program, "attenuate");

        cl_float3 sourcePosition = {-5, -5, -5, 0};

#ifdef DIAGNOSTIC
        test
        (   cl::EnqueueArgs (queue, cl::NDRange (NUM_RAYS))
        ,   cl_directions
        ,   sourcePosition
        ,   cl_triangles
        ,   triangles.size()
        ,   cl_vertices
        ,   cl_spheres
        ,   cl_surfaces
        ,   cl_reflections
        ,   NUM_IMPULSES
        );

        cl::copy
        (   queue
        ,   cl_reflections
        ,   reflections.begin()
        ,   reflections.end()
        );
#endif

        raytrace 
        (   cl::EnqueueArgs (queue, cl::NDRange (NUM_RAYS))
        ,   cl_directions
        ,   sourcePosition
        ,   cl_triangles
        ,   triangles.size()
        ,   cl_vertices
        ,   cl_spheres
        ,   cl_surfaces
        ,   cl_impulses
        ,   NUM_IMPULSES
        );

        for (int i = 0; i != speakers.size(); ++i)
        {
            attenuate 
            (   cl::EnqueueArgs (queue, cl::NDRange (NUM_RAYS))
            ,   cl_impulses
            ,   cl_attenuated_impulses
            ,   NUM_IMPULSES
            ,   cl_directions
            ,   speakers [i]
            );

            cl::copy 
            (   queue
            ,   cl_attenuated_impulses
            ,   attenuated_impulses [i].begin()
            ,   attenuated_impulses [i].end()
            );
        }
#ifdef __CL_ENABLE_EXCEPTIONS
    }
    catch (cl::Error error)
    {
        cerr << "encountered opencl error:" << endl;
        cerr << error.what() << endl;
        cerr << error.err() << endl;
    }
#endif

#ifdef DIAGNOSTIC
    for (int i = 0; i != NUM_RAYS; ++i)
    {
        StringBuffer stringBuffer;
        Writer <StringBuffer> writer (stringBuffer);

        writer.StartArray();
        for (int j = 0; j != NUM_IMPULSES; ++j)
        {
            Reflection reflection = reflections [i * NUM_IMPULSES + j];
            if 
            (   ! 
                (   reflection.volume.s [0] 
                || reflection.volume.s [1] 
                || reflection.volume.s [2]
                )
            )
                break;

            writer.StartObject();

            writer.String ("position");
            writer.StartArray();
            for (int k = 0; k != 3; ++k)
                writer.Double (reflection.position.s [k]);
            writer.EndArray();

            writer.String ("volume");
            writer.StartArray();
            for (int k = 0; k != 3; ++k)
                writer.Double (reflection.volume.s [k]);
            writer.EndArray();

            writer.EndObject();
        }
        writer.EndArray();

        cout << stringBuffer.GetString() << endl;
    }
#endif

    const float SAMPLE_RATE = 44100;
    vector <vector <cl_float3>> flattened;
    for (int i = 0; i != speakers.size(); ++i)
    {
        flattened.push_back 
        (   flattenImpulses 
            (   attenuated_impulses [i]
            ,   SAMPLE_RATE
            )
        );
    }
    
    vector <vector <float>> outdata = process (flattened, SAMPLE_RATE);
    
    vector <float> interleaved (outdata.size() * outdata [0].size());
    
    for (int i = 0; i != outdata.size(); ++i)
    {
        for (int j = 0; j != outdata [i].size(); ++j)
        {
            interleaved [j * outdata.size() + i] = outdata [i] [j];
        }
    }
    
    SndfileHandle outfile ("para.aiff", SFM_WRITE, SF_FORMAT_AIFF | SF_FORMAT_PCM_16, outdata.size(), SAMPLE_RATE);
    outfile.write (interleaved.data(), interleaved.size());

    return 0;
}

