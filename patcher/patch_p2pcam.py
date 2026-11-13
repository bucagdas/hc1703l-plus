#!/usr/bin/env python3
# HC1703L Plus - p2pcam patcher. MIT License, see ../LICENSE.
# Builds a patched p2pcam from YOUR OWN stock binary. Two stock builds are known (see BUILDS below);
# any other binary is refused. Every patch verifies the original bytes first and aborts on any mismatch.
# Usage: python3 -I patch_p2pcam.py <stock p2pcam> <output>
import sys,hashlib,struct
src,dst=sys.argv[1],sys.argv[2]
b=bytearray(open(src,'rb').read())
OFF=0x10000

# ---- the two known stock builds ----------------------------------------------------------------------------
# Firmware 57.0.8.0 is the author's camera, tested daily. 57.1.2.0 was ported statically from a user's dump
# (issue #1): every site below was found and checked against the 57.0.8.0 code, but it is tested on that camera only.
BUILDS={
'5e2dc1a15734e214':dict(fw='57.0.8.0',
    chazhi=(0x1a794,'c4f85493'),
    strings=[(0x32f04d,'203.107.1.1'),(0x32f05d,'203.107.1.33'),(0x32f06d,'203.107.1.34'),(0x32f07d,'203.107.1.35'),
             (0x32ba6c,'http://139.224.31.65/device_log_upload.php'),(0x2306d8,'47.107.191.43'),
             (0x32b5ff,'http://ep.tange365.com'),(0x32b5e3,'http://ep.test.tange365.com'),
             (0x32a89d,'fidistr.tange365.com'),(0x32a8b2,'fidistr-test.tange365.com'),
             (0x231210,'www.goke-agps.com'),(0x231250,'http://www.goke-agps.com:7777/brdcGPD.dat'),
             (0x32a263,'teltun'),(0x2346c4,'telnetd&'),(0x22d67c,'telnetd &'),(0x231e80,'killall telnetd')],
    video=0,                                     # the video addresses below are 57.0.8.0's; other builds add this delta
    probe_call=0x37156,
    tange_call=(0x16be4,'fcf04eff'), tange=0x113a84,
    tange_old=('4ef250510f202de9f041bab0c0f2330103f700e80520b7f7adfa01e002f786e91ef74afa03460120002bf7d0',
               'bc50c0f2330002f73ce841f6802149f21070c0f23301c0f22200b4f7cdfffcf7e7f904f7f9f8002840f0968004f7d4f8002800f04a8104f7d1f978b94ef2ec55c0f2330504e002f71ce8012002f7b0e80ef7',
               'eddd26f7f7fb0120c3f05efc18f76afb002800f0'),
    do_update=0x583ac,
    plt_bind=0x16758, plt_getpeername=0x15c70,
    binds=[260692,261168,262130,351288,354586,357830,358402,361452,822730,948086,972430,979564,979592,985896,987186,
           998680,998714,1002448,1002500,1026732,1026760,1026980,1028080,1028122,1035176,1687694,1690364,1690618,
           1690992,1774162,1796662,1796752],
    rtp_bind=0x53492,
    sndbuf=0x55a4a, rdreq=0x55070, rdreq_buf=0x3ae050, rdreq_calls=(351928,352388),
    venc_memset=(0x5ef24,'b7f79ce8'),
    ymv=0x19bae,
    reboot_time=((0x30be6,'adf178fe'),(0x30fe0,'adf17bfc')),
    noprobe_flag=0x3a52c4,
    ota=[]),
'146e7faff2f2dfd0':dict(fw='57.1.2.0',
    chazhi=(0x1ad82,'c4f86c33'),
    strings=[(0x37f82d,'203.107.1.1'),(0x37f83d,'203.107.1.33'),(0x37f84d,'203.107.1.34'),(0x37f85d,'203.107.1.35'),
             (0x37c1ed,'http://139.224.31.65/device_log_upload.php'),(0x27ee74,'47.107.191.43'),
             (0x37bd83,'http://ep.tange365.com'),(0x37bd67,'http://ep.test.tange365.com'),
             (0x3799db,'fidistr.tange365.com'),(0x3799f0,'fidistr-test.tange365.com'),
             (0x27f9a4,'www.goke-agps.com'),(0x27f9e4,'http://www.goke-agps.com:7777/brdcGPD.dat'),
             (0x3798a3,'teltun'),(0x2830e0,'telnetd&'),(0x27bd48,'telnetd &'),(0x2806c8,'killall telnetd')],
    video=0x3758,                                # the whole video module moved by the same amount
    probe_call=0x39306,
    tange_call=(0x16e1c,'52f1f4fc'), tange=0x169808,
    tange_old=('49f680310f202de9f041bab0c0f23901adf65cea052064f745fd01e0acf6c6ebc9f6dafe03460120002bf7d0',
               'ec30c0f23900acf66aea44f6746147f6a420c0f23801c0f2270062f747fbfcf7a5f8c0ef100000231a93cded1b0bc6ed010baef6d7fc002840f09680aef6b2fc002800f04a81aef6affd78b949f61c45c0f2',
               'c1170df19bfb1a9b024601201344ab42edddd2f6'),
    do_update=0x5bb04,
    plt_bind=0x16980, plt_getpeername=0x15e50,
    binds=[0x42184,0x42360,0x42722,0x59390,0x5a072,0x5ad1e,0x5af5a,0x5bb44,0xd3798,0xeffc6,0xf7396,0x1036a4,0x1036c0,
           0x104f60,0x10546a,0x108150,0x108172,0x109008,0x10903c,0x10eee4,0x10ef00,0x10efdc,0x10f428,0x10f452,
           0x10fc40,0x14e7f4,0x1e969e,0x1ea10c,0x1ea20a,0x1ea380,0x1fe852,0x204036,0x204090],
    rtp_bind=0x56bea,
    sndbuf=0x591a2, rdreq=0x587c8, rdreq_buf=0x40a9d8, rdreq_calls=(0x59610,0x597dc),
    venc_memset=(0x6267c,'b3f7faed'),
    ymv=0x1a18e,
    reboot_time=((0x3218a,'faf114f8'),(0x32584,'f9f117fe')),
    noprobe_flag=0x40157c,
    ota=[(0x379749,'http://ihome-ota-service.tange365.com/'),(0x379770,'http://ihome-ota-service.test.tange365.com/')]),
}
H=hashlib.sha256(b).hexdigest()
assert H[:16] in BUILDS,'not a supported stock p2pcam (sha256 %s...); known builds: %s'%(H[:16],', '.join(
    '%s (fw %s)'%(k,v['fw']) for k,v in BUILDS.items()))
