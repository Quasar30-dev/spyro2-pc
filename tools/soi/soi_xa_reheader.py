"""Correct standalone draft XA headers without repeating PCM quantization."""
import argparse
import hashlib
import json
from pathlib import Path
import numpy as np
import ps1disc


def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('report',type=Path)
    ap.add_argument('--out',type=Path,required=True);a=ap.parse_args();a.out.mkdir(parents=True,exist_ok=True)
    r=json.loads(a.report.read_text());source=Path(r['xa']);raw=source.read_bytes()
    assert hashlib.sha256(raw).hexdigest()==r['xa_sha256']
    data=np.frombuffer(raw,np.uint8).reshape(-1,2352).copy();n=len(data)
    payload_hash=hashlib.sha256(data[:,24:2328].tobytes()).hexdigest()
    for i in range(n):data[i,16:24]=[1,1,0x64|(0x81 if i==n-1 else 0),1]*2
    ps1disc.encode(data,0,np.ones(n,dtype=bool),np.zeros(n,dtype=bool))
    assert hashlib.sha256(data[:,24:2328].tobytes()).hexdigest()==payload_hash
    path=a.out/'market-mesa-draft.xa';path.write_bytes(data.tobytes())
    r.update(payload_encoder_sha256=r['encoder_sha256'],
             payload_source=str(source),payload_source_sha256=r['xa_sha256'],payload_sha256=payload_hash,
             header_probe_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
             xa=str(path.resolve()),xa_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
             submode='0x64 except final0xe5; no per-sector EOR')
    report=a.out/'xa-draft-report.json';report.write_text(json.dumps(r,indent=2))
    print('SOURCE',source,'sha256',r['payload_source_sha256'])
    print('OUTPUT',path.resolve(),'sha256',r['xa_sha256'],'payload unchanged',payload_hash)
    print('LIMIT independent decode, original-controller EOF/loop and game integration remain unfinished')


if __name__=='__main__':main()
