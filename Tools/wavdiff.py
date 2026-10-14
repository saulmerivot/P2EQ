"""wavdiff.py a.wav b.wav -> max abs difference and level of a (24-bit/16-bit wav)."""
import sys, wave, numpy as np

def load(p):
    w = wave.open(p, 'rb'); raw = w.readframes(w.getnframes()); sw = w.getsampwidth(); w.close()
    if sw == 3:
        a = np.frombuffer(raw, dtype=np.uint8).reshape(-1, 3).astype(np.int32)
        x = a[:, 0] | (a[:, 1] << 8) | (a[:, 2] << 16)
        return np.where(x >= 1 << 23, x - (1 << 24), x) / float(1 << 23)
    return np.frombuffer(raw, dtype=np.int16) / 32768.0

a, b = load(sys.argv[1]), load(sys.argv[2])
n = min(len(a), len(b))
level = 20 * np.log10(np.sqrt(np.mean(a[:n] ** 2)) + 1e-12)
print('diff %.4f  level %.1f dB' % (np.max(np.abs(a[:n] - b[:n])), level))