A=BUILDS[H[:16]]
print('stock p2pcam, firmware',A['fw'])

def code(va,old,new,why):
    o=va-OFF; assert bytes(b[o:o+len(old)])==old,(hex(va),bytes(b[o:o+len(old)]).hex()); assert len(old)==len(new)
    b[o:o+len(old)]=new; print('CODE',hex(va),old.hex(),'->',new.hex(),why)
def st(va,old,new,why):
    o=va-OFF; ob=old.encode()+b'\0'; assert bytes(b[o:o+len(ob)])==ob,(hex(va),old,bytes(b[o:o+len(ob)]))
    nb=new.encode(); assert len(nb)<=len(old)
    b[o:o+len(ob)]=nb+b'\0'*(len(ob)-len(nb)); print('STR ',hex(va),repr(old),'->',repr(new),why)
h=bytes.fromhex

# Thumb-2 encoders for the call sites and the routines below.
def _bl(at,to,x):
    d=to-((at+4)&~3 if x else at+4); assert -(1<<24)<=d<(1<<24) and not d&1
    S=(d>>24)&1; i1=(d>>23)&1; i2=(d>>22)&1; j1=(1-i1)^S; j2=(1-i2)^S
    return struct.pack('<HH',0xf000|S<<10|((d>>12)&0x3ff),(0xc000 if x else 0xd000)|j1<<13|j2<<11|((d>>1)&0x7ff))
