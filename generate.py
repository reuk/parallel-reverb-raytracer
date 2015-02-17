from subprocess import call
from os.path import join
from tempfile import NamedTemporaryFile
from math import pi, sin, cos
import json

def do_trace(source, mic, facing, filename):
    prog = join("build", "bin", "parallel_raytrace")

    with NamedTemporaryFile() as f:
        json.dump({
            "source_position": source,
            "mic_position": mic,
            "rays": 1024 * 32,
            "reflections": 128,
            "sample_rate": 44100,
            "bit_depth": 16,
            "hrtf": {"facing": facing, "up": [0, 1, 0]}
        }, f)

        f.flush()

        call(
            [prog, f.name, join("assets", "room3.dxf"), join("assets", "mat.json"), filename]
        )

def filename(i):
    return "_".join([str(i), "out.aiff"])

def main():
    NUM_ANGLES = 1
    RADIUS = 5

    MIC = [0, -75, 20]

    for i in range(NUM_ANGLES):
        angle = i * 2 * pi / NUM_ANGLES

        source = [MIC[0] + cos(angle) * RADIUS, MIC[1], MIC[2] + sin(angle) * RADIUS]
        facing = [j - k for j, k in zip(source, MIC)]

        do_trace(
            source,
            MIC,
            facing,
            filename(i)
        )

if __name__ == "__main__":
    main()
