"""Finite automatic PC captures of the same BIN/CUE using a task-private exe."""
import argparse
import hashlib
import json
import shutil
import ctypes as c
import ctypes.wintypes as w
import subprocess
import uuid
from pathlib import Path
import numpy as np
from PIL import Image,ImageDraw
import mesa_compare as compare
import soi_pc_proof as pc
import ds_harness as desktop


def hidden_run(cmd,timeout,**unused):
    """Run only our copied executable on a new, never-shown Win32 desktop."""
    desktop.k32.WaitForSingleObject.argtypes=[w.HANDLE,w.DWORD]
    desktop.k32.TerminateProcess.argtypes=[w.HANDLE,w.UINT]
    desktop.k32.CloseHandle.argtypes=[w.HANDLE]
    desktop.k32.GetExitCodeProcess.argtypes=[w.HANDLE,c.POINTER(w.DWORD)]
    desktop.u32.CloseDesktop.argtypes=[w.HANDLE]
    desktop.DESKTOP='SoiPcPrivate'+uuid.uuid4().hex
    desktop.u32.CreateDesktopW.restype=w.HANDLE
    desk=desktop.u32.CreateDesktopW(desktop.DESKTOP,None,None,0,0x10000000,None)
    if not desk: raise c.WinError()
    handle=None
    try:
        handle=desktop.spawn_on_desktop(subprocess.list2cmdline(cmd),str(pc.SCRATCH))
        print('PRIVATE DESKTOP',desktop.DESKTOP,'exe',cmd[0],flush=True)
        assert desktop.k32.WaitForSingleObject(handle,int(timeout*1000))==0,'private PC job deadline'
        code=w.DWORD(); assert desktop.k32.GetExitCodeProcess(handle,c.byref(code))
        return subprocess.CompletedProcess(cmd,code.value,'','')
    finally:
        if handle:
            desktop.stop(handle); desktop.k32.CloseHandle(handle)
        desktop.u32.CloseDesktop(desk)


def sha(p):
    with Path(p).open('rb') as f: return hashlib.file_digest(f,'sha256').hexdigest()


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('cue',type=Path)
    ap.add_argument('--name',default='sol-test05-pc')
    ap.add_argument('--task-root',type=Path,default=Path('D:/Theia-staging/spyro2-mod/work/sol-retry-20261105'))
    ap.add_argument('--views',type=Path)
    ap.add_argument('--build-report',type=Path)
    ap.add_argument('--retail-reference',action='store_true')
    ap.add_argument('--exe',type=Path,help='explicit successful main candidate to copy for private automated captures')
    ap.add_argument('--slot',type=int,default=17)
    a=ap.parse_args()
    a.cue=a.cue.resolve()
    task=a.task_root.resolve()
    work=task/a.name
    if work.exists(): raise SystemExit('refusing to reuse PC artifact directory')
    work.mkdir(parents=True)
    source=(a.exe or pc.PROJECT/'build-soi/spyro_workbench.exe').resolve()
    executable=work/source.name; shutil.copyfile(source,executable)
    assert sha(executable)==sha(source)
    digest=sha(a.cue.with_suffix('.bin'))
    print('ARTIFACT',a.cue,'BIN sha256',digest,flush=True)
    print('PRIVATE EXE',executable,'sha256',sha(executable),'equals source',source,flush=True)
    pc.EXE=executable; pc.SCRATCH=work
    views=json.loads((a.views or pc.PROJECT/'data/soi/ps1/ds/test02-final-views.json').read_text())
    if not a.views: views=views[:4]
    eng=lambda p:tuple(np.array(p)[[0,2,1]]/1024.)
    cams=[(eng(v['camera']),tuple(np.array(eng(v['spyro']))+[0,.35,0])) for v in views]
    images,report=compare.camera_run_disc(a.name,eng(views[0]['spyro']),cams,a.cue,slot=a.slot,size=(960,720),run=hidden_run)
    print('REPORT',json.dumps(report),flush=True)
    assert len(images)==len(views) and report.get('frames',0)>0
    assert report.get('wad_slot')==a.slot and report.get('retail_script_faults')==0
    if a.retail_reference:
        if a.slot==17:assert report.get('loose_gems')==91 and report.get('retail_objects')==201
        else:assert report.get('retail_objects',0)>0 and report.get('triangles',0)>0
    else:
        expected=json.loads(a.build_report.read_text()) if a.build_report else None
        assert report.get('loose_gems')==110 and report.get('retail_objects')==(expected['objects']['count'] if expected else 220)
        assert report.get('triangles')==10320
    sheet=Image.new('RGB',(960,720*len(images)))
    for i,im in enumerate(images): sheet.paste(im,(0,720*i))
    out=pc.PROJECT/'data/soi/ps1/ds/shots'/f'{a.name}.png'; sheet.save(out)
    assert sha(a.cue.with_suffix('.bin'))==digest
    print('PC DISC LOAD/CAPTURE PASS',digest,len(images),'automated cameras; no manual application test;',out,flush=True)


if __name__=='__main__': main()
