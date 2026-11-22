; helpers.inc - shell-local parsing/printing helpers
; (common layer).
; skip spaces: hl advanced past ' 's (destroys a)
skip_sp:
        ld      a, (hl)
        cp      ' '
        ret     nz
        inc     hl
        jr      skip_sp

; upcase: a -> A-Z if a-z (destroys f)
upcase:
        cp      'a'
        ret     c
        cp      'z' + 1
        ret     nc
        sub     020h
        ret

; hexval: a = char -> a = 0..15, or carry set if not hex
hexval:
        cp      '0'
        ret     c
        cp      '9' + 1
        jr      nc, hv_letter
        sub     '0'
        ret
hv_letter:
        call    upcase
        cp      'A'
        ret     c
        cp      'F' + 1
        jr      c, hv_ok
        scf
        ret
hv_ok:
        sub     'A' - 10
        ret

; parse_hex: hl = text -> de = value, a = digit count,
;            hl advanced past digits, carry set if no digits.
;            preserves bc.
parse_hex:
        push    bc
        ld      de, 0
        ld      b, 0
ph_loop:
        ld      a, (hl)
        call    hexval
        jr      c, ph_end
        inc     hl
        inc     b
        push    hl
        ld      h, d
        ld      l, e
        add     hl, hl
        add     hl, hl
        add     hl, hl
        add     hl, hl                  ; value * 16
        ld      e, a
        ld      d, 0
        add     hl, de
        ex      de, hl                  ; de = new value
        pop     hl
        jr      ph_loop
ph_end:
        ld      a, b
        or      a
        jr      z, ph_none
        pop     bc
        or      a                       ; clear carry
        ret
ph_none:
        pop     bc
        scf
        ret

; parse_name: hl = text -> NAMEBUF (10 chars, space padded,
; uppercased). hl advanced past the token.
; carry set if the token is empty or longer than 10 chars.
parse_name:
        push    hl
        ld      hl, NAMEBUF
        ld      b, 10
        ld      a, ' '
pn_sp:
        ld      (hl), a
        inc     hl
        djnz    pn_sp
        pop     hl
        ld      de, NAMEBUF
        ld      c, 0                    ; c = char count
pn_lp:
        ld      a, (hl)
        cp      ' '
        jr      z, pn_end
        or      a
        jr      z, pn_end
        ld      a, c
        cp      10
        jr      nc, pn_bad              ; token too long
        ld      a, (hl)
        call    upcase
        ld      (de), a
        inc     hl
        inc     de
        inc     c
        jr      pn_lp
pn_end:
        ld      a, c
        or      a
        jr      z, pn_bad
        or      a                       ; carry = 0
        ret
pn_bad:
        scf
        ret

; parse_tname: hl = text -> TNAMEBUF (13 chars, space padded,
; uppercased). Used by T (load by name) and S/A (save).
;   T form: carry set when no token is present.
;   S/A form: call parse_tname_d, which substitutes the
;   default name TAPEOS when no token is present.
parse_tname:
        push    hl
        ld      hl, TNAMEBUF
        ld      b, 13
        ld      a, ' '
pt_sp:
        ld      (hl), a
        inc     hl
        djnz    pt_sp
        pop     hl
        ld      de, TNAMEBUF
        ld      c, 0
pt_lp:
        ld      a, (hl)
        cp      ' '
        jr      z, pt_end
        or      a
        jr      z, pt_end
        ld      a, c
        cp      13
        jr      nc, pt_bad
        ld      a, (hl)
        call    upcase
        ld      (de), a
        inc     hl
        inc     de
        inc     c
        jr      pt_lp
pt_end:
        ld      a, c
        or      a
        jr      z, pt_none
        or      a
        ret
pt_none:
        scf
        ret
pt_bad:
        scf
        ret

; parse_tname_d: parse_tname with the default name TAPEOS
; when the line has no token. carry set only on a bad token.
parse_tname_d:
        call    parse_tname
        ret     nc
        push    hl
        ld      hl, sv_name
        ld      de, TNAMEBUF
        ld      bc, 6
        ldir
        pop     hl
        or      a
        ret

; prt_name: print NAMEBUF, stopping at the first space.
prt_name:
        ld      hl, NAMEBUF
        ld      b, 10
prn_lp:
        ld      a, (hl)
        cp      ' '
        ret     z
        inc     hl
        push    hl
        push    bc
        call    C_CONOUT
        pop     bc
        pop     hl
        djnz    prn_lp
        ret

