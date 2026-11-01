#!/usr/bin/env python3
# HC1703L Plus - p2pcam patcher. MIT License, see ../LICENSE.
# Builds a patched p2pcam from YOUR OWN stock binary (sha256 5e2dc1a15734e214...).
# Every patch verifies the original bytes first and aborts on any mismatch.
# Usage: python3 -I patch_p2pcam.py <stock p2pcam> <output>
import sys,hashlib
src,dst=sys.argv[1],sys.argv[2]
b=bytearray(open(src,'rb').read())
assert hashlib.sha256(b).hexdigest().startswith('5e2dc1a15734e214'),'not the supported stock p2pcam (sha256 must start with 5e2dc1a15734e214)'
OFF=0x10000
def code(va,old,new,why):
    o=va-OFF; assert bytes(b[o:o+len(old)])==old,(hex(va),bytes(b[o:o+len(old)]).hex()); assert len(old)==len(new)
    b[o:o+len(old)]=new; print('CODE',hex(va),old.hex(),'->',new.hex(),why)
def st(va,old,new,why):
    o=va-OFF; ob=old.encode()+b'\0'; assert bytes(b[o:o+len(ob)])==ob,(hex(va),old,bytes(b[o:o+len(ob)]))
    nb=new.encode(); assert len(nb)<=len(old)
    b[o:o+len(ob)]=nb+b'\0'*(len(ob)-len(nb)); print('STR ',hex(va),repr(old),'->',repr(new),why)
L='127.0.0.1'
# 1) Native 720p: stop forcing support_chazhi=1 (the sensor is 1280x720; stock upscales to a fake 1080p)
code(0x1a794,bytes.fromhex('c4f85493'),bytes.fromhex('aff30080'),'str.w sb,[r4,#0x354] -> nop.w')
# 2) Cloud endpoints -> loopback
for va,o in ((0x32f04d,'203.107.1.1'),(0x32f05d,'203.107.1.33'),(0x32f06d,'203.107.1.34'),(0x32f07d,'203.107.1.35')): st(va,o,L,'HTTPDNS')
st(0x32ba6c,'http://139.224.31.65/device_log_upload.php','http://127.0.0.1/device_log_upload.php','log upload')
st(0x2306d8,'47.107.191.43',L,'4G speed test')
st(0x32b5ff,'http://ep.tange365.com','http://127.0.0.1','cloud API')
st(0x32b5e3,'http://ep.test.tange365.com','http://127.0.0.1','cloud API (test)')
st(0x32a89d,'fidistr.tange365.com','localhost','distribution')
st(0x32a8b2,'fidistr-test.tange365.com','localhost','distribution (test)')
st(0x231210,'www.goke-agps.com','localhost','AGPS')
st(0x231250,'http://www.goke-agps.com:7777/brdcGPD.dat','http://127.0.0.1/brdcGPD.dat','AGPS')
# 3) Vendor reverse tunnel: break the cloud command name
st(0x32a263,'teltun','zz_off','teltun command')
# 4) Do not let the cloud/6670 command path start or kill telnetd
st(0x2346c4,'telnetd&','true','cmdServer: start telnet')
st(0x22d67c,'telnetd &','true','start telnet')
st(0x231e80,'killall telnetd','true','kill telnet')
# 5) 25 FPS: every frame-rate site. VFP immediate 12.5 (imm8 0x29) -> 25.0 (imm8 0x39)
code(0x5f840,bytes.fromhex('b2ee090a'),bytes.fromhex('b3ee090a'),'df_isp_daynight df_set_fps 12.5->25')
code(0x5fb16,bytes.fromhex('b2ee098b'),bytes.fromhex('b3ee098b'),'video init d8 12.5->25')
code(0x5fba0,bytes.fromhex('f2ee097a'),bytes.fromhex('f3ee097a'),'channel0 fps 12.5->25')
code(0x5fc96,bytes.fromhex('f2ee097a'),bytes.fromhex('f3ee097a'),'window fps 12.5->25')
code(0x600b2,bytes.fromhex('f2ee097a'),bytes.fromhex('f3ee097a'),'channel1 fps 12.5->25')
code(0x5fa30,bytes.fromhex('c4f25012'),bytes.fromhex('c4f2d012'),'device fps 13.0->26.0')
# encoder: frm_rate_o 12->25, GOP 25->50 (still one keyframe every 2 s)
code(0x6018e,bytes.fromhex('4ff00c03'),bytes.fromhex('4ff01903'),'main stream frm_rate_o 12->25')
code(0x5fe5e,bytes.fromhex('4ff00c03'),bytes.fromhex('4ff01903'),'sub stream frm_rate_o 12->25')
code(0x60184,bytes.fromhex('1920'),bytes.fromhex('3220'),'main stream GOP 25->50')
code(0x5fe56,bytes.fromhex('4ff0190c'),bytes.fromhex('4ff0320c'),'sub stream GOP 25->50')
# 6) 720p main stream VBR cap 1024 -> 2048 kbps (keep bits per frame at 25 FPS)
code(0x601a2,bytes.fromhex('4ff48063'),bytes.fromhex('4ff40063'),'main stream max_bit_rate 0x400->0x800')
# 7) 25 FPS in day mode only. df_isp_daynight: night/white-light always take the second branch, 12.5 there
code(0x5f83e,bytes.fromhex('44d0'),bytes.fromhex('44e0'),'night/white light: beq -> b second branch')
code(0x5f8ca,bytes.fromhex('b2ee000a'),bytes.fromhex('b2ee090a'),'second branch df_set_fps 8.0->12.5')
# 8) Real night branch: (param & ~2)==0 jumps to the 0x2c branch regardless of sensor, df_set_fps(8.0) there
code(0x5f748,bytes.fromhex('b2ee000a'),bytes.fromhex('b2ee090a'),'night df_set_fps 8.0->12.5 (sensor already runs 12.5)')
# 9) Never start the Tange cloud SDK: nop the 3rd call in main; main falls into its pause() loop
code(0x16be4,bytes.fromhex('fcf04eff'),bytes.fromhex('aff30080'),'main: bl 0x113a84 -> nop.w, Tange disabled')
# 10) Unauthenticated LAN surfaces (early return: movs r0,#0 ; bx lr)
RET0=bytes.fromhex('00207047')
code(0x583ac,bytes.fromhex('f0b50022'),RET0,'do_update LAN firmware receiver disabled')
# 11) Route every bind() through bind_lo (see asm/bind_lo.S). Lives in the now-dead Tange code area.
code(0x113a84,bytes.fromhex('4ef250510f202de9f041bab0c0f2330103f700e80520b7f7adfa01e002f786e91ef74afa03460120002bf7d0'),bytes.fromhex('0b88022b0ed14b8863b142f6022c634508d044f2004c634504d040f27f03c0f200134b6000b502f756ee00bd'),'bind_lo routine')
for a,o,n in [(260692, 'd6f780ee', 'd4f016f8'), (261168, 'd6f792ed', 'd3f028ff'), (262130, 'd6f7b2eb', 'd3f047fd'), (341138, 'c3f762e9', 'c0f0f7fa'), (351288, 'c0f78eed', 'bdf024ff'), (354586, 'bff71eef', 'bdf0b3f8'), (357830, 'bff7c8e8', 'bcf05dfa'), (358402, 'bef7aaef', 'bcf03ff9'), (361452, 'bef7b4e9', 'bbf04afb'), (822730, '4df7c6ec', '4af05bfe'), (948086, '2ef7f0ef', '2cf085f9'), (972430, '29f764e8', '26f0f9f9'), (979564, '27f774ea', '24f00afc'), (979592, '27f766ea', '24f0fcfb'), (985896, '25f716ee', '22f0acff'), (987186, '25f792eb', '22f027fd'), (998680, '22f71eed', '1ff0b4fe'), (998714, '22f70eed', '1ff0a3fe'), (1002448, '21f7c2ed', '1ef058ff'), (1002500, '21f7a8ed', '1ef03eff'), (1026732, '1bf754ee', '18f0eaff'), (1026760, '1bf746ee', '18f0dcff'), (1026980, '1bf7d8ed', '18f06eff'), (1028080, '1bf7b2eb', '18f048fd'), (1028122, '1bf79eeb', '18f033fd'), (1035176, '19f7d6ed', '16f06cff'), (1687694, '7af664eb', '77f7f9fc'), (1690364, '79f62cee', '76f7c2ff'), (1690618, '79f6aeed', '76f743ff'), (1690992, '79f6f2ec', '76f788fe'), (1774162, '65f682ea', '62f717fc'), (1796662, '5ff690ee', '5df725f8'), (1796752, '5ff662ee', '5cf7f8ff')]:
    if a==0x53492: continue  # RTP/UDP send socket (FUN_00053454): never receives, must stay on the LAN
    code(a,bytes.fromhex(o),bytes.fromhex(n),'bind -> bind_lo')
