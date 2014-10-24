#include "rayverb.h"
#include "helpers.h"

#include "sndfile.hh"

#include <sstream>
#include <iostream>
#include <vector>
#include <random>
#include <algorithm>
#include <functional>

using namespace std;

vector <float> dconvolve (const vector <float> & a, const vector <float> & b)
{
    vector <float> ret (a.size() + b.size() - 1, 0);

    for (unsigned long i = 0; i != a.size(); ++i)
    {
        const float in = a [i];

        for (unsigned long j = 0; j != b.size(); ++j)
        {
            ret [i + j] += in * b [j];
        }
    }

    return ret;
}

int main (int argc, const char * argv[])
{
    const float SAMPLE_RATE = 44100;

    SndfileHandle infile ("../../pulse.aif");
    vector <float> impulse (infile.frames());
    infile.read (impulse.data(), impulse.size());

    Sphere sphere = {(cl_float3) {5, 5, 5, 0}, 1};

    vector <Speaker> speakers 
    {   (Speaker) {(cl_float3) {1, 0, 0}, 0.5}
    ,   (Speaker) {(cl_float3) {0, 1, 0}, 0.5}
    };
    
    const unsigned long NUM_RAYS = 1024 * 1;
    const unsigned long NUM_IMPULSES = 128;

    float impulse_sum = 0;
    for (const auto & i : impulse)
        impulse_sum += fabs (i);

    div (impulse, impulse_sum);

    vector <float> temp = impulse;
    vector <vector <float>> precalc;

    for (int i = 0; i != NUM_IMPULSES; ++i)
    {
        precalc.push_back (temp);
        temp = dconvolve (impulse, temp);
    } 

    /*
    for (int i = 0; i != precalc.size(); ++i)
    {
        stringstream str;
        str << "para_" << i << ".aiff";

        vector <float> norm (precalc [i]);
        normalize (norm);

        if (i == 2)
        {
            for (const auto & j : norm)
            {
                cout << j << endl;
            }
        }

        SndfileHandle outfile 
        (   str.str()
        ,   SFM_WRITE
        ,   SF_FORMAT_AIFF | SF_FORMAT_PCM_16
        ,   1
        ,   SAMPLE_RATE
        );
        outfile.write (norm.data(), norm.size());
    }
    */

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

        scene.trace 
        (   (cl_float3) {-5, -5, -5, 0}
        ,   sphere
        );

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

    vector <vector <float>> flattened;
    for (const auto & i : attenuated)
    {
        flattened.push_back 
        (   flattenCustomImpulses 
            (   i
            ,   NUM_IMPULSES
            ,   precalc
            ,   SAMPLE_RATE
            )
        );
    }

    normalize (flattened);

    vector <vector <float>> outdata (flattened);

    vector <float> interleaved (outdata.size() * outdata [0].size());
    
    for (int i = 0; i != outdata.size(); ++i)
    {
        for (int j = 0; j != outdata [i].size(); ++j)
            interleaved [j * outdata.size() + i] = outdata [i] [j];
    }
    
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
