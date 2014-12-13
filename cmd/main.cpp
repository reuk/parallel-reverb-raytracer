#include "rayverb.h"
#include "helpers.h"

#include "sndfile.hh"

#include <iostream>
#include <vector>
#include <random>
#include <algorithm>
#include <functional>

#include <gflags/gflags.h>

using namespace std;

void write_aiff
(   const string & fname
,   const vector <vector <float>> & outdata
,   float sr
)
{
    vector <float> interleaved (outdata.size() * outdata [0].size());

    for (int i = 0; i != outdata.size(); ++i)
        for (int j = 0; j != outdata [i].size(); ++j)
            interleaved [j * outdata.size() + i] = outdata [i] [j];

    SndfileHandle outfile
    (   fname
    ,   SFM_WRITE
    ,   SF_FORMAT_AIFF | SF_FORMAT_PCM_16
    ,   outdata.size()
    ,   sr
    );
    outfile.write (interleaved.data(), interleaved.size());
}

void flatten_and_write
(   const string & fname
,   const vector <vector <Impulse>> & impulses
,   float sr
)
{
    vector <vector <VolumeType>> flattened = flattenImpulses
    (   impulses
    ,   sr
    );
    vector <vector <float>> outdata = process
    (   RayverbFiltering::FILTER_TYPE_BIQUAD_TWOPASS
    ,   flattened
    ,   sr
    );
    write_aiff (fname, outdata, sr);
}

//  probably best to load configuration from a file?
//
//  model on commandline
//  source+mic+speakers/hrtf in file
//  output on commandline
//
//  required inputs
//      model file
//      source position
//      mic position
//      speakers - any number
//          direction
//          quality
//      hrtf
//          pointing
//          up
//          hrtf file
//      material file
//      output filename
//      filtering method
//      sampling rate
//      bit depth
//      ray number
//      reflection number

int main(int argc, const char * argv[])
{
    cl_float3 source = {0, -50, 20, 0};
    cl_float3 mic = {-5, -55, 25, 0};

    vector <Speaker> speakers
    {   (Speaker) {(cl_float3) {1, 0, 0}, 0.5}
    ,   (Speaker) {(cl_float3) {0, 1, 0}, 0.5}
    };

    const unsigned long NUM_RAYS = 1024 * 32;
    const unsigned long NUM_IMPULSES = 64;

    vector <cl_float3> directions = getRandomDirections (NUM_RAYS);
    vector <vector <Impulse>> attenuated;
    vector <vector <Impulse>> hrtf;

    try
    {
        cl::Context context = getContext();

        Scene scene
        (   context
        ,   NUM_IMPULSES
        ,   directions
        ,   TEST_FILE
        ,   "../../mat_updated.json"
        );

        scene.trace (mic, source);

        attenuated = scene.attenuate (speakers);
        hrtf = scene.hrtf ("../../hrtf_analysis/IRC1050.json");
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

    flatten_and_write ("speaker.aiff", attenuated, SAMPLE_RATE);
    flatten_and_write ("hrtf.aiff", hrtf, SAMPLE_RATE);

    return 0;
}