# 12) SO_SNDBUF was applied to the wrong socket (fdCtp); apply it to the RTSP client socket
code(0x55a4a,bytes.fromhex('d3f84c03'),bytes.fromhex('286a00bf'),'RTSP SO_SNDBUF on the right socket')
# 13) Accept non-RTSP requests on 554 only from loopback (see asm/req_gate.S). Closes the CTP/HTTP command path over 554.
code(0x113c00,bytes.fromhex('bc50c0f2330002f73ce841f6802149f21070c0f23301c0f22200b4f7cdfffcf7e7f904f7f9f8002840f0968004f7d4f8002800f04a8104f7d1f978b94ef2ec55c0f2330504e002f71ce8012002f7b0e80ef7'),bytes.fromhex('70b586b004460d4641f732fa064601281cdb4ef25003c0f23a031b78242b15d0ab6813b11b78522b10d0102304932046694604aa02f71ce8002806d1019b40f27f02c0f20012934200d00026304606b070bd'),'req_gate routine')
for a,o,n in [(351928, 'fff7daf8', 'bdf0a2fe'), (352388, 'fef7f4ff', 'bdf0bcfd')]:
    code(a,bytes.fromhex(o),bytes.fromhex(n),'request read -> req_gate')
# 14) get_venc_stream: drop the per-frame 300 KiB memset; the copy below only uses len bytes (CPU)
code(0x5ef24,bytes.fromhex('b7f79ce8'),bytes.fromhex('aff30080'),'get_venc_stream memset -> nop.w')
# 15) Vertical tracking: support_ymv defaults to 1, which drops dy. Nothing on this camera sets it (no defalut.config).
code(0x19bae,bytes.fromhex('0121'),bytes.fromhex('0021'),'support_ymv default 1->0')
open(dst,'wb').write(b)
print('sha256',hashlib.sha256(b).hexdigest())
