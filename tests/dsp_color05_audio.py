#!/usr/bin/env python3
"""Black-box 0.5 audio behavior contracts against the real WAV renderer.

No product imports, private hooks, commercial audio, or fitted reference kernels.
The deterministic stimuli and literal thresholds are independent of DSP source.

Breaks this suite is intended to catch:
  * keeping a wide dry-like spectral floor under the tuned main layer;
  * exciting a tuned filter without moving a sustained off-scale sinusoid;
  * assigning every resonance the same decay time instead of approximately Q38;
  * changing delay/dry identity while adding the new processing mode;
  * applying the three creative motions only to the extra layer above Color100.

Typical first RED (0.4 executable stays immutable):
  python3 tests/dsp_color05_audio.py --renderer ../iris-color-04/build/linux/iris-render \
      --output build/reference/05-test-red.json

New CLI mode switches can be supplied without changing the assertions:
  --extra-arg=--engine --extra-arg=prism
  --legacy-arg=--engine --legacy-arg=legacy
The optional legacy switches are used only for the dry/delay contract.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import warnings

import numpy as np
from scipy import signal
from scipy.io import wavfile

FS = 48000
E4 = 329.6275569128699
F4 = 349.2282314330039
E6 = 1318.5102276514797
G4 = 391.99543598174927
BASE = ["--preset", "init", "--root", "D#", "--scale", "minor-pent",
        "--color", "150", "--snap", "100", "--morph-off", "--gate-off",
        "--decay", "200", "--low", "230", "--high", "4000",
        "--transient", "0", "--width", "100", "--output", "0", "--no-limit",
        "--attack-tail-off", "--dive-off", "--motion-off", "--formant-off", "--tamer-off",
        "--fft", "2048", "--spectral", "1024", "--tail", "0", "--block", "0"]
SINGLE_E = ["--root", "E", "--scale", "custom", "--mask", "16", "--low", "60", "--high", "18000"]


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def rms(x) -> float:
    z = np.asarray(x, dtype=np.float64)
    return float(np.sqrt(np.mean(z*z)))


def db(amplitude: float) -> float:
    return float(20*np.log10(max(abs(float(amplitude)), 1e-30)))


def stereo(x) -> np.ndarray:
    z = np.asarray(x, dtype=np.float32)
    return np.column_stack([z, z]) if z.ndim == 1 else z


class Harness:
    def __init__(self, args):
        self.args = args
        self.exe = args.renderer.resolve()
        if not self.exe.is_file():
            raise FileNotFoundError(self.exe)
        self.renderer_sha256 = sha(self.exe)
        self.work = args.work or args.output.parent/(args.output.stem+"-fixtures")
        self.work.mkdir(parents=True, exist_ok=True)
        self.sources = {}
        self.renders = []
        self.cases = []

    def source(self, name: str, x) -> tuple[Path, np.ndarray]:
        z = stereo(x)
        p = self.work/(name+".input.wav")
        wavfile.write(p, FS, z)
        self.sources[name] = {"path": str(p.resolve()), "frames": len(z), "sha256": sha(p),
                              "sample_format": "IEEE float32 stereo", "sample_rate": FS}
        return p, z

    def render(self, name, source, options=(), *, legacy=False):
        src = source[0]
        dest = self.work/(name+".output.wav")
        mode_args = self.args.legacy_arg if legacy else self.args.extra_arg
        cmd = [str(self.exe), str(src), str(dest), *BASE, *mode_args, *map(str, options)]
        run = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
        record = {"name": name, "command": cmd, "exit_code": run.returncode,
                  "stdout": run.stdout.strip(), "stderr": run.stderr.strip()}
        self.renders.append(record)
        if run.returncode:
            raise RuntimeError(f"Renderer invocation failed for {name}: {run.stderr or run.stdout}")
        lines = [line for line in run.stdout.splitlines() if line.strip().startswith('{')]
        metadata = json.loads(lines[-1])
        with warnings.catch_warnings():
            warnings.simplefilter("ignore", wavfile.WavFileWarning)
            rate, y = wavfile.read(dest)
        if rate != FS or y.ndim != 2 or y.shape[1] != 2 or y.dtype != np.float32:
            raise RuntimeError(f"Unexpected real renderer WAV format for {name}: {rate}, {y.shape}, {y.dtype}")
        if not np.isfinite(y).all():
            raise RuntimeError(f"Non-finite real renderer audio for {name}")
        record.update({"metadata": metadata, "output_sha256": sha(dest), "output_frames": len(y)})
        return y.astype(np.float64), metadata

    def check(self, name, break_caught, metrics, assertions):
        checks = [{"requirement": label, "passed": bool(value)} for label, value in assertions]
        passed = all(c["passed"] for c in checks)
        self.cases.append({"name": name, "break_caught": break_caught, "measurements": metrics,
                           "assertions": checks, "passed": passed})
        print(("PASS" if passed else "FAIL"), name, json.dumps(metrics, allow_nan=False), flush=True)


def noise_source(seconds=4.0):
    n = round(seconds*FS)
    # The two halves have independent seeds; thresholds must pass each half.
    a = np.random.default_rng(50183).normal(0, .055, (n//2, 2))
    b = np.random.default_rng(90271).normal(0, .055, (n-n//2, 2))
    x = np.concatenate([a, b])
    x[:, 1] = .7*x[:, 0] + .714142842854285*x[:, 1]
    return x


def burst_source():
    duration = 3.8
    n = round(duration*FS)
    t = np.arange(n)/FS
    rng = np.random.default_rng(517903)
    x = np.zeros((n, 2))
    onsets = np.array([.15, .70, 1.25, 1.80, 2.35, 2.90, 3.45])
    for j, onset in enumerate(onsets):
        u = t-onset
        on = (u >= 0) & (u < .32)
        v = u[on]
        envelope = (1-np.exp(-v/.0008))*np.exp(-v/.15)
        broadband = rng.normal(0, .13, (len(v), 2))
        bass = .025*np.sin(2*np.pi*(73.41619198 if j % 2 else 82.40688923)*v)
        # An original, modest harmonic bass component coexists with broad noise.
        for k in range(2, 15):
            bass += .012/k*np.sin(2*np.pi*82.40688923*k*v + .31*k)
        x[on] += envelope[:, None]*(broadband+bass[:, None])
    return x, onsets


def grid_metrics(x, y, start, end):
    sl = slice(round(start*FS), round(end*FS))
    def psd(z):
        f, p = signal.welch(z[sl], FS, nperseg=16384, noverlap=12288, nfft=65536, axis=0)
        return f, np.mean(p, axis=1)
    f, px = psd(x)
    _, py = psd(y)
    gain = 10*np.log10((py+1e-25)/(px+1e-25))
    notes = [440*2**((m-69)/12) for m in range(48, 112) if m % 12 in (1, 3, 6, 8, 10)]
    peaks, gaps = [], []
    for hz in notes:
        if 340 < hz < 3400:
            mask = (f >= hz*2**(-25/1200)) & (f <= hz*2**(25/1200))
            peaks.append(float(np.max(gain[mask])))
    for low, high in zip(notes, notes[1:]):
        if 340 < low < high < 3400:
            mask = (f >= low*(high/low)**.35) & (f <= low*(high/low)**.65)
            gaps.append(float(np.median(gain[mask])))
    peak = float(np.median(peaks)); gap = float(np.median(gaps))
    return {"median_note_peak_gain_db": peak, "median_gap_gain_db": gap,
            "note_gap_contrast_db": peak-gap, "number_of_note_peaks": len(peaks),
            "number_of_gaps": len(gaps), "peak_gains_db": peaks, "gap_gains_db": gaps}


def tone_amplitudes(y, frequencies, start, end):
    z = np.asarray(y, dtype=np.float64)
    if z.ndim == 2:
        z = z.mean(axis=1)
    ids = np.arange(round(start*FS), round(end*FS))
    columns = []
    for f in frequencies:
        columns.extend([np.sin(2*np.pi*f*ids/FS), np.cos(2*np.pi*f*ids/FS)])
    columns.append(np.ones(len(ids)))
    coefficients = np.linalg.lstsq(np.column_stack(columns), z[ids], rcond=None)[0]
    return [float(np.hypot(coefficients[2*k], coefficients[2*k+1])) for k in range(len(frequencies))]


def strongest_frequency(y, start, end, low, high):
    z = np.asarray(y, dtype=np.float64)
    if z.ndim == 2:
        z = z.mean(axis=1)
    z = z[round(start*FS):round(end*FS)]
    nfft = max(262144, 1 << (len(z)-1).bit_length())
    f, p = signal.periodogram(z, FS, window='blackmanharris', nfft=nfft, detrend=False)
    candidates = np.flatnonzero((f >= low) & (f <= high))
    k = candidates[np.argmax(p[candidates])]
    # Quadratic interpolation of the log spectral peak, independent of DSP.
    logp = np.log(np.maximum(p[k-1:k+2], 1e-30))
    offset = .5*(logp[0]-logp[2])/(logp[0]-2*logp[1]+logp[2]+1e-30)
    return float((k+np.clip(offset, -.5, .5))*FS/nfft)


def tail_q(y, frequency, stop, expected_q):
    nwin = int(round(16*FS/frequency))
    nwin += nwin % 2
    window = signal.windows.hann(nwin, sym=False)
    kernel = window*np.exp(-2j*np.pi*frequency*np.arange(nwin)/FS)
    # Two equal cascaded poles can have (a+b*t)*exp(-t/tau), so an early
    # straight-line dB fit overestimates Q. Start at least five expected pole
    # time constants after stop, and after the main analysis-window release.
    # This expected-Q guard only chooses a late observation window. Neither
    # the regression nor the measured Q reuses any product implementation.
    guard = max(.06, 5*expected_q/(np.pi*frequency))
    centers = np.arange(stop+guard+nwin/(2*FS), stop+max(.38, guard+.22), .002)
    levels = []
    for center in centers:
        begin = round(center*FS)-nwin//2
        value = abs(kernel@y[begin:begin+nwin])/(window.sum()/2)
        levels.append(db(value))
    levels = np.array(levels)
    maximum = float(np.max(levels))
    use = (levels > -145) & (levels >= maximum-38)
    if np.count_nonzero(use) < 8:
        return {"valid": False, "reason": "too few measurable tail windows", "max_dbfs": maximum,
                "usable_windows": int(np.count_nonzero(use))}
    time = centers[use]-stop
    slope, intercept = np.polyfit(time, levels[use], 1)
    fitted = slope*time+intercept
    span = float(np.ptp(levels[use]))
    r2 = float(1-np.sum((levels[use]-fitted)**2)/max(np.sum((levels[use]-levels[use].mean())**2), 1e-25))
    if slope >= 0:
        return {"valid": False, "reason": "tail has nonnegative envelope slope", "slope_db_per_s": float(slope)}
    tau = float(-20/(np.log(10)*slope))
    return {"valid": True, "frequency_hz": frequency, "Q": float(np.pi*frequency*tau),
            "tau_ms": tau*1000, "equivalent_bandwidth_hz": float(1/(np.pi*tau)),
            "slope_db_per_s": float(slope), "r_squared": r2, "decay_span_db": span,
            "window_samples": nwin, "interval_s": [float(centers[use][0]), float(centers[use][-1])],
            "usable_windows": int(np.count_nonzero(use)), "max_dbfs": maximum}


def run_tests(h):
    # 1. A wide unchanged main layer raises off-scale valleys and fails this.
    continuous = h.source("continuous_noise", noise_source())
    y, _ = h.render("continuous_color150", continuous)
    grid = {label: grid_metrics(continuous[1], y, a, b)
            for label, a, b in [('seed_A', .10, 1.90), ('seed_B', 2.10, 3.90)]}
    h.check("continuous_noise_color150_grid", "Wide unchanged main layer fills the off-scale gaps.", grid,
        [(f"{label}: note/gap contrast >=18 dB, gaps <=-10 dB, median note peak >=-2 dB",
          m['note_gap_contrast_db'] >= 18 and m['median_gap_gain_db'] <= -10 and m['median_note_peak_gain_db'] >= -2)
         for label, m in grid.items()])

    # 2. Passing a stationary stimulus must not hide broad transient leakage.
    burst, onsets = burst_source()
    bursts = h.source("repeated_bass_noise", burst)
    y150, _ = h.render("bursts_color150", bursts)
    m = grid_metrics(bursts[1], y150, .10, 3.78)
    h.check("repeated_broadband_bass_color150_grid", "Transient handling bypasses the musical band and refills gaps.", m,
        [("note/gap contrast >=18 dB and median gaps <=-9 dB", m['note_gap_contrast_db'] >= 18 and m['median_gap_gain_db'] <= -9),
         ("allowed-note peaks remain audible, >=-2 dB relative to input", m['median_note_peak_gain_db'] >= -2)])

    # 3. A fixed filter cannot move a sustained F4 oscillator to E4 in steady state.
    t = np.arange(round(1.8*FS))/FS
    x = .10*np.sin(2*np.pi*F4*t+.27)*np.clip(t/.008, 0, 1)
    single = h.source("off_scale_F4", x)
    moved, _ = h.render("F4_to_E4_color100", single, [*SINGLE_E, "--color", "100"])
    target, original = tone_amplitudes(moved, [E4, F4], .70, 1.70)
    peak = strongest_frequency(moved, .70, 1.70, 250, 450)
    cents = float(1200*np.log2(peak/E4))
    m = {"target_hz": E4, "input_hz": F4, "strongest_output_frequency_hz": peak,
         "output_peak_cents_from_E4": cents, "target_sine_amplitude": target,
         "remaining_F4_amplitude": original, "target_to_original_db": db(target/max(original, 1e-30)),
         "target_to_input_amplitude_db": db(target/.10)}
    h.check("sustained_off_scale_tone_is_relocated", "Filtering/amplification is substituted for real frequency relocation.", m,
        [("steady-state output peak is within 5 cents of E4", abs(cents) <= 5),
         ("E4 amplitude exceeds unchanged F4 by at least20 dB", m['target_to_original_db'] >= 20),
         ("moving the tone does not merely mute it: target amplitude >=0.015", target >= .015)])

    # 4. Difference of150 and100 isolates the added resonance; two octaves
    # cannot share the same millisecond pole time and still be constant Q.
    t = np.arange(round(1.4*FS))/FS
    stop = .75
    gate = ((t >= .15) & (t < stop)).astype(float)
    qsource = h.source("tail_two_octaves", np.column_stack([
        .10*np.sin(2*np.pi*E4*t+.31)*gate, .10*np.sin(2*np.pi*E6*t+.71)*gate]))
    q100, _ = h.render("tail_color100", qsource, [*SINGLE_E, "--color", "100", "--decay", str(h.args.decay_ms)])
    q150, _ = h.render("tail_color150", qsource, [*SINGLE_E, "--color", "150", "--decay", str(h.args.decay_ms)])
    residual = q150-q100
    fits = [tail_q(residual[:, 0], E4, stop, h.args.target_q),
            tail_q(residual[:, 1], E6, stop, h.args.target_q)]
    valid = all(v['valid'] for v in fits)
    ratio = fits[1]['Q']/fits[0]['Q'] if valid else None
    low_q = h.args.target_q*.65; high_q = h.args.target_q*1.35
    h.check("resonance_tail_is_near_constant_Q", "All notes use the same decay milliseconds instead of near-constant Q.",
        {"decay_parameter_ms": h.args.decay_ms, "target_Q": h.args.target_q,
         "accepted_Q_range": [low_q, high_q], "high_to_low_Q_ratio": ratio, "fits": fits},
        [("both late dominant-pole tails have R2>=0.96 and observable decay>=18 dB",
          valid and all(v['r_squared'] >= .96 and v['decay_span_db'] >= 18 for v in fits)),
         ("each Q is within35% of the declared baseline", valid and all(low_q <= v['Q'] <= high_q for v in fits)),
         ("two-octave Q ratio stays within0.75..1.333", valid and .75 <= ratio <= 1.3333333333333333)])

    # 5. Identity is compared with the exact float32 fixture, not a separately
    # rounded float64 generator, and actual latency is checked in the waveform.
    identity = h.source("identity_noise_and_impulse", noise_source(.36))
    identity[1][0] = (.50, -.375)
    wavfile.write(identity[0], FS, identity[1])
    h.sources['identity_noise_and_impulse']['sha256'] = sha(identity[0])
    modes = [('default', False)] + ([('legacy', True)] if h.args.legacy_arg else [])
    rows = []
    all_fx = ["--attack-tail-on", "--attack-color", "0", "--tail-color", "200",
              "--dive-on", "--dive-semitones", "12", "--motion-on", "--motion-depth", "100",
              "--formant-on", "--formant-mix", "100", "--tamer-on", "--tamer-amount", "100",
              "--eq-band", "7:9", "--keep-latency"]
    for mode, legacy in modes:
        for setting, options in [('Color0', ['--color', '0']), ('Bypass', ['--color', '150', '--bypass'])]:
            dry, metadata = h.render(f"identity_{mode}_{setting}", identity, [*all_fx, *options], legacy=legacy)
            latency = int(metadata['latency_samples'])
            aligned = dry[latency:latency+len(identity[1])]
            rows.append({"mode": mode, "setting": setting, "reported_latency": latency,
                         "output_frames": len(dry), "expected_output_frames": len(identity[1])+h.args.expected_latency,
                         "prefix_exact_zero": bool(np.all(dry[:latency] == 0)),
                         "aligned_dry_bit_exact": bool(np.array_equal(aligned, identity[1])),
                         "max_abs_error": float(np.max(abs(aligned-identity[1])))})
    h.check("fixed_latency_and_exact_dry_paths", "New processing delays or alters Color0/bypass and breaks host compensation.", {"runs": rows},
        [("every mode/path keeps declared fixed latency and output length",
          all(v['reported_latency'] == h.args.expected_latency and v['output_frames'] == v['expected_output_frames'] for v in rows)),
         ("Color0 and bypass have an exact zero prefix then bit-exact input", all(v['prefix_exact_zero'] and v['aligned_dry_bit_exact'] for v in rows))])

    #6. AtColor100 there is no extra layer. The attack/tail control must still
    # change the core's time envelope, not just multiply an absent add-on.
    base100, _ = h.render("bursts_color100", bursts, ["--color", "100"])
    attack, _ = h.render("bursts_attack_tail_color100", bursts,
        ["--color", "100", "--attack-tail-on", "--attack-color", "0", "--tail-color", "200", "--color-transition", "100"])
    sos = signal.butter(4, [300, 3700], fs=FS, btype='bandpass', output='sos')
    ref_band = signal.sosfilt(sos, base100, axis=0)
    mod_band = signal.sosfilt(sos, attack, axis=0)
    early_gains, late_gains = [], []
    for onset in onsets[:-1]:
        early = slice(round((onset+.010)*FS), round((onset+.045)*FS))
        late = slice(round((onset+.18)*FS), round((onset+.27)*FS))
        early_gains.append(db(rms(mod_band[early])/max(rms(ref_band[early]), 1e-30)))
        late_gains.append(db(rms(mod_band[late])/max(rms(ref_band[late]), 1e-30)))
    contrast = float(np.median(np.array(late_gains)-np.array(early_gains)))
    delta = rms(mod_band-ref_band)/max(rms(ref_band), 1e-30)
    h.check("attack_tail_changes_main_layer_at_color100", "Attack/tail only controls the absent extra layer at Color100.",
        {"early_gain_db_by_burst": early_gains, "late_gain_db_by_burst": late_gains,
         "median_late_minus_early_gain_db": contrast, "midband_difference_relative_rms": delta},
        [("attack/tail changes relative early-versus-late level by at least3 dB", abs(contrast) >= 3),
         ("midband effect difference is at least25% RMS of core output", delta >= .25)])

    #7. A stable E4 is asked to start one octave up and land back at E4.
    t = np.arange(round(1.05*FS))/FS
    onset = .15
    gate = ((t >= onset) & (t < .93)).astype(float)
    dive_source = h.source("dive_E4_burst", .10*np.sin(2*np.pi*E4*(t-onset)+.31)*gate)
    dive, _ = h.render("dive_main_color100", dive_source,
        [*SINGLE_E, "--color", "100", "--dive-on", "--dive-semitones", "12", "--dive-time", "160"])
    early_hz = strongest_frequency(dive, onset+.018, onset+.058, 250, 950)
    settled_hz = strongest_frequency(dive, .63, .88, 250, 950)
    early_st = float(12*np.log2(early_hz/E4)); settled_cents = float(1200*np.log2(settled_hz/E4))
    h.check("pitch_dive_moves_main_layer_at_color100", "PitchDive moves only an absent extra layer or never lands on the target.",
        {"early_peak_hz": early_hz, "early_semitones_above_target": early_st,
         "settled_peak_hz": settled_hz, "settled_cents_from_target": settled_cents},
        [("early core output is at least3 semitones above E4", early_st >= 3),
         ("late core output lands within8 cents of E4", abs(settled_cents) <= 8)])

    #8. Two stable allowed notes should trade emphasis at the requested rate.
    t = np.arange(round(2.15*FS))/FS
    motion_source = h.source("motion_two_notes", .065*(np.sin(2*np.pi*E4*t+.31)+np.sin(2*np.pi*G4*t+.71)))
    moving, _ = h.render("motion_main_color100", motion_source,
        ["--root", "E", "--scale", "custom", "--mask", "144", "--low", "60", "--high", "18000",
         "--color", "100", "--motion-on", "--motion-depth", "100", "--motion-free", "--motion-rate", "2"])
    ratios = []
    for a, b in [(.20, .40), (.70, .90), (1.20, 1.40), (1.70, 1.90)]:
        ae, ag = tone_amplitudes(moving, [E4, G4], a, b)
        ratios.append({"interval_s": [a, b], "E4_amplitude": ae, "G4_amplitude": ag,
                       "E_to_G_db": db(ae/max(ag, 1e-30))})
    swing = float(np.ptp([v['E_to_G_db'] for v in ratios]))
    h.check("scale_motion_alternates_main_notes_at_color100", "ScaleMotion controls only an absent extra layer, leaving main note balance static.",
        {"intervals": ratios, "note_balance_swing_db": swing},
        [("E/G emphasis changes by at least10 dB", swing >= 10),
         ("the selected pair retains an audible component in each interval", all(max(v['E4_amplitude'], v['G4_amplitude']) >= .012 for v in ratios))])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--renderer', type=Path, required=True)
    parser.add_argument('--output', type=Path, default=Path('build/reference/05-audio-contracts.json'))
    parser.add_argument('--work', type=Path)
    parser.add_argument('--extra-arg', action='append', default=[], help='Repeated raw CLI token for the new/default mode; use = for --flags.')
    parser.add_argument('--legacy-arg', action='append', default=[], help='Repeated raw CLI token selecting legacy mode; enables extra dry/latency checks.')
    parser.add_argument('--expected-latency', type=int, default=3072)
    parser.add_argument('--target-q', type=float, default=38)
    parser.add_argument('--decay-ms', type=float, default=200)
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    h = Harness(args)
    error = None
    try:
        run_tests(h)
    except Exception as exc:
        error = f'{type(exc).__name__}: {exc}'
        print('ERROR', error, file=sys.stderr, flush=True)
    unchanged = sha(h.exe) == h.renderer_sha256
    passed = error is None and unchanged and len(h.cases) == 8 and all(c['passed'] for c in h.cases)
    result = {'contract_version': '0.5.audio.2', 'timestamp_utc': datetime.now(timezone.utc).isoformat(),
              'renderer': str(h.exe), 'renderer_sha256': h.renderer_sha256, 'renderer_unchanged': unchanged,
              'test_script_sha256': sha(Path(__file__)), 'sample_rate': FS,
              'new_mode_extra_args': args.extra_arg, 'legacy_mode_extra_args': args.legacy_arg,
              'expected_latency': args.expected_latency, 'target_q': args.target_q, 'decay_parameter_ms': args.decay_ms,
              'fixtures': h.sources, 'render_calls': h.renders, 'cases': h.cases,
              'passed_cases': sum(c['passed'] for c in h.cases), 'failed_cases': sum(not c['passed'] for c in h.cases),
              'error': error, 'passed': passed,
              'scope': 'Black-box behavioral criteria on independent synthetic inputs. No claim of Chroma algorithm identity or perceptual equivalence.'}
    args.output.write_text(json.dumps(result, indent=2, allow_nan=False)+'\n')
    print(f'Result: {result["passed_cases"]}/{len(h.cases)} passed; {args.output}', flush=True)
    return 0 if passed else (2 if error else 1)


if __name__ == '__main__':
    raise SystemExit(main())
