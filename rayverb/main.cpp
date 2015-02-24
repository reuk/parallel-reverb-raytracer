#include "rayverb.h"
#include "helpers.h"

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

cl_float3 normalize (const cl_float3 & v)
{
    cl_float len =
        1.0 / sqrt (v.s [0] * v.s [0] + v.s [1] * v.s [1] + v.s [2] * v.s [2]);
    return {{v.s [0] * len, v.s [1] * len, v.s [2] * len}};
}

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

void flatten_and_write
(   const string & fname
,   const vector <vector <Impulse>> & impulses
,   float sr
,   unsigned long bd
)
{
    auto flattened = flattenImpulses (impulses, sr);
    auto processed = process
    (   RayverbFiltering::FILTER_TYPE_BIQUAD_TWOPASS
    ,   flattened
    ,   sr
    );
    write_aiff (fname, processed, sr, bd);
}

bool validateJson3dVector (const Value & value)
{
    if (! value.IsArray())
        return false;
    auto components = 3;
    if (value.Size() != components)
        return false;
    for (auto i = 0; i != components; ++i)
    {
        if (! value [i].IsNumber())
            return false;
    }

    return true;
}

cl_float3 getJson3dVector (const Value & value)
{
    return
    {   {static_cast <cl_float> (value [(SizeType) 0].GetDouble())
    ,   static_cast <cl_float> (value [(SizeType) 1].GetDouble())
    ,   static_cast <cl_float> (value [(SizeType) 2].GetDouble())}
    };
}

enum AttenuationMode
{   SPEAKER
,   HRTF
};

