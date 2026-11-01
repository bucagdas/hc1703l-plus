@ HC1703L Plus - wrapper for FUN_00055070 (read/parse request). MIT License.
@ Non-RTSP requests (CTP, HTTP, SOAP) on port 554 are accepted only from 127.0.0.1.
@ Interleaved RTP/RTCP packets start with '$'; their version pointer is invalid, so they pass.
@ Returning 0 makes the caller close the connection through its normal path.
        .syntax unified
        .thumb
        .cpu cortex-a7
        .text
        .global req_gate
        .thumb_func
req_gate:
        push    {r4, r5, r6, lr}
        sub     sp, #24
        mov     r4, r0
        mov     r5, r1
        .word   0xaaaaaaaa              @ bl 0x55070
        mov     r6, r0
        cmp     r0, #1
        blt     2f
        movw    r3, #0xe050
        movt    r3, #0x003a
        ldrb    r3, [r3]
        cmp     r3, #0x24
        beq     2f
        ldr     r3, [r5, #8]
        cbz     r3, 1f
        ldrb    r3, [r3]
        cmp     r3, #0x52               @ 'R' -> RTSP/
        beq     2f
1:      movs    r3, #16
        str     r3, [sp, #16]
        mov     r0, r4
        mov     r1, sp
        add     r2, sp, #16
        .word   0xbbbbbbbb              @ blx getpeername
        cmp     r0, #0
        bne     3f
        ldr     r3, [sp, #4]
        movw    r2, #0x007f
        movt    r2, #0x0100
        cmp     r3, r2
        beq     2f
3:      movs    r6, #0
2:      mov     r0, r6
        add     sp, #24
        pop     {r4, r5, r6, pc}
