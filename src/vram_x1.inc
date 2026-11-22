; vram_x1.inc - X1 text VRAM cell access primitives.
; rd_chr/rd_att take a cell offset in hl; wr_chr2/wr_att2
; store the raw character/attribute code in a.
; ---------------------------------------------------------------
; Text VRAM cell access (offset in hl, 0..2047)
;   rd_chr / rd_att : -> a
;   wr_chr2 / wr_att2 : a = value
;   all destroy bc only (a as documented)
; ---------------------------------------------------------------
rd_chr:
        ld      c, l
        ld      a, h
        add     a, 030h
        ld      b, a
        in      a, (c)
        ret

rd_att:
        ld      c, l
        ld      a, h
        add     a, 020h
        ld      b, a
        in      a, (c)
        ret

wr_chr2:
        push    af
        ld      c, l
        ld      a, h
        add     a, 030h
        ld      b, a
        pop     af
        out     (c), a
        ret

wr_att2:
        push    af
        ld      c, l
        ld      a, h
        add     a, 020h
        ld      b, a
        pop     af
        out     (c), a
        ret

; wr_cell: a = char, hl = offset; writes char + (cur_attr)
wr_cell:
        call    wr_chr2
        ld      a, (cur_attr)
        call    wr_att2
        ret

