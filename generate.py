from subprocess import call
from os.path import join
from tempfile import NamedTemporaryFile
from math import pi, sin, cos
import json

def get_basic_config(source, mic):
    return {
        "source_position": source,
        "mic_position": mic,
        "rays": 1024 * 32,
        "reflections": 128,
        "sample_rate": 44100,
        "bit_depth": 16,
    }

def get_hrtf_config(source, mic, hrtf):
    ret = get_basic_config(source, mic)
    ret["hrtf"] = hrtf
    return ret

def get_speaker_config(source, mic, speakers):
    ret = get_basic_config(source, mic)
    ret["speakers"] = speakers
    return ret

def do_trace(config_obj, filename):
    prog = join("build", "bin", "parallel_raytrace")

    with NamedTemporaryFile() as f:
        json.dump(config_obj, f)
        f.flush()
        call([prog, f.name, join("assets", "room3.dxf"), join("assets", "mat.json"), filename])

def filename(i, prefix):
    return join("responses", "_".join([prefix, str(i), "out.aiff"]))

def main():
    NUM_ANGLES = 8

    MIC = [0, -75, 20]

    def write_impulses(radius, prefix):
        for i in range(NUM_ANGLES):
            angle = i * 2 * pi / NUM_ANGLES

            source = [MIC[0] + sin(angle) * radius, MIC[1], MIC[2] + cos(angle) * radius]

            shape = 0.5

            def get_speaker_angle(angle):
                return {"direction": [sin(angle), 0, cos(angle)], "shape": shape}

            #config_obj = get_speaker_config(source, MIC, [get_speaker_angle(-11 * pi / 18), get_speaker_angle(-3 * pi / 18), get_speaker_angle(0), get_speaker_angle(3 * pi / 18), get_speaker_angle(11 * pi / 18)])
            config_obj = get_speaker_config(source, MIC, [get_speaker_angle(-3 * pi / 18), get_speaker_angle(3 * pi / 18)])
            #config_obj = get_hrtf_config(source, MIC, {"facing": [0, 0, 1], "up": [0, 1, 0]})

            do_trace(config_obj, filename(i, prefix))

    write_impulses(5, "near")
    write_impulses(10, "med")
    write_impulses(15, "far")

if __name__ == "__main__":
    main()
