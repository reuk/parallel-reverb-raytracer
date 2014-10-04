#include "rayverb.h"

#include "sndfile.hh"

//#define VERBOSE

#ifdef DIAGNOSTIC
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#endif

#include <iostream>
#include <vector>
#include <random>
#include <algorithm>
#include <functional>

using namespace std;

#ifdef DIAGNOSTIC
using namespace rapidjson;

void print_diagnostic 
(   unsigned long nrays
,   unsigned long nreflections
,   const std::vector <Reflection> & reflections
)
{
    for (int i = 0; i != nrays; ++i)
    {
        StringBuffer stringBuffer;
        Writer <StringBuffer> writer (stringBuffer);

        writer.StartArray();
        for (int j = 0; j != nreflections; ++j)
        {
            Reflection reflection = reflections [i * nreflections + j];
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
}
#endif

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

int main(int argc, const char * argv[])
{
    vector <Sphere> spheres;
    spheres.push_back ((Sphere) {(cl_float3) {5, 5, 5, 0}, 1});

    vector <Speaker> speakers;
    speakers.push_back ((Speaker) {(cl_float3) {1, 0, 0}, 0.5});
    speakers.push_back ((Speaker) {(cl_float3) {0, 1, 0}, 0.5});
    
    const unsigned long NUM_RAYS = 1024 * 32;
    const unsigned long NUM_IMPULSES = 128;

    vector <cl_float3> directions = getDirections (NUM_RAYS);
    vector <vector <Impulse>> attenuated;

    try
    {
        vector <cl::Platform> platform;
        cl::Platform::get (&platform);

        cl_context_properties cps [3] = {
            CL_CONTEXT_PLATFORM,
            (cl_context_properties) (platform [0]) (),
            0
        };
        
        cl::Context context (CL_DEVICE_TYPE_GPU, cps);

        Scene scene 
        (   context
        ,   NUM_RAYS
        ,   NUM_IMPULSES
        ,   directions
        ,   "/Users/reuben/Desktop/basic.obj"
        );

        attenuated = scene.trace 
        (   (cl_float3) {-5, -5, -5, 0}
        ,   spheres
        ,   speakers
        );
    }
    catch (cl::Error error)
    {
        cerr << "encountered opencl error:" << endl;
        cerr << error.what() << endl;
        cerr << error.err() << endl;
    }
    catch (runtime_error error)
    {
        cerr << "encountered runtime error:" << endl;
        cerr << error.what() << endl;
    }

    const float SAMPLE_RATE = 44100;
    vector <vector <cl_float3>> flattened;
    for (int i = 0; i != attenuated.size(); ++i)
    {
        flattened.push_back 
        (   flattenImpulses 
            (   attenuated [i]
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
