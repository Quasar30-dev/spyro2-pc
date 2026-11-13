"""Pixel closing stinger and animated retail guidebook chapter spreads."""
from pathlib import Path
import subprocess,json,math,functools
import numpy as np
from PIL import Image,ImageDraw,ImageFont,ImageFilter,ImageEnhance
from author_powers_trailer import ROOT,OUT,RetailArt

def encoder(path,w,h,filters=None):
    cmd=['ffmpeg','-hide_banner','-loglevel','error','-nostdin','-y','-f','rawvideo','-pix_fmt','rgb24','-s',f'{w}x{h}','-r','60','-i','pipe:0','-an']
    if filters:cmd+=['-vf',filters]
    cmd+=['-c:v','libx264','-preset','fast','-crf','17','-threads','4','-pix_fmt','yuv420p','-video_track_timescale','60000','-movflags','+faststart',str(path)]
    return subprocess.Popen(cmd,stdin=subprocess.PIPE,creationflags=subprocess.CREATE_NO_WINDOW)

def finish(p):p.stdin.close();assert p.wait()==0
def ease(x):x=np.clip(x,0,1);return x*x*(3-2*x)

def opening_logo():
    # Composite the returned GPT Image artwork unchanged onto a black card.
    logo=Image.open(OUT/'source/spyro-2-pc-logo-gpt.png').convert('RGBA')
    logo=logo.crop(logo.getbbox())
    stream=encoder(OUT/'edit/opening-logo.mp4',1920,1080)
    for frame in range(360):
        t=frame/60;im=Image.new('RGBA',(1920,1080),(0,0,0,255))
        w=round(1250+50*ease(t/3));emblem=logo.resize((w,round(logo.height*w/logo.width)),Image.Resampling.LANCZOS)
        a=ease(t/.4);emblem.putalpha(emblem.getchannel('A').point(lambda v:round(v*a)))
        im.alpha_composite(emblem,((1920-emblem.width)//2,(1080-emblem.height)//2))
        if frame==120:im.convert('RGB').save(OUT/'review/opening-logo.jpg')
        stream.stdin.write(im.convert('RGB').tobytes())
    finish(stream);print('Rendered GPT Image edited Spyro 2 PC opening logo',flush=True)

def stinger():
    mask=np.array(Image.open(OUT/'source/openai-blossom.png').getchannel('A').resize((72,72),Image.Resampling.LANCZOS))>110
    yy,xx=np.where(mask);target=np.column_stack((xx-36.,yy-36.));theta=np.arctan2(target[:,1],target[:,0])
    radius=np.linalg.norm(target,axis=1);sector=((theta+np.pi)/(np.pi/3)).astype(int)%6
    palette=np.array([(232,45,56),(249,167,39),(55,177,104),(37,126,228),(95,90,240),(245,71,121)],float)
    font=ImageFont.truetype(str(OUT/'source/zrnic rg.otf'),25)
    small=ImageFont.truetype(str(OUT/'source/zrnic rg.otf'),11)
    stream=encoder(OUT/'edit/stinger.mp4',320,240,'scale=1440:1080:flags=neighbor,pad=1920:1080:240:0:black,setsar=1')
    for frame in range(360):
        t=frame/60;im=Image.new('RGB',(320,240));arr=np.array(im)
        settle=ease((t-.35)/2.4);angle=(1-settle)*2.6
        c,s=np.cos(angle),np.sin(angle);rot=target@np.array([[c,s],[-s,c]])
        scatter=(1-settle)*(17+radius*.28)
        x=np.rint(160+rot[:,0]*(.58+.42*settle)+np.cos(theta+t*2)*scatter).astype(int)
        y=np.rint(88+rot[:,1]+np.sin(theta+t*2)*scatter).astype(int)
        white=ease((t-1.85)/.95);rgb=palette[sector]*(1-white)+np.array([235,235,239])*white
        alpha=ease((t-.15)/.45)*(1-ease((t-5.45)/.55))
        ok=(x>=0)&(x<320)&(y>=0)&(y<240)
        arr[y[ok],x[ok]]=(rgb[ok]*alpha).astype('uint8');im=Image.fromarray(arr);d=ImageDraw.Draw(im)
        def title(text,y,font,at):
            a=ease((t-at)/.35)*(1-ease((t-5.45)/.55));box=d.textbbox((0,0),text,font=font)
            d.text((160-(box[2]-box[0])/2-box[0],y-box[1]),text,font=font,fill=tuple(round(v*a) for v in (226,226,234)))
        title('made with the assistance of',152,small,2.1);title('GPT 6',174,font,2.55)
        if frame in (60,150,240):im.resize((1440,1080),Image.Resampling.NEAREST).save(OUT/'review'/f'stinger-{frame}.png')
        stream.stdin.write(im.tobytes())
    finish(stream);print('Rendered six-second assistance closing stinger',flush=True)

def joke():
    art=RetailArt();card=Image.new('RGBA',(1920,1080))
    lines=[('No plants were harmed during',332),('the making of this port...',418),
           ('the water went straight to',571),("OpenAI's data centers so the",657),
           ('plants simply dont exist ;)',743)]
    for line,y in lines:
        glyphs=art.text(line,1,8)
        assert glyphs.width<=1720,line
        card.alpha_composite(glyphs,((1920-glyphs.width)//2,y))
    card.save(OUT/'source/plants-sting.png');card.convert('RGB').save(OUT/'review/plants-sting.jpg')
    stream=encoder(OUT/'edit/plants-sting.mp4',1920,1080)
    for frame in range(600):
        t=frame/60;alpha=ease((t-.15)/.45)*(1-ease((t-9.1)/.9))
        stream.stdin.write(ImageEnhance.Brightness(card.convert('RGB')).enhance(alpha).tobytes())
    finish(stream);print('Rendered yellow retail-font plant joke',flush=True)

def coming():
    art=RetailArt();logo=Image.open(ROOT/'data/expansions/backrealms/logo.png').convert('RGBA')
    logo=logo.resize((1440,round(logo.height*1440/logo.width)),Image.Resampling.LANCZOS)
    heading=art.text('Coming soon',1,9);stream=encoder(OUT/'edit/coming-soon.mp4',1920,1080)
    for frame in range(210):
        t=frame/60;im=Image.new('RGBA',(1920,1080),(0,0,0,255))
        text=heading.copy();text.putalpha(heading.getchannel('A').point(lambda a:round(a*ease((t-.1)/.35))))
        im.alpha_composite(text,((1920-text.width)//2,115))
        emblem=logo.copy();emblem.putalpha(logo.getchannel('A').point(lambda a:round(a*ease((t-.45)/.5))))
        im.alpha_composite(emblem,((1920-emblem.width)//2,245))
        if frame==105:im.convert('RGB').save(OUT/'review/coming-soon.jpg')
        stream.stdin.write(im.convert('RGB').tobytes())
    finish(stream);print('Rendered Coming soon with the existing Spyro 2.5 Backrealms logo',flush=True)

def still(name,sec):
    path=OUT/'source'/f'{name}-still.png'
    subprocess.run(['ffmpeg','-hide_banner','-loglevel','error','-y','-ss',str(sec),'-i',str(OUT/'gameplay'/f'{name}.mp4'),'-frames:v','1',str(path)],check=True,creationflags=subprocess.CREATE_NO_WINDOW)
    return Image.open(path).convert('RGB')

def fit(im,size):
    w,h=size;ratio=max(w/im.width,h/im.height);im=im.resize((round(im.width*ratio),round(im.height*ratio)),Image.Resampling.LANCZOS)
    return im.crop(((im.width-w)//2,(im.height-h)//2,(im.width+w)//2,(im.height+h)//2))

def plate(art,number,left,right):
    # Compose in PS1 UI coordinates, then apply the engine's 512 -> 320 aspect.
    im=art.book.resize((1536,720),Image.Resampling.NEAREST)
    for ox,content in [(0,left),(256,right)]:
        heading,photo,caption,lines=content
        def txt(label,y,color=8,scale=3):
            natural=art.text(label,color,1,False)
            scale=min(scale,540/max(1,natural.width))
            g=art.text(label,color,scale,False);x=round((ox+128)*3-g.width/2)
            assert ox*3+114<=x and x+g.width<=(ox+256)*3-114,label
            assert round(y*3)+g.height<=216*3,label
            im.alpha_composite(g,(x,round(y*3)))
        txt(heading,32,8,3.2)
        # Preserve the retail inset gold picture border and screen proportions.
        draw=ImageDraw.Draw(im);box=(round((ox+34)*3),59*3,round((ox+222)*3),143*3)
        draw.rectangle((box[0]-7,box[1]-7,box[2]+7,box[3]+7),fill=(80,73,24,255));draw.rectangle((box[0]-4,box[1]-4,box[2]+4,box[3]+4),fill=(207,198,112,255))
        pic=fit(photo,(round(188*3*.625),84*3)).resize((188*3,84*3),Image.Resampling.LANCZOS)
        im.alpha_composite(pic.convert('RGBA'),(box[0],box[1]));txt(caption,153,8,2.8)
        for i,line in enumerate(lines):txt(line,179+i*14,8,2.5)
    g=art.text(str(number),8,2.8,False);im.alpha_composite(g,(round(380*3-g.width/2),218*3))
    return im

def warp(im,quad,size):
    # Inverse homography: destination page corners -> source pixel corners.
    src=[(0,0),(im.width,0),(im.width,im.height),(0,im.height)];mat=[];b=[]
    for (x,y),(u,v) in zip(quad,src):
        mat+=[[x,y,1,0,0,0,-u*x,-u*y],[0,0,0,x,y,1,-v*x,-v*y]];b+=[u,v]
    coeff=np.linalg.solve(np.array(mat),np.array(b))
    return im.transform(size,Image.Transform.PERSPECTIVE,coeff,Image.Resampling.BICUBIC)

def book_video(name,plate,previous,back,seconds=3.5):
    # Retail page rotation around the centre seam, perspective depth 512.
    booksize=(1280,960);current=plate.resize(booksize,Image.Resampling.LANCZOS);prev=previous.resize(booksize,Image.Resampling.LANCZOS)
    bg=ImageEnhance.Brightness(fit(back,(1920,1080)).filter(ImageFilter.GaussianBlur(18))).enhance(.32).convert('RGBA')
    stream=encoder(OUT/'edit'/f'{name}.mp4',1920,1080)
    page=prev.crop((640,0,1280,960));nextleft=current.crop((0,0,640,960))
    for frame in range(round(seconds*60)):
        t=frame/60;im=bg.copy();layer=current.copy()
        if t<.75:
            q=ease(t/.75);angle=q*np.pi
            # Old left remains visible until the moving right page passes centre.
            if q<.5:layer.paste(prev.crop((0,0,640,960)),(0,0))
            x=640+640*np.cos(angle)/(1-.5*np.sin(angle));depthscale=1/(1-.5*np.sin(angle))
            y0=480-480*depthscale;y1=480+480*depthscale
            if abs(x-640)>3:
                moving=page if q<.5 else nextleft.transpose(Image.Transpose.FLIP_LEFT_RIGHT)
                quad=[(640,0),(x,y0),(x,y1),(640,960)]
                folded=warp(moving,quad,booksize);shade=Image.new('RGBA',booksize,(0,0,0,round(75*np.sin(angle))))
                shade.putalpha(folded.getchannel('A').point(lambda a:round(a*75*np.sin(angle)/255)))
                folded=Image.alpha_composite(folded,shade);layer=Image.alpha_composite(layer,folded)
        # Slow push toward the pictures retains the entire original book border.
        scale=1+.022*ease(t/seconds);w=round(1280*scale);h=round(960*scale);layer=layer.resize((w,h),Image.Resampling.LANCZOS)
        shadow=Image.new('RGBA',(w,h),(0,0,0,150));shadow.putalpha(layer.getchannel('A').point(lambda a:a*3//5));im.alpha_composite(shadow,((1920-w)//2+12,(1080-h)//2+15))
        im.alpha_composite(layer,((1920-w)//2,(1080-h)//2))
        if frame==90:im.convert('RGB').save(OUT/'review'/f'{name}.jpg')
        stream.stdin.write(im.convert('RGB').tobytes())
    finish(stream);print('Rendered',name,flush=True)

def titles(art):
    headings={
        'regular-run':'THE ADVENTURE RETURNS', 'charge-jump':'LEAP INTO THE CLASSICS', 'underwater':'BENEATH THE SURFACE', 'whirlwind':'RIDE THE WINDS',
        'yellow-charge':'SUPERCHARGE: YELLOW', 'red-charge':'SUPERCHARGE: RED', 'dodge-roll':'THE JULY DODGE ROLL',
        'glide-boost':'GLIDE FURTHER', 'superfly':'FLY FASTER', 'carry':'TAKE HOLD', 'drop-smash':'DROP. SMASH. COLLECT.',
        'homing-flame':'HOMING AND SPLITTING SUPERFLAME', 'wall-flame':'SHATTER STONE', 'green-bomb':'GREEN FIRE BOMBS',
        'building-breach':'MAKE YOUR OWN PATH', 'melee':'UNLEASH THE COMBO', 'headbash-crater':'HEADBASH: CRATER',
        'headbash-lava':'HEADBASH: LAVA APOCALYPSE', 'wall-charge':'IMPACT. RECOIL. DESTRUCTION.', 'boss':'ENTER THE CROWNWORKS'}
    for name,label in headings.items():
        im=Image.new('RGBA',(1920,1080));g=art.text(label,1,4.4 if len(label)>24 else 5);x=(1920-g.width)//2;y=935
        shadow=Image.new('RGBA',g.size,(0,0,0,0));shadow.putalpha(g.getchannel('A'));im.alpha_composite(shadow,(x+3,y+4));im.alpha_composite(g,(x,y));im.save(OUT/'titles'/f'{name}.png')
    im=Image.new('RGBA',(1920,1080))
    emblem=Image.open(OUT/'source/spyro-2-pc-logo-gpt.png').convert('RGBA')
    emblem=emblem.crop(emblem.getbbox())
    emblem=emblem.resize((1000,round(emblem.height*1000/emblem.width)),Image.Resampling.LANCZOS)
    assert 155+emblem.height<680
    im.alpha_composite(emblem,((1920-emblem.width)//2,155))
    for text,y,scale,color in [('CLASSIC WORLDS. NEW POSSIBILITIES.',700,5,1),('60 FPS  /  OPTIONAL UPGRADES  /  YOUR GAME COPY',799,3.5,3),('FAN-MADE PC PORT  /  WORK IN PROGRESS',950,2.5,3)]:
        g=art.text(text,color,scale);x=(1920-g.width)//2;shadow=Image.new('RGBA',g.size,(0,0,0,0));shadow.putalpha(g.getchannel('A'));im.alpha_composite(shadow,(x+4,y+5));im.alpha_composite(g,(x,y))
    im.save(OUT/'titles/end.png')

def books():
    art=RetailArt();titles(art)
    spreads=[
        ('book-adventure',('SPYRO 2 PC',still('run-pov-v2',2),'RETURN TO AVALAR',['RUN. LEAP. GLIDE.','THE ADVENTURE RETURNS']),('EXPLORE AVALAR',still('underwater-close',3.0),'DIVE INTO THE CLASSICS',['FAMILIAR WORLDS','NEW POSSIBILITIES'])),
        ('book-speed',('BEYOND SPEED',still('red-pov-v2',1.1),'YELLOW. THEN RED.',['THE MAIN MODEL RETURNS','DODGE WITH THE JULY ROLL']),('FLIGHT EVOLVED',still('carry-close',.75),'TAKE TO THE SKIES',['TWO EXTRA GLIDE BOOSTS','GRAB. CARRY. DROP.'])),
        ('book-fire',('FIRE REFORGED',still('door-bomb-pov-v2',2.2),'GREEN FIRE BOMBS',['HOMING SUPERFLAME','SPLITTING IMPACTS']),('SHATTER THE WORLD',still('farm-breach-pov-v6',3),'MAKE YOUR OWN PATH',['CRACK WALLS','BLAST THROUGH STONE'])),
        ('book-combat',('STRIKE BACK',still('melee-pov-v2',.65),'UNLEASH THE COMBO',['NEW MELEE MOVES','ALL OPTIONAL']),('RESHAPE THE GROUND',still('headbash-lava',3),'LAVA APOCALYPSE',['CRATER SHOCKWAVES','STAND YOUR GROUND']))]
    back=still('run-pov-v2',2);previous=art.book.resize((1536,720),Image.Resampling.NEAREST)
    for i,(name,left,right) in enumerate(spreads):
        p=plate(art,i+1,left,right);p.save(OUT/'source'/f'{name}.png');book_video(name,p,previous,back);previous=p
    print('Rendered retail-font titles and guidebook chapters',flush=True)
    # Keep the fan-port end card on black; the discarded castle flyover is
    # not reused as a blurred background or hidden in another chapter.
    card=Image.new('RGBA',(1920,1080),(0,0,0,255));card.alpha_composite(Image.open(OUT/'titles/end.png').convert('RGBA'))
    stream=encoder(OUT/'edit/end-card.mp4',1920,1080)
    for _ in range(360):stream.stdin.write(card.convert('RGB').tobytes())
    finish(stream)

if __name__=='__main__':
    import argparse
    p=argparse.ArgumentParser();p.add_argument('part',choices=['logo','stinger','joke','coming','books','all']);a=p.parse_args()
    if a.part in ('logo','all'):opening_logo()
    if a.part in ('stinger','all'):stinger()
    if a.part in ('joke','all'):joke()
    if a.part in ('coming','all'):coming()
    if a.part in ('books','all'):books()
