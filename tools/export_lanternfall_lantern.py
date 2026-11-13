from pathlib import Path
import json,numpy as np,trimesh
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'data/custom-levels/lanternfall/props'
scene=trimesh.Scene();report=[]
for p in json.loads((OUT/'hanging-lantern-mesh.json').read_text()):
    # Blender Z-up to glTF Y-up, retaining a right-handed basis.
    vertices=np.asarray(p['vertices'])[:,[0,2,1]]*np.array([1,1,-1])
    mesh=trimesh.Trimesh(vertices=vertices,faces=p['faces'],process=True)
    mesh.fix_normals()
    assert mesh.is_watertight and abs(mesh.volume)>1e-10,p['name']
    material=trimesh.visual.material.PBRMaterial(name=p['name'],baseColorFactor=np.asarray(p['color']),
        metallicFactor=p['metallic'],roughnessFactor=p['roughness'],emissiveFactor=np.asarray(p['color'][:3])*min(p['emission'],1))
    mesh.visual=trimesh.visual.texture.TextureVisuals(material=material)
    scene.add_geometry(mesh,node_name=p['name'],geom_name=p['name'])
    report.append({'name':p['name'],'vertices':len(mesh.vertices),'triangles':len(mesh.faces),'watertight':bool(mesh.is_watertight),'volume':float(mesh.volume)})
scene.export(OUT/'hanging-lantern.glb')
import struct
payload=(OUT/'hanging-lantern.glb').read_bytes();length=struct.unpack_from('<I',payload,12)[0]
gltf=json.loads(payload[20:20+length])
for source,exported in zip(json.loads((OUT/'hanging-lantern-mesh.json').read_text()),gltf['materials']):
    assert np.max(np.abs(np.asarray(source['color'])-exported['pbrMetallicRoughness']['baseColorFactor']))<.005
(ROOT/'data/validation/lanternfall/lantern-geometry.json').write_text(json.dumps({'passed':True,'parts':report,'billboards':0,'triangles':sum(p['triangles'] for p in report)},indent=2))
print(f'{len(report)} closed volumetric parts exported; no billboard surfaces')
