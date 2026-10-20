#!/usr/bin/env python3
"""Measure audible/effective additional Color energy without changing the DSP.

Raw outputs are never loudness-normalized. The residual after a separately
reported scalar RMS match distinguishes spectral change from simple loudness.
Only gate/morph-off subtraction is a literal isolated parallel resonance layer;
with Morph on it measures the total audible consequence of adding that layer.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import numpy as np
from scipy import signal
from scipy.io import wavfile

FS = 48000
E4 = 440 * 2 ** ((64-69)/12)
MASK = sum(1 << n for n in (2,4,7,9,11))  # E minor pentatonic, absolute classes.
BASE = ['--root','E','--scale','minor-pent','--low','180','--high','8000',
        '--decay','200','--transient','0','--width','100','--output','0',
        '--no-limit','--tail','0','--block','256']
CONFIGS = {
    'morph_off_gate_off': ['--morph-off','--gate-off'],
    'morph30_gate_off': ['--morph','30','--gate-off'],
    'morph100_gate_off': ['--morph','100','--gate-off'],
    'morph_off_gate_minus60': ['--morph-off','--gate','-60'],
    'morph30_gate_minus60': ['--morph','30','--gate','-60'],
    'morph_off_gate_minus42': ['--morph-off','--gate','-42'],
}

def rms(x):
    return float(np.sqrt(np.mean(np.asarray(x,dtype=np.float64)**2)))

def db(x):
    return None if x == 0 else float(20*np.log10(abs(x)))

def focus(x, fs):
    x=np.asarray(x,dtype=np.float64)
    if x.ndim==1:x=x[:,None]
    w=signal.windows.hann(len(x),sym=False)
    p=np.sum(abs(np.fft.rfft(x*w[:,None],axis=0))**2,axis=1)
    f=np.fft.rfftfreq(len(x),1/fs)
    allowed=np.zeros(len(f),dtype=bool)
    for n in range(128):
        if MASK & (1<<(n%12)):
            hz=440*2**((n-69)/12)
            if 180 <= hz <= 8000:
                allowed |= abs(f-hz) <= max(2.0,hz*(2**(20/1200)-1))
    band=(f>=243)&(f<=7040)
    return float(p[allowed&band].sum()/max(p[band].sum(),1e-30))

def sources(project):
    t=np.arange(round(1.9*FS))/FS
    env=np.minimum(1,np.maximum(0,(t-.10)/.002))*np.minimum(1,np.maximum(0,(1.30-t)/.002))
    tone=.2*np.sin(2*np.pi*E4*t)*env
    rng=np.random.default_rng(310218)
    noise=signal.sosfilt(signal.butter(4,[180,8000],btype='bandpass',fs=FS,output='sos'),rng.normal(size=len(t)))
    noise*=.10/rms(noise[round(.2*FS):round(1.2*FS)])
    noise*=env
    yield 'on_scale_tone',np.column_stack((tone,tone)),100,(.45,1.15),(1.36,1.72)
    yield 'noise_without_snap',np.column_stack((noise,noise)),0,(.45,1.15),(1.36,1.72)
    yield 'noise_with_snap',np.column_stack((noise,noise)),100,(.45,1.15),(1.36,1.72)
    source=project/'audio'/'raw'/'01_dry.wav'
    fs,demo=wavfile.read(source)
    assert fs==FS and demo.ndim==2 and demo.shape[1]==2
    # 2 bars (3.2 seconds) plus silence keeps the original synthetic material
    # but permits an unambiguous fixed stop/tail analysis for the probe.
    stop=round(3.2*FS)
    demo=demo[:round(4.0*FS)].astype(np.float64)
    demo[stop:]=0
    demo[stop-96:stop]*=np.linspace(1,0,96)[:,None]
    yield 'original_bass_demo',demo,100,(.25,3.10),(3.27,3.65)

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--renderer',type=Path,required=True)
    ap.add_argument('--out',type=Path,required=True)
    ap.add_argument('--project',type=Path,default=Path(__file__).resolve().parents[1])
    ap.add_argument('--label',default='unspecified renderer')
    args=ap.parse_args(); args.out.mkdir(parents=True,exist_ok=True)
    exe=args.renderer.resolve(); calls=[]; rows=[]
    for name,x,snap,active,tail in sources(args.project):
        input_path=args.out/f'{name}_input.wav'
        wavfile.write(input_path,FS,np.asarray(x,dtype=np.float32))
        body=slice(round(active[0]*FS),round(active[1]*FS)); ending=slice(round(tail[0]*FS),round(tail[1]*FS))
        for config,extra in CONFIGS.items():
            outputs={}
            for color in (100,150,200):
                out=args.out/f'{name}_{config}_color{color}.wav'
                command=[str(exe),str(input_path),str(out),*BASE,'--snap',str(snap),*extra,'--color',str(color)]
                run=subprocess.run(command,check=True,capture_output=True,text=True,timeout=90)
                fs,y=wavfile.read(out)
                assert fs==FS and y.shape==x.shape and np.isfinite(y).all()
                outputs[color]=np.asarray(y,dtype=np.float64)
                calls.append({'source':name,'config':config,'color':color,'stdout':run.stdout.strip()})
            y100=outputs[100]; base_rms=rms(y100[body]); focus100=focus(y100[body],FS)
            for color in (100,150,200):
                y=outputs[color]; delta=y-y100; yrms=rms(y[body]); matched=y*(base_rms/max(yrms,1e-30))
                row={'source':name,'snap':snap,'config':config,'color':color,
                     'output_body_rms':yrms,'output_body_rms_change_db':db(yrms/base_rms),
                     'output_peak':float(abs(y).max()),'effect_delta_rms_over_color100_db':db(rms(delta[body])/base_rms),
                     'effect_delta_peak':float(abs(delta).max()),'gain_matched_residual_over_color100_db':db(rms((matched-y100)[body])/base_rms),
                     'allowed_note_energy_share':focus(y[body],FS),'allowed_note_share_change_percentage_points':100*(focus(y[body],FS)-focus100),
                     'effect_delta_tail_rms':rms(delta[ending]),'active_seconds':active,'tail_seconds':tail}
                rows.append(row)
            r=rows[-1]
            print(f'{name:20s} {config:25s} C200: delta={r["effect_delta_rms_over_color100_db"]:.2f}dB body gain={r["output_body_rms_change_db"]:.2f}dB peak={r["output_peak"]:.4f} focus+={r["allowed_note_share_change_percentage_points"]:.2f}pp',flush=True)
    note_count=sum(bool(MASK&(1<<(n%12))) and 180<=440*2**((n-69)/12)<=8000 for n in range(128))
    radius=float(np.exp(-np.log(1000)/(FS*.2)))
    report={'version':'color03 diagnostic; '+args.label,'renderer_sha256':hashlib.sha256(exe.read_bytes()).hexdigest(),
            'sample_rate':FS,'common_arguments':BASE,'configurations':CONFIGS,'render_calls':calls,'measurements':rows,
            'focus_definition':'Energy within +/-20 cents (minimum +/-2 Hz) of E-minor-pentatonic notes, measured inside 243–7040 Hz by independent Hann FFT.',
            'delta_definition':'Output(Color C) minus Output(Color100) at equal raw gain. With Morph on this is an effective difference, not the isolated pre-Morph resonator.',
            'baseline_theory':{'applies_to':'Immutable 0.2 baseline only; this is not the revised 0.3 calibration formula.',
                               'active_resonator_count':note_count,'pole_radius':radius,'input_coefficient_1_minus_r':1-radius,
                               'color200_layer_scalar':.8*2/np.sqrt(note_count),
                               'approx_white_noise_layer_rms_over_input':1.6*np.sqrt(1-radius)}}
    (args.out/'probe.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
    print('Wrote',args.out/'probe.json',flush=True)

if __name__=='__main__':main()
