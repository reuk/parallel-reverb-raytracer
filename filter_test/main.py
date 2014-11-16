import numpy as np
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
    wl = (2 ** 14) - 1

    boundaries = [22, 44, 88, 177, 354, 707, 1414, 2828, 5657, 11314, 20000]
    boundaries = zip(boundaries[:-1], boundaries[1:])

    kernels = [bandpass_kernel(sr, i, j, wl) for i, j in boundaries]

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
