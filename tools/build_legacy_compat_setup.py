#!/usr/bin/env python3
"""Build a no-CRT Windows x64 compatibility EXE around validated Iris 0.7 NSIS.

This builder requires clang + lld-link, not Wine, MinGW, NSIS or the VST SDK.
The wrapper only relocates an existing legacy file/reparse point at the exact
VST3 installation path and then invokes the original, CRC-verified NSIS setup.
"""
from pathlib import Path
import argparse, hashlib, json, shutil, struct, subprocess, sys, tempfile, zlib
import runpy

ROOT = Path(__file__).resolve().parents[1]
COMPAT = ROOT / 'packaging/windows/compat'
CHECK_INSTALLER = runpy.run_path(str(ROOT/'tools/build_windows_installer.py'))['validate_nsis_installer']
MAGIC = b'IRIS07F1'
FOOTER = '<8sII'


def validate_wrapper(path, old_payload):
    data = path.read_bytes()
    marker, length, crc = struct.unpack(FOOTER, data[-16:])
    if marker != MAGIC or length != len(old_payload):
        raise ValueError('bad wrapper footer length/signature')
    body = data[-16-length:-16]
    if body != old_payload or zlib.crc32(body) != crc:
        raise ValueError('wrapped NSIS payload mismatch')
    if data[:2] != b'MZ' or data[0x3c:0x40] == b'\0\0\0\0':
        raise ValueError('wrapper is not a PE image')
    offset=struct.unpack_from('<I',data,0x3c)[0]
    if data[offset:offset+4]!=b'PE\0\0' or struct.unpack_from('<H',data,offset+4)[0]!=0x8664:
        raise ValueError('wrapper is not Windows x64')
    CHECK_INSTALLER(path.parent/'wrapped-original-setup.nsis.exe') if False else None
    return hashlib.sha256(data).hexdigest()


def run(*cmd):
    completed = subprocess.run(list(map(str,cmd)), capture_output=True, text=True)
    if completed.returncode:
        raise RuntimeError(f'Command failed: {cmd}\n{completed.stdout}\n{completed.stderr}')


def build(source_setup, output):
    if not source_setup.is_file():
        raise ValueError(f'Original v0.7 installer missing: {source_setup}')
    crc=CHECK_INSTALLER(source_setup)
    old=source_setup.read_bytes()
    with tempfile.TemporaryDirectory(prefix='iris-compat-pe-') as tmp:
        tmp=Path(tmp)
        for dll in ('kernel32','shell32','user32'):
            run('lld-link','/dll','/noentry','/machine:x64',f'/def:{COMPAT / (dll + ".def")}',
                f'/out:{tmp/(dll+"-stub.dll")}',f'/implib:{tmp/(dll+".lib")}')
        obj=tmp/'compat.obj'
        run('clang','-target','x86_64-pc-windows-msvc','-fshort-wchar','-mno-stack-arg-probe',
            '-O2','-ffreestanding','-fno-stack-protector','-fno-builtin','-c',
            COMPAT/'compat_bootstrap.c','-o',obj)
        stub=tmp/'compat-stub.exe'
        run('lld-link','/entry:mainCRTStartup','/subsystem:windows','/nodefaultlib','/machine:x64',
            '/opt:ref','/opt:icf',f'/out:{stub}',obj,
            tmp/'kernel32.lib',tmp/'shell32.lib',tmp/'user32.lib')
        output.parent.mkdir(parents=True,exist_ok=True)
        temp=output.with_name(output.name+'.tmp')
        try:
            with temp.open('wb') as f:
                f.write(stub.read_bytes()); f.write(old)
                f.write(struct.pack(FOOTER,MAGIC,len(old),zlib.crc32(old)))
            sha=validate_wrapper(temp,old)
            temp.replace(output)
        finally:temp.unlink(missing_ok=True)
    manifest={
        'product':'Iris Color', 'wrapper_version':'0.7.0-fix1',
        'original_installer_sha256':hashlib.sha256(old).hexdigest(),
        'original_installer_nsis_crc32':crc,
        'wrapper_sha256':sha,'bytes':output.stat().st_size,
        'purpose':'Move existing single-file VST3/reparse leaf to numbered safe backup before calling original NSIS',
        'limitations':'Not yet executed on the user\'s Windows PC; cannot validate FL Studio scan or UAC here.',
        'source':str(COMPAT/'compat_bootstrap.c')
    }
    output.with_suffix('.json').write_text(json.dumps(manifest,indent=2)+'\n')
    output.with_suffix('.sha256').write_text(f'{sha}  {output.name}\n')
    return manifest

if __name__=='__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('--original',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    try:
        print(json.dumps(build(args.original,args.output),indent=2))
    except Exception as e:
        print(f'Error: {e}',file=sys.stderr);raise SystemExit(1)
