; disasm_util.inc - DISASM shared utility layer: line input,
; decimal print, hex/range parsing, and the TOSD directory
; layer (dir_load / find_entry / find_free). Shared by the
; standalone DISASM (disasm/src/disasm.z80) and the EDAS
; disassembler module EDIS (tapeos/src/edis.z80) - roadmap
; item 60. Expects the including file to have defined the
; C_* IOCS entries, DIRBUF / INBUF and the E_* error codes,
; and to provide: err, and the variables find_name, dr_lo,
; dr_hi, pd_buf, pd_ptr, pd_val, txtptr (defined in
; disasm_core.inc).

read_line:
        ld      hl, INBUF
        ld      b, 40
        call    C_GETLIN
        ret

; print_dec: print HL as unsigned decimal.
print_dec:
        ld      (pd_val), hl
        ld      hl, pd_buf + 6
        ld      (pd_ptr), hl
        xor     a
        ld      (hl), a
pd_lp:
        ld      hl, (pd_val)
        call    pd_div
        ld      (pd_val), hl
        add     a, '0'
        ld      hl, (pd_ptr)
        dec     hl
        ld      (pd_ptr), hl
        ld      (hl), a
        ld      hl, (pd_val)
        ld      a, h
        or      l
        jr      nz, pd_lp
        ld      hl, (pd_ptr)
        jp      C_PRTMSG
; pd_div: HL = HL / 10 (unsigned), A = remainder.
pd_div:
        ld      bc, 10
        ld      de, 0
        ld      a, 16
pdv_lp:
        add     hl, hl
        ex      de, hl
        adc     hl, hl
        ex      de, hl
        push    hl
        ld      h, d
        ld      l, e
        or      a
        sbc     hl, bc
        jr      c, pdv_skip
        ld      d, h
        ld      e, l
        pop     hl
        inc     hl
        push    hl
pdv_skip:
        pop     hl
        dec     a
        jr      nz, pdv_lp
        ld      a, e
        ret

; ---------------------------------------------------------------
; Input parsing. txtptr walks INBUF.
; ---------------------------------------------------------------

; parse_hex_line: parse the hex number at the start of
; INBUF (digits, optional trailing H). -> HL = value.
; Errors jump to err with E_NUM.
parse_hex_line:
        ld      hl, INBUF
        ld      (txtptr), hl
        call    skip_sp
        call    parse_hex
        jp      c, err_num
        ret
err_num:
        ld      a, E_NUM
        jp      err

; parse_range_line: "XXXX-YYYY" (or a single "XXXX")
; from INBUF -> (dr_lo), (dr_hi). Errors -> err.
parse_range_line:
        ld      hl, INBUF
        ld      (txtptr), hl
        call    skip_sp
        call    parse_hex
        jp      c, err_num
        ld      (dr_lo), hl
        ld      (dr_hi), hl
        call    skip_sp
        ld      hl, (txtptr)
        ld      a, (hl)
        cp      '-'
        ret     nz
        inc     hl
        ld      (txtptr), hl
        call    skip_sp
        call    parse_hex
        jp      c, err_num
        ld      (dr_hi), hl
        ld      de, (dr_lo)
        or      a
        sbc     hl, de
        ret     nc
        ld      a, E_RANGE
        jp      err

; skip_sp: advance txtptr past spaces.
skip_sp:
        ld      hl, (txtptr)
ss_lp:
        ld      a, (hl)
        cp      ' '
        jr      nz, ss_dn
        inc     hl
        jr      ss_lp
ss_dn:
        ld      (txtptr), hl
        ret

; parse_hex: hex digits at txtptr -> HL = value,
; carry set if no digit. txtptr stops at the first
; non-hex character (a trailing 'H' is consumed).
parse_hex:
        ld      hl, (txtptr)
        ld      de, 0                   ; accumulated value
        ld      c, 0                    ; digit count
ph_lp:
        ld      a, (hl)
        call    hex_val
        jr      c, ph_end
        ex      de, hl                  ; HL = value
        add     hl, hl
        add     hl, hl
        add     hl, hl
        add     hl, hl                  ; value *= 16
        or      l
        ld      l, a
        ex      de, hl                  ; DE = value
        inc     hl
        inc     c
        jr      ph_lp
ph_end:
        ld      a, c
        or      a
        jr      z, ph_bad
        ld      a, (hl)
        cp      'H'
        jr      z, ph_h
        cp      'h'
        jr      nz, ph_ok
ph_h:
        inc     hl
ph_ok:
        ld      (txtptr), hl
        ex      de, hl                  ; HL = value
        or      a
        ret
ph_bad:
        scf
        ret

; hex_val: A = char -> A = nibble, or carry set.
hex_val:
        cp      '0'
        jr      c, hv_bad
        cp      '9' + 1
        jr      c, hv_num
        cp      'A'
        jr      c, hv_bad
        cp      'F' + 1
        jr      c, hv_up
        cp      'a'
        jr      c, hv_bad
        cp      'f' + 1
        jr      nc, hv_bad
        sub     'a' - 10
        ret
hv_num:
        sub     '0'
        ret
hv_up:
        sub     'A' - 10
        ret
hv_bad:
        scf
        ret

; ---------------------------------------------------------------
; TOSD file layer (FD, drive 0) - same contracts as
; NEWASM / SCC. Directory: record 32; entries of 16
; bytes from offset 16: name (10, space padded), start
; record (+10), size (+12), load address (+14); next
; free record at +6.
; ---------------------------------------------------------------

; name_copy: copy the GETLIN input at HL into the
; 11-byte name field at DE: upper-case, truncate to
; 10 chars, pad with spaces, NUL after char 10.
name_copy:
        ld      b, 10
nc_lp:
        ld      a, (hl)
        or      a
        jr      z, nc_pad
        cp      'a'
        jr      c, nc_st
        cp      'z' + 1
        jr      nc, nc_st
        sub     020h
nc_st:
        ld      (de), a
        inc     de
        inc     hl
        djnz    nc_lp
        jr      nc_dn
nc_pad:
        ld      a, ' '
nc_pl:
        ld      (de), a
        inc     de
        djnz    nc_pl
nc_dn:
        xor     a
        ld      (de), a
        ret

; dir_load: read the directory sector into DIRBUF.
dir_load:
        ld      de, DIRREC
        ld      hl, DIRBUF
        jp      C_DREAD

; find_entry: (find_name) -> 10-byte name. Returns
; HL = entry pointer in DIRBUF, carry clear; or
; carry set when not found.
find_entry:
        ld      hl, DIRBUF + 16
        ld      b, 15
fe_lp:
        ld      a, (hl)
        or      a
        jr      z, fe_nx                ; skip empty entries
        push    hl
        push    bc
        ld      de, (find_name)
        ld      b, 10
fe_cmp:
        ld      a, (de)
        cp      (hl)
        jr      nz, fe_dif
        inc     hl
        inc     de
        djnz    fe_cmp
        pop     bc
        pop     hl
        or      a
        ret
fe_dif:
        pop     bc
        pop     hl
fe_nx:
        ld      de, 16
        add     hl, de
        djnz    fe_lp
        scf
        ret

; find_free: first empty directory entry -> HL,
; or carry set when the directory is full.
find_free:
        ld      hl, DIRBUF + 16
        ld      b, 15
ff_lp:
        ld      a, (hl)
        or      a
        jr      z, ff_ok
        ld      de, 16
        add     hl, de
        djnz    ff_lp
        scf
        ret
ff_ok:
        or      a
        ret

