import numpy as np
import scipy.signal as signal
import matplotlib.pyplot as plt
from scikits.audiolab import Format, Sndfile
from os.path import splitext

def write_file(outfile, snd, sr):
    bitdepth = {8: "pcm8", 16: "pcm16", 24: "pcm24"}
    f = Format(splitext(outfile)[1][1:], bitdepth[16])
    sndfile = Sndfile(outfile, "w", f, 1, sr)
    sndfile.write_frames(normalize(snd))

def bandpass_coeffs(lo, hi, sr):
    lo = float(lo)
    hi = float(hi)
    c = np.sqrt(lo * hi)
    omega = 2 * np.pi * c / sr
    cs = np.cos(omega)
    sn = np.sin(omega)
    bandwidth = np.log2(hi / lo)
    Q = sn / (np.log(2.0) * bandwidth * omega)
    alpha = sn * np.sinh(1 / (2 * Q))

    b = [alpha, 0, -alpha]
    a = [1 + alpha, -2 * cs, 1 - alpha]

    return b, a

def phase(h):
    return np.unwrap(np.arctan2(np.imag(h), np.real(h)))

def sine_sweep(lower, upper, length, sr):
    lo = 2 * np.pi * lower / sr
    hi = 2 * np.pi * upper / sr
    phase = (np.arange(length) / (length - 1.)) * (hi - lo) + lo
    phase = np.cumsum(phase)
    phase -= lo
    return np.sin(phase)

def normalize(vec):
    return vec / np.max(np.abs(vec))

def main():
    sr = 44100.0

    boundaries = [20, 190, 380, 760, 1520, 3040, 6080, 12160, 20000]
    boundaries = zip(boundaries[:-1], boundaries[1:])

    c = [bandpass_coeffs(lo, hi, sr) for lo, hi in boundaries]

    wh = [signal.freqz(b, a) for b, a in c]

    plt.subplot(211)
    plt.title("Frequency response of EQ filters")
    for w, h in wh:
        plt.plot((w / np.pi) * (sr / 2), 20 * np.log10(np.abs(h)))
    plt.ylabel('Amplitude Response (dB)')
    plt.xlabel('Frequency (rad/sample)')
    plt.grid()

    plt.subplot(212)
    plt.title("Phase response of EQ filters")
    for w, h in wh:
        plt.plot(w, phase(h))
    plt.ylabel('Phase (radians)')
    plt.xlabel('Frequency (rad/sample)')
    plt.grid()

    plt.show()

    sig = (np.random.rand(sr * 10) * 2 - 1) * 0.9
    #sig = sine_sweep(20, 20000, sr * 10, sr) * 0.9

    filtered = [signal.lfilter(b, a, sig) for b, a in c]

    [write_file(str(i) + ".aiff", f, sr) for i, f in enumerate(filtered)]

    output = reduce(lambda x, y: x + y, filtered)
    write_file("summed_output.aiff", output, sr)

if __name__ == "__main__":
    main()
