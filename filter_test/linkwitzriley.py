import numpy as np
import scipy.signal as signal
import matplotlib.pyplot as plt
from scikits.audiolab import Format, Sndfile
from os.path import splitext

def getC(co, sr):
    wcT = np.pi * co / sr
    return np.cos(wcT) / np.sin(wcT)

def lopass1ord(c):
    a0 = c + 1
    b = [1 / a0, 1 / a0]
    a = [1.0, (1 - c) / a0]
    return np.convolve(b, b), np.convolve(a, a)

def hipass1ord(c):
    a0 = c + 1
    b = [c / a0, -c / a0]
    a = [1.0, (1 - c) / a0]
    return np.convolve(b, b), np.convolve(a, a)

def lopass2ord(c):
    a0 = c * c + c * np.sqrt(2) + 1

    b = [1 / a0, 2 / a0, 1 / a0]
    a = [1.0, (-2 * (c * c - 1)) / a0, (c * c - np.sqrt(2) * c + 1) / a0]

    return b, a

def hipass2ord(c):
    a0 = c * c + c * np.sqrt(2) + 1

    b = [(c * c) / a0, (-2 * c * c) / a0, (c * c) / a0]
    a = [1.0, (-2 * (c * c - 1)) / a0, (c * c - np.sqrt(2) * c + 1) / a0]

    return b, a

def bandpass(lo, hi, sr):
    a, b = lopass1ord(getC(hi, sr))
    x, y = hipass1ord(getC(lo, sr))
    return np.convolve(a, x), np.convolve(b, y)

def phase(h):
    return np.unwrap(np.arctan2(np.imag(h), np.real(h)))

def main():
    sr = 44100.0

    boundaries = [90, 175, 350, 700, 1400, 2800, 5600, 11200, 20000]
    boundaries = zip(boundaries[:-1], boundaries[1:])

    labels = [str(lo) + " - " + str(hi) + "Hz" for lo, hi in boundaries]
    #labels.append("Full Range")

    c = [bandpass(lo, hi, sr) for lo, hi in boundaries]

    wh = [signal.freqz(b, a) for b, a in c]
    #wh.append(
    #    reduce(lambda (w, y), (_, x): [w, y + x], wh)
    #)

    plt.subplot(211)
    plt.title("Frequency response of EQ filters")
    for (w, h), l in zip(wh, labels):
        plt.plot((w / np.pi) * (sr / 2), 20 * np.log10(np.abs(h)), label=l)
    plt.ylabel('Amplitude Response (dB)')
    plt.xlabel('Frequency (Hz)')
    plt.grid()
    plt.legend()

    plt.subplot(212)
    plt.title("Phase response of EQ filters")
    for (w, h), l in zip(wh, labels):
        plt.plot((w / np.pi) * (sr / 2), phase(h), label=l)
    plt.ylabel('Phase (radians)')
    plt.xlabel('Frequency (Hz)')
    plt.grid()
    plt.legend()

    plt.show()

if __name__ == "__main__":
    main()
