#include "rayverb.h"
#include "helpers.h"

#include "sndfile.hh"

#include <iostream>
#include <vector>
#include <random>
#include <algorithm>
#include <functional>

using namespace std;

int main(int argc, const char * argv[])
{
    /*
    cl_float3 source = {5, 5, 5, 0};
    cl_float3 mic = {-5, -5, -5, 0};
    */

    cl_float3 source = {0, -50, 20, 0};
    cl_float3 mic = {-5, -55, 25, 0};

    vector <Speaker> speakers
    {   (Speaker) {(cl_float3) {1, 0, 0}, 0.5}
    ,   (Speaker) {(cl_float3) {0, 1, 0}, 0.5}
    };

    const unsigned long NUM_RAYS = 1024 * 32;
    const unsigned long NUM_IMPULSES = 32;

    vector <cl_float3> directions = getRandomDirections (NUM_RAYS);
    vector <vector <Impulse>> attenuated;

    try
    {
        cl::Context context = getContext();

        Scene scene
        (   context
        ,   NUM_IMPULSES
        ,   directions
        ,   TEST_FILE
        );

        scene.trace (mic, source);

        attenuated = scene.attenuate (speakers);
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

    if (attenuated.empty())
    {
        cerr << "No raytrace results returned." << endl;
        return 1;
    }

    const float SAMPLE_RATE = 44100;
    vector <vector <VolumeType>> flattened (attenuated.size());
    transform
    (   begin (attenuated)
    ,   end (attenuated)
    ,   begin (flattened)
    ,   [&] (const vector <Impulse> & i)
        {
            return flattenImpulses (i, SAMPLE_RATE);
        }
    );

    vector <vector <float>> outdata = process
    (   RayverbFiltering::FILTER_TYPE_BIQUAD_TWOPASS
    ,   flattened
    ,   SAMPLE_RATE
    );

    vector <float> interleaved (outdata.size() * outdata [0].size());

    for (int i = 0; i != outdata.size(); ++i)
        for (int j = 0; j != outdata [i].size(); ++j)
            interleaved [j * outdata.size() + i] = outdata [i] [j];

    SndfileHandle outfile
    (   "para.aiff"
    ,   SFM_WRITE
    ,   SF_FORMAT_AIFF | SF_FORMAT_PCM_16
    ,   outdata.size()
    ,   SAMPLE_RATE
    );
    outfile.write (interleaved.data(), interleaved.size());

    return 0;
}
