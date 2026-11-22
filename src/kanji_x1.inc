; kanji_x1.inc - X1 kf_draw2: draw kf_gbuf (8x8) at 2x into
; X1 GVRAM (block-offset ports; see the M3/M4 notes).

; kf_draw2: draw kf_gbuf (8x8) at 2x at cell (kf_cell).
;   Occupies cells (kf_cell), +1, +40, +41 (blanked, attr 07).
;   GVRAM port for block offset j of cell c is (j << 11) + c,
;   j = 8..15 blue rows, 16..23 red, 24..31 green.
kf_draw2:
        xor     a
        ld      (kf_dy), a
kd2_y:
        xor     a
        ld      (kf_dx), a
kd2_x:
        ld      hl, (kf_cell)           ; cell = base + dy*40 + dx
        ld      a, (kf_dy)
        or      a
        jr      z, kd2_c1
        ld      de, 40
        add     hl, de
kd2_c1:
        ld      a, (kf_dx)
        ld      e, a
        ld      d, 0
        add     hl, de
        ld      (kf_cc), hl
        ld      a, ' '                  ; blank text cell on top
        call    wr_chr2
        ld      a, 07h
        call    wr_att2
        ld      a, 8                    ; first plane offset (B)
        ld      (kf_j0), a
kd2_pl:
        xor     a
        ld      (kf_r), a
kd2_rl:
        ld      a, (kf_dy)              ; source row = dy*4 + r/2
        add     a, a
        add     a, a
        ld      c, a
        ld      a, (kf_r)
        srl     a
        add     a, c
        ld      e, a
        ld      d, 0
        ld      hl, kf_gbuf
        add     hl, de
        ld      a, (hl)
        call    kf_xpand1
        ld      a, (kf_dx)
        or      a
        jr      z, kd2_b0
        ld      a, (kf_xlo)
        jr      kd2_out
kd2_b0:
        ld      a, (kf_xhi)
kd2_out:
        ld      (kf_byte), a
        ld      a, (kf_j0)              ; port = ((j0+r) << 11) + cell
        ld      hl, kf_r
        add     a, (hl)                 ; j
        sla     a
        sla     a
        sla     a                       ; j * 8
        ld      hl, (kf_cc)
        ld      c, l
        ld      b, a
        ld      a, h
        and     07h
        or      b
        ld      b, a
        ld      a, (kf_byte)
        out     (c), a
        ld      a, (kf_r)
        inc     a
        ld      (kf_r), a
        cp      8
        jr      nz, kd2_rl
        ld      a, (kf_j0)
        add     a, 8                    ; next plane (R, then G)
        ld      (kf_j0), a
        cp      32
        jr      nz, kd2_pl
        ld      a, (kf_dx)
        inc     a
        ld      (kf_dx), a
        cp      2
        jp      nz, kd2_x
        ld      a, (kf_dy)
        inc     a
        ld      (kf_dy), a
        cp      2
        jp      nz, kd2_y
        ret
