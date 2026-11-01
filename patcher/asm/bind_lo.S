@ HC1703L Plus - bind() wrapper. MIT License. Binds AF_INET sockets to 127.0.0.1.
@ Exceptions: port 0 (outgoing), 554 (RTSP), 68 (DHCP). AF_UNIX is left alone.
@ Deliberately closes vendor services to the LAN; their threads keep running.
        .syntax unified
        .thumb
        .cpu cortex-a7
        .text
        .global bind_lo
        .thumb_func
bind_lo:
        ldrh    r3, [r1]
        cmp     r3, #2
        bne     1f
        ldrh    r3, [r1, #2]
        cbz     r3, 1f
        movw    ip, #0x2a02
        cmp     r3, ip
        beq     1f
        movw    ip, #0x4400
        cmp     r3, ip
        beq     1f
        movw    r3, #0x007f
        movt    r3, #0x0100
        str     r3, [r1, #4]
1:      push    {lr}
        .word   0xdeadbeef              @ placeholder: blx bind@plt (0x16758), filled in by patch_p2pcam.py
        pop     {pc}