enum RequiredKeys
{   SOURCE_POSITION
,   MIC_POSITION
,   RAYS
,   REFLECTIONS
,   SAMPLE_RATE
,   BIT_DEPTH
};

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

    cl_float3 source = {{0, 0, 0, 0}};
    cl_float3 mic = {{0, 0, 1, 0}};

    auto numRays = 1024 * 8;
    auto numImpulses = 64;

    auto sampleRate = 44100.0;
    auto bitDepth = 16;

    Document document;
    attemptJsonParse (config_filename, document);
    if (! document.IsObject())
    {
        cerr << "Rayverb config must be stored in a JSON object" << endl;
        return 1;
    }

    map <RequiredKeys, string> requiredKeys
    {   {SOURCE_POSITION, "source_position"}
    ,   {MIC_POSITION, "mic_position"}
    ,   {RAYS, "rays"}
    ,   {REFLECTIONS, "reflections"}
    ,   {SAMPLE_RATE, "sample_rate"}
    ,   {BIT_DEPTH, "bit_depth"}
    };

    for (const auto & i : requiredKeys)
    {
        if (! document.HasMember (i.second.c_str()))
        {
            cerr
            <<  "Key '"
            <<  i.second
            <<  "' is required in json configuration object"
            <<  endl;
            return 1;
        }
    }

    for (const auto & i : {SOURCE_POSITION, MIC_POSITION})
    {
        auto str = requiredKeys [i].c_str();
        if (! validateJson3dVector (document [str]))
        {
            cerr
            <<  "Value for "
            <<  str
            <<  " must be a 3-element numeric array"
            <<  endl;
            return 1;
        }
    }

    for (const auto & i : {RAYS, REFLECTIONS, SAMPLE_RATE, BIT_DEPTH})
    {
        auto str = requiredKeys [i].c_str();
        if (! document [str].IsNumber())
        {
            cerr << "Value for " << str << " must be a number" << endl;
            return 1;
        }
    }

    source = getJson3dVector (document [requiredKeys [SOURCE_POSITION].c_str()]);
    mic = getJson3dVector (document [requiredKeys [MIC_POSITION].c_str()]);

    numRays = document [requiredKeys [RAYS].c_str()].GetInt();
    numImpulses = document [requiredKeys [REFLECTIONS].c_str()].GetInt();
    sampleRate = document [requiredKeys [SAMPLE_RATE].c_str()].GetInt();
    bitDepth = document [requiredKeys [BIT_DEPTH].c_str()].GetInt();

    map <AttenuationMode, string> modeKeys
    {   {SPEAKER, "speakers"}
    ,   {HRTF, "hrtf"}
    };

    auto mode = HRTF;
    cl_float3 facing = {{0, 0, 1}};
    cl_float3 up = {{0, 1, 0}};
    vector <Speaker> speakers;

    auto count = 0;
    for (const auto & i : modeKeys)
    {
        if (document.HasMember (i.second.c_str()))
            ++count;
    }

    if (count != 1)
    {
        cerr
        <<  "Config object must contain information for exactly one of these keys:"
        <<  endl;
        for (const auto & i : modeKeys)
            cerr << "    " << i.second << endl;

        return 1;
    }

    if (document.HasMember (modeKeys [SPEAKER].c_str()))
    {
        mode = SPEAKER;
        Value & v = document [modeKeys [SPEAKER].c_str()];

        if (! v.IsArray())
        {
            cerr
            <<  "Speaker definitions must be stored in a json array"
            <<  endl;
            return 1;
        }

        for (auto i = v.Begin(); i != v.End(); ++i)
        {
            auto direction = "direction";

            if (! i->HasMember (direction))
            {
                cerr
                <<  "Speaker definition must contain direction vector"
                <<  endl;
                return 1;
            }

            if (! validateJson3dVector ((*i) [direction]))
            {
                cerr
                <<  "Value for "
                <<  direction
                <<  " must be a 3-element numeric array"
                <<  endl;
                return 1;
            }

            auto shape = "shape";

            if (! i->HasMember (shape))
            {
                cerr
                <<  "Speaker definition must contain shape parameter"
                <<  endl;
                return 1;
            }

            if (! (*i) [shape].IsNumber())
            {
                cerr << "Value for " << shape << " must be a number" << endl;
                return 1;
            }

            speakers.push_back
            (   {   normalize (getJson3dVector ((*i) [direction]))
                ,   static_cast <cl_float> ((*i) [shape].GetDouble())
                }
            );
        }
    }
    else if (document.HasMember (modeKeys [HRTF].c_str()))
    {
        mode = HRTF;
        Value & v = document [modeKeys [HRTF].c_str()];

        auto facing_str = "facing";
        auto up_str = "up";

        if (! v.HasMember (facing_str))
        {
            cerr
            <<  "HRTF definition must contain "
            <<  facing_str
            <<  " vector"
            <<  endl;
            return 1;
        }

        if (! v.HasMember (up_str))
        {
            cerr
            <<  "HRTF definition must contain "
            <<  up_str
            <<  " vector"
            <<  endl;
            return 1;
        }

        if (! validateJson3dVector (v [facing_str]))
        {
            cerr
            <<  "Value for "
            <<  facing_str
            <<  " must be a 3-element numeric array"
            <<  endl;
            return 1;
        }

        if (! validateJson3dVector (v [up_str]))
        {
            cerr
            <<  "Value for "
            <<  up_str
            <<  " must be a 3-element numeric array"
            <<  endl;
            return 1;
        }

        facing = normalize (getJson3dVector (v [facing_str]));
        up = normalize (getJson3dVector (v [up_str]));
    }

    auto directions = getRandomDirections (numRays);
    vector <vector <Impulse>> attenuated;
#ifdef DIAGNOSTIC
    vector <Impulse> raw;
#endif

    try
    {
        Scene scene
        (   numImpulses
        ,   model_filename
        ,   material_filename
        );

        scene.trace (mic, source, directions);

        switch (mode)
        {
        case SPEAKER:
            attenuated = scene.attenuate (mic, speakers);
            break;
        case HRTF:
            attenuated = scene.hrtf(mic, facing, up);
            break;
        default:
            cerr << "This point should never be reached. Aborting" << endl;
            return 1;
        }

#ifdef DIAGNOSTIC
        raw = scene.getRawImages();
#endif
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

    //fixPredelay (attenuated, mic, source);
    flatten_and_write (output_filename, attenuated, sampleRate, bitDepth);

#ifdef DIAGNOSTIC
    //print_diagnostic (numRays, numImpulses, raw);
    print_diagnostic (numRays, NUM_IMAGE_SOURCE, raw);
#endif

    return 0;
}
