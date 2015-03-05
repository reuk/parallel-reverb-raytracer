#include "rayverb.h"
#include "helpers.h"
#include "config.h"

#include "rapidjson/rapidjson.h"
#include "rapidjson/error/en.h"
#include "rapidjson/document.h"

#include "sndfile.hh"

#include <iostream>
#include <vector>
#include <random>
#include <algorithm>
#include <functional>
#include <map>

#include <gflags/gflags.h>

using namespace std;
using namespace rapidjson;

void write_aiff
(   const string & fname
,   const vector <vector <float>> & outdata
,   float sr
,   unsigned long bd
)
{
    vector <float> interleaved (outdata.size() * outdata [0].size());

    for (auto i = 0; i != outdata.size(); ++i)
        for (auto j = 0; j != outdata [i].size(); ++j)
            interleaved [j * outdata.size() + i] = outdata [i] [j];

    map <unsigned long, unsigned long> depthTable
    {   {16, SF_FORMAT_PCM_16}
    ,   {24, SF_FORMAT_PCM_24}
    };

    auto i = depthTable.find (bd);
    if (i != depthTable.end())
    {
        SndfileHandle outfile
        (   fname
        ,   SFM_WRITE
        ,   SF_FORMAT_AIFF | i->second
        ,   outdata.size()
        ,   sr
        );
        outfile.write (interleaved.data(), interleaved.size());
    }
    else
    {
        cerr
        <<  "Can't write a file with that bit-depth. Supported bit-depths:"
        <<  endl;
        for (const auto & j : depthTable)
            cerr << "    " << j.first << endl;
    }
}

int main(int argc, const char * argv[])
{
    argc -= 1;

    if (argc != 4)
    {
        cerr << "Command-line parameters are <config file (.json)> <model file> <material file (.json)> <output file (.aif)>" << endl;
        return 1;
    }

    string config_filename (argv [1]);
    string model_filename (argv [2]);
    string material_filename (argv [3]);
    string output_filename (argv [4]);

    //  required params
    cl_float3 source = {{0, 0, 0, 0}};
    cl_float3 mic = {{0, 0, 1, 0}};
    auto numRays = 1024 * 8;
    auto numImpulses = 64;
    auto sampleRate = 44100.0;
    auto bitDepth = 16;

    //  optional params
    auto filter = RayverbFiltering::FILTER_TYPE_BIQUAD_ONEPASS;
    auto hipass = false;
    auto normalize = true;
    auto volumme_scale = 1.0;
    auto trim_predelay = false;
    auto remove_direct = false;
    auto trim_tail = true;
    auto output_mode = ALL;

    AttenuationModel attenuationModel;

    Document document;
    attemptJsonParse (config_filename, document);

    if (document.HasParseError())
    {
        cerr << "Encountered error while parsing config file:" << endl;
        cerr << GetParseError_En (document.GetParseError()) << endl;
        exit (1);
    }

    if (! document.IsObject())
    {
        cerr << "Rayverb config must be stored in a JSON object" << endl;
        exit (1);
    }

    ConfigValidator cv;

    cv.addRequiredValidator ("rays", numRays);
    cv.addRequiredValidator ("reflections", numImpulses);
    cv.addRequiredValidator ("sample_rate", sampleRate);
    cv.addRequiredValidator ("bit_depth", bitDepth);
    cv.addRequiredValidator ("source_position", source);
    cv.addRequiredValidator ("mic_position", mic);

    cv.addRequiredValidator ("attenuation_model", attenuationModel);

    cv.addOptionalValidator ("filter", filter);
    cv.addOptionalValidator ("hipass", hipass);
    cv.addOptionalValidator ("normalize", normalize);
    cv.addOptionalValidator ("volumme_scale", volumme_scale);
    cv.addOptionalValidator ("trim_predelay", trim_predelay);
    cv.addOptionalValidator ("remove_direct", remove_direct);
    cv.addOptionalValidator ("trim_tail", trim_tail);
    cv.addOptionalValidator ("output_mode", output_mode);

    try
    {
        cv.run (document);
    }
    catch (runtime_error error)
    {
        cerr << "encountered error reading config file:" << endl;
        cerr << error.what() << endl;
        exit (1);
    }

    auto directions = getRandomDirections (numRays);
    vector <vector <Impulse>> attenuated;
    try
    {
        Raytracer raytracer
        (   numImpulses
        ,   model_filename
        ,   material_filename
        );

        raytracer.raytrace (mic, source, directions);

        RaytracerResults results;
        switch (output_mode)
        {
        case ALL:
            results = raytracer.getAllRaw (remove_direct);
            break;
        case IMAGE_ONLY:
            results = raytracer.getRawImages (remove_direct);
            break;
        case DIFFUSE_ONLY:
            results = raytracer.getRawDiffuse();
            break;
        default:
            cerr << "This point should never be reached. Aborting" << endl;
            exit (1);
        }

#ifdef DIAGNOSTIC
        print_diagnostic
        (   numRays
        ,   numImpulses
        ,   raytracer.getRawDiffuse().impulses
        ,   "impulse.dump"
        );
#endif
        switch (attenuationModel.mode)
        {
        case AttenuationModel::SPEAKER:
            attenuated = SpeakerAttenuator().attenuate
            (   results
            ,   attenuationModel.speakers
            );
            break;
        case AttenuationModel::HRTF:
            attenuated = HrtfAttenuator().attenuate
            (   results
            ,   attenuationModel.hrtf.facing
            ,   attenuationModel.hrtf.up
            );
            break;
        default:
            cerr << "This point should never be reached. Aborting" << endl;
            exit (1);
        }
    }
    catch (cl::Error error)
    {
        cerr << "encountered opencl error:" << endl;
        cerr << error.what() << endl;
        cerr << error.err() << endl;
        return 1;
    }
    catch (runtime_error error)
    {
        cerr << "encountered runtime error:" << endl;
        cerr << error.what() << endl;
        exit (1);
    }

    if (attenuated.empty())
    {
        cerr << "No raytrace results returned." << endl;
        exit (1);
    }

    if (trim_predelay)
        fixPredelay (attenuated);

    auto flattened = flattenImpulses (attenuated, sampleRate);
    auto processed = process
    (   filter
    ,   flattened
    ,   sampleRate
    ,   normalize
    ,   hipass
    ,   trim_tail
    ,   volumme_scale
    );
    write_aiff (output_filename, processed, sampleRate, bitDepth);
    return 0;
}