def bl(at,to): return _bl(at,to,False)          # Thumb -> Thumb
def blx(at,to): assert not to&3; return _bl(at,to,True)   # Thumb -> ARM (PLT)
def _mov(op,rd,v): return struct.pack('<HH',op|((v>>11)&1)<<10|(v>>12)&15,((v>>8)&7)<<12|rd<<8|v&0xff)
def movw(rd,v): return _mov(0xf240,rd,v&0xffff)
def movt(rd,v): return _mov(0xf2c0,rd,v>>16)

L='127.0.0.1'
# Replacement for every vendor string patched below (same text in both builds).
REPL={'203.107.1.1':L,'203.107.1.33':L,'203.107.1.34':L,'203.107.1.35':L,                      # HTTPDNS
      'http://139.224.31.65/device_log_upload.php':'http://127.0.0.1/device_log_upload.php',  # log upload
      '47.107.191.43':L,                                                                      # 4G speed test
      'http://ep.tange365.com':'http://127.0.0.1','http://ep.test.tange365.com':'http://127.0.0.1',  # cloud API
      'fidistr.tange365.com':'localhost','fidistr-test.tange365.com':'localhost',             # distribution
      'www.goke-agps.com':'localhost','http://www.goke-agps.com:7777/brdcGPD.dat':'http://127.0.0.1/brdcGPD.dat',  # AGPS
      'http://ihome-ota-service.tange365.com/':'http://127.0.0.1/',                          # OTA (57.1.2.0)
      'http://ihome-ota-service.test.tange365.com/':'http://127.0.0.1/',
      'teltun':'zz_off',                                   # 3) vendor reverse tunnel: break the cloud command name
      'telnetd&':'true','telnetd &':'true','killall telnetd':'true'}  # 4) cloud/6670 path must not start or kill telnetd
# 1) Native 720p: stop forcing support_chazhi=1 (the sensor is 1280x720; stock upscales to a fake 1080p)
code(A['chazhi'][0],h(A['chazhi'][1]),h('aff30080'),'str.w [hwcfg support_chazhi],1 -> nop.w')
# 2)-4) Cloud endpoints -> loopback, teltun, telnetd
for va,o in A['strings']+A['ota']: st(va,o,REPL[o],'vendor string')
# 5) 25 FPS: every frame-rate site. VFP immediate 12.5 (imm8 0x29) -> 25.0 (imm8 0x39)
V=A['video']
code(0x5f840+V,h('b2ee090a'),h('b3ee090a'),'df_isp_daynight df_set_fps 12.5->25')
code(0x5fb16+V,h('b2ee098b'),h('b3ee098b'),'video init d8 12.5->25')
code(0x5fba0+V,h('f2ee097a'),h('f3ee097a'),'channel0 fps 12.5->25')
code(0x5fc96+V,h('f2ee097a'),h('f3ee097a'),'window fps 12.5->25')
code(0x600b2+V,h('f2ee097a'),h('f3ee097a'),'channel1 fps 12.5->25')
code(0x5fa30+V,h('c4f25012'),h('c4f2d012'),'device fps 13.0->26.0')
# encoder: frm_rate_o 12->25, GOP 25->50 (still one keyframe every 2 s)
code(0x6018e+V,h('4ff00c03'),h('4ff01903'),'main stream frm_rate_o 12->25')
code(0x5fe5e+V,h('4ff00c03'),h('4ff01903'),'sub stream frm_rate_o 12->25')
code(0x60184+V,h('1920'),h('3220'),'main stream GOP 25->50')
code(0x5fe56+V,h('4ff0190c'),h('4ff0320c'),'sub stream GOP 25->50')
# 6) 720p main stream VBR cap 1024 -> 2048 kbps (keep bits per frame at 25 FPS)
code(0x601a2+V,h('4ff48063'),h('4ff40063'),'main stream max_bit_rate 0x400->0x800')
# 7) 25 FPS in day mode only. df_isp_daynight: night/white-light always take the second branch, 12.5 there
code(0x5f83e+V,h('44d0'),h('44e0'),'night/white light: beq -> b second branch')
code(0x5f8ca+V,h('b2ee000a'),h('b2ee090a'),'second branch df_set_fps 8.0->12.5')
# 8) Real night branch: (param & ~2)==0 jumps to the 0x2c branch regardless of sensor, df_set_fps(8.0) there
code(0x5f748+V,h('b2ee000a'),h('b2ee090a'),'night df_set_fps 8.0->12.5 (sensor already runs 12.5)')
# 9) Never start the Tange cloud SDK: nop its call in main; main falls into its pause() loop.
#    The SDK's entry function is dead code from now on; the routines of 11, 13 and 17 live in it.
code(A['tange_call'][0],h(A['tange_call'][1]),h('aff30080'),'main: bl tange -> nop.w, Tange disabled')
T=A['tange']; BIND_LO=T; REQ_GATE=T+0x17c; DAY_PROBE=T+0x27c
# 10) Unauthenticated LAN surfaces (early return: movs r0,#0 ; bx lr)
code(A['do_update'],h('f0b50022'),h('00207047'),'do_update LAN firmware receiver disabled')
# 11) Route every bind() through bind_lo (see asm/bind_lo.S).
code(BIND_LO,h(A['tange_old'][0]),h('0b88022b0ed14b8863b142f6022c634508d044f2004c634504d040f27f03c0f200134b6000b5')
     +blx(BIND_LO+38,A['plt_bind'])+h('00bd'),'bind_lo routine')
