#!/usr/bin/env python3
# HC1703L Plus - encoder rate-control patch. MIT License, see ../LICENSE.
# Usage: python3 -I patch_rc.py <input p2pcam (output of patch_p2pcam.py)> <output> <A|B|C|C2>
# build.sh uses C2 (25 FPS, 2560 kbps cap). A/B/C are kept for experiments and are less tested.
# Code VA -> file offset: va-0x10000. .data strings are addressed by file offset directly.
import sys
src,dst,S=sys.argv[1:4]
b=bytearray(open(src,'rb').read())
SETS={ # fps, device_fps, frm_rate_o, gop, max_kbps, tex_base, tex_factor, m2s_min_qp
 'A' :(12.5,13.0,12,25,2048,20,400000,28),
 'B' :(20.0,21.0,20,40,2048,21,400000,28),
 'C' :(25.0,26.0,25,50,2048,22,400000,28),
 'C2':(25.0,26.0,25,50,2560,21,400000,28),
}
fps,dev,fro,gop,mx,tb,tf,m2s=SETS[S]
def vimm(v):
    return {12.5:0x29,20.0:0x34,25.0:0x39}[v]
def setb(va,mask_ok,new,why):
    o=va-0x10000; cur=bytes(b[o:o+len(new)])
    assert cur in mask_ok,(hex(va),cur.hex(),why)
    b[o:o+len(new)]=new; print('CODE %06x %s -> %s %s'%(va,cur.hex(),new.hex(),why))
i=vimm(fps);H,L=i>>4,i&15
for va,hi,tail,why in ((0x5f840,0xb0,0x0a,'df_isp_daynight df_set_fps'),(0x5fb16,0xb0,0x8b,'video init d8'),(0x5fba0,0xf0,0x7a,'channel0 fps'),(0x5fc96,0xf0,0x7a,'window fps'),(0x600b2,0xf0,0x7a,'channel1 fps')):
    old=[bytes([hi|2,0xee,9,tail]),bytes([hi|3,0xee,9,tail])]
    setb(va,old,bytes([hi|H,0xee,L,tail]),why)
imm={13.0:0x50,21.0:0xa8,26.0:0xd0}[dev]
setb(0x5fa30,[bytes.fromhex('c4f25012'),bytes.fromhex('c4f2d012')],bytes([0xc4,0xf2,imm,0x12]),'device fps')
setb(0x6018e,[bytes.fromhex('4ff00c03'),bytes.fromhex('4ff01903')],bytes([0x4f,0xf0,fro,0x03]),'main frm_rate_o')
setb(0x5fe5e,[bytes.fromhex('4ff00c03'),bytes.fromhex('4ff01903')],bytes([0x4f,0xf0,fro,0x03]),'sub frm_rate_o')
setb(0x60184,[bytes.fromhex('1920'),bytes.fromhex('3220')],bytes([gop,0x20]),'main GOP')
setb(0x5fe56,[bytes.fromhex('4ff0190c'),bytes.fromhex('4ff0320c')],bytes([0x4f,0xf0,gop,0x0c]),'sub GOP')
MX={1024:'4ff48063',1536:'4ff4c063',2048:'4ff40063',2560:'4ff42063',3072:'4ff44063'}
setb(0x601a2,[bytes.fromhex('4ff48063'),bytes.fromhex('4ff40063')],bytes.fromhex(MX[mx]),'720p main max_bit_rate %d'%mx)
def rep(off,slot,old,new,why):
    ob=old.encode();nb=new.encode()
    assert bytes(b[off:off+len(ob)+1])==ob+b'\0',(hex(off),why)
    assert len(nb)<slot,why
    b[off:off+slot]=nb+b'\0'*(slot-len(nb))
    print('DATA %06x %s'%(off,new),why)
N='0,51,0,51,0,80,12,1000,100000,1,16,250000,24,0,0,30,2,48,35,4,0'
D='0,51,0,51,0,70,12,10,20000,1,16,180000,24,0,0,30,2,24,35,4,0'
n2='0,51,0,51,0,100,12,1000,100000,1,16,%d,%d,0,0,30,2,48,%d,4,0'%(tf,tb,m2s)
d2='0,51,0,51,0,100,12,10,20000,1,16,%d,%d,0,0,30,2,24,%d,4,0'%(tf,tb,m2s)
rep(0x381020,100,N,n2,'g_rc_param night')
rep(0x381084,100,D,d2,'g_rc_param day')
rep(0x3810e8,100,N,n2,'g_rc_param white light')
R='4,60,6,60,0,0,45,8,2,6,300000,800000,500,3000,40,48,40,40,400000,800000,500,3000,0,0,0,0,20000000,20000000,4,100,300,0,0,-1'
rep(0x3811cc,0x80,R,'2,60,2'+R[6:],'g_rgl_qp day: I/P rgl ceiling 4/6 -> 2/2')
open(dst,'wb').write(b)
