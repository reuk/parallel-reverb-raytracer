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

def main():
    parser = argparse.ArgumentParser(description="Analyse HRTF kernels.")
    parser.add_argument("folderpath", type=str, help="folder full of HRTF kernels")
    parser.add_argument("outfile", type=str, help="file to which to write output")
    args = parser.parse_args()

    filenames = [f for f in listdir(args.folderpath) if isfile(join(args.folderpath, f))]

    plot = False

    out = []

    for fname in filenames:
        radius, azimuth, elevation = decode_filename(fname)

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
            plt.plot(freqs, np.abs(fft))

            band_amps = [fft[i:j] for i, j in bin_bands]
            band_amps = [sum(np.abs(i)) / len(i) for i in band_amps]

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

if __name__ == "__main__":
    main()
