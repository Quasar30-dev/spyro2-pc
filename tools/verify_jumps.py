"""Check flat-air vertical equations against actual US retail MIPS instructions.

Horizontal steering/vector helper calls are skipped deliberately: this verifies
80033300's vertical timing/velocity rules, not the full collision controller.
"""
import argparse,json,subprocess,zlib
from pathlib import Path
from capstone import Cs,CS_ARCH_MIPS,CS_MODE_MIPS32,CS_MODE_LITTLE_ENDIAN
from verify_secondary import Mips,signed

def verify(exe,probe,output):
    data=exe.read_bytes()
    if zlib.crc32(data)!=0xcdc0917c:raise ValueError('Unmapped executable')
    cpu=Mips();cpu.memory[0x10000:0x10000+len(data)-2048]=data[2048:]
    cs=Cs(CS_ARCH_MIPS,CS_MODE_MIPS32+CS_MODE_LITTLE_ENDIAN)
    instructions=list(cs.disasm(data[0x800+0x23300:0x800+0x251d8],0x80033300))
    hooks={int(i.op_str,16) for i in instructions if i.mnemonic=='jal'}
    cases=[];expected=[]
    for charge in (False,True):
        for ticks in (0,1,5,11,12,13,14,30,60):
            for held in (False,True):
                for released in (False,True):
                    for velocity in (-9000,-512,0,23,3520,4096,8192):
                        cpu.write(0x8006a040,21 if charge else 5)
                        cpu.write(0x8006a048,4 if charge else 2)
                        cpu.write(0x8006a044,int(released))
                        cpu.write(0x8006a13c,0);cpu.write(0x8006a08c,velocity)
                        cpu.write(0x800683b0,(0x40 if held else 0)|0x80)
                        cpu.write(0x8006a04c,ticks)
                        cpu.call(0x80033300,hooks,{4:0})
                        cases.append((velocity,ticks,int(charge),int(held),int(released)))
                        expected.append(signed(cpu.read(0x8006a08c)))
    process=subprocess.run([str(probe),'--oracle'],input='\n'.join(' '.join(map(str,c)) for c in cases)+'\n',text=True,capture_output=True,check=True)
    actual=list(map(int,process.stdout.split()))
    if len(actual)!=len(expected):raise ValueError('Probe result count mismatch')
    mismatches=[{'input':c,'retail':e,'native':a} for c,e,a in zip(cases,expected,actual) if a!=e]
    result={'cases':len(cases),'instructions':cpu.instructions,'mismatches':mismatches,'scope':__doc__}
    output.parent.mkdir(parents=True,exist_ok=True);output.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:v for k,v in result.items() if k!='scope'}))
    if mismatches:raise SystemExit(1)

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--exe',type=Path,default=Path('data/disc/SCUS_944.25'))
    p.add_argument('--probe',type=Path,default=Path('build/misc-candidate/Release/spyro_jump_tests.exe'))
    p.add_argument('--output',type=Path,default=Path('data/validation/misc-20260920/jump-oracle.json'))
    a=p.parse_args();verify(a.exe,a.probe,a.output)
