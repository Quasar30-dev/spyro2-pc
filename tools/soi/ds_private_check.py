"""Finite, event-driven checks on a task-owned DuckStation copy/hidden desktop.

Never attaches until the listening socket's PID equals the process just created.
Uses only the retained process handle for shutdown. Does not use the old harness's
startup, screenshot, timeout-polling or sleep routines.
"""
import argparse
import ctypes as c
import ctypes.wintypes as w
import hashlib
import json
import shutil
import socket
import struct
import subprocess
import time
import uuid
from pathlib import Path

import ds_harness as d
import ds_pad_proof as route

ROOT=Path(__file__).resolve().parents[2]
TASK=Path('D:/Theia-staging/spyro2-mod/work/sol-retry-20261105')
SOURCE=Path('D:/Theia-tools/duckstation')
K=d.k32; U=d.u32
K.FindFirstChangeNotificationW.argtypes=[w.LPCWSTR,w.BOOL,w.DWORD]
K.FindFirstChangeNotificationW.restype=w.HANDLE
K.FindNextChangeNotification.argtypes=[w.HANDLE]
K.FindCloseChangeNotification.argtypes=[w.HANDLE]
K.WaitForMultipleObjects.argtypes=[w.DWORD,c.POINTER(w.HANDLE),w.BOOL,w.DWORD]
K.WaitForSingleObject.argtypes=[w.HANDLE,w.DWORD]
K.TerminateProcess.argtypes=[w.HANDLE,w.UINT]
K.CloseHandle.argtypes=[w.HANDLE]
U.CloseDesktop.argtypes=[w.HANDLE]


def sha(p):
    with Path(p).open('rb') as f: return hashlib.file_digest(f,'sha256').hexdigest()


class Changes:
    def __init__(self,path):
        self.h=K.FindFirstChangeNotificationW(str(path),True,0x1|0x8|0x10)
        if self.h in (None,c.c_void_p(-1).value): raise c.WinError()
    def arm(self):
        if not K.FindNextChangeNotification(self.h): raise c.WinError()
    def wait(self,process,deadline):
        ms=max(1,min(20000,int((deadline-time.monotonic())*1000)))
        handles=(w.HANDLE*2)(self.h,process)
        got=K.WaitForMultipleObjects(2,handles,False,ms)
        if got==1: raise RuntimeError('owned DuckStation exited')
        if got!=0: raise TimeoutError('filesystem event deadline')
        self.arm()
    def close(self): K.FindCloseChangeNotification(self.h)


def port_owner(port):
    api=c.windll.iphlpapi.GetExtendedTcpTable
    size=w.DWORD(0)
    api(None,c.byref(size),False,2,5,0) # AF_INET, TCP_TABLE_OWNER_PID_ALL
    buf=c.create_string_buffer(size.value)
    if api(buf,c.byref(size),False,2,5,0): raise c.WinError()
    n=struct.unpack_from('<I',buf,0)[0]
    matches=[]
    for i in range(n):
        state,addr,p,_,_,pid=struct.unpack_from('<6I',buf,4+24*i)
        if socket.ntohs(p&65535)==port and state==2: matches.append(pid)
    return matches


