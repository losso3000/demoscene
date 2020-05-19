; vim: ft=asm68k:ts=8:sw=8:

ROTZOOM_W			= 24
ROTZOOM_H			= 24
COPPER_HALFROW_INSTRUCTIONS	= ROTZOOM_W/2+2
SINGLEROW_SIZE			= COPPER_HALFROW_INSTRUCTIONS*4

	xdef _PlotTextureAsm

	section	'.text',code

; -----------------------------------------------------------------------------
; _PlotTextureAsm(copperDst, texture, uv, uvDeltaRow, uvDeltaCol)
;                 a0         a1       d5  d6          d1
; -----------------------------------------------------------------------------

_PlotTextureAsm:
	movem.l	d2-d7/a2-a6,-(a7)
	pea	restore
	add.w	#4+2,a0		; first color write data word
	moveq	#9,d2		; shift amount
	include	data/textureloop-generated.s
	rts

restore:
	movem.l	(a7)+,d2-d7/a2-a6
	rts