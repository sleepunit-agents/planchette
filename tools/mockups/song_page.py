from PIL import Image, ImageDraw, ImageFont
import sys
import matplotlib, os; F=os.path.join(os.path.dirname(matplotlib.__file__),'mpl-data/fonts/ttf/')
def f(s,b=False,m=False): return ImageFont.truetype(F+('DejaVuSansMono'+('-Bold' if b else '') if m else 'DejaVuSans'+('-Bold' if b else ''))+'.ttf',s)
S=2; W,H=1366,1024
im=Image.new('RGB',(W*S,H*S),'#141317'); d=ImageDraw.Draw(im)
def R(x,y,w,h,c,r=10,o=None,ow=2): d.rounded_rectangle([x*S,y*S,(x+w)*S,(y+h)*S],r*S,fill=c,outline=o,width=ow*S if o else 0)
def T(x,y,s,t,c='#efebe4',b=False,m=False,a='la'): d.text((x*S,y*S),t,fill=c,font=f(s*S,b,m),anchor=a)
def L(x1,y1,x2,y2,c,w=1): d.line([x1*S,y1*S,x2*S,y2*S],fill=c,width=w*S)
PANEL,RAISED,DIM,SOFT,TXT='#1d1c22','#26252d','#9a96a0','#c9c5cd','#efebe4'
Y,BL,PK,OL,LI='#f2d16b','#9cc0e8','#f0a3c8','#a7bd74','#b8a6ff'
def label(x,y,t,c=DIM): T(x,y,11,t.upper(),c,True)
def slider(x,y,w,name,val,frac,c,inherit=False):
    T(x,y+3,13,name,SOFT); bx=x+120; bw=w-120-58
    R(bx,y,bw,22,RAISED,6); R(bx,y,max(10,bw*frac),22,c if not inherit else '#4a4954',6)
    T(x+w,y+4,13,val,TXT,True,True,'ra')
def seg(x,y,w,items,on,c='#efebe4',h=34,fs=13):
    n=len(items); gw=(w-(n-1)*6)/n
    for i,t in enumerate(items):
        sel=(i==on); R(x+i*(gw+6),y,gw,h,c if sel else RAISED,8)
        T(x+i*(gw+6)+gw/2,y+h/2,fs,t,'#141317' if sel else SOFT,sel,a='mm')
# top bar
T(24,22,22,'Planchette',TXT,True)
for i,t in enumerate(['Play','Groups','Changes','Song','Form']):
    sel=t=='Song'; R(190+i*96,14,88,40,'#efebe4' if sel else RAISED,10); T(234+i*96,34,14,t,'#141317' if sel else TXT,True,a='mm')
R(830,14,170,40,RAISED,10); T(846,34,14,'Untitled  ▾',TXT,True,a='lm'); 
R(1010,14,64,40,RAISED,10); T(1042,34,13,'Undo',TXT,True,a='mm'); R(1080,14,64,40,'#1d1c22',10); T(1112,34,13,'Redo','#55535c',True,a='mm')
R(1160,14,184,40,PK,10); T(1252,34,15,'Generate','#141317',True,a='mm')
L(0,68,W,68,'#33323b')
# ---- col 1: key + time
x0,y0=20,84; R(x0,y0,300,920,PANEL,16)
label(40,104,'Key')
T(40,128,46,'A',TXT,True); T(92,146,24,'Minor',SOFT)
# piano as the one root picker
kx,ky,kw=40,206,260; ww=kw/7
whites=['C','D','E','F','G','A','B']; scale={'A','B','C','D','E','F','G'}
for i,n in enumerate(whites):
    R(kx+i*ww,ky,ww-3,96,Y if n=='A' else '#efebe4',4); T(kx+i*ww+ww/2-1,ky+84,10,n,'#141317',True,a='mm')
for i in [0,1,3,4,5]:
    R(kx+(i+1)*ww-12,ky,20,58,'#0e0d11',3)
T(40,312,11,'tap the root  ·  hold to step round the fifths',DIM)
label(40,344,'Scale')
R(40,364,260,44,RAISED,10); T(54,386,15,'Minor',TXT,True,a='lm'); T(286,386,12,'Diatonic  ▾',DIM,a='rm')
T(40,416,11,'40 scales in 6 families, in a picker,',DIM); T(40,431,11,'not 40 pills on the page',DIM)
label(40,470,'Time')
for j,(n,v) in enumerate([('Beats a bar','4'),('Beat','1/4'),('Bars a phrase','4')]):
    yy=494+j*48; T(40,yy+10,13,n,SOFT); R(196,yy,104,34,RAISED,8); T(212,yy+17,15,'−',DIM,a='mm'); T(248,yy+17,14,v,TXT,True,True,'mm'); T(284,yy+17,15,'+',DIM,a='mm')
T(40,646,11,'moved here from "Every line": meter is',DIM); T(40,661,11,'the song\'s, not a line\'s',DIM)
# ---- col 2: harmony (the hero)
x1=336; R(x1,y0,600,920,PANEL,16)
label(356,104,'Harmony',Y); T(916,104,11,'A minor · a change every bar',DIM,a='ra')
chords=[('Am7','A C E G'),('Em7','E G B D'),('G7','G B D F'),('Am7','A C E G')]
for i,(c,n) in enumerate(chords):
    cx=356+i*142; R(cx,128,134,96,Y if i==0 else RAISED,12)
    col='#141317' if i==0 else TXT
    T(cx+14,142,26,c,col,True); T(cx+14,196,11,n,'#5a4d1e' if i==0 else DIM,m=True)
    if i in (1,2,3):
        tag={1:'late ▸',2:'◂ push',3:'◂ push'}[i]; R(cx+80,134,48,20,'#3a3424',6); T(cx+104,144,10,tag,Y,True,a='mm')
