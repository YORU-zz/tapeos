; gram_x1.inc - X1 GVRAM clear at boot (M3 dash fix).
; ---------------------------------------------------------------
; clear_gram: zero GVRAM bank 0 (the bytes the text/graphics
; mixer reads: offsets 8..31 of every 32-byte cell block).
;
; Why: on the X1 the graphic plane is composited UNDER the text
; plane, so any garbage in GVRAM shines through blank text cells.
; The compatible IPL leaves bytes in GVRAM (in the xmil core its
; text writes partially leak into GVRAM through the one-shot
; GVRAM write latch: PPI port C bit5 falling edge sets
; iocore.s.mode, and following port OUTs are redirected to
; gram2_o until the next IN). The leaked attribute byte (0x07)
; and character byte (0x20) show up as the infamous dash/dot
; pattern on every blank cell. An OS must clear GVRAM at boot;
; that is what this does. Port for block offset j of cell c is
; (j << 11) + c, j = 8..31 (see tools/mkd88 / README M3 notes).
; ---------------------------------------------------------------
clear_gram:
        ld      hl, 0                   ; cell index 0..2047
cg_cell:
        ld      a, 8                    ; j = 8..31
cg_j:
        push    af
        push    hl
        sla     a
        sla     a
        sla     a                       ; a = j * 8
        ld      b, a
        ld      a, h
        and     07h                     ; cell >> 8 (cell < 2048)
        or      b
        ld      b, a                    ; B = port high byte
        ld      c, l                    ; C = port low byte
        xor     a
        out     (c), a
        pop     hl
        pop     af
        inc     a
        cp      32
        jr      nz, cg_j
        inc     hl
        ld      a, h
        cp      08h
        jr      nz, cg_cell
        ret

