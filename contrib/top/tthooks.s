; tthooks.s - the vertical blank interrupt server that tasktime.c adds.
;
; 50 or 60 times a second it calls tt_tick() (C), which notes the task
; that is running.  A server of the VERTB chain has to return with Z set
; (d0 = 0) so that the servers after it run too.

	xdef	_tt_vblank
	xref	_tt_tick

	section	code,code

_tt_vblank:
	movem.l	d2-d7/a2-a6,-(sp)
	jsr	_tt_tick
	movem.l	(sp)+,d2-d7/a2-a6
	moveq	#0,d0
	rts

	end
