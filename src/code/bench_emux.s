/*
 * emux opcodes (https://n64brew.dev/wiki/Emux), used by benchmark mode.
 *
 * Encoded by hand since the assembler doesn't know them:
 *   0x42000000 | rd << 20 | rt << 15 | code << 6 | funct
 * They do nothing on real hardware or emulators without emux, so XDETECT is used to gate the rest.
 */

#include "PR/asm.h"
#include "PR/regdef.h"

.text
.set noreorder

/* s32 Bench_EmuxDetect(void): nonzero if emux is available (XDETECT v0, code 1) */
LEAF(Bench_EmuxDetect)
    move    v0, zero
    .word   0x42200060
    jr      ra
     nop
END(Bench_EmuxDetect)

/* void Bench_XProfStart(u32 slot): XPROF a0, code 1 */
LEAF(Bench_XProfStart)
    .word   0x42400068
    jr      ra
     nop
END(Bench_XProfStart)

/* void Bench_XProfStop(u32 slot): XPROF a0, code 2 */
LEAF(Bench_XProfStop)
    .word   0x424000A8
    jr      ra
     nop
END(Bench_XProfStop)

/* void Bench_XProfClear(u32 slot): XPROF a0, code 3 */
LEAF(Bench_XProfClear)
    .word   0x424000E8
    jr      ra
     nop
END(Bench_XProfClear)

/* u32 Bench_XProfRead(s32 slot, u32 metric): XPROFREAD a0, a1. Returns the low 32 bits. */
LEAF(Bench_XProfRead)
    .word   0x42428029
    jr      ra
     move   v0, a1
END(Bench_XProfRead)

/* void Bench_EmuxExit(void): XIOCTL code 1 */
LEAF(Bench_EmuxExit)
    .word   0x4200006C
    jr      ra
     nop
END(Bench_EmuxExit)
