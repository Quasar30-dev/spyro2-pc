"""Final colour pass, executed through Blender MCP before export."""
import bpy,math
from mathutils import Vector
scene=bpy.context.scene
assert scene.get('level_id')=='hollowbloom-springs'
if not scene.get('hollowbloom_colour_pass'):
    for o in scene.objects:
        if o.type!='MESH' or not o.name.startswith(('Pale plum blossom','Old willow bloom')):continue
        for v in o.data.color_attributes['Color'].data:
            r,g,b,a=v.color;v.color=(r*.93,g*.82,b*.92,a)
    lamps=[sum((v.co for v in o.data.vertices),Vector())/len(o.data.vertices) for o in scene.objects if o.type=='MESH' and o.name.startswith('Honey light')]
    for o in scene.objects:
        if o.type!='MESH' or not o.name.startswith(('Terrain ','Causeway')):continue
        attr=o.data.color_attributes.get('Color')
        for loop in o.data.loops:
            p=o.data.vertices[loop.vertex_index].co
            strength=max((max(0,1-math.hypot(p.x-l.x,p.y-l.y)/3.5) for l in lamps),default=0)*.3
            c=attr.data[loop.index].color
            attr.data[loop.index].color=(c[0]*(1-strength)+.95*strength,c[1]*(1-strength)+.8*strength,c[2]*(1-strength)+.48*strength,1)
    scene['hollowbloom_colour_pass']=1
result={'scene':scene.name,'colour_pass':scene['hollowbloom_colour_pass']}