seg(356,236,560,['1/4','1/2','1 bar','2','4 bars'],2)
slider(356,288,560,'Repeats after','4',0.25,Y)
slider(356,322,560,'Reach','8',0.5,Y)
slider(356,356,560,'Wander','8',0.5,Y)
seg(356,392,560,['Ranked chords','Mode moves'],0,h=30,fs=12)
# push panel
R(352,440,568,452,'#22211a',14,o='#4a4227',ow=1)
label(372,458,'Push',Y); T(420,458,12,'the chords go where the riff goes',SOFT)
R(372,484,250,40,RAISED,10); T(386,504,12,'Source',DIM,a='lm'); T(450,504,14,'Leads',TXT,True,a='lm'); T(606,504,12,'▾',DIM,a='rm')
T(636,504,11,'group\'s own rhythm, before dealing',DIM,a='lm')
slider(372,540,528,'Amount','16',1.0,Y)
for j,(n,v) in enumerate([('Early','2 steps'),('Late','1 step')]):
    xx=372+j*180; T(xx,592,13,n,SOFT,a='lm'); R(xx+50,576,118,32,RAISED,8); T(xx+64,592,15,'−',DIM,a='mm'); T(xx+109,592,13,v,TXT,True,True,'mm'); T(xx+154,592,15,'+',DIM,a='mm')
T(732,592,13,'Hits',SOFT,a='lm'); seg(770,576,130,['All','Accents'],0,h=32,fs=11)
# timeline: 4 bars, 16 steps each; riff 12 steps hits 1,5,7,11
tx,ty,tw=372,630,528; steps=64; sw=tw/steps
label(372,622-4,'What you\'ll hear · 4 bars',DIM)
ty=640
for b in range(5): L(tx+b*16*sw,ty,tx+b*16*sw,ty+200,'#4a4954',1)
for b in range(4): T(tx+b*16*sw+4,ty+2,10,str(b+1),DIM,m=True)
T(tx-2,ty+30,10,'riff',BL,True,a='rm') if False else None
ry=ty+22; T(tx,ry-2,10,'Leads · 12 steps',BL)
for s in range(steps):
    if s%12 in (1,5,7,11):
        acc=s%12 in (1,7); hh=26 if acc else 16
        R(tx+s*sw+1,ry+44-hh,sw-2,hh,BL,2)
cy=ty+80; T(tx,cy-14,10,'chords · grid (ghost) and arrival',Y)
grid=[0,16,32,48]; arr=[0,17,31,47]; names=['Am7','Em7','G7','Am7']
for i in range(4):
    g0=grid[i]; g1=grid[i+1] if i<3 else 64
    R(tx+g0*sw,cy+8,(g1-g0)*sw-2,18,'#2b2a31',4)  # grid ghost
    a0=arr[i]; a1=arr[i+1] if i<3 else 64
    R(tx+a0*sw,cy+34,(a1-a0)*sw-2,30,Y,6); T(tx+a0*sw+8,cy+49,12,names[i],'#141317',True,a='lm')
    if a0!=g0:
        L(tx+g0*sw,cy+26,tx+a0*sw,cy+34,Y,2)
T(tx,cy+82,11,'bar 2 waits a step for the riff  ·  bars 3 and 4 push in a 1/16 early',DIM)
T(tx,cy+98,11,'12 against 16: the riff meets each bar line somewhere new, so the push moves',DIM)
seg(356,906,560,['Lock chords','New progression'],-1,h=40)
# ---- col 3: line defaults
x2=952; R(x2,y0,394,920,PANEL,16)
label(972,104,'Every line, unless a group says')
label(972,136,'Pitch',LI)
for j,(n,v,fr) in enumerate([('Range','8',.5),('Wander','8',.5),('Octave jumps','16',1),('Mode colour','12',.75)]):
    slider(972,158+j*36,354,n,v,fr,LI)
T(972,312,13,'Octaves',SOFT); R(1092,308,176,22,RAISED,6); R(1092+176*0.15,308,176*0.6,22,LI,6); T(1326,312,13,'−1…+2',TXT,True,True,'ra')
label(972,356,'Touch',PK)
for j,(n,v,fr) in enumerate([('Accent','50',.5),('Gate','100%',.5)]):
    slider(972,378+j*36,354,n,v,fr,PK)
T(972,466,11,'one range control for octave low/high;',DIM); T(972,481,11,'meter moved to Key & Time',DIM)
R(972,530,354,96,'#18171c',12,o='#33323b',ow=1)
T(988,548,12,'Colour means one thing:',SOFT,True)
for j,(c,t) in enumerate([(Y,'harmony'),(BL,'rhythm'),(LI,'pitch'),(PK,'touch / play')]):
    R(988+ (j%2)*168,576+(j//2)*24,12,12,c,3); T(1008+(j%2)*168,576+(j//2)*24-1,12,t,SOFT)
T(W-20,H-12,10,'MOCKUP · Song page with Push · not the plugin',DIM,a='rd')
im=im.resize((W,H),Image.LANCZOS); im.save(sys.argv[1])
