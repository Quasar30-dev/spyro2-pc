"""Close exposed retail canopy seams when a joined forest tree is isolated.

Summer Forest shares canopy boundaries across terrain regions. Keep the source
tree vertices/UVs intact and close only unmatched foliage edges, never the trunk.
The runtime performs the same construction from the user's mounted data.
"""
from collections import defaultdict
from mathutils import Vector
import math
def close_tree_canopy(rows):
    edges=defaultdict(list)
    key=lambda v:tuple(round(x*64) for x in v[:3])
    for i in range(0,len(rows),3):
        tri=rows[i:i+3]
        for a,b in zip(tri,tri[1:]+tri[:1]):edges[tuple(sorted((key(a),key(b))))].append((a,b))
    boundary=[entries[0] for entries in edges.values() if len(entries)==1 and entries[0][0][8]==49]
    remaining=set(range(len(boundary)));caps=[]
    leaf=[Vector(v[:3]) for v in rows if v[8]==49]
    if not leaf:return rows
    lo=Vector([min(p[j] for p in leaf) for j in range(3)]);hi=Vector([max(p[j] for p in leaf) for j in range(3)])
    centre=(lo+hi)/2;radii=(hi-lo)/2
    def unit(p):return Vector([p[j]/max(.01,radii[j]) for j in range(3)])
    def surface(p):
        d=p-centre;n=unit(d).length
        return centre+d/n if n>.001 else p
    def emit(a,b,c,region):
        for (pos,col),uv in zip((a,b,c),((.02,.02),(.98,.02),(.5,.98))):
            caps.append([*pos,*col,(64+uv[0]*64)/1024,(192+uv[1]*64)/384,49,region])
    while remaining:
        pending=[min(remaining)];component=[];remaining.remove(pending[0])
        while pending:
            i=pending.pop();component.append(i);keys={key(v) for v in boundary[i]}
            neighbours=[j for j in remaining if any(key(v) in keys for v in boundary[j])]
            for j in neighbours:remaining.remove(j);pending.append(j)
        unique={key(v):v for i in component for v in boundary[i]};hub=sum((Vector(v[:3]) for v in unique.values()),Vector())/len(unique)
        # A single centre fan caves inward on the large shared canopy joins.
        # Follow the foliage envelope with two rings, preserving every source edge.
        normal=sum(((Vector(boundary[i][0][:3])-centre).cross(Vector(boundary[i][1][:3])-centre) for i in component),Vector()).normalized()
        if normal.dot(hub-centre)<0:normal=-normal
        d=unit(hub-centre);n=unit(normal);aa=n.dot(n);bb=2*d.dot(n)
        if aa>.00001:hub+=normal*max(0,(-bb+math.sqrt(max(0,bb*bb-4*aa*(d.dot(d)-1))))/(2*aa))
        for i in component:
            a,b=boundary[i];colour=Vector([(a[j]+b[j])/2 for j in (3,4,5)])
            ap=(Vector(a[:3]),Vector(a[3:6]));bp=(Vector(b[:3]),Vector(b[3:6]))
            for t in (1/3,2/3):
                av=Vector(a[:3]).lerp(hub,t);bv=Vector(b[:3]).lerp(hub,t)
                an=(av.lerp(surface(av),.65),Vector(a[3:6]).lerp(colour,t));bn=(bv.lerp(surface(bv),.65),Vector(b[3:6]).lerp(colour,t))
                emit(bp,ap,an,a[9]);emit(bp,an,bn,a[9]);ap,bp=an,bn
            emit(bp,ap,(hub,colour),a[9])
    return rows+caps
