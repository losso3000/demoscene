        xdef    ___mulsi3

        section '.text',code

___mulsi3:
        move.w  4(sp),d0      ; x0 -> d0
        mulu.w  10(sp),d0     ; x0 * y1
        move.w  6(sp),d1      ; x1 -> d1
        mulu.w  8(sp),d1      ; x1 * y0
        add.w   d1,d0
        swap    d0
        clr.w   d0
        move.w  6(sp),d1      ; x1 -> d1
        mulu.w  10(sp),d1     ; x1 * y1
        add.l   d1,d0
        rts

; vim: ft=asm68k:ts=8:sw=8
