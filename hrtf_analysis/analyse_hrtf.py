import numpy as np
import argparse
import json
import matplotlib.pyplot as plt
from scikits.audiolab import Sndfile

from os import walk, makedirs, listdir
from os.path import join, exists, dirname, isfile, splitext

FREQUENCY_BOUNDARIES = [0, 190, 380, 760, 1520, 3040, 6080, 12160, 20000]

def decode_filename(fname):
    fname, _ = splitext(fname)
    parts = fname.split("_")

    if len(parts) is not 6:
        raise RuntimeError("Filename isn't in the IRCAM Listen filename format")

    def extract(part):
        return int(part[1:])

    radius = extract(parts[3])
    azimuth = extract(parts[4])
    elevation = extract(parts[5])

    return radius, azimuth, elevation

def write_file(data, outfile):
    out = """
    #include "rayverb.h"
    //  [channel][azimuth][elevation]
    const std::array <std::array <std::array <VolumeType, 180>, 360>, 2> HRTF_DATA =
    """

    l = []
    r = []

    def get_entry(a, e):
        for i in data:
            if i[0]["a"] == a and i[0]["e"] == e:
                return i[1]
        return [[0, 0, 0, 0, 0, 0, 0, 0], [0, 0, 0, 0, 0, 0, 0, 0]]

    for a in range(360):
        a_min = 0
        a_max = 360

        for i in data:
            tmp = i[0]["a"]

            if a < tmp <= a_max:
                a_max = tmp
            if a_min < tmp <= a:
                a_min = tmp

        l_for_angle = []
        r_for_angle = []

        for e in range(180):
            e_min = 0
            e_max = 180

            for i in data:
                tmp = i[0]["e"]

                if e < tmp <= e_max:
                    e_max = tmp
                if e_min < tmp <= e:
                    e_min = tmp

            c_0_0 = get_entry (a_min, e_min)
            c_1_0 = get_entry (a_max, e_min)
            c_0_1 = get_entry (a_min, e_max)
            c_1_1 = get_entry (a_max, e_max)

            a_ratio = (a - a_min) / (a_max - a_min)
            e_ratio = (e - e_min) / (e_max - e_min)

            def chan_dat(chan):
                a_0 = [i + (j - i) * a_ratio for i, j in zip(c_0_0[chan], c_1_0[chan])]
                a_1 = [i + (j - i) * a_ratio for i, j in zip(c_0_1[chan], c_1_1[chan])]
                ret = [i + (j - i) * e_ratio for i, j in zip(a_0, a_1)]
                return ret

            l_for_angle.append(chan_dat(0))
            r_for_angle.append(chan_dat(1))

        l.append(l_for_angle)
        r.append(r_for_angle)

    def construct(d):
        out = ""
        if isinstance(d, list):
            if len(d) == 8:
                out += "(VolumeType)"
            out += "{{"
            out += construct(d[0])
            for i in d[1:]:
                out += ", "
                out += construct(i)
            out += "}}\n"
        else:
            out += str(d)
        return out

    dat = [l, r]

    out += construct(dat)
    out += ";"

    with open(outfile, "w") as f:
        f.write(out)

def main():
    parser = argparse.ArgumentParser(description="Analyse HRTF kernels.")
    parser.add_argument("folderpath", type=str, help="folder full of HRTF kernels")
    parser.add_argument("outfile", type=str, help="file to which to write output")
    args = parser.parse_args()

    filenames = [f for f in listdir(args.folderpath) if isfile(join(args.folderpath, f))]

    plot = False

    out = []

    if plot:
        filenames = filenames[:3]

    for fname in filenames:
        radius, azimuth, elevation = decode_filename(fname)

        elevation = (90 + 360 - elevation) % 360

        fpath = join(args.folderpath, fname)
        sndfile = Sndfile(fpath, "r")
        sr = sndfile.samplerate
        data = sndfile.read_frames(sndfile.nframes)

        channels = np.hsplit(data, 2)
        channels = [np.squeeze(i) for i in channels]

        bin_boundaries = [i * sndfile.nframes / sr for i in FREQUENCY_BOUNDARIES]
        bin_bands = zip(bin_boundaries[:-1], bin_boundaries[1:])

        if plot:
            plt.figure()

        channel_coeffs = []

        for signal in channels:
            fft = np.fft.rfft(signal)
            freqs = np.fft.rfftfreq(signal.size, 1.0 / sr)
            plt.plot(freqs, np.power(np.abs(fft), 2))

            band_amps = [fft[i:j] for i, j in bin_bands]
            band_amps = [sum(np.power(np.abs(i), 2)) / len(i) for i in band_amps]

            channel_coeffs.append(band_amps)

            if plot:
                to_plot = [[k] * (j - i) for k, (i, j) in zip(band_amps, bin_bands)]
                to_plot = sum(to_plot, [])
                to_plot += [0] * (len(freqs) - len(to_plot))
                plt.plot(freqs, to_plot)

        if plot:
            plt.show()

        out.append([{'r': radius, 'a': azimuth, 'e': elevation}, channel_coeffs])

    with open(args.outfile, 'w') as f:
        json.dump(out, f)

    write_file(out, join("rayverb", "hrtf.cpp"))

if __name__ == "__main__":
    main()
