; cmd_kanji.inc - K command: Misaki-font Japanese display
; (common layer; glyph fetch via IOCS DREAD, drawing via
; the machine layer's kf_draw2 in kanji_<machine>.inc).

; ---------------------------------------------------------------
; K - Japanese text demo. Glyphs come from the Misaki font
; (8x8 dot, JIS X 0208; Copyright (C) 2002-2021 Num Kadoma,
; free software - see README) stored on FD as the file
; "MISAKI8" (built by tools/mkfont.py, placed by
; tools/putfont.py). Glyphs are fetched on demand through
; IOCS DREAD with a one-sector cache (FNTBUF): the glyph for
; JIS code (ku, ten) sits at file offset
; 16 + ((ku-1)*94 + (ten-1)) * 8, and glyphs never straddle
; a sector (all offsets are 8-aligned, sectors are 256).
; Each glyph is drawn at 2x (16x16 screen pixels = 2x2 text
; cells) into GVRAM under blank text cells; white = all three
; planes (the compositing path of the M3 background fix).
; If the font file is missing or has a bad header, an error
; line is printed instead.
; ---------------------------------------------------------------
cmd_kanji:
        ld      hl, kf_name             ; NAMEBUF <- "MISAKI8"
        ld      de, NAMEBUF
        ld      bc, 10
        ldir
        call    dir_load_fd             ; the font lives on FD
        jp      c, kf_nofont
        call    dir_magic_ok
        jp      c, kf_nofont
        call    find_entry
        jp      c, kf_nofont
        ld      de, 10
        add     hl, de
        ld      e, (hl)
        inc     hl
        ld      d, (hl)                 ; de = font start record
        ld      (kf_start), de
        ld      hl, FNTBUF              ; read the header sector
        call    C_DREAD
        jp      c, kf_nofont
        ld      hl, (kf_start)
        ld      (kf_rec), hl            ; cache now holds it
        ld      hl, kf_magic            ; must start with "MSK8"
        ld      de, FNTBUF
        ld      b, 4
kf_mg:
        ld      a, (de)
        cp      (hl)
        jp      nz, kf_nofont
        inc     hl
        inc     de
        djnz    kf_mg
        ld      hl, kf_sig              ; success marker (harness)
        ld      de, KFSIG
        ld      bc, 7
        ldir
        ld      hl, kf_str1             ; headline at row 14
        ld      de, 14 * 40 + 2
        call    kf_drawstr
        ld      hl, kf_str2             ; greeting at row 18
        ld      de, 18 * 40 + 2
        call    kf_drawstr
        ld      hl, msg_kanji
        call    C_PRTMSG
        jp      shell_loop
kf_nofont:
        ld      hl, msg_kferr
        call    C_PRTMSG
        jp      shell_loop

; kf_drawstr: draw a JIS-code string at 2x.
;   hl = (ku, ten) byte pairs, terminated by a 0 byte
;   de = top-left text cell of the first glyph
kf_drawstr:
        ld      (kf_cell), de
        ld      (kf_strp), hl
kds_lp:
        ld      hl, (kf_strp)
        ld      a, (hl)
        or      a
        ret     z
        ld      (kf_ku), a
        inc     hl
        ld      a, (hl)
        ld      (kf_ten), a
        inc     hl
        ld      (kf_strp), hl
        call    kf_fetch                ; glyph -> kf_gbuf
        jp      c, err_exit
        call    kf_draw2
        ld      hl, (kf_cell)
        inc     hl
        IF      DEFINED KF_ADV2
        inc     hl                      ; 2x targets: 2 cells right
        ENDIF
        ld      (kf_cell), hl
        jr      kds_lp

; kf_fetch: load the glyph for (kf_ku, kf_ten) into kf_gbuf.
;   carry set on disk error.
;   index = (ku-1)*94 + (ten-1); a sector holds 32 glyphs, so
;   record = start + index/32 (+1 if 16 + (index%32)*8 >= 256)
;   and the in-sector offset is (16 + (index%32)*8) mod 256.
kf_fetch:
        ld      a, (kf_ku)
        dec     a
        ld      e, a
        ld      d, 0                    ; de = ku - 1
        ld      hl, 0
        ld      b, 94
kff_m94:
        add     hl, de
        djnz    kff_m94                 ; hl = (ku-1) * 94
        ld      a, (kf_ten)
        dec     a
        ld      e, a
        ld      d, 0
        add     hl, de                  ; hl = glyph index
        ld      a, l
        and     01fh
        ld      (kf_rem), a             ; index mod 32
        ld      b, 5
kff_sh:
        srl     h
        rr      l
        djnz    kff_sh                  ; hl = index / 32
        ld      a, (kf_rem)
        add     a, a
        add     a, a
        add     a, a                    ; (index mod 32) * 8
        add     a, 16                   ; + header size
        ld      (kf_off), a             ; in-sector offset
        jr      nc, kff_nc
        inc     hl                      ; header pushed it over
kff_nc:
        ld      de, (kf_start)
        add     hl, de                  ; hl = record to read
        ld      de, (kf_rec)
        or      a
        sbc     hl, de
        jr      z, kff_copy             ; sector cache hit
        add     hl, de                  ; hl = record again
        ld      (kf_rec), hl
        ex      de, hl                  ; de = record
        ld      hl, FNTBUF
        call    C_DREAD
        jr      nc, kff_copy
        ld      hl, 0ffffh              ; read failed: invalidate
        ld      (kf_rec), hl
        scf
        ret
kff_copy:
        ld      a, (kf_off)
        ld      e, a
        ld      d, 0
        ld      hl, FNTBUF
        add     hl, de
        ld      de, kf_gbuf
        ld      bc, 8
        ldir
        or      a                       ; carry clear
        ret

; kf_xpand1: expand one glyph row byte to 2x width.
;   a = source byte -> (kf_xhi) = left half, (kf_xlo) = right
;   (nibble table: every bit doubled, MSB = leftmost pixel)
kf_xpand1:
        push    hl
        push    af
        and     0f0h
        rrca
        rrca
        rrca
        rrca                            ; a = high nibble
        ld      hl, kf_xtab
        add     a, l
        ld      l, a
        jr      nc, kfx_1
        inc     h
kfx_1:
        ld      a, (hl)
        ld      (kf_xhi), a
        pop     af
        and     00fh                    ; a = low nibble
        ld      hl, kf_xtab
        add     a, l
        ld      l, a
        jr      nc, kfx_2
        inc     h
kfx_2:
        ld      a, (hl)
        ld      (kf_xlo), a
        pop     hl
        ret
