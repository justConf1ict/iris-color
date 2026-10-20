#!/usr/bin/env python3
"""Build the generic Windows VST3 comparison host from the pinned SDK sources."""
from concurrent.futures import ThreadPoolExecutor
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

ROOT=Path(__file__).resolve().parents[1]
SOURCES=json.loads((ROOT/'third_party/SOURCES.json').read_text())
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--compiler')
parser.add_argument('--jobs',type=int,default=min(4,os.cpu_count() or 1))
args=parser.parse_args()
compiler=args.compiler or str(ROOT/'tools/.toolchains'/SOURCES['llvm_mingw_cross_compiler']['directory']/'bin/x86_64-w64-mingw32-clang++')
if not shutil.which(compiler):raise SystemExit('Compiler not found; run tools/native_build_bootstrap.py --cross-windows or pass --compiler.')
sdk=ROOT/'third_party/vst3sdk'
if not (sdk/'public.sdk/source/vst/hosting/hostclasses.cpp').is_file():raise SystemExit('Pinned SDK missing; run tools/native_build_bootstrap.py.')
build=ROOT/'build/reference-host/windows';objects=build/'objects';objects.mkdir(parents=True,exist_ok=True)
sources=[sdk/line for line in (ROOT/'tools/reference_host_sdk_sources.txt').read_text().splitlines() if line]
sources.append(ROOT/'tools/reference_vst3_render.cpp')
flags=['-std=c++17','-O2','-DNDEBUG','-DRELEASE=1','-I'+str(sdk),'-fvisibility=hidden','-DUNICODE','-D_UNICODE','-DNOMINMAX','-D_WIN32_WINNT=0x0A00','-Wno-pragma-pack']
commands=[]
def run(command):
    result=subprocess.run(list(map(str,command)),text=True,capture_output=True)
    if result.returncode:raise RuntimeError(' '.join(map(str,command))+'\n'+result.stdout+result.stderr)
    return result.stdout+result.stderr
def compile_source(source):
    obj=objects/(str(source.relative_to(ROOT)).replace('/','__')+'.o')
    command=[compiler,*flags,'-c',source,'-o',obj]
    obj.with_suffix('.log').write_text(run(command))
    return obj,list(map(str,command))
with ThreadPoolExecutor(max_workers=max(1,args.jobs)) as pool:compiled=list(pool.map(compile_source,sources))
commands.extend(command for _,command in compiled)
binary=build/'reference-vst3-render.exe'
link=[compiler,'-o',binary,*[obj for obj,_ in compiled],'-static','-lole32','-luuid','-luser32','-lgdi32','-lgdiplus','-lshell32','-lwinmm','-Wl,--no-insert-timestamp']
(build/'link.log').write_text(run(link));commands.append(list(map(str,link)))
def digest(path):return hashlib.file_digest(path.open('rb'),'sha256').hexdigest()
receipt={'binary':str(binary.relative_to(ROOT)),'sha256':digest(binary),'compiler':run([compiler,'--version']).splitlines()[0],
         'vst3sdk_commit':SOURCES['vst3sdk']['commit'],'source_sha256':digest(ROOT/'tools/reference_vst3_render.cpp'),
         'sdk_dependency_list_sha256':digest(ROOT/'tools/reference_host_sdk_sources.txt'),'commands':commands}
(build/'build-receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
print('Built: '+str(binary))
