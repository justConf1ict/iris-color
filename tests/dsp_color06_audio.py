#!/usr/bin/env python3
"""Clarity behavior, gain balance, protected bands, and exact old endpoints.

The probes are independently generated synthetic audio. A frequency balance
change is measured after RMS matching; it is not a perceptual preference score.
Removing Clarity or replacing it with a uniform volume increase fails this test.
"""
import argparse
import json
from pathlib import Path
import subprocess
import numpy as np
from scipy import signal

FS=48000
LATENCY=3072

def rms(x):return float(np.sqrt(np.mean(np.asarray(x,dtype=float)**2)))
def db(x):return float(20*np.log10(max(abs(x),1e-30)))

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--driver',required=True,type=Path)
    ap.add_argument('--baseline-driver',type=Path)
    ap.add_argument('--work',required=True,type=Path)
    args=ap.parse_args();args.work.mkdir(parents=True,exist_ok=True)
    checks=[]
    def check(name,passed,**measurements):
        checks.append(dict(name=name,passed=bool(passed),measurements=measurements))
        print(('PASS ' if passed else 'FAIL ')+name+' '+json.dumps(measurements),flush=True)
    def render(name,x,clarity=0,mode=1,color=150,bypass=0,block=137,driver=None):
        src=args.work/(name+'.input.f32');dest=args.work/(name+'.output.f32')
        np.asarray(x,dtype='<f4').tofile(src)
        subprocess.run([str((driver or args.driver).resolve()),str(src),str(dest),str(FS),str(clarity),str(block),str(mode),str(color),str(bypass)],check=True,timeout=120)
        result=np.fromfile(dest,dtype='<f4').reshape(-1,2)
        assert result.shape==(len(x)+LATENCY,2) and np.isfinite(result).all()
        return result[LATENCY:],result
    def bands(x):
        f,p=signal.welch(x[int(.5*FS):],FS,nperseg=8192,axis=0)
        p=p.mean(axis=1)
        return [float(np.sum(p[(f>=a)&(f<b)])) for a,b in [(300,800),(1500,3500),(30,150),(9000,18000)]]
    def presence(x):
        b=bands(x);return 10*np.log10(b[1]/b[0])
    rng=np.random.default_rng(619137)
    n=FS*4;f=np.fft.rfftfreq(n,1/FS)
    z=np.fft.rfft(rng.normal(size=(n,2)),axis=0)
    z/=np.sqrt(np.maximum(f,35))[:,None]
    x=np.fft.irfft(z,n,axis=0);x=(x*.08/rms(x)).astype(np.float32)
    old,old_full=render('pink-0',x)
    mild,_=render('pink-35',x,35)
    strong,_=render('pink-70',x,70)
    mild_match=mild*(rms(old)/rms(mild));strong_match=strong*(rms(old)/rms(strong))
    d35=float(presence(mild_match)-presence(old));d70=float(presence(strong_match)-presence(old))
    check('clarity_changes_balance_at_equal_RMS',d35>.6 and d70>d35+.5,presence_change_db_35=d35,presence_change_db_70=d70)
    level35=db(rms(mild)/rms(old));level70=db(rms(strong)/rms(old))
    check('broadband_output_energy_stays_balanced',abs(level35)<.3 and abs(level70)<.3,rms_change_db_35=level35,rms_change_db_70=level70)
    if args.baseline_driver:
        baseline,_=render('original-v05',x,driver=args.baseline_driver)
        check('clarity_zero_is_bit_exact_v05',np.array_equal(old,baseline),max_abs_error=float(np.max(np.abs(old-baseline))))
    t=np.arange(FS*2)/FS
    outer=np.column_stack([.15*np.sin(2*np.pi*80*t),.15*np.sin(2*np.pi*10000*t)]).astype(np.float32)
    outer0,_=render('outer-0',outer);outer100,_=render('outer-100',outer,100)
    levels=[db(rms(outer100[FS:,c])/rms(outer0[FS:,c])) for c in range(2)]
    check('protected_sub_and_air',all(abs(v)<.1 for v in levels),gain_change_db_80Hz_10kHz=levels)
    for name,mode,color,bypass in [('color-zero',1,0,0),('bypass',1,150,1)]:
        y,full=render(name,x[:FS//2],100,mode,color,bypass)
        check(name+'_exact_dry_and_latency',np.array_equal(y,x[:FS//2]) and np.all(full[:LATENCY]==0))
    legacy0,_=render('legacy-0',x[:FS],0,0);legacy100,_=render('legacy-100',x[:FS],100,0)
    check('legacy_is_unchanged',np.array_equal(legacy0,legacy100))
    impulse=np.zeros((FS,2),np.float32);impulse[FS//4]=(.3,-.2)
    i0,_=render('impulse-0',impulse);i100,_=render('impulse-100',impulse,100)
    at0=int(np.argmax(np.abs(i0[:,0])));at100=int(np.argmax(np.abs(i100[:,0])))
    check('no_added_transient_delay',at0==at100,peak_sample_0=at0,peak_sample_100=at100)
    one,_=render('block-1',x[:FS//3],70,block=1);many,_=render('block-512',x[:FS//3],70,block=512)
    check('block_partition_invariance',np.array_equal(one,many))
    report={'sample_rate':FS,'latency_samples':LATENCY,'checks':checks,'passed':all(v['passed'] for v in checks),'scope':'Synthetic behavior and level balance only; no listening preference or proprietary algorithm equivalence claim.'}
    (args.work/'results.json').write_text(json.dumps(report,indent=2)+'\n')
    return 0 if report['passed'] else 1

if __name__=='__main__':raise SystemExit(main())
