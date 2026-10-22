; tthooks.s - the launch and switch hooks tasktime.c gives every task.
;
; Exec calls tc_Launch when a task gets the CPU and tc_Switch when it
; loses it.  The C side (tt_launch, tt_switch) times the slice and returns
; the hook the task had before, or 0; that one is then called with all
; registers as exec left them.

	xdef	_tt_launch_stub
	xdef	_tt_switch_stub
	xref	_tt_launch
	xref	_tt_switch

	section	code,code

_tt_launch_stub:
	subq.l	#4,sp			; room for the earlier hook
	movem.l	d0-d7/a0-a6,-(sp)
	jsr	_tt_launch
	move.l	d0,60(sp)
	movem.l	(sp)+,d0-d7/a0-a6
	tst.l	(sp)
	beq.s	.none
	rts				; "returns" into the earlier hook
.none:	addq.l	#4,sp
	rts

_tt_switch_stub:
	subq.l	#4,sp
	movem.l	d0-d7/a0-a6,-(sp)
	jsr	_tt_switch
	move.l	d0,60(sp)
	movem.l	(sp)+,d0-d7/a0-a6
	tst.l	(sp)
	beq.s	.none2
	rts
.none2:	addq.l	#4,sp
	rts

	end
