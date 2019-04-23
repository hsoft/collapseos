; mmap
;
; Block device that maps to memory.
;
; *** DEFINES ***
; MMAP_START: Memory address where the mmap begins

; *** VARIABLES ***
MMAP_PTR	.equ	MMAP_RAMSTART
MMAP_RAMEND	.equ	MMAP_PTR+2

; *** CODE ***

mmapInit:
	xor	a
	ld	(MMAP_PTR), a
	ld	(MMAP_PTR+1), a
	ret

; Increase mem pointer by one
_mmapForward:
	ld	hl, (MMAP_PTR)
	inc	hl
	ld	(MMAP_PTR), hl
	ret

; Returns absolute addr of memory pointer in HL.
_mmapAddr:
	push	de
	ld	hl, (MMAP_PTR)
	ld	de, MMAP_START
	add	hl, de
	jr	nc, .end
	; we have carry? out of bounds, set to maximum
	ld	hl, 0xffff
.end:
	pop	de
	ret

; if out of bounds, will continually return the last char
; TODO: add bounds check and return Z accordingly.
mmapGetC:
	push	hl
	call	_mmapAddr
	ld	a, (hl)
	call	_mmapForward
	cp	a	; ensure Z
	pop	hl
	ret

mmapPutC:
	push	hl
	call	_mmapAddr
	ld	(hl), a
	call	_mmapForward
	pop	hl
	ret

mmapSeek:
	cp	1
	jr	z, .forward
	cp	2
	jr	z, .backward
	cp	3
	jr	z, .beginning
	cp	4
	jr	z, .end
	; all other modes are considered absolute
	jr	.set		; for absolute mode, HL is already correct
.forward:
	ld	de, (MMAP_PTR)
	add	hl, de
	jr	nc, .set
	; we have carry? out of bounds, set to maximum
	ld	hl, 0xffff
	jr	.set
.backward:
	; TODO - subtraction are more complicated...
	jr	.set
.beginning:
	ld	hl, 0
	jr	.set
.end:
	ld	hl, 0xffff-MMAP_START
.set:
	ld	(MMAP_PTR), hl
	ret

mmapTell:
	ld	hl, (MMAP_PTR)
	ret