for a in A['binds']:
    assert a!=A['rtp_bind']  # RTP/UDP send socket: never receives, must stay on the LAN
    code(a,blx(a,A['plt_bind']),bl(a,BIND_LO),'bind -> bind_lo')
# 12) SO_SNDBUF was applied to the wrong socket (fdCtp); apply it to the RTSP client socket
code(A['sndbuf'],h('d3f84c03'),h('286a00bf'),'RTSP SO_SNDBUF on the right socket')
# 13) Accept non-RTSP requests on 554 only from loopback (see asm/req_gate.S). Closes the CTP/HTTP command path over 554.
code(REQ_GATE,h(A['tange_old'][1]),h('70b586b004460d46')+bl(REQ_GATE+8,A['rdreq'])+h('064601281cdb')
     +movw(3,A['rdreq_buf'])+movt(3,A['rdreq_buf'])+h('1b78242b15d0ab6813b11b78522b10d0102304932046694604aa')
     +blx(REQ_GATE+52,A['plt_getpeername'])+h('002806d1019b40f27f02c0f20012934200d00026304606b070bd'),'req_gate routine')
for a in A['rdreq_calls']:
    code(a,bl(a,A['rdreq']),bl(a,REQ_GATE),'request read -> req_gate')
# 14) get_venc_stream: drop the per-frame 300 KiB memset; the copy below only uses len bytes (CPU)
code(A['venc_memset'][0],h(A['venc_memset'][1]),h('aff30080'),'get_venc_stream memset -> nop.w')
# 15) Vertical tracking: support_ymv defaults to 1, which drops dy. Nothing on this camera sets it (no defalut.config).
code(A['ymv'],h('0121'),h('0021'),'support_ymv default 1->0')
# 16) /home/reboot.time is rewritten every 10 min on a cloud-registered camera (flash). Keep the timer, drop the write.
#     The write on reboot and start.sh's write after p2pcam exits stay, so a warm reboot keeps the time.
for (va,o),why in zip(A['reboot_time'],('reboot.time 10 min timer write -> nop.w','reboot.time first write -> nop.w')):
    code(va,h(o),h('aff30080'),why)
# 17) Switch for the hourly forced-day probe (asm/day_probe.S).
#     Flag: hwcfg field only_wireless_bind, filled by the loader and read by nothing else.
code(DAY_PROBE,h(A['tange_old'][2]),h('fb68043b')+movw(2,A['noprobe_flag'])+movt(2,A['noprobe_flag'])+h('126802b17f237047'),'day_probe stub')
code(A['probe_call'],h('fb68043b'),bl(A['probe_call'],DAY_PROBE),'isImageNeedForceToDay hour check -> bl day_probe')
# 18) Sub stream (640x360) VBR cap 384 -> 768 kbps
code(0x5fe6e+V,h('4ff4c073'),h('4ff44073'),'sub stream max_bit_rate 0x180->0x300')
open(dst,'wb').write(b)
print('sha256',hashlib.sha256(b).hexdigest())