class Gdb(d.Gdb):
    def __init__(self,port):
        self.s=socket.create_connection(('127.0.0.1',port),timeout=10)
        self.s.setsockopt(socket.IPPROTO_TCP,socket.TCP_NODELAY,1)
        self.s.settimeout(20)
    def _recv(self):
        buf=b''
        while True: # event-driven socket reads, bounded by socket timeout
            b=self.s.recv(65536)
            if not b: raise ConnectionError('private GDB disconnected')
            buf+=b
            i=buf.find(b'$'); j=buf.find(b'#',i+1)
            if i>=0 and j>=0 and len(buf)>=j+3:
                body=buf[i+1:j]
                assert int(buf[j+1:j+3],16)==sum(body)&255,'GDB checksum'
                self.s.sendall(b'+'); return body.decode()
    def regs(self):
        # DuckStation marks unsupported FPU registers with 'x'; we need GPRs.
        b=bytes.fromhex(self._send('g')[:32*8])
        return struct.unpack('<'+'I'*(len(b)//4),b)
    def pc(self):
        return struct.unpack('<I',bytes.fromhex(self._send('g')[37*8:38*8]))[0]


class Session:
    def __init__(self,cue,log,name,*,start_paused=False):
        cue=Path(cue).resolve()
        # Original13918 transfers ceil(byte_count/2048) complete sectors;
        # original70498 allocates the following scene at the declared overlay
        # size. Retail's197 WAD sizes are all sector aligned. An unaligned
        # extension corrupts the adjacent first texture descriptors when the
        # late overlay transfer writes its final sector padding.
        with cue.with_suffix('.bin').open('rb') as f:
            f.seek(500*2352+24);head=f.read(2048)
        unaligned=[]
        for index in range(255):
            offset,size=struct.unpack_from('<II',head,index*8)
            if (offset,size)==(0,0):break
            if size%2048:unaligned.append((index,size))
        assert not unaligned,('Original CD transport requires2KB-aligned WAD payload sizes; append zero padding, preserve code addresses. See data/soi/rom-targets-20261105/original-overlay-sector-overrun.json',unaligned)
        self.log=log; self.gdb=None; self.h=None; self.desk=None
        self.home=TASK/f'duckstation-{name}'
        if self.home.exists(): raise RuntimeError('private profile already exists: '+str(self.home))
        self.home.mkdir(parents=True)
        for p in SOURCE.iterdir():
            if p.is_file() and (p.suffix in ('.exe','.dll') or p.name in ('qt.conf','portable.txt','LICENSE.txt')):
                if p.name not in ('uninstaller.exe','updater.exe'): shutil.copyfile(p,self.home/p.name)
        for name_ in ('QtPlugins','resources','translations','shaders'):
            shutil.copytree(SOURCE/name_,self.home/name_)
        # Every task disc here is US. Copy exactly the configured original
        # NTSC-U BIOS, rather than duplicating unrelated regional BIOS banks
        # into every private profile. The source remains read-only.
        import configparser
        source_cfg=configparser.ConfigParser();source_cfg.read(SOURCE/'settings.ini')
        bios_name=source_cfg['BIOS']['PathNTSCU']
        assert Path(bios_name).name==bios_name and (SOURCE/'bios'/bios_name).is_file()
        (self.home/'bios').mkdir();shutil.copyfile(SOURCE/'bios'/bios_name,self.home/'bios'/bios_name)
        assert sha(SOURCE/'bios'/bios_name)==sha(self.home/'bios'/bios_name)
        self.exe=self.home/d.EXE.name
        assert sha(self.exe)==sha(SOURCE/d.EXE.name)
        with socket.socket() as free:
            free.bind(('127.0.0.1',0)); self.port=free.getsockname()[1]
        settings=(SOURCE/'settings.ini').read_text()
        settings=settings.replace('GDBServerPort = 2345',f'GDBServerPort = {self.port}')
        if start_paused:
            assert 'StartPaused = false' in settings
            settings=settings.replace('StartPaused = false','StartPaused = true')
        settings=settings.replace('D:/Theia-tools/duckstation/bios',(self.home/'bios').as_posix())
        (self.home/'settings.ini').write_text(settings)
        for n in ('screenshots','savestates','memcards'): (self.home/n).mkdir(exist_ok=True)
        self.watch=Changes(self.home)
        d.EXE=self.exe; d.DESKTOP=f'SoiSolPrivate{uuid.uuid4().hex}'
        try:
            self.h,self.pid,self.desk=d.start(str(cue))
            log('OWNED',self.pid,'desktop',d.DESKTOP,'exe',self.exe,'sha256',sha(self.exe),'port',self.port)
            deadline=time.monotonic()+40
            while True:
                txt=(self.home/'duckstation.log').read_text(errors='replace') if (self.home/'duckstation.log').exists() else ''
                if f'listening on 127.0.0.1:{self.port}' in txt: break
                try:
                    self.watch.wait(self.h,deadline)
                except TimeoutError:
                    # DuckStation buffers its log. At the single startup-event
                    # deadline, inspect the OS listener once; never retry/poll.
                    if port_owner(self.port)==[self.pid]: break
                    raise
            owners=port_owner(self.port)
            assert owners==[self.pid],(owners,self.pid)
            log('GDB OWNERSHIP',owners,'matches owned PID; connecting')
            self.win=None
            for hwnd,title,cls,visible in d.window_titles():
                pid=w.DWORD(); U.GetWindowThreadProcessId(hwnd,c.byref(pid))
                if pid.value==self.pid and cls.startswith('Qt') and "Spyro" in title:
                    self.win=hwnd; break
            assert self.win,'no owned hidden display window'
            self.gdb=Gdb(self.port); self.running=False # attach pauses DuckStation
        except BaseException:
            if self.h:
                log('OWNED DESKTOP WINDOWS',d.window_titles())
                out=self.home/'startup-uia.txt'
                helper=Path(__file__).with_name('ds_uia_dump.ps1')
                ph=d.spawn_on_desktop(f'powershell.exe -NoProfile -NonInteractive -File "{helper}" -Out "{out}"',str(self.home))
                K.WaitForSingleObject(ph,15000); d.stop(ph); K.CloseHandle(ph)
                if out.exists(): log('OWNED DESKTOP UIA',out.read_text(errors='replace'))
            self.close(); raise
    def resume(self): self.gdb.s.sendall(b'$c#63'); self.running=True
    def pause(self):
        self.gdb.s.sendall(b'\x03'); self.gdb.s.settimeout(20)
        result=self.gdb._recv(); self.running=False; return result
    def screenshot(self,dest):
        folder=self.home/'screenshots'
        before={p:(p.stat().st_mtime_ns,p.stat().st_size) for p in folder.glob('*.png')}
        # Queue both events to the owned hidden window. No wall-clock key sleep.
        scan=U.MapVirtualKeyW(0x79,0)
        result=c.c_size_t()
        U.SendMessageTimeoutW(self.win,0x100,0x79,1|(scan<<16),2,3000,c.byref(result))
        U.SendMessageTimeoutW(self.win,0x101,0x79,1|(scan<<16)|0xc0000000,2,3000,c.byref(result))
        deadline=time.monotonic()+15
        while True:
            for p in folder.glob('*.png'):
                if before.get(p)!=(p.stat().st_mtime_ns,p.stat().st_size):
                    try: raw=p.read_bytes()
                    except PermissionError: continue
                    if raw.endswith(b'IEND\xaeB`\x82'):
                        dest.parent.mkdir(parents=True,exist_ok=True); dest.write_bytes(raw); return dest
            self.watch.wait(self.h,deadline)
    def close(self):
        if self.gdb: self.gdb.close()
        if self.h:
            d.stop(self.h); K.CloseHandle(self.h); self.log('STOPPED owned PID',self.pid)
        if self.desk: U.CloseDesktop(self.desk)
        if hasattr(self,'watch'): self.watch.close()
    def __enter__(self): return self
    def __exit__(self,*a):
        if a[0] and hasattr(self,'gdb'):
            try:
                self.gdb.s.settimeout(5)
                stop='already stopped at owned breakpoint'
                if self.running:
                    self.gdb.s.sendall(b'\x03')
                    stop=self.gdb._recv()
                regs=self.gdb.regs()
                self.log('FAILURE STOP',stop,'PC',hex(self.gdb.pc()),'registers',json.dumps(regs))
                self.log('FAILURE CONTROL REGISTERS GDB g[32:38]',self.gdb._send('g')[32*8:38*8])
                (self.home/'failure-ram.bin').write_bytes(self.gdb.read(0x80000000,0x200000))
                self.log('FAILURE RAM',self.home/'failure-ram.bin')
            except BaseException as e:
                self.log('FAILURE diagnostic unavailable',repr(e))
        self.close()


class Pad(d.Pad):
    def __init__(self,s,*,defer_start=False):
        super().__init__(s)
        if not defer_start:self.start()
    def start(self):
        self.g.s.sendall(b'$c#63'); self._wait_stop()
    def _wait_stop(self,limit=30.,probe=None):
        self.s.running=True
        self.g.s.settimeout(limit)
        for _ in range(8):  # finite original loader events between controller ticks
            stop=self.g._recv(); pc=self.g.pc()
            self.s.running=False
            assert pc!=0x8007ded0,'Moneybags handler reached'
            if hasattr(self,'trace_points') and pc in self.trace_points:
                self.s.log('FIRST FRAME TRACE',hex(pc),'GPR',json.dumps(self.g.regs()),
                           'CONTROL',self.g._send('g')[32*8:38*8])
                if pc==0x800265b0:
                    self.s.log('FIRST FRAME NEAR GPU',self.g.read(self.g.regs()[30],52).hex())
                assert self.g._send(f'z0,{pc:x},4')=='OK'
                self.trace_points.remove(pc)
                self.g.s.sendall(b'$c#63')
                self.s.running=True
                continue
            if pc!=0x8001d2d0: return stop
            if self.loader_observer():
                assert self.g._send('z0,8001d2d0,4')=='OK'
            self.g.s.sendall(b'$c#63')
            self.s.running=True
        raise AssertionError('unexpected repeated gameplay loader events')
    def shot(self,dest,free_run=None):
        self.release()
        p=self.s.screenshot(dest)
        self.rearm()
        return p


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('cue',type=Path); ap.add_argument('name')
    ap.add_argument('--ticks',type=int,default=600)
    ap.add_argument('--build-report',type=Path)
    ap.add_argument('--task-root',type=Path,default=TASK)
    ap.add_argument('--trace-first-frame',action='store_true')
    ap.add_argument('--entry',choices=('new-game','standalone','expanded'),default='new-game',
                    help='normal legacy new game, zero-input standalone, or Summer controller crossing into separate Mesa14')
    a=ap.parse_args()
    global_task = a.task_root.resolve()
    globals()['TASK'] = global_task
    log=d.Log(a.name)
    image=a.cue.with_suffix('.bin')
    digest=sha(image); log('ARTIFACT',image,'sha256',digest)
    build=json.loads(a.build_report.read_text()) if a.build_report else None
    ordinal=29 if a.entry=='expanded' else 1
    loader_capture={}
    # A menu-free bootstrap can finish loading before the first pad tick.
    # Pause the owned emulator at boot and arm the loader observer before
    # resuming; otherwise the observer incorrectly misses a successful load.
    cold_observer=a.entry=='standalone' and bool(build)
    with Session(a.cue,log,a.name,start_paused=cold_observer) as s:
        pad=Pad(s,defer_start=cold_observer)
        g=s.gdb
        if build:
            def observe_loader():
                if struct.unpack('<I',g.read(d.LEVEL,4))[0]!=ordinal: return False
                base=struct.unpack('<I',g.read(0x80066f14,4))[0]
                index=build['moneybags_inert']['index']; address=base+88*index
                record=g.read(address,88)
                assert struct.unpack_from('<H',record,54)[0]==0x220 and record[0x48]==0xfd
                count=build['objects']['count']; final=g.read(base+88*(count-1),88)
                assert final[0x4d]==1 and final[0x3a]==0xff,'final record not initialized by loader'
                assert g.read(base+88*count+0x48,1)==b'\xff','terminal sentinel misplaced'
                loader_capture.update(address=address,record=record.hex(),final=final.hex(),
                                      original_purchase=g.read(0x8006467e,2))
                log('MONEYBAGS LOADED RECORD',digest,'PC 8001d2d0 address',hex(address),'index',index,'bytes',record.hex())
                log('MONEYBAGS LOADER PASS',digest,'state 0xfd retained at original loader completion;',
                    'final index',count-1,'initialized bytes',final.hex(),'; sentinel after',count,'records')
                assert g._send('Z0,8007ded0,4')=='OK'
                assert g.write(0x8006467e,struct.pack('<H',1))=='OK'
                assert g.read(0x80067008,4)==bytes(4)
                log('PURCHASE FIXTURE ARMED',digest,'before first Mesa actor update; purchased=1 timer=0; RAM only')
                if a.trace_first_frame:
                    pad.trace_points={0x8007043c,0x800265b0,0x80028ba4,0x80041438,0x80000080}
                    for point in pad.trace_points:assert g._send(f'Z0,{point:x},4')=='OK'
                return True
            pad.loader_observer=observe_loader
            assert g._send('Z0,8001d2d0,4')=='OK'
        if cold_observer:pad.start()
        if a.entry=='standalone':
            from mesa_standalone_check import arrive
            arrive(pad,log,'standalone renderer gate')
        else:
            assert route.to_glimmer(pad,log,limit=1200),'failed new game route'
            if a.entry=='expanded':
                from mesa_portal_check import enter_from_glimmer
                enter_from_glimmer(pad,log,digest)
        pad.run(0,20)
        assert pad.state()[:2]==(0,ordinal),pad.state()
        log('ARRIVE PASS',digest,pad.state())
        g=s.gdb
        n=struct.unpack('<I',g.read(0x800673d8,4))[0]
        flag=struct.unpack('<I',g.read(0x800673e4,4))[0]
        distance=struct.unpack('<I',g.read(0x80067400,4))[0]
        log('LOADED visibility',flag,'distance',hex(distance),'regions',n)
        if 'MarketMesa' in a.cue.name:
            assert flag==1 and distance==0x2c000
            log('VISIBILITY RUNTIME PASS',digest,'normal retail loader distance 0x2c000')
        if a.build_report:
            assert loader_capture,'Mesa loader breakpoint not observed'
            base=struct.unpack('<I',g.read(0x80066f14,4))[0]
            records=g.read(base,88*build['objects']['count'])
            for e in build['gem_records']:
                rec=records[88*e['index']:88*(e['index']+1)]
                assert struct.unpack_from('<H',rec,54)[0]==1
                assert (rec[80],rec[73],rec[60],rec[84])==(e['value'],e['variant'],e['model'],e['colour']),e
                pointer=struct.unpack_from('<I',rec)[0]
                assert 0x80000000<=pointer<0x80200000
            log('GEM RUNTIME PASS',digest,len(build['gem_records']),'records; value/variant/model/colour and relocated parameters')
            if 'container_records' in build:
                converted={}
                if build.get('container_conversion'):
                    conversion=json.loads(Path(build['container_conversion']).read_text())
                    assert conversion['archive_sha256']==build['archive_sha256']
                    converted={x['index']:x for x in conversion['containers']}
                    assert len(converted)==5
                    assert all(records[88*x['old_index']+72]==0xfd for x in converted.values())
                for e in build['container_records']:
                    rec=records[88*e['index']:88*(e['index']+1)]
                    expected=0xc9 if e['index'] in converted else 0xc8
                    assert struct.unpack_from('<H',rec,54)[0]==expected and rec[80]==e['value']==1
                log('CONTAINER CONTENTS RUNTIME PASS',digest,len(build['container_records']),
                    'loaded basket/charge-only records contain source value 1; field check, not manual interaction acceptance')
            inert=build['moneybags_inert']; money=base+88*inert['index']
            rec=g.read(money,88)
            assert struct.unpack_from('<H',rec,54)[0]==0x220 and rec[0x48]==0xfd
            assert all(records[88*i+0x48]!=0xff for i in range(build['objects']['count']))
            log('MONEYBAGS POST-ROUTE RECORD',digest,'address',hex(money),'bytes',rec.hex())
            # RAM-only saved-purchase fixture: exercise the original early-frame
            # condition for 12 logic ticks, with a trap on the unsafe handler.
            saved_flag=loader_capture['original_purchase']; saved_timer=g.read(0x80067008,4)
            assert g.write(0x8006467e,struct.pack('<H',1))=='OK'
            for tick in range(12):
                assert g.write(0x80067008,bytes(4))=='OK'
                assert g.read(0x8006467e,2)==b'\x01\x00'
                pad.run(0,1)
                assert g.pc()==d.LOGIC
                assert g.read(money+0x48,1)==b'\xfd'
            log('MONEYBAGS PURCHASED FLAG PASS',digest,'12 finite ticks; flag 8006467e=1, timer 80067008=0 before each tick;',
                '8007ded0 never reached; record state 0xfd; RAM fixture only, not a full old-save playthrough')
            assert g.write(0x8006467e,saved_flag)=='OK'
            assert g.write(0x80067008,saved_timer)=='OK'
        descriptor_bytes=None
        if build:
            import s2scene_build
            from spyrodisc import wad_index
            source_archive=Path(build['archive']).read_bytes()
            assert hashlib.sha256(source_archive).hexdigest()==build['archive_sha256']
            children=wad_index(source_archive[:65536],len(source_archive))
            source_scene=s2scene_build.split(source_archive[children[1].offset:children[1].offset+children[1].size])
            descriptor_bytes=source_scene['parts'][0][8:]
            pointer=struct.unpack('<I',g.read(0x800673f4,4))[0]
            assert len(descriptor_bytes)==48*build['materials']
            assert g.read(pointer,len(descriptor_bytes))==descriptor_bytes,'original CD load corrupted texture descriptors'
            log('LOADED TEXTURE DESCRIPTORS PASS',digest,len(descriptor_bytes),'bytes exact at',hex(pointer),'; declared overlay sector padding did not overwrite the adjacent scene')
        frames=[]
        for turn in (0,24,24,24):
            if turn: pad.run(d.buttons('R1'),turn); pad.run(0,10)
            tag=f'view{len(frames)}'
            camera=struct.unpack('<iii',g.read(0x80067eac,12)); spyro=pad.state()[2]
            try:
                shot=pad.shot(d.OUT/'shots'/f'{a.name}-{tag}.png')
            except TimeoutError:
                log('CAPTURE unavailable; continuing finite renderer checks')
                if s.running: pad.rearm()
                shot=None
            frames.append(dict(tag=tag,path=str(shot),camera=camera,spyro=spyro))
        # Observe the real far renderer selecting records, without patching it.
        g._send(f'z0,{d.LOGIC:x},4')
        if build and 'uv_polish' in build:
            import mesa_uv_check
            import s2scene_build
            from spyrodisc import wad_index
            archive=Path(build['archive']).read_bytes()
            assert hashlib.sha256(archive).hexdigest()==build['archive_sha256']
            children=wad_index(archive[:65536],len(archive))
            scene=s2scene_build.split(archive[children[1].offset:children[1].offset+children[1].size])
            desc=scene['parts'][0][8:]
            allowed=set()
            for m in range(build['materials']):
                for q in range(4):
                    dsc=desc[m*48+16+q*8:m*48+24+q*8]
                    allowed.add((tuple(mesa_uv_check.consumer(dsc)),int.from_bytes(dsc[2:4],'little'),dsc[6]))
            assert g._send('Z0,800265b0,4')=='OK'
            packets=[]
            for _ in range(24):
                g.s.sendall(b'$c#63'); pad._wait_stop()
                assert g.pc()==0x800265b0
                fp=g.regs()[30]; primitive=g.read(fp,52)
                assert primitive[7] in (0x3c,0x3e)
                uv=tuple((primitive[p],primitive[p+1]) for p in (12,24,36,48))
                clut=int.from_bytes(primitive[14:16],'little'); page=int.from_bytes(primitive[26:28],'little')&255
                assert (uv,clut,page) in allowed,(uv,clut,page)
                packets.append(primitive.hex())
            log('NEAR GPU POLYGT4',json.dumps(packets))
            log('NEAR GPU SOURCE DESCRIPTOR PASS',digest,'24 original near packets match exact candidate descriptors; no claim of all-camera/clip coverage')
            g._send('z0,800265b0,4')
        assert g._send('Z0,80028ba4,4')=='OK'
        far=[]
        for _ in range(12):
            g.s.sendall(b'$c#63'); pad._wait_stop()
            regs=g.regs(); ptr=regs[15]
            head=g.read(ptr,28)
            assert head[16]>0 and head[16]+head[17]+2*head[18]==head[23]
            far.append(dict(pointer=hex(ptr),counts=list(head[16:24])))
        log('FAR RENDERER RECORDS',json.dumps(far))
        log('FAR RENDERER RUNTIME PASS',digest,'12 finite original-renderer stops; nonempty valid records')
        g._send('z0,80028ba4,4'); g._send('Z0,80028f9c,4')
        polys=[]
        for _ in range(6):
            g.s.sendall(b'$c#63'); pad._wait_stop()
            fp=g.regs()[30]
            primitive=g.read(fp,36)
            assert primitive[7]==0x38
            polys.append(primitive.hex())
        log('FAR GPU POLYG4',json.dumps(polys))
        log('FAR GPU EMISSION PASS',digest,'6 original POLY_G4 commands captured')
        g._send('z0,80028f9c,4'); g._send(f'Z0,{d.LOGIC:x},4')
        g.s.sendall(b'$c#63'); pad._wait_stop()
        t0=struct.unpack('<I',g.read(0x8006700c,4))[0]
        for chunk in range(0,a.ticks,300):
            pad.run(0,min(300,a.ticks-chunk))
            log('FINITE PROGRESS',min(chunk+300,a.ticks),'/',a.ticks,'state',pad.state(),
                'timer',struct.unpack('<I',g.read(0x8006700c,4))[0])
        t1=struct.unpack('<I',g.read(0x8006700c,4))[0]
        assert t1>t0
        log('FINITE TICKS PASS',digest,a.ticks,'tick timer',t0,t1)
        if descriptor_bytes:
            pointer=struct.unpack('<I',g.read(0x800673f4,4))[0]
            assert g.read(pointer,len(descriptor_bytes))==descriptor_bytes,'texture descriptors changed during bounded gameplay'
            log('FINAL TEXTURE DESCRIPTORS PASS',digest,len(descriptor_bytes),'bytes remain exact after cameras/GPU stops/finite ticks')
        if a.build_report:
            rec=g.read(money,88)
            assert rec[0x48]==0xfd and struct.unpack_from('<H',rec,54)[0]==0x220
            log('MONEYBAGS FINAL RECORD PASS',digest,'address',hex(money),'bytes',rec.hex())
        (d.OUT/f'{a.name}-views.json').write_text(json.dumps(frames,indent=1))
        (s.home/'ram.bin').write_bytes(g.read(0x80000000,0x200000))
        log('RAM',s.home/'ram.bin')
    assert sha(image)==digest
    log('PRIVATE CHECK PASS',digest,'automated only; manual gameplay/visual acceptance remains with user')


if __name__=='__main__': main()
