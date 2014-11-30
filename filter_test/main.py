import numpy as np
import scipy.signal as signal
import matplotlib.pyplot as plt
from scikits.audiolab import Format, Sndfile
from os.path import splitext

def sinc_kernel(fc, N):
    if not N % 2:
        raise RuntimeError("N must be an odd number")
    return np.sinc(2 * fc * (np.arange(N) - (N - 1) / 2.))

def lopass_kernel(sr, cutoff, wl):
    window = np.blackman(wl)
    kernel = sinc_kernel(cutoff / sr, wl)
    kernel *= window
    kernel /= np.sum(kernel)
    return kernel

def hipass_kernel(sr, cutoff, wl):
    kernel = lopass_kernel(sr, cutoff, wl)
    kernel = -kernel
    kernel[wl / 2] += 1
    return kernel

def bandpass_kernel(sr, cl, ch, wl):
    lop = lopass_kernel(sr, ch, wl)
    hip = hipass_kernel(sr, cl, wl)
    kernel = np.convolve(lop, hip)
    return kernel

def normalize(vec):
    return vec / np.max(np.abs(vec))

def write_file(outfile, snd, sr):
    bitdepth = {8: "pcm8", 16: "pcm16", 24: "pcm24"}
    f = Format(splitext(outfile)[1][1:], bitdepth[16])
    sndfile = Sndfile(outfile, "w", f, 1, sr)
    sndfile.write_frames(normalize(snd))

def sine_sweep(lower, upper, length, sr):
    lo = 2 * np.pi * lower / sr
    hi = 2 * np.pi * upper / sr
    phase = (np.arange(length) / (length - 1.)) * (hi - lo) + lo
    phase = np.cumsum(phase)
    phase -= lo
    return np.sin(phase)

def main():
    sr = 44100.0
    wl = 31

    boundaries = [20, 190, 380, 760, 1520, 3040, 6080, 12160, 20000]
    boundaries = zip(boundaries[:-1], boundaries[1:])

    labels = [str(lo) + " - " + str(hi) + "Hz" for lo, hi in boundaries]
    labels.append("Full Range")

    kernels = [bandpass_kernel(sr, i, j, wl) for i, j in boundaries]

    kernels.append(
        reduce(lambda x, y: x + y, kernels)
    )

    wh = [signal.freqz(k) for k in kernels]

    plt.figure(1)
    for k, l in zip(kernels, labels):
        plt.plot(k, label=l)
    plt.title("Filter Coefficients (%d taps)" % len(kernels[0]))
    plt.grid()
    plt.legend()

    plt.figure(2)
    plt.clf()
    for (w, h), l in zip(wh, labels):
        plt.plot((w / np.pi) * (sr / 2), 20 * np.log10(np.abs(h)), label=l)
    plt.ylabel("Amplitude Response (dB)")
    plt.xlabel("Frequency (Hz)")
    plt.title("Frequency Response of Parallel Filters")
    plt.grid()
    plt.legend()

    plt.show()

    summed = reduce(lambda x, y: x + y, kernels)
    write_file("summed.aiff", summed, sr)

    #sig = (np.random.rand(sr * 10) * 2 - 1) * 0.9
    sig = sine_sweep(20, 20000, sr * 10, sr) * 0.9

    convolved = [np.convolve(sig, k) for k in kernels]

    [write_file(str(i) + ".aiff", f, sr) for i, f in enumerate(convolved)]

    output = reduce(lambda x, y: x + y, convolved)
    write_file("summed_output.aiff", output, sr)

if __name__ == "__main__":
    main()
