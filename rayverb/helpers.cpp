#include "helpers.h"

#ifdef DIAGNOSTIC
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#endif

#include <iostream>
#include <cmath>
#include <random>

using namespace std;

#ifdef DIAGNOSTIC
using namespace rapidjson;

void print_diagnostic 
(   unsigned long nrays
,   unsigned long nreflections
,   const vector <Reflection> & reflections
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

// -1 <= z <= 1, -pi <= theta <= pi
cl_float3 spherePoint (float z, float theta) 
{
    const float ztemp = sqrtf (1 - z * z);
    return (cl_float3) {ztemp * cosf (theta), ztemp * sinf (theta), z, 0};
}

vector <cl_float3> getRandomDirections (unsigned long num)
{
    vector <cl_float3> ret (num);
    uniform_real_distribution <float> zDist (-1, 1);
    uniform_real_distribution <float> thetaDist (-M_PI, M_PI);
    default_random_engine engine;

    for_each (begin (ret), end (ret), [&] (cl_float3 & i)
        {
            i = spherePoint (zDist (engine), thetaDist (engine));
        }
    );
    
    return ret;
}

cl::Context getContext()
{
    vector <cl::Platform> platform;
    cl::Platform::get (&platform);

    cl_context_properties cps [3] = {
        CL_CONTEXT_PLATFORM,
        (cl_context_properties) (platform [0]) (),
        0
    };
    
    return cl::Context (CL_DEVICE_TYPE_GPU, cps);
}
