; scroll.inc - cursor offset + screen scroll (common
; layer, built on the machine display primitives).
; cur_offset: -> hl = cur_y * 40 + cur_x (destroys a, de)
cur_offset:
        ld      a, (cur_y)
        ld      l, a
        ld      h, 0
        add     hl, hl
        add     hl, hl
        add     hl, hl                  ; y * 8
        ld      e, l
        ld      d, h
        add     hl, hl
        add     hl, hl                  ; y * 32
        add     hl, de                  ; y * 40
        ld      a, (cur_x)
        ld      e, a
        ld      d, 0
        add     hl, de
        ret

; scroll_up: shift the whole screen one line up, clear line 24
scroll_up:
        ld      hl, 40
        ld      (sc_src), hl
        ld      hl, 0
        ld      (sc_dst), hl
su_loop:
        ld      hl, (sc_src)
        call    rd_chr
        ld      (sc_tc), a
        ld      hl, (sc_src)
        call    rd_att
        ld      (sc_ta), a
        ld      hl, (sc_dst)
        ld      a, (sc_tc)
        call    wr_chr2
        ld      hl, (sc_dst)
        ld      a, (sc_ta)
        call    wr_att2
        ld      hl, (sc_src)
        inc     hl
        ld      (sc_src), hl
        ld      hl, (sc_dst)
        inc     hl
        ld      (sc_dst), hl
        ld      a, h
        cp      03h
        jr      nz, su_loop
        ld      a, l
        cp      0c0h                    ; dst == 960 -> done
        jr      nz, su_loop
        ld      hl, 960
su_clr:
        push    hl
        ld      a, SPACE_CODE           ; raw display code of blank
        call    wr_chr2
        pop     hl
        push    hl
        ld      a, 07h
        call    wr_att2
        pop     hl
        inc     hl
        ld      a, h
        cp      03h
        jr      nz, su_clr
        ld      a, l
        cp      0e8h                    ; offset == 1000 -> done
        jr      nz, su_clr
        ret

