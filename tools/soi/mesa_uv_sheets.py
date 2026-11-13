"""Arrange existing automatic captures; no running-app visual acceptance."""
import argparse
import hashlib
import json
from pathlib import Path

from PIL import Image,ImageDraw


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--task',type=Path,required=True)
    ap.add_argument('--after',default='soi-uv06-reserved-pc')
    a=ap.parse_args(); root=Path(__file__).resolve().parents[2]
    out=root/'data/soi/ps1/uv06-review';out.mkdir(parents=True,exist_ok=True)
    views=json.loads((a.task/'uv/views.json').read_text())
    def capture(name,i):
        return a.task/name/f'seq_{name}'/f'frame_{6+i*8:05d}.bmp'
    def make(name,rows,labels):
        sheet=Image.new('RGB',(1280,500*len(rows)),(24,24,30));dr=ImageDraw.Draw(sheet);inputs=[]
        for j,(left,right,tag) in enumerate(rows):
            for col,path in enumerate((left,right)):
                image=Image.open(path).convert('RGB');sheet.paste(image.resize((640,480)),(col*640,j*500+20))
                dr.text((col*640+5,j*500+4),labels[col]+' | '+tag,fill='white')
                inputs.append({'path':str(path.resolve()),'sha256':hashlib.sha256(path.read_bytes()).hexdigest()})
        path=out/name;sheet.save(path)
        return {'path':str(path.resolve()),'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'captures':inputs}
    before='soi-uv06-before'; after=a.after;retail='soi-uv06-retail'
    result={}
    result['before_after']=make('uv06-before-after.png',[(capture(before,i),capture(after,i),views[i]['tag']) for i in (0,4,5,6,7)],
                                ('test05 delivered','UV EXPERIMENT - unreleased; acceptance pending'))
    result['retail_comparison']=make('uv06-vs-retail.png',[(capture(after,i),capture(retail,j),tag) for i,j,tag in [(0,0,'ground'),(6,1,'oblique'),(4,2,'overhead'),(7,3,'reverse')]],
                                      ('Market Mesa UV experiment','Original Glimmer, own spawn'))
    result['after_capture_name']=after
    result['limits']='Automated native PC captures; different level layouts in retail comparison. No user visual acceptance. Experiment remains unreleased.'
    (out/'sheets.json').write_text(json.dumps(result,indent=2))
    print('SHEETS',json.dumps({k:v['path'] for k,v in result.items() if isinstance(v,dict)}))
    print('LIMIT',result['limits'])


if __name__=='__main__':main()
