@ HC1703L Plus - switch for the hourly forced-day probe. MIT License.
@ FUN_000370f8 (isImageNeedForceToDay) forces day mode for the first 10 s of every hour from 04:00 to 16:59.
@ patch_p2pcam.py replaces its "ldr r3,[r7,#0xc] ; subs r3,#4" (r3 = tm_hour - 4) at 0x37156 with "bl day_probe".
@ When the flag is non-zero, r3 is moved out of the window so the probe never fires; /tmp/forceday still works.
@ The flag is the hwcfg field at 0x3a52c4 (key only_wireless_bind): the loader fills it, nothing else reads it.
@ r2 is free at the call site; the caller compares r3 next, so the flags set here do not matter.
        .syntax unified
        .thumb
        .cpu cortex-a7
        .text
        .global day_probe
        .thumb_func
day_probe:
        ldr     r3, [r7, #0xc]
        subs    r3, #4
        movw    r2, #0x52c4
        movt    r2, #0x003a
        ldr     r2, [r2]
        cbz     r2, 1f
        movs    r3, #0x7f
1:      bx      lr
