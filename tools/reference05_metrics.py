"""Descriptive same-source audio metrics, never a perceptual similarity score.

The read / psd / evaluate functions are extracted without numerical changes
from the analysis project's candidate_metrics.py, so the 0.5 receipt uses the
same Welch spectrum, log-frequency grid, note windows and envelope definition.
This self-contained copy makes the public export script reproducible.
"""
from pathlib import Path

import numpy as np
from scipy import signal
from scipy.io import wavfile

NOTE_HZ = np.array([440 * 2 ** ((n - 69) / 12) for n in range(42, 112)
                    if n % 12 in (3, 6, 8, 10, 1)])


def read(path):
    fs, x = wavfile.read(path)
    return fs, x.astype(np.float64) / (2 ** (np.iinfo(x.dtype).bits - 1) if x.dtype.kind == 'i' else 1)


def psd(x, fs):
    f, p = signal.welch(x, fs, nperseg=16384, noverlap=12288, nfft=65536, axis=0)
    return f, p.mean(axis=1)


def evaluate(y, x, reference, fs, region):
    a, b = (int(v * fs) for v in region)
    y, x, reference = y[a:b], x[a:b], reference[a:b]
    f, py = psd(y, fs)
    _, px = psd(x, fs)
    _, pr = psd(reference, fs)
    gain = 10 * np.log10((py + 1e-25) / (px + 1e-25))
    wanted = 10 * np.log10((pr + 1e-25) / (px + 1e-25))
    logf = np.geomspace(340, 3400, 1200)
    error = np.interp(logf, f, gain - wanted)
    level = np.mean(error)
    peaks, gaps = [], []
    for hz in NOTE_HZ:
        if 340 < hz < 3400:
            m = (f > hz * 2**(-30/1200)) & (f < hz * 2**(30/1200))
            peaks.append(float(np.max(gain[m])))
    for low, high in zip(NOTE_HZ, NOTE_HZ[1:]):
        if low < 340 or high > 3400:
            continue
        m = (f >= low * (high/low)**.3) & (f <= low * (high/low)**.7)
        gaps.append(float(np.median(gain[m])))
    sos = signal.butter(4, [300, 3700], btype='bandpass', fs=fs, output='sos')
    ym = signal.sosfilt(sos, y, axis=0)
    rm = signal.sosfilt(sos, reference, axis=0)
    def envelope(z):
        powers = np.mean(z*z, axis=1)
        return np.sqrt(np.convolve(powers, np.ones(512)/512, mode='valid')[::128])
    ey, er = envelope(ym), envelope(rm)
    active = er > np.max(er) * .02
    rms_ratio = 20*np.log10(np.sqrt(np.mean(ym*ym)/np.mean(rm*rm)) + 1e-25)
    return {'log_spectral_rmse_db':float(np.sqrt(np.mean(error**2))),
            'shape_rmse_after_one_level_offset_db':float(np.sqrt(np.mean((error-level)**2))),
            'mean_log_spectral_offset_db':float(level),
            'median_note_peak_gain_db':float(np.median(peaks)),
            'median_gap_gain_db':float(np.median(gaps)),
            'note_gap_contrast_db':float(np.median(peaks)-np.median(gaps)),
            'midband_rms_vs_reference_db':float(rms_ratio),
            'midband_envelope_correlation':float(np.corrcoef(ey[active], er[active])[0,1]),
            'midband_lr_correlation':float(np.corrcoef(ym.T)[0,1])}
