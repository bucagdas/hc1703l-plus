@ HC1703L Plus - longer pre-recording for motion events. MIT License.
@ The stock SD recorder (libmyRecord, FUN_001d6840 in 57.0.8.0) keeps the newest frames in a ring buffer. When a
@ recording starts it scans the buffer once and starts at the NEWEST keyframe, or for an event at the one before it
@ if that is less than 5 s older. With a keyframe every 2 s that is only 1-4 s before the event.
@ This block replaces the selection after that scan (0x1d6c08-0x1d6c5d, 86 bytes): for an event it starts at the
@ OLDEST keyframe that is at most PRE ms older than the newest frame. Other recordings keep the newest keyframe.
@ patch_p2pcam.py also raises the ring buffer from 1 MiB to 4 MiB so that it holds PRE ms at full bit rate.
@ Locals of the stock function (r7 frame): [r7,#4] channel, [r7,#0x30] ring, [r7,#0x48] newest frame's ms,
@ [r7,#0x4c] newest keyframe, [r7,#0x54] chosen frame. A frame header: +4 type, +8 ms, +0xc data.
@ Channel state: CH + channel*0xe0, +0x1c event number (0 = no event), +0xc flags (bit 3 = keep newest).
        .syntax unified
        .thumb
        .cpu cortex-a7
        .text
        .equ    PRE, 10000
        .global prerec
        .thumb_func
prerec:
        ldr     r3, [r7, #0x4c]
        str     r3, [r7, #0x54]          @ default: newest keyframe, as stock
        movw    r3, #0x1111              @ placeholder: CH low half
        movt    r3, #0x2222              @ placeholder: CH high half
        ldr     r2, [r7, #4]
        movs    r1, #0xe0
        mla     r3, r1, r2, r3
        ldr     r2, [r3, #0x1c]
        cbz     r2, 9f                   @ not an event
        ldr     r2, [r3, #0xc]
        lsls    r2, r2, #28
        bmi     9f                       @ flag 8: stock keeps the newest keyframe too
        ldr     r0, [r7, #0x30]
        .word   0xcccccccc               @ placeholder: bl ring_first
1:      cbz     r0, 9f
        ldr     r3, [r0, #0xc]
        cbz     r3, 2f
        ldr     r3, [r0, #4]
        cmp     r3, #1
        beq     3f
        subs    r3, #3
        cmp     r3, #10
        bhi     2f
3:      ldr     r3, [r0, #8]
        ldr     r2, [r7, #0x48]
        subs    r2, r2, r3
        movw    r1, #PRE
        cmp     r2, r1
        bls     4f                       @ oldest keyframe inside the window
2:      mov     r1, r0
        ldr     r0, [r7, #0x30]
        .word   0xdddddddd               @ placeholder: bl ring_next
        b       1b
4:      str     r0, [r7, #0x54]
9:      .word   0xeeeeeeee               @ placeholder: b back to the stock code after the old block
