; tbcore.inc - the shared TinyBASIC front end for TBC
; (compiler) and TBI (JIT interpreter). Included by both
; drivers; see tbc.z80 / tbi.z80 for the program shells.
; OUROBO/ROS reproduction project (P2), roadmap item 6.
;
; In the fiction (ep. 35), the protagonist's TinyBASIC -
; a BASIC with X-BASIC-like syntax - is the first language
; of the new OS: it has a compiler (which emits TOSR
; relocatable binaries) and an interpreter (TBI), and the
; compiler is the first program written in the new OS's
; own assembler. This is that compiler. The language
; definition follows reloc-plan.md section 5.2; X-BASIC
; materials were used as reference only, nothing is
; incorporated (see tinybasic.md).
;
; Architecture (details in tinybasic.md):
;   - The source is streamed from FD once per pass,
;     like NEWASM. Three passes: pass 1 collects global
;     variables and function signatures; pass 2a measures
;     (parses everything, computes frame layouts, code
;     sizes, the literal pool and the used library set);
;     pass 2b emits the identical parse into the output
;     buffer. Every emitted instruction has a size that
;     does not depend on addresses (absolute jumps and
;     calls everywhere), so the two parses agree exactly
;     and no fixup list is needed: the TOSR relocation
;     bitmap is written directly during emission.
;   - Output is a TOSR v1 file, origin 08000h: the
;     stage-1 stub (109 bytes), a small runtime entry,
;     the user functions, the string literal pool, and
;     the library routines the program actually uses
;     (from tblib.z80, linked by the compiler).
;   - Calling convention: the caller pushes 16-bit
;     argument slots in source order (arg1 first) and
;     CALLs; the callee builds an IY frame - saved IY at
;     (IY+0), return address at (IY+2), argN at (IY+4),
;     arg1 at (IY + 4 + 2*(nargs-1)); locals live below
;     IY at negative displacements. The return value is
;     in HL. Globals live in a heap right behind the
;     loaded module, based at IX.
;
; Memory map:
;   04000h..        this program (code + data)
;   06800h          DIRBUF: TOSD directory sector
;   06900h          SECBUF: source sector buffer
;   06a00h..075ffh  BITMAP: TOSR relocation bitmap
;   08000h..        output file image (stub at 08000h,
;                   body at 0806dh), guarded to 0e000h

; ---- IOCS (X1) ----
C_CONOUT        equ     0e406h          ; IOCS 2
C_PRTMSG        equ     0e409h          ; IOCS 3
C_CRLF          equ     0e40ch          ; IOCS 4
C_GETLIN        equ     0e40fh          ; IOCS 5
C_DREAD         equ     0e42dh          ; IOCS 15
C_DWRITE        equ     0e430h          ; IOCS 16

DIRBUF          equ     06800h
SECBUF          equ     06900h
BITMAP          equ     06a00h
; symbol tables live in the tool scratch area (as in
; NEWASM), not in the binary: 07600h..07effh
gtab            equ     07600h          ; GMAX * 32
ftab            equ     07a00h          ; FMAX * 32
ltab            equ     07c00h          ; LMAX * 32
labtab          equ     07e00h          ; LABMAX * 2
ORIGIN          equ     08000h
STUB_LEN        equ     109
DIRREC          equ     32              ; FD directory record
FDMAXREC        equ     1280            ; FD records (320 KB)
STBLK           equ     0e640h          ; status block

; ---- tokens ----
T_EOF           equ     0
T_IDENT         equ     1
T_NUM           equ     2
T_STR           equ     3
T_EQ            equ     0a0h            ; ==
T_NE            equ     0a1h            ; !=
T_LE            equ     0a2h            ; <=
T_GE            equ     0a3h            ; >=
T_AND           equ     0a4h            ; &&
T_OR            equ     0a5h            ; ||
T_SHL           equ     0a6h            ; <<
T_SHR           equ     0a7h            ; >>
K_FUNC          equ     081h
K_ENDFUNC       equ     082h
K_INT           equ     083h
K_CHAR          equ     084h
K_VOID          equ     085h
K_DIM           equ     086h
K_IF            equ     087h
K_THEN          equ     088h
K_ELSE          equ     089h
K_ENDIF         equ     08ah
K_WHILE         equ     08bh
K_ENDWHILE      equ     08ch
K_REPEAT        equ     08dh
K_UNTIL         equ     08eh
K_FOR           equ     08fh
K_TO            equ     090h
K_STEP          equ     091h
K_NEXT          equ     092h
K_BREAK         equ     093h
K_RETURN        equ     094h

; ---- error codes (also in the status block) ----
E_SYNTAX        equ     1
E_UNDEF         equ     2
E_DUP           equ     3
E_ARGS          equ     4
E_STR           equ     5
E_BIG           equ     6
E_FILE          equ     7
E_NOMAIN        equ     8
E_BREAK         equ     9
E_TYPE          equ     10

; ---- table limits ----
GMAX            equ     32              ; globals
FMAX            equ     16              ; functions
LMAX            equ     16              ; locals + params
LABMAX          equ     128             ; labels
FORMAX          equ     8               ; for nesting (frame)

        org     04000h

; ---------------------------------------------------------------
; Entry: the drivers' start code. The shell's G command
; lands here (04000h) and jumps to the driver.
; ---------------------------------------------------------------
        jp      driver_start
core_start:
; err: A = error code. Prints "ERR LINE n En", records the
; status block, and returns to the shell.
err:
        ld      (err_code), a
        ld      hl, STBLK + 3
        ld      (hl), a
        inc     hl
        ld      de, (curline)
        ld      (hl), e
        inc     hl
        ld      (hl), d
        ld      hl, msg_err
        call    C_PRTMSG
        ld      hl, (curline)
        call    print_dec
        ld      a, ' '
        call    C_CONOUT
        ld      a, 'E'
        call    C_CONOUT
        ld      a, (err_code)
        ld      l, a
        ld      h, 0
        call    print_dec
        call    C_CRLF
        ld      sp, (saved_sp)
        ret

; print_dec: print HL as unsigned decimal.
print_dec:
        ld      (pd_val), hl
        ld      hl, pd_buf + 6
        ld      (pd_ptr), hl
        xor     a
        ld      (hl), a                 ; NUL terminator
pd_lp:
        ld      hl, (pd_val)
        call    pd_div                  ; HL = HL/10, A = rem
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
; Clobbers BC, DE.
pd_div:
        ld      bc, 10
        ld      de, 0                   ; remainder in DE
        ld      a, 16
pdv_lp:
        add     hl, hl                  ; dividend bit -> carry
        ex      de, hl
        adc     hl, hl                  ; remainder = rem*2 + carry
        ex      de, hl
        push    hl
        ld      h, d
        ld      l, e
        or      a
        sbc     hl, bc
        jr      c, pdv_skip
        ld      d, h
        ld      e, l                    ; remainder -= 10
        pop     hl
        inc     hl                      ; quotient bit = 1
        push    hl
pdv_skip:
        pop     hl
        dec     a
        jr      nz, pdv_lp
        ld      a, e                    ; remainder (< 10)
        ret

; ---------------------------------------------------------------
; TOSD file layer (FD, drive 0) - same contracts as NEWASM.
; Directory: record 32; entries of 16 bytes from offset 16:
; name (10, space padded), start record (+10), size (+12),
; load address (+14); next free record at +6.
; ---------------------------------------------------------------

; name_copy: copy the GETLIN input at HL into the 11-byte
; name field at DE: upper-case, truncate to 10 chars,
; pad with spaces, NUL after char 10.
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
        inc     hl
        inc     de
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

; find_entry: (find_name) -> 10-byte name. Returns HL =
; entry pointer in DIRBUF and carry clear, or carry set.
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
fe_nf:
        scf
        ret

; ---------------------------------------------------------------
; Streaming source reader. The source file is read record
; by record into SECBUF; passes rewind and re-read.
; ---------------------------------------------------------------
src_rewind:
        ld      hl, 0
        ld      (src_recno), hl
        ld      hl, (src_size)
        ld      (src_left), hl
        ld      a, 255
        ld      (src_off), a            ; force a load on 1st read
        xor     a
        ld      (pb_valid), a
        ld      hl, 1
        ld      (curline), hl
        ret

; rd_raw: next raw byte in A (01ah at EOF). Counts lines.
rd_raw:
        ld      a, (pb_valid)
        or      a
        jr      z, rr_fresh
        xor     a
        ld      (pb_valid), a
        ld      a, (pb_ch)
        ret
rr_fresh:
        ld      hl, (src_left)
        ld      a, h
        or      l
        jr      z, rr_eof
        ld      a, (src_off)
        inc     a
        ld      (src_off), a
        jr      nz, rr_have
        ; load the next record
        push    hl
        ld      hl, (src_rec)
        ld      de, (src_recno)
        add     hl, de
        ex      de, hl
        ld      hl, SECBUF
        call    C_DREAD
        ld      hl, (src_recno)
        inc     hl
        ld      (src_recno), hl
        pop     hl
        xor     a
        ld      (src_off), a
rr_have:
        dec     hl
        ld      (src_left), hl
        ld      a, (src_off)
        ld      l, a
        ld      h, 0
        ld      de, SECBUF
        add     hl, de
        ld      a, (hl)
        cp      00ah
        ret     nz
        push    hl
        ld      hl, (curline)
        inc     hl
        ld      (curline), hl
        pop     hl
        ret
rr_eof:
        ld      a, 01ah
        ret

; rd_unget: push back the byte in A (one level).
rd_unget:
        ld      (pb_ch), a
        ld      a, 1
        ld      (pb_valid), a
        ret

; ---------------------------------------------------------------
; Lexer. lex reads the next token into (tok); numbers set
; (tokval), identifiers set namebuf (upper-cased), strings
; set strbuf and (tokslen). The parser's current token is
; whatever lex produced last. Clobbers all registers.
; ---------------------------------------------------------------
lex:
lx_ws:
        call    rd_raw
        cp      01ah
        jr      z, lx_eof
        cp      ' '
        jr      z, lx_ws
        cp      009h
        jr      z, lx_ws
        cp      00dh
        jr      z, lx_ws
        cp      00ah
        jr      z, lx_ws
        cp      '/'
        jr      nz, lx_tok
        call    rd_raw                  ; maybe a comment
        cp      '*'
        jr      z, lx_comment
        call    rd_unget
        ld      a, '/'
        jp      lx_single
lx_comment:
        call    rd_raw
        cp      01ah
        jr      z, lx_synerr
        cp      '*'
        jr      nz, lx_comment
        call    rd_raw
        cp      '/'
        jr      z, lx_ws
        call    rd_unget
        jr      lx_comment
lx_eof:
        xor     a                       ; T_EOF = 0
        ld      (tok), a
        ret
lx_tok:
        ; letter or '_' -> identifier / keyword
        cp      '_'
        jp      z, lx_ident
        cp      'A'
        jr      c, lx_numchk
        cp      'Z' + 1
        jp      c, lx_ident
        cp      'a'
        jr      c, lx_numchk
        cp      'z' + 1
        jp      c, lx_ident
lx_numchk:
        cp      '0'
        jr      c, lx_strchk
        cp      '9' + 1
        jp      c, lx_number
lx_strchk:
        cp      '"'
        jp      z, lx_string
        ; operators and punctuation
        cp      '='
        jp      z, lx_eq
        cp      '!'
        jp      z, lx_ne
        cp      '<'
        jp      z, lx_lt
        cp      '>'
        jp      z, lx_gt
        cp      '&'
        jp      z, lx_amp
        cp      '|'
        jp      z, lx_bar
        cp      '+'
        jr      z, lx_single
        cp      '-'
        jr      z, lx_single
        cp      '*'
        jr      z, lx_single
        cp      '%'
        jr      z, lx_single
        cp      '^'
        jr      z, lx_single
        cp      '~'
        jr      z, lx_single
        cp      '('
        jr      z, lx_single
        cp      ')'
        jr      z, lx_single
        cp      ','
        jr      z, lx_single
lx_synerr:
        ld      a, E_SYNTAX
        jp      err
lx_single:
        ld      (tok), a
        ret

; lx_eq2: shared two-char operator tail. Entry: A = the
; second char to test, (lx_want) = it, (lx_two) = token if
; matched, (lx_one) = token if not.
lx_eq:
        ld      a, '='
        ld      (lx_want), a
        ld      a, T_EQ
        ld      (lx_two), a
        ld      a, '='
        ld      (lx_one), a
        jr      lx_op2
lx_ne:
        ld      a, '='
        ld      (lx_want), a
        ld      a, T_NE
        ld      (lx_two), a
        ld      a, '!'
        ld      (lx_one), a
        jr      lx_op2
lx_lt:
        call    rd_raw
        cp      '<'
        jr      z, lx_shl
        cp      '='
        jr      z, lx_le
        call    rd_unget
        ld      a, '<'
        jr      lx_single
lx_shl:
        ld      a, T_SHL
        jr      lx_single
lx_le:
        ld      a, T_LE
        jr      lx_single
lx_gt:
        call    rd_raw
        cp      '>'
        jr      z, lx_shr
        cp      '='
        jr      z, lx_ge
        call    rd_unget
        ld      a, '>'
        jr      lx_single
lx_shr:
        ld      a, T_SHR
        jr      lx_single
lx_ge:
        ld      a, T_GE
        jr      lx_single
lx_amp:
        ld      a, '&'
        ld      (lx_want), a
        ld      a, T_AND
        ld      (lx_two), a
        ld      a, '&'
        ld      (lx_one), a
        jr      lx_op2
lx_bar:
        ld      a, '|'
        ld      (lx_want), a
        ld      a, T_OR
        ld      (lx_two), a
        ld      a, '|'
        ld      (lx_one), a
lx_op2:
        call    rd_raw
        ld      hl, lx_want
        cp      (hl)
        jr      z, lx_two_t
        call    rd_unget
        ld      a, (lx_one)
        jp      lx_single
lx_two_t:
        ld      a, (lx_two)
        jp      lx_single

; identifier: first char in A. Collect into namebuf,
; upper-cased, at most 15 chars (extras consumed).
lx_ident:
        ld      hl, namebuf
        ld      b, 0                    ; stored length
lx_id_lp:
        cp      '_'
        jr      z, lx_id_st
        cp      'A'
        jr      c, lx_id_num           ; digits (and enders)
        cp      'Z' + 1
        jr      c, lx_id_st
        cp      'a'
        jr      c, lx_id_end           ; between 'Z' and 'a'
        cp      'z' + 1
        jr      nc, lx_id_end
        sub     020h                    ; fold to upper case
        jr      lx_id_st
lx_id_num:
        cp      '0'
        jr      c, lx_id_end
        cp      '9' + 1
        jr      nc, lx_id_end
lx_id_st:
        ld      c, a
        ld      a, b
        cp      15
        jr      nc, lx_id_skip
        ld      (hl), c
        inc     hl
        inc     b
lx_id_skip:
        push    hl
        call    rd_raw
        pop     hl
        jr      lx_id_lp
lx_id_end:
        call    rd_unget
        xor     a
        ld      (hl), a                 ; NUL-terminate namebuf
        ; keyword lookup
        ld      hl, kwtab
lx_kw_lp:
        ld      a, (hl)
        or      a
        jr      z, lx_kw_no
        ld      (lx_kwcode), a
        inc     hl
        push    hl
        ld      de, namebuf
lx_kw_cmp:
        ld      a, (de)
        cp      (hl)
        jr      nz, lx_kw_dif
        or      a
        jr      z, lx_kw_yes
        inc     hl
        inc     de
        jr      lx_kw_cmp
lx_kw_dif:
        pop     hl
lx_kw_nx:
        ld      a, (hl)
        inc     hl
        or      a
        jr      nz, lx_kw_nx
        jr      lx_kw_lp
lx_kw_yes:
        pop     hl
        ld      a, (lx_kwcode)
        ld      (tok), a
        ret
lx_kw_no:
        ld      a, T_IDENT
        ld      (tok), a
        ret

; number: first digit in A. Decimal, or 0x hex.
lx_number:
        ld      hl, 0
        ld      (tokval), hl
        cp      '0'
        jr      nz, lx_dec
        call    rd_raw
        cp      'x'
        jr      z, lx_hex
        cp      'X'
        jr      z, lx_hex
        call    rd_unget
        ld      a, '0'
lx_dec:
        sub     '0'
        jr      c, lx_num_end           ; char < '0'
        cp      10
        jr      nc, lx_num_end          ; char > '9'
        ld      e, a
        ld      d, 0
        ld      hl, (tokval)
        add     hl, hl                  ; *2
        ld      b, h
        ld      c, l                    ; BC = *2
        add     hl, hl                  ; *4
        add     hl, hl                  ; *8
        add     hl, bc                  ; *10
        add     hl, de
        ld      (tokval), hl
        call    rd_raw
        jr      lx_dec
lx_num_end:
        add     a, '0'                  ; restore the char
        call    rd_unget
lx_num_ok:
        ld      a, T_NUM
        ld      (tok), a
        ret
lx_hex:
        call    rd_raw
        cp      '0'
        jr      c, lx_hex_end
        cp      '9' + 1
        jr      c, lx_hex_d
        cp      'A'
        jr      c, lx_hex_end2
        cp      'F' + 1
        jr      c, lx_hex_l
        cp      'a'
        jr      c, lx_hex_end2
        cp      'f' + 1
        jr      nc, lx_hex_end2
        sub     020h
lx_hex_l:
        sub     'A' - 10
        jr      lx_hex_add
lx_hex_d:
        sub     '0'
lx_hex_add:
        ld      e, a
        ld      hl, (tokval)
        add     hl, hl
        add     hl, hl
        add     hl, hl
        add     hl, hl                  ; *16
        ld      d, 0
        add     hl, de
        ld      (tokval), hl
        jr      lx_hex
lx_hex_end:
        call    rd_unget
        jr      lx_num_ok
lx_hex_end2:
        call    rd_unget
        jr      lx_num_ok

; string literal: collect into strbuf (max 63 chars).
lx_string:
        ld      hl, strbuf
        ld      b, 0
lx_str_lp:
        push    hl
        call    rd_raw
        pop     hl
        cp      '"'
        jr      z, lx_str_dn
        cp      01ah
        jr      z, lx_str_err
        cp      00ah
        jr      z, lx_str_err
        ld      c, a
        ld      a, b
        cp      63
        jr      nc, lx_str_skip
        ld      (hl), c
        inc     hl
        inc     b
lx_str_skip:
        jr      lx_str_lp
lx_str_dn:
        xor     a
        ld      (hl), a
        ld      a, b
        ld      (tokslen), a
        ld      a, T_STR
        ld      (tok), a
        ret
lx_str_err:
        ld      a, E_STR
        jp      err

; ---------------------------------------------------------------
; Emit machinery. epc is the current body offset. In the
; measure pass (pass = 1) bytes are only counted; in the
; emit pass (pass = 2) they are stored at OUTBASE + epc,
; and e_wm also sets the relocation bitmap bit for the
; high byte. All e_* routines preserve registers.
; ---------------------------------------------------------------
e_b:
        push    hl
        push    de
        push    af
        ld      hl, (epc)
        ld      de, 05f00h
        or      a
        sbc     hl, de
        jr      nc, e_big
        ld      hl, (epc)
        ld      a, (pass)
        cp      2
        jr      nz, e_b_cnt
        pop     af
        push    af
        ld      de, (outbase_v)
        add     hl, de
        ld      (hl), a
e_b_cnt:
        ld      hl, (epc)
        inc     hl
        ld      (epc), hl
        pop     af
        pop     de
        pop     hl
        ret
e_big:
        ld      a, E_BIG
        jp      err

; e_w: emit HL as a word (low, high). Not relocated.
e_w:
        push    af
        ld      a, l
        call    e_b
        ld      a, h
        call    e_b
        pop     af
        ret

; e_wm: emit HL as a word and mark the high byte as a
; relocation target.
e_wm:
        push    af
        ld      a, l
        call    e_b
        ld      a, h
        call    e_bm
        pop     af
        ret

; e_bm: emit the byte in A, marking its position.
e_bm:
        ld      hl, (epc)
        call    bm_set
        jp      e_b

; bm_set: set the bitmap bit for image offset HL (the
; stub applies the bitmap from the image base, stub
; included -- mktosr.py / relocdemo.z80 convention; the
; stub's own bytes simply never get marked).
bm_set:
        push    af
        push    bc
        push    de
        push    hl
        ld      a, (pass)
        cp      2
        jr      nz, bm_done
        ld      a, l
        and     7
        ld      b, a
        srl     h
        rr      l
        srl     h
        rr      l
        srl     h
        rr      l                       ; HL = offset / 8
        ld      de, BITMAP
        add     hl, de
        ld      a, 080h
        ld      c, b
        inc     c
bm_rot:
        dec     c
        jr      z, bm_or
        rrca
        jr      bm_rot
bm_or:
        or      (hl)
        ld      (hl), a
bm_done:
        pop     hl
        pop     de
        pop     bc
        pop     af
        ret

; ---------------------------------------------------------------
; Labels. def_label records the current epc in the measure
; pass; jumps always emit absolute addresses (JP), so the
; emit pass just looks the offsets up.
; ---------------------------------------------------------------
new_label:
        ld      hl, (label_cnt)
        inc     hl
        ld      (label_cnt), hl
        ld      a, h
        or      a
        jr      nz, nl_big
        ld      a, l
        cp      LABMAX
        jr      nc, nl_big
        ret
nl_big:
        ld      a, E_BIG
        jp      err

; def_label: HL = label id.
def_label:
        ld      a, (pass)
        cp      1
        ret     nz
        push    hl
        add     hl, hl
        ld      de, labtab
        add     hl, de
        ld      de, (epc)
        ld      (hl), e
        inc     hl
        ld      (hl), d
        pop     hl
        ret

; label_addr: HL = label id -> HL = ORIGIN + its offset.
label_addr:
        add     hl, hl
        ld      de, labtab
        add     hl, de
        ld      e, (hl)
        inc     hl
        ld      d, (hl)
        ld      hl, ORIGIN
        add     hl, de
        ret

; ---------------------------------------------------------------
; Symbol tables. Entries are 32 bytes; name at +0 (16
; bytes, NUL padded). Globals: +16 off, +18 kind
; (0 int, 1 char, 2 int array, 3 char array), +20 elems.
; Functions: +16 addr, +18 ret (0 void, 1 int, 2 char),
; +19 nargs, +20 ptypes[8] (0 int, 1 char), +28 frame.
; Locals: +16 disp (signed), +18 kind (0 int, 1 char),
; +19 isparam.
; ---------------------------------------------------------------
gtab_ptr:                               ; A = index -> HL
        ld      l, a
        ld      h, 0
        add     hl, hl
        add     hl, hl
        add     hl, hl
        add     hl, hl
        add     hl, hl                  ; *32
        ld      de, gtab
        add     hl, de
        ret
ftab_ptr:
        ld      l, a
        ld      h, 0
        add     hl, hl
        add     hl, hl
        add     hl, hl
        add     hl, hl
        add     hl, hl
        ld      de, ftab
        add     hl, de
        ret
ltab_ptr:
        ld      l, a
        ld      h, 0
        add     hl, hl
        add     hl, hl
        add     hl, hl
        add     hl, hl
        add     hl, hl
        ld      de, ltab
        add     hl, de
        ret

; name_eq: HL = entry name pointer; Z set iff it equals
; namebuf. Clobbers A, B, DE, HL.
name_eq:
        ld      de, namebuf
        ld      b, 16
neq_lp:
        ld      a, (de)
        cp      (hl)
        ret     nz
        or      a
        ret     z
        inc     hl
        inc     de
        djnz    neq_lp
        xor     a
        ret

; find_global: namebuf -> HL = entry, A = index, or carry.
find_global:
        ld      a, (gcount)
        or      a
        jr      z, fg_nf
        ld      b, a
        xor     a
fg_lp:
        push    bc
        push    af
        call    gtab_ptr
        call    name_eq
        jr      z, fg_yes
        pop     af
        inc     a
        pop     bc
        djnz    fg_lp
fg_nf:
        scf
        ret
fg_yes:
        pop     af
        pop     bc
        push    af
        call    gtab_ptr
        pop     af
        or      a
        ret

; find_local: namebuf -> HL = entry, A = index, or carry.
find_local:
        ld      a, (lcount)
        or      a
        jr      z, fg_nf
        ld      b, a
        xor     a
fl_lp:
        push    bc
        push    af
        call    ltab_ptr
        call    name_eq
        jr      z, fg_yes2
        pop     af
        inc     a
        pop     bc
        djnz    fl_lp
        scf
        ret
fg_yes2:
        pop     af
        pop     bc
        push    af
        call    ltab_ptr
        pop     af
        or      a
        ret

; find_func: namebuf -> HL = entry, A = index, or carry.
find_func:
        ld      a, (fcount)
        or      a
        jr      z, fg_nf
        ld      b, a
        xor     a
ffu_lp:
        push    bc
        push    af
        call    ftab_ptr
        call    name_eq
        jr      z, fg_yes3
        pop     af
        inc     a
        pop     bc
        djnz    ffu_lp
        scf
        ret
fg_yes3:
        pop     af
        pop     bc
        push    af
        call    ftab_ptr
        pop     af
        or      a
        ret

; copy_name: copy namebuf (16 bytes) to HL.
copy_name:
        ld      de, namebuf
        ld      b, 16
cn_lp:
        ld      a, (de)
        ld      (hl), a
        inc     hl
        inc     de
        djnz    cn_lp
        ret

; ---------------------------------------------------------------
; Library table access (libtab.inc, generated from
; tblib.z80). Entry = 16 bytes: off, len, selfidx, nself,
; dumidx, ndum, dep_lo, dep_hi.
; ---------------------------------------------------------------
lib_ptr:                                ; A = id -> HL
        ld      l, a
        ld      h, 0
        add     hl, hl
        add     hl, hl
        add     hl, hl
        add     hl, hl                  ; *16
        ld      de, libtab
        add     hl, de
        ret

; mark_used: A = library id; set its bit in used_mask.
mark_used:
        push    hl
        push    de
        push    af
        ld      hl, used_mask
        cp      8
        jr      c, mu_0
        inc     hl
        cp      16
        jr      c, mu_1
        inc     hl
        sub     16
        jr      mu_2
mu_1:
        sub     8
mu_2:
mu_0:
        ld      e, a
        ld      a, 1
        inc     e
mu_rot:
        dec     e
        jr      z, mu_set
        add     a, a
        jr      mu_rot
mu_set:
        or      (hl)
        ld      (hl), a
        pop     af
        pop     de
        pop     hl
        ret

; used_test: A = id -> Z clear iff used (closure mask).
used_test:
        push    hl
        push    de
        ld      hl, used_mask
        cp      8
        jr      c, ut_0
        inc     hl
        cp      16
        jr      c, ut_1
        inc     hl
        sub     16
        jr      ut_2
ut_1:
        sub     8
ut_2:
ut_0:
        ld      e, a
        ld      a, 1
        inc     e
ut_rot:
        dec     e
        jr      z, ut_and
        add     a, a
        jr      ut_rot
ut_and:
        and     (hl)
        pop     de
        pop     hl
        ret

; ---------------------------------------------------------------
; Pass 1: collect globals and function signatures.
; ---------------------------------------------------------------
pass1:
        xor     a
        ld      (gcount), a
        ld      (fcount), a
        ld      hl, 0
        ld      (gtotal), hl
        call    src_rewind
        call    lex
p1_lp:
        ld      a, (tok)
        or      a
        ret     z                       ; EOF: done
        cp      K_DIM
        jr      z, p1_dim
        cp      K_INT
        jp      z, p1_gdecl
        cp      K_CHAR
        jp      z, p1_gdecl
        cp      K_FUNC
        jp      z, p1_func
        ld      a, E_SYNTAX
        jp      err
p1_dim:
        call    lex                     ; type
        ld      a, (tok)
        cp      K_INT
        jr      z, p1_di2
        cp      K_CHAR
        jr      z, p1_dc2
        ld      a, E_SYNTAX
        jp      err
p1_di2:
        ld      a, 2                    ; int array
        jr      p1_ds
p1_dc2:
        ld      a, 3                    ; char array
p1_ds:
        ld      (gk_kind), a
        call    lex                     ; name
        ld      a, (tok)
        cp      T_IDENT
        jr      nz, p1_syn
        ld      hl, namebuf
        ld      de, savname
        ld      bc, 16
        ldir
        call    lex                     ; '('
        ld      a, (tok)
        cp      '('
        jr      nz, p1_syn
        call    lex                     ; size
        ld      a, (tok)
        cp      T_NUM
        jr      nz, p1_syn
        ld      hl, (tokval)
        ld      a, h
        or      l
        jr      z, p1_syn               ; size must be >= 1
        ld      (gk_elems), hl
        call    lex                     ; ')'
        ld      a, (tok)
        cp      ')'
        jr      nz, p1_syn
        call    lex
        ; register: name in savname -> move to namebuf
        ld      hl, savname
        ld      de, namebuf
        ld      bc, 16
        ldir
        ld      a, (gk_kind)
        ld      (gk_kind2), a
        call    reg_global
        jp      p1_lp
p1_syn:
        ld      a, E_SYNTAX
        jp      err
p1_gdecl:
        ld      a, (tok)
        cp      K_INT
        jr      z, p1_gi
        ld      a, 1                    ; char scalar
        jr      p1_gs
p1_gi:
        xor     a                       ; int scalar
p1_gs:
        ld      (gk_kind), a
p1_gdlp:
        call    lex                     ; name
        ld      a, (tok)
        cp      T_IDENT
        jr      nz, p1_syn
        ld      a, (gk_kind)
        ld      (gk_kind2), a
        ld      hl, 1
        ld      (gk_elems), hl
        call    reg_global
        call    lex
        cp      ','
        jr      z, p1_gdlp
        jp      p1_lp
p1_func:
        call    lex                     ; return type
        ld      a, (tok)
        cp      K_INT
        jr      z, p1_fi
        cp      K_CHAR
        jr      z, p1_fc
        cp      K_VOID
        jr      z, p1_fv
        jr      p1_syn
p1_fi:
        ld      a, 1
        jr      p1_fs
p1_fc:
        ld      a, 2
        jr      p1_fs
p1_fv:
        xor     a
p1_fs:
        ld      (gk_ret), a
        call    lex                     ; name
        ld      a, (tok)
        cp      T_IDENT
        jr      nz, p1_syn
        call    find_func
        jp      nc, p1_dup
        call    find_global
        jp      nc, p1_dup
        ld      a, (fcount)
        cp      FMAX
        jp      nc, p1_big
        call    ftab_ptr
        ld      (gk_fptr), hl
        call    copy_name
        ld      hl, (gk_fptr)
        ld      de, 18
        add     hl, de
        ld      a, (gk_ret)
        ld      (hl), a                 ; ret kind
        ld      a, (fcount)
        inc     a
        ld      (fcount), a
        xor     a
        ld      (gk_nargs), a
        call    lex                     ; '('
        ld      a, (tok)
        cp      '('
        jp      nz, p1_syn
        call    lex
        cp      ')'
        jr      z, p1_fdone
p1_fplp:
        ld      a, (tok)                ; param type
        cp      K_INT
        jr      z, p1_fpi
        cp      K_CHAR
        jr      z, p1_fpc
        jp      p1_syn
p1_fpi:
        xor     a
        jr      p1_fps
p1_fpc:
        ld      a, 1
p1_fps:
        push    af
        ld      a, (gk_nargs)
        cp      8
        jr      nc, p1_big
        ld      hl, (gk_fptr)
        ld      de, 20
        add     hl, de
        ld      e, a
        ld      d, 0
        add     hl, de
        pop     af
        ld      (hl), a                 ; ptypes[nargs]
        ld      a, (gk_nargs)
        inc     a
        ld      (gk_nargs), a
        call    lex                     ; param name
        ld      a, (tok)
        cp      T_IDENT
        jp      nz, p1_syn
        call    lex
        cp      ','
        jr      nz, p1_fpd
        call    lex
        jr      p1_fplp
p1_fpd:
        cp      ')'
        jp      nz, p1_syn
p1_fdone:
        ld      hl, (gk_fptr)
        ld      de, 19
        add     hl, de
        ld      a, (gk_nargs)
        ld      (hl), a                 ; nargs
        ; skip the body: lex until ENDFUNC
p1_skip:
        call    lex
        ld      a, (tok)
        or      a
        jp      z, p1_syn               ; EOF inside function
        cp      K_ENDFUNC
        jr      nz, p1_skip
        call    lex
        jp      p1_lp
p1_dup:
        ld      a, E_DUP
        jp      err
p1_big:
        ld      a, E_BIG
        jp      err

; reg_global: register the global in namebuf; kind in
; (gk_kind2), element count in (gk_elems). Sets its offset
; from gtotal and advances gtotal by the byte size.
reg_global:
        call    find_global
        jr      nc, p1_dup
        call    find_func
        jr      nc, p1_dup
        ld      a, (gcount)
        cp      GMAX
        jr      nc, p1_big
        call    gtab_ptr
        push    hl
        call    copy_name
        pop     hl
        ld      de, 16
        add     hl, de
        ld      de, (gtotal)
        ld      (hl), e
        inc     hl
        ld      (hl), d                 ; offset
        inc     hl
        ld      a, (gk_kind2)
        ld      (hl), a                 ; kind
        inc     hl
        inc     hl                      ; skip pad
        ld      de, (gk_elems)
        ld      (hl), e
        inc     hl
        ld      (hl), d                 ; elems
        ; byte size = elems * (kind even ? 2 : 1)
        ld      a, (gk_kind2)
        and     1
        jr      nz, rg_char
        ld      h, d                    ; int: DE = elems * 2
        ld      l, e
        add     hl, hl
        ex      de, hl
rg_char:
        ld      hl, (gtotal)
        add     hl, de
        ld      (gtotal), hl
        ld      a, (gcount)
        inc     a
        ld      (gcount), a
        ret

; ---------------------------------------------------------------
; Layout (between the measure and emit passes): compute
; the literal pool base, close the used-library set over
; dependencies, assign library addresses, and derive all
; TOSR sizes.
; ---------------------------------------------------------------
layout:
        ld      hl, (epc)
        ld      (litbase), hl           ; pool right after code
        ld      de, (litcur)
        add     hl, de
        ld      (libbase), hl
        ; dependency closure over used_mask
        ld      b, 20
lay_cl:
        push    bc
        call    lay_sweep
        pop     bc
        djnz    lay_cl
        ; assign library addresses in id order
        ld      hl, (libbase)
        ld      (lay_cur), hl
        xor     a
        ld      (lay_id), a
lay_al:
        ld      a, (lay_id)
        cp      LIB_NROUT
        jr      nc, lay_sizes
        call    used_test
        jr      z, lay_inc
        ld      hl, (lay_cur)
        ld      de, ORIGIN
        add     hl, de
        ld      (lay_val), hl           ; absolute address
        ld      a, (lay_id)
        add     a, a
        ld      l, a
        ld      h, 0
        ld      de, libaddr
        add     hl, de                  ; HL = &libaddr[id]
        ld      de, (lay_val)
        ld      (hl), e
        inc     hl
        ld      (hl), d
        ld      a, (lay_id)
        call    lib_ptr
        ld      de, 2
        add     hl, de
        ld      e, (hl)
        inc     hl
        ld      d, (hl)                 ; DE = routine length
        ld      hl, (lay_cur)
        add     hl, de
        ld      (lay_cur), hl
lay_inc:
        ld      a, (lay_id)
        inc     a
        ld      (lay_id), a
        jr      lay_al
lay_sizes:
        ld      hl, (lay_cur)
        ld      de, (rt_stub)
        or      a
        sbc     hl, de                  ; body size excl. stub
        ld      (bodysize), hl
        ; bitmap_size = (stub + bodysize + 7) / 8: the
        ; bitmap covers the whole pre-bitmap section
        ld      de, (rt_stub)
        add     hl, de
        ld      de, 7
        add     hl, de
        srl     h
        rr      l
        srl     h
        rr      l
        srl     h
        rr      l
        ld      (bitmap_size), hl
        ; total_file = 109 + bodysize + bitmap_size + 16
        ld      hl, (bodysize)
        ld      de, (bitmap_size)
        add     hl, de
        ld      de, STUB_LEN + 16
        add     hl, de
        ld      (total_file), hl
        ; total_padded = total_file rounded up to a page
        ld      de, 255
        add     hl, de
        xor     a
        ld      l, a
        ld      (total_padded), hl
        ; heap_blocks = ceil(gtotal / 256)
        ld      hl, (gtotal)
        ld      de, 255
        add     hl, de
        ld      l, h
        ld      h, 0
        ld      (heap_blocks), hl
        ; the heap must fit behind the module at origin
        ld      hl, (total_padded)
        ld      de, (gtotal)
        add     hl, de
        ld      de, 06001h
        or      a
        sbc     hl, de
        jr      nc, lay_big             ; > 06000h: too big
        ; find Main
        ld      hl, namebuf
        ld      b, 16
lay_z:
        ld      (hl), 0
        inc     hl
        djnz    lay_z
        ld      hl, str_main
        ld      de, namebuf
        ld      bc, 5
        ldir
        call    find_func
        jr      c, lay_nomain
        ld      de, 16
        add     hl, de
        ld      e, (hl)
        inc     hl
        ld      d, (hl)
        ld      (main_addr), de
        ret
lay_big:
        ld      a, E_BIG
        jp      err
lay_nomain:
        ld      a, E_NOMAIN
        jp      err

; lay_sweep: for each used routine, OR its dependency
; mask into used_mask.
lay_sweep:
        xor     a
        ld      (lay_id), a
lsw_lp:
        ld      a, (lay_id)
        cp      LIB_NROUT
        ret     nc
        call    used_test
        jr      z, lsw_nx
        ld      a, (lay_id)
        call    lib_ptr
        ld      de, 12
        add     hl, de                  ; HL = &dep_lo
        ld      e, (hl)
        inc     hl
        ld      d, (hl)
        inc     hl
        ld      a, (hl)                 ; dep_hi low byte
        ld      (lsw_lo), de
        ld      (lsw_hi), a
        ld      hl, used_mask
        ld      a, (lsw_lo)
        or      (hl)
        ld      (hl), a
        inc     hl
        ld      a, (lsw_lo + 1)
        or      (hl)
        ld      (hl), a
        inc     hl
        ld      a, (lsw_hi)
        or      (hl)
        ld      (hl), a
lsw_nx:
        ld      a, (lay_id)
        inc     a
        ld      (lay_id), a
        jr      lsw_lp

; ---------------------------------------------------------------
; emit_library: copy the used routines into the body and
; patch their self references and cross references.
; Runs after the emit parse; the cursor starts at libbase.
; ---------------------------------------------------------------
emit_library:
        ld      hl, (libbase)
        ld      (lay_cur), hl           ; body offset cursor
        xor     a
        ld      (lay_id), a
el_lp:
        ld      a, (lay_id)
        cp      LIB_NROUT
        jp      nc, el_done
        call    used_test
        jp      z, el_inc
        ld      a, (lay_id)
        call    lib_ptr
        ld      (el_tab), hl
        ; copy the template bytes: libimg + off -> OUTBASE + cur
        ld      de, 0
        add     hl, de                  ; HL = entry (off at +0)
        ld      e, (hl)
        inc     hl
        ld      d, (hl)                 ; DE = off
        inc     hl
        ld      c, (hl)
        inc     hl
        ld      b, (hl)                 ; BC = len
        ld      (el_len), bc
        ld      hl, libimg
        add     hl, de                  ; source
        push    hl
        push    bc
        ld      hl, (lay_cur)
        ld      de, (outbase_v)
        add     hl, de
        ex      de, hl                  ; DE = dest
        pop     bc
        pop     hl
        ldir
        ; patch self references
        ld      hl, (el_tab)
        ld      de, 4
        add     hl, de
        ld      e, (hl)
        inc     hl
        ld      d, (hl)                 ; selfidx
        inc     hl
        ld      a, (hl)                 ; nself (low byte)
        ld      (el_cnt), a
        ld      hl, libself
        add     hl, de
        add     hl, de
        add     hl, de
        add     hl, de                  ; HL = &libself[selfidx]
        ld      (el_ptr), hl
        ld      a, (el_cnt)
        or      a
        jr      z, el_dum
el_sf:
        ld      hl, (el_ptr)
        ld      e, (hl)
        inc     hl
        ld      d, (hl)                 ; pos
        inc     hl
        ld      c, (hl)
        inc     hl
        ld      b, (hl)                 ; tgt (routine-relative)
        inc     hl
        ld      (el_ptr), hl
        ; word address = OUTBASE + cur + pos
        ld      hl, (lay_cur)
        add     hl, de
        ld      (el_wa), hl             ; body offset of word
        ld      de, (outbase_v)
        add     hl, de
        ex      de, hl                  ; DE = memory address
        ; value = ORIGIN + cur + tgt
        ld      hl, (lay_cur)
        add     hl, bc
        push    de
        ld      de, ORIGIN
        add     hl, de
        pop     de
        ex      de, hl
        ld      (hl), e
        inc     hl
        ld      (hl), d
        ; mark the high byte: offset = wa + 1
        ld      hl, (el_wa)
        inc     hl
        call    bm_set
        ld      a, (el_cnt)
        dec     a
        ld      (el_cnt), a
        jr      nz, el_sf
el_dum:
        ; patch dummy (cross-routine) references
        ld      hl, (el_tab)
        ld      de, 8
        add     hl, de
        ld      e, (hl)
        inc     hl
        ld      d, (hl)                 ; dumidx
        inc     hl
        ld      a, (hl)                 ; ndum (low byte)
        ld      (el_cnt), a
        ld      hl, libdum
        add     hl, de
        add     hl, de
        add     hl, de
        add     hl, de                  ; HL = &libdum[dumidx]
        ld      (el_ptr), hl
        ld      a, (el_cnt)
        or      a
        jr      z, el_next
el_df:
        ld      hl, (el_ptr)
        ld      e, (hl)
        inc     hl
        ld      d, (hl)                 ; pos
        inc     hl
        ld      a, (hl)                 ; target routine id
        inc     hl
        inc     hl                      ; skip pad
        ld      (el_ptr), hl
        ld      hl, (lay_cur)
        add     hl, de
        ld      (el_wa), hl
        ld      de, (outbase_v)
        add     hl, de
        ex      de, hl                  ; DE = memory address
        ; value = libaddr[tgtid]
        add     a, a
        ld      l, a
        ld      h, 0
        push    de
        ld      de, libaddr
        add     hl, de
        ld      e, (hl)
        inc     hl
        ld      d, (hl)
        pop     hl                      ; HL = memory address
        ld      (hl), e
        inc     hl
        ld      (hl), d
        ld      hl, (el_wa)
        inc     hl
        call    bm_set
        ld      a, (el_cnt)
        dec     a
        ld      (el_cnt), a
        jr      nz, el_df
el_next:
        ld      hl, (lay_cur)
        ld      de, (el_len)
        add     hl, de
        ld      (lay_cur), hl
el_inc:
        ld      a, (lay_id)
        inc     a
        ld      (lay_id), a
        jp      el_lp
el_done:
        ; cursor must have reached bodysize + rt_stub
        ; (lay_cur is ORIGIN-relative, bodysize is not)
        ld      hl, (bodysize)
        ld      de, (rt_stub)
        add     hl, de
        ld      de, (lay_cur)
        or      a
        sbc     hl, de
        ret     z
        ld      a, E_BIG
        jp      err

; ---------------------------------------------------------------
; parse_program: the measure / emit parse. On entry (pass)
; is 1 (measure) or 2 (emit). Emits the runtime entry,
; then parses globals (consumed, already registered in
; pass 1) and function definitions.
; ---------------------------------------------------------------
parse_program:
        call    src_rewind
        ld      hl, 0
        ld      (label_cnt), hl
        ld      (litcur), hl
        ld      hl, (rt_stub)
        ld      (epc), hl
        ; the used mask is reset only in the measure pass:
        ; layout closes it over dependencies, and the emit
        ; pass (and emit_library) must see the closed set.
        ld      a, (pass)
        cp      1
        jr      nz, pp_nrs
        xor     a
        ld      (used_mask), a
        ld      (used_mask + 1), a
        ld      (used_mask + 2), a
pp_nrs:
        xor     a
        ld      (loopdep), a
        ld      (fordep), a
        call    lex
        call    emit_rtentry
pp_lp:
        ld      a, (tok)
        or      a
        ret     z                       ; EOF
        cp      K_DIM
        jr      z, pp_g
        cp      K_INT
        jr      z, pp_g
        cp      K_CHAR
        jr      z, pp_g
        cp      K_FUNC
        jr      z, pp_f
        ld      a, E_SYNTAX
        jp      err
pp_g:
        call    parse_global2
        jr      pp_lp
pp_f:
        call    parse_function
        jr      pp_lp

; parse_global2: consume a global declaration (already
; registered in pass 1). Mirrors the pass-1 grammar.
parse_global2:
        ld      a, (tok)
        cp      K_DIM
        jr      z, pg_dim
        ; scalar declaration: type is current
pg_sl:
        call    lex                     ; name
        ld      a, (tok)
        cp      T_IDENT
        jr      nz, pg_syn
        call    lex
        cp      ','
        ret     nz
        jr      pg_sl
pg_dim:
        call    lex                     ; type
        call    lex                     ; name
        ld      a, (tok)
        cp      T_IDENT
        jr      nz, pg_syn
        call    lex                     ; '('
        ld      a, (tok)
        cp      '('
        jr      nz, pg_syn
        call    lex                     ; size
        ld      a, (tok)
        cp      T_NUM
        jr      nz, pg_syn
        call    lex                     ; ')'
        ld      a, (tok)
        cp      ')'
        jr      nz, pg_syn
        call    lex
        ret
pg_syn:
        ld      a, E_SYNTAX
        jp      err

; ---------------------------------------------------------------
; emit_rtentry: the runtime entry at body offset 0.
;   LD HL, entry   (marked: after relocation HL = the
;                   runtime address of this instruction)
;   LD DE, -109    -> HL = module base
;   LD DE, total_padded ; ADD HL, DE -> heap base
;   PUSH HL / POP IX
;   zero the heap (globals area)
;   CALL Main (marked)
;   RET
; Sizes never depend on values, so the measure pass emits
; the same byte count with placeholder operands.
; ---------------------------------------------------------------
emit_rtentry:
        ld      a, 021h
        call    e_b
        ld      hl, (rt_stub)
        ld      de, ORIGIN
        add     hl, de
        call    e_wm                    ; LD HL, entry
        ld      a, 011h
        call    e_b
        ld      hl, (rt_stub)
        ld      a, l
        cpl
        ld      l, a
        ld      a, h
        cpl
        ld      h, a
        inc     hl
        call    e_w                     ; LD DE, -stub_len
        ld      a, 019h
        call    e_b                     ; ADD HL, DE
        ld      a, 011h
        call    e_b
        ld      hl, (total_padded)
        call    e_w                     ; LD DE, total_padded
        ld      a, 019h
        call    e_b                     ; ADD HL, DE
        ld      a, 0e5h
        call    e_b                     ; PUSH HL
        ld      a, 0ddh
        call    e_b
        ld      a, 0e1h
        call    e_b                     ; POP IX
        ld      hl, (gtotal)
        ld      a, h
        or      l
        jr      z, er_main
        ld      a, 001h
        call    e_b
        ld      hl, (gtotal)
        call    e_w                     ; LD BC, gtotal
        ; zero loop: LD (HL),0 / INC HL / DEC BC /
        ; LD A,B / OR C / JR NZ, -8
        ld      hl, er_zloop
        ld      b, 8
        call    e_seq
er_main:
        ld      a, 0cdh
        call    e_b
        ld      hl, (main_addr)
        call    e_wm                    ; CALL Main
        ld      a, 0c9h
        call    e_b                     ; RET
        ret
er_zloop:
        db      036h, 0, 023h, 00bh, 078h, 0b1h, 020h, 0f8h

; e_seq: emit B bytes from HL.
e_seq:
        ld      a, (hl)
        call    e_b
        inc     hl
        djnz    e_seq
        ret

; ---------------------------------------------------------------
; parse_function: parse and compile one function. The
; current token is K_FUNC. Locals are rebuilt from the
; header; pass 1 already registered the signature.
; Frame: params at (IY + 4 + 2*(nargs-1-i)); locals below
; IY; a fixed 32-byte hidden area (FORMAX for-loops x
; end+step slots) follows the user locals.
; ---------------------------------------------------------------
parse_function:
        call    lex                     ; return type
        ld      a, (tok)
        cp      K_INT
        jr      z, pf_i
        cp      K_CHAR
        jr      z, pf_c
        cp      K_VOID
        jr      z, pf_v
        jp      pf_syn
pf_i:
        ld      a, 1
        jr      pf_t
pf_c:
        ld      a, 2
        jr      pf_t
pf_v:
        xor     a
pf_t:
        ld      (cfun_ret), a
        call    lex                     ; name
        ld      a, (tok)
        cp      T_IDENT
        jp      nz, pf_syn
        call    find_func
        ld      a, E_UNDEF
        jp      c, err
        ld      (cfun_ptr), hl          ; entry pointer
        ; record the function address: ORIGIN + epc
        ld      de, 16
        add     hl, de
        ld      de, (epc)
        push    hl
        ld      hl, ORIGIN
        add     hl, de
        ex      de, hl
        pop     hl
        ld      (hl), e
        inc     hl
        ld      (hl), d
        ; record where this function's literals begin
        ld      hl, (cfun_ptr)
        ld      de, 30
        add     hl, de
        ld      de, (litcur)
        ld      (hl), e
        inc     hl
        ld      (hl), d
        ; locals: none yet
        xor     a
        ld      (lcount), a
        call    lex                     ; '('
        ld      a, (tok)
        cp      '('
        jp      nz, pf_syn
        call    lex
        cp      ')'
        jr      z, pf_pdone
pf_plp:
        ld      a, (tok)                ; param type
        cp      K_INT
        jr      z, pf_pi
        cp      K_CHAR
        jr      z, pf_pc
        jr      pf_syn
pf_pi:
        xor     a
        jr      pf_ps
pf_pc:
        ld      a, 1
pf_ps:
        ld      (pf_kind), a
        call    lex                     ; param name
        ld      a, (tok)
        cp      T_IDENT
        jr      nz, pf_syn
        ld      a, (pf_kind)
        ld      (al_kind), a
        ld      a, 1
        ld      (al_param), a
        call    add_local
        call    lex
        cp      ','
        jr      nz, pf_pchk
        call    lex
        jr      pf_plp
pf_pchk:
        cp      ')'
        jr      nz, pf_syn
pf_pdone:
        call    lex                     ; past ')'
        ; param displacements: param i (0-based, in local
        ; order) sits at 4 + 2*(nargs-1-i)
        ld      a, (lcount)
        ld      (pf_nargs), a
        ; cross-check with the pass-1 signature
        ld      hl, (cfun_ptr)
        ld      de, 19
        add     hl, de
        ld      a, (hl)
        ld      hl, pf_nargs
        cp      (hl)
        jr      nz, pf_args
        xor     a
        ld      (pf_idx), a
pf_pdl:
        ld      a, (pf_idx)
        ld      hl, pf_nargs
        cp      (hl)
        jr      nc, pf_decls
        call    ltab_ptr                ; A = local index
        ld      de, 16
        add     hl, de                  ; &disp
        ld      a, (pf_nargs)
        dec     a
        ld      e, a
        ld      a, (pf_idx)
        sub     e                       ; A = i - (nargs-1) <= 0
        neg                             ; A = nargs-1-i
        add     a, a
        add     a, 4                    ; disp = 4 + 2*(...)
        ld      (hl), a
        inc     hl
        ld      (hl), 0
        ld      a, (pf_idx)
        inc     a
        ld      (pf_idx), a
        jr      pf_pdl
pf_args:
        ld      a, E_ARGS
        jp      err
pf_syn:
        ld      a, E_SYNTAX
        jp      err
pf_decls:
        ; local declarations (before any statement)
        ld      a, (tok)
        cp      K_INT
        jr      z, pf_dt
        cp      K_CHAR
        jr      z, pf_dt
        jr      pf_layout
pf_dt:
        cp      K_INT
        jr      z, pf_di
        ld      a, 1
        jr      pf_ds
pf_di:
        xor     a
pf_ds:
        ld      (pf_kind), a
pf_dlp:
        call    lex                     ; local name
        ld      a, (tok)
        cp      T_IDENT
        jr      nz, pf_syn
        ld      a, (pf_kind)
        ld      (al_kind), a
        xor     a
        ld      (al_param), a
        call    add_local
        call    lex
        cp      ','
        jr      z, pf_dlp
        jr      pf_decls
pf_layout:
        ; local displacements: cumulative from IY down
        ld      hl, 0
        ld      (pf_cum), hl
        xor     a
        ld      (pf_idx), a
pf_llp:
        ld      a, (pf_idx)
        ld      hl, lcount
        cp      (hl)
        jr      nc, pf_pro
        call    ltab_ptr
        ld      de, 19
        add     hl, de
        ld      a, (hl)                 ; isparam
        or      a
        jr      nz, pf_lnx              ; params already set
        dec     hl                      ; &kind (at +18)
        ld      a, (hl)
        push    hl
        ld      hl, (pf_cum)
        inc     hl                      ; char: +1
        or      a
        jr      z, pf_li2
        jr      pf_lst
pf_li2:
        inc     hl                      ; int: +2 total
pf_lst:
        ld      (pf_cum), hl            ; cum = bytes so far
        ex      de, hl                  ; DE = cum
        pop     hl                      ; &kind
        dec     hl
        dec     hl                      ; &disp (at +16)
        xor     a
        sub     e
        ld      (hl), a                 ; disp low = -cum
        inc     hl
        ld      (hl), 0ffh              ; disp high
pf_lnx:
        ld      a, (pf_idx)
        inc     a
        ld      (pf_idx), a
        jr      pf_llp
pf_pro:
        ld      hl, (pf_cum)
        ld      (frame_user), hl
        ld      de, FORMAX * 4
        add     hl, de
        ld      (frame_tot), hl
        ; store frame size in the function entry (+28)
        ld      hl, (cfun_ptr)
        ld      de, 28
        add     hl, de
        ld      de, (frame_tot)
        ld      (hl), e
        inc     hl
        ld      (hl), d
        ; prologue: PUSH IY / LD IY,0 / ADD IY,SP /
        ; LD HL,-frame_tot / ADD HL,SP / LD SP,HL
        ld      hl, pf_pro1
        ld      b, 9
        call    e_seq
        ld      hl, (frame_tot)
        ld      a, h
        cpl
        ld      h, a
        ld      a, l
        cpl
        ld      l, a
        inc     hl                      ; HL = -frame_tot
        call    e_w
        ld      hl, pf_pro2
        ld      b, 2
        call    e_seq
        ; body
        call    new_label
        ld      (retlab), hl
        xor     a
        ld      (loopdep), a
        ld      (fordep), a
        call    parse_stmts
        ld      a, (tok)
        cp      K_ENDFUNC
        jp      nz, pf_syn
        call    lex
        ld      hl, (retlab)
        call    def_label
        ld      hl, pf_epi
        ld      b, 5
        call    e_seq                   ; LD SP,IY/POP IY/RET
        ret
pf_pro1:
        db      0fdh, 0e5h, 0fdh, 021h, 0, 0, 0fdh, 039h, 021h
pf_pro2:
        db      039h, 0f9h
pf_epi:
        db      0fdh, 0f9h, 0fdh, 0e1h, 0c9h

; add_local: register namebuf as a local; kind in
; (al_kind), isparam in (al_param). Disp set later.
add_local:
        call    find_local
        jr      nc, pf_dup
        ld      a, (lcount)
        cp      LMAX
        jr      nc, pf_big
        call    ltab_ptr
        push    hl
        call    copy_name
        pop     hl
        ld      de, 18
        add     hl, de
        ld      a, (al_kind)
        ld      (hl), a
        inc     hl
        ld      a, (al_param)
        ld      (hl), a
        ld      a, (lcount)
        inc     a
        ld      (lcount), a
        ret
pf_dup:
        ld      a, E_DUP
        jp      err
pf_big:
        ld      a, E_BIG
        jp      err

; ---------------------------------------------------------------
; Statements.
; ---------------------------------------------------------------
; Label registers lb1..lb6 are per-statement scratch:
; nested statements clobber them, so every handler that
; uses them saves them on entry and restores on exit.
lb_save:
        pop     de                      ; return address
        ld      hl, (lb1)
        push    hl
        ld      hl, (lb2)
        push    hl
        ld      hl, (lb3)
        push    hl
        ld      hl, (lb4)
        push    hl
        ld      hl, (lb5)
        push    hl
        ld      hl, (lb6)
        push    hl
        push    de                      ; return address on top
        ret
lb_restore:
        pop     bc                      ; return address
        pop     hl
        ld      (lb6), hl
        pop     hl
        ld      (lb5), hl
        pop     hl
        ld      (lb4), hl
        pop     hl
        ld      (lb3), hl
        pop     hl
        ld      (lb2), hl
        pop     hl
        ld      (lb1), hl
        push    bc
        ret

parse_stmts:
        ld      a, (tok)
        or      a
        ret     z                       ; EOF
        cp      K_ENDFUNC
        ret     z
        cp      K_ELSE
        ret     z
        cp      K_ENDIF
        ret     z
        cp      K_ENDWHILE
        ret     z
        cp      K_UNTIL
        ret     z
        cp      K_NEXT
        ret     z
        call    parse_stmt
        jr      parse_stmts

parse_stmt:
        ld      a, (tok)
        cp      T_IDENT
        jp      z, st_ident
        cp      K_IF
        jp      z, st_if
        cp      K_WHILE
        jp      z, st_while
        cp      K_REPEAT
        jp      z, st_repeat
        cp      K_FOR
        jp      z, st_for
        cp      K_BREAK
        jp      z, st_break
        cp      K_RETURN
        jp      z, st_return
        ld      a, E_SYNTAX
        jp      err

; expect: A = token that must be current; then advance.
expect:
        ld      hl, tok
        cp      (hl)
        jp      nz, pf_syn
        call    lex
        ret

; e_jz: emit "LD A,H / OR L / JP Z, label HL".
e_jz:
        push    hl
        ld      hl, sq_jz
        ld      b, 3
        call    e_seq
        pop     hl
        call    label_addr
        jp      e_wm

; e_jp: emit "JP label HL".
e_jp:
        push    hl
        ld      a, 0c3h
        call    e_b
        pop     hl
        call    label_addr
        jp      e_wm
sq_jz:
        db      07ch, 0b5h, 0cah

; push_loop: break label in HL, continue label in DE.
push_loop:
        ld      a, (loopdep)
        cp      8
        jp      nc, pf_big
        push    hl                      ; break
        push    de                      ; cont
        add     a, a
        add     a, a                    ; *4
        ld      e, a
        ld      d, 0
        ld      hl, loopstk
        add     hl, de                  ; HL = &entry
        pop     de                      ; cont
        pop     bc                      ; break in BC
        ld      (hl), c
        inc     hl
        ld      (hl), b
        inc     hl
        ld      (hl), e
        inc     hl
        ld      (hl), d
        ld      a, (loopdep)
        inc     a
        ld      (loopdep), a
        ret

; pop_loop: drop the innermost loop context.
pop_loop:
        ld      a, (loopdep)
        dec     a
        ld      (loopdep), a
        ret

; loop_break: HL = the innermost break label.
loop_break:
        ld      a, (loopdep)
        dec     a
        add     a, a
        add     a, a
        ld      e, a
        ld      d, 0
        ld      hl, loopstk
        add     hl, de
        ld      e, (hl)
        inc     hl
        ld      d, (hl)
        ex      de, hl
        ret

; ---------------------------------------------------------------
; Variable access. find_var resolves namebuf to a scalar:
; locals first (they shadow globals), then globals.
; Sets (v_kind) 0 = local / 1 = global, (v_disp) =
; IY displacement or IX offset, (v_type) 0 = int / 1 = char.
; Carry set if not found or not a scalar.
; ---------------------------------------------------------------
find_var:
        call    find_local
        jr      nc, fv_loc
        call    find_global
        ret     c
        jr      fv_glb
fv_loc:
        ld      de, 16
        add     hl, de
        ld      e, (hl)
        inc     hl
        ld      d, (hl)
        ld      (v_disp), de
        inc     hl
        ld      a, (hl)                 ; kind at +18
        ld      (v_type), a
        xor     a
        ld      (v_kind), a
        or      a
        ret
fv_glb:
        ; HL = entry from find_global
        ld      de, 18
        add     hl, de
        ld      a, (hl)                 ; kind
        cp      2
        jr      nc, fv_arr              ; array: not a scalar
        ld      (v_type), a
        ld      de, -2
        add     hl, de                  ; &off (+16)
        ld      e, (hl)
        inc     hl
        ld      d, (hl)
        ld      (v_disp), de
        ld      a, 1
        ld      (v_kind), a
        or      a
        ret
fv_arr:
        scf
        ret

; load_var: emit a load of the variable into HL.
load_var:
        ld      a, (v_kind)
        or      a
        jr      nz, lv_glb
        ; local: (IY + d)
        ld      hl, (v_disp)
        ld      (tmp_disp), hl
        ld      a, (v_type)
        or      a
        jr      nz, lv_lc
        jp      ld_disp                 ; int: L + H
lv_lc:
        ld      hl, sq_lod              ; LD L, (IY+d)
        ld      b, 2
        call    e_seq
        ld      a, (tmp_disp)
        call    e_b
        ld      hl, sq_ldh0             ; LD H, 0
        ld      b, 2
        jp      e_seq
lv_glb:
        ld      hl, (v_disp)
        ld      a, h
        or      a
        jr      nz, lv_slow
        ld      a, l
        cp      127
        jr      nc, lv_slow
        ; fast path: (IX + off)
        ld      (tmp_disp), hl
        ld      a, (v_type)
        or      a
        jr      nz, lv_gc
        ld      hl, sq_ilod
        ld      b, 2
        call    e_seq
        ld      a, (tmp_disp)
        call    e_b
        ld      hl, sq_ihod
        ld      b, 2
        call    e_seq
        ld      a, (tmp_disp)
        inc     a
        jp      e_b
lv_gc:
        ld      hl, sq_ilod
        ld      b, 2
        call    e_seq
        ld      a, (tmp_disp)
        call    e_b
        ld      hl, sq_ldh0
        ld      b, 2
        jp      e_seq
lv_slow:
        ld      a, 021h                 ; LD HL, off
        call    e_b
        ld      hl, (v_disp)
        call    e_w
        ld      hl, sq_ixde             ; PUSH IX/POP DE/ADD
        ld      b, 4
        call    e_seq
        ld      a, (v_type)
        or      a
        jr      nz, lv_sc
        ld      hl, sq_ldw              ; LD E,(HL)/INC/LD D/EX
        ld      b, 4
        jp      e_seq
lv_sc:
        ld      hl, sq_ldb              ; LD L,(HL)/LD H,0
        ld      b, 3
        jp      e_seq

; ld_disp: emit LD L,(IY+d) / LD H,(IY+d+1) for the
; signed displacement in (tmp_disp).
ld_disp:
        ld      hl, sq_lod
        ld      b, 2
        call    e_seq
        ld      a, (tmp_disp)
        call    e_b
        ld      hl, sq_hod
        ld      b, 2
        call    e_seq
        ld      a, (tmp_disp)
        inc     a
        jp      e_b

; st_disp: emit LD (IY+d),L / LD (IY+d+1),H.
st_disp:
        ld      hl, sq_sod
        ld      b, 2
        call    e_seq
        ld      a, (tmp_disp)
        call    e_b
        ld      hl, sq_shd
        ld      b, 2
        call    e_seq
        ld      a, (tmp_disp)
        inc     a
        jp      e_b

sq_lod: db      0fdh, 06eh              ; LD L, (IY+d)
sq_hod: db      0fdh, 066h              ; LD H, (IY+d)
sq_sod: db      0fdh, 075h              ; LD (IY+d), L
sq_shd: db      0fdh, 074h              ; LD (IY+d), H
sq_ilod: db     0ddh, 06eh              ; LD L, (IX+d)
sq_ihod: db     0ddh, 066h              ; LD H, (IX+d)
sq_isod: db     0ddh, 075h              ; LD (IX+d), L
sq_ishd: db     0ddh, 074h              ; LD (IX+d), H
sq_ldh0: db     026h, 0                 ; LD H, 0
sq_ixde: db     0ddh, 0e5h, 0d1h, 019h  ; PUSH IX/POP DE/ADD HL,DE
sq_ldw: db      05eh, 023h, 056h, 0ebh  ; LD E,(HL)/INC HL/LD D,(HL)/EX
sq_ldb: db      06eh, 026h, 0            ; LD L,(HL)/LD H,0

; store_var: emit a store of HL into the variable.
store_var:
        ld      a, (v_kind)
        or      a
        jr      nz, sv_glb
        ld      hl, (v_disp)
        ld      (tmp_disp), hl
        ld      a, (v_type)
        or      a
        jr      nz, sv_lc
        jp      st_disp
sv_lc:
        ld      hl, sq_sod
        ld      b, 2
        call    e_seq
        ld      a, (tmp_disp)
        jp      e_b
sv_glb:
        ld      hl, (v_disp)
        ld      a, h
        or      a
        jr      nz, sv_slow
        ld      a, l
        cp      127
        jr      nc, sv_slow
        ld      (tmp_disp), hl
        ld      a, (v_type)
        or      a
        jr      nz, sv_gc
        ld      hl, sq_isod
        ld      b, 2
        call    e_seq
        ld      a, (tmp_disp)
        call    e_b
        ld      hl, sq_ishd
        ld      b, 2
        call    e_seq
        ld      a, (tmp_disp)
        inc     a
        jp      e_b
sv_gc:
        ld      hl, sq_isod
        ld      b, 2
        call    e_seq
        ld      a, (tmp_disp)
        jp      e_b
sv_slow:
        ld      a, 0e5h                 ; PUSH HL (value)
        call    e_b
        ld      a, 021h                 ; LD HL, off
        call    e_b
        ld      hl, (v_disp)
        call    e_w
        ld      hl, sq_ixde
        ld      b, 4
        call    e_seq                   ; HL = address
        ld      a, 0d1h                 ; POP DE (value)
        call    e_b
        ld      a, (v_type)
        or      a
        jr      nz, sv_sc
        ld      hl, sq_stw              ; LD (HL),E/INC/LD (HL),D
        ld      b, 3
        jp      e_seq
sv_sc:
        ld      a, 073h                 ; LD (HL), E
        jp      e_b
sq_stw: db      073h, 023h, 072h

; ---------------------------------------------------------------
; Array access (globals only). (gv_ptr) = the global
; entry. The index expression value is in HL.
; ---------------------------------------------------------------
; arr_load: scale the index, compute the address, load.
arr_load:
        ld      hl, (gv_ptr)
        ld      de, 18
        add     hl, de
        ld      a, (hl)                 ; kind (2 int / 3 char)
        cp      2
        jr      nz, al_char
        ld      a, 029h                 ; ADD HL, HL (scale)
        call    e_b
al_char:
        call    arr_addr
        ld      hl, (gv_ptr)
        ld      de, 18
        add     hl, de
        ld      a, (hl)
        cp      2
        jr      nz, al_c2
        ld      hl, sq_ldw
        ld      b, 4
        jp      e_seq
al_c2:
        ld      hl, sq_ldb
        ld      b, 3
        jp      e_seq

; arr_addr: HL = index (scaled) -> emit address
; computation leaving the address in HL.
arr_addr:
        ld      a, 011h                 ; LD DE, off
        call    e_b
        ld      hl, (gv_ptr)
        ld      de, 16
        add     hl, de
        ld      e, (hl)
        inc     hl
        ld      d, (hl)
        ex      de, hl
        call    e_w
        ld      hl, sq_aadr
        ld      b, 5                    ; ADD HL,DE/PUSH IX/POP DE/ADD
        jp      e_seq
sq_aadr: db     019h, 0ddh, 0e5h, 0d1h, 019h

; arr_store_tail: the scaled index is on the stack and
; the value is in HL. Emits the store.
arr_store_tail:
        ld      a, 0e5h                 ; PUSH HL (value)
        call    e_b
        ld      a, 0c1h                 ; POP BC (value)
        call    e_b
        ld      a, 0e1h                 ; POP HL (index)
        call    e_b
        ld      a, 011h                 ; LD DE, off
        call    e_b
        ld      hl, (gv_ptr)
        ld      de, 16
        add     hl, de
        ld      e, (hl)
        inc     hl
        ld      d, (hl)
        ex      de, hl
        call    e_w
        ld      hl, sq_aadr
        ld      b, 5
        call    e_seq                   ; HL = address
        ld      hl, (gv_ptr)
        ld      de, 18
        add     hl, de
        ld      a, (hl)
        cp      2
        jr      nz, ast_c
        ld      hl, sq_astw             ; LD (HL),C/INC/LD (HL),B
        ld      b, 3
        jp      e_seq
ast_c:
        ld      a, 071h                 ; LD (HL), C
        jp      e_b
sq_astw: db     071h, 023h, 070h

; ---------------------------------------------------------------
; Statement implementations.
; ---------------------------------------------------------------
st_if:
        call    lb_save
        call    lex                     ; past IF
        call    parse_expr
        ld      a, K_THEN
        call    expect
        call    new_label
        ld      (lb1), hl
        call    e_jz
        call    parse_stmts
        ld      a, (tok)
        cp      K_ELSE
        jr      nz, st_if_n
        call    new_label
        ld      (lb2), hl
        call    e_jp
        ld      hl, (lb1)
        call    def_label
        call    lex                     ; past ELSE
        call    parse_stmts
        ld      hl, (lb2)
        call    def_label
        jr      st_if_e
st_if_n:
        ld      hl, (lb1)
        call    def_label
st_if_e:
        call    lb_restore
        ld      a, K_ENDIF
        jp      expect

st_while:
        call    lb_save
        call    lex                     ; past WHILE
        call    new_label
        ld      (lb1), hl               ; top
        call    new_label
        ld      (lb2), hl               ; end
        ld      hl, (lb1)
        call    def_label
        call    parse_expr
        ld      hl, (lb2)
        call    e_jz
        ld      hl, (lb2)
        ld      de, (lb1)
        call    push_loop               ; break=end, cont=top
        call    parse_stmts
        call    pop_loop
        ld      a, K_ENDWHILE
        call    expect
        ld      hl, (lb1)
        call    e_jp
        ld      hl, (lb2)
        call    def_label
        call    lb_restore
        ret

st_repeat:
        call    lb_save
        call    lex                     ; past REPEAT
        call    new_label
        ld      (lb1), hl               ; top
        call    new_label
        ld      (lb2), hl               ; continue (until expr)
        call    new_label
        ld      (lb3), hl               ; end
        ld      hl, (lb1)
        call    def_label
        ld      hl, (lb3)
        ld      de, (lb2)
        call    push_loop
        call    parse_stmts
        call    pop_loop
        ld      a, K_UNTIL
        call    expect
        ld      hl, (lb2)
        call    def_label
        call    parse_expr
        ld      hl, (lb1)
        call    e_jz                    ; loop while false
        ld      hl, (lb3)
        call    def_label
        call    lb_restore
        ret

st_break:
        ld      a, (loopdep)
        or      a
        jr      z, st_brk_e
        call    lex                     ; past BREAK
        call    loop_break
        jp      e_jp
st_brk_e:
        ld      a, E_BREAK
        jp      err

st_return:
        call    lex                     ; past RETURN
        ld      a, (tok)
        cp      T_NUM
        jr      z, st_ret_x
        cp      T_IDENT
        jr      z, st_ret_x
        cp      T_STR
        jr      z, st_ret_x
        cp      '('
        jr      z, st_ret_x
        cp      '-'
        jr      z, st_ret_x
        cp      '!'
        jr      z, st_ret_x
        cp      '~'
        jr      z, st_ret_x
        cp      '+'
        jr      z, st_ret_x
        ; no expression
        ld      a, (cfun_ret)
        or      a
        jr      z, st_ret_j             ; void: nothing
        ld      a, 021h                 ; LD HL, 0
        call    e_b
        ld      hl, 0
        call    e_w
        jr      st_ret_j
st_ret_x:
        ld      a, (cfun_ret)
        or      a
        jr      z, st_ret_v             ; void with a value
        call    parse_expr
        ld      a, (cfun_ret)
        cp      2
        jr      nz, st_ret_j
        ld      a, (exprt)
        or      a
        jr      nz, st_ret_j            ; already char
        ld      hl, sq_ldh0             ; LD H, 0 (truncate)
        ld      b, 2
        call    e_seq
st_ret_j:
        ld      hl, (retlab)
        jp      e_jp
st_ret_v:
        ld      a, E_TYPE
        jp      err

; ---------------------------------------------------------------
; st_for: for v = a to b [step s] ... next.
; Hidden slots (per nesting depth, in the frame's fixed
; hidden area): end at -(frame_user + 4 + 4*depth),
; step at -(frame_user + 2 + 4*depth).
; ---------------------------------------------------------------
st_for:
        call    lb_save
        ld      a, (fordep)
        cp      FORMAX
        jp      nc, pf_big
        call    lex                     ; variable name
        ld      a, (tok)
        cp      T_IDENT
        jp      nz, pf_syn
        call    find_var
        ld      a, E_UNDEF
        jp      c, err
        call    for_ptr_cur             ; HL = &forstk[depth]
        ld      a, (v_kind)
        ld      (hl), a
        inc     hl
        ld      a, (v_type)
        ld      (hl), a
        inc     hl
        ld      de, (v_disp)
        ld      (hl), e
        inc     hl
        ld      (hl), d
        inc     hl
        ; end/step displacements
        ld      de, (frame_user)
        ld      a, (fordep)
        add     a, a
        add     a, a                    ; 4*depth
        add     a, e
        ld      e, a
        jr      nc, sf_nc
        inc     d
sf_nc:
        ; DE = frame_user + 4*depth; end disp = -(DE+4),
        ; step disp = -(DE+2)
        push    hl
        ld      hl, 4
        add     hl, de
        ld      a, l
        cpl
        ld      l, a
        ld      a, h
        cpl
        ld      h, a
        inc     hl                      ; HL = -(DE+4)
        ex      de, hl                  ; DE = end disp
        pop     hl
        ld      (hl), e
        inc     hl
        ld      (hl), d
        inc     hl
        inc     de
        inc     de                      ; DE = step disp
        ld      (hl), e
        inc     hl
        ld      (hl), d
        ; init: v = a
        call    lex                     ; past the variable name
        ld      a, '='
        call    expect
        call    parse_expr
        call    for_restore
        call    store_var
        ld      a, K_TO
        call    expect
        call    parse_expr
        call    for_end_ptr             ; HL = &end disp
        ld      e, (hl)
        inc     hl
        ld      d, (hl)
        ld      (tmp_disp), de
        call    st_disp                 ; end slot = value
        ld      a, (tok)
        cp      K_STEP
        jr      nz, sf_nostep
        call    lex
        call    parse_expr
        jr      sf_step
sf_nostep:
        ld      a, 021h                 ; LD HL, 1
        call    e_b
        ld      hl, 1
        call    e_w
sf_step:
        call    for_step_ptr
        ld      e, (hl)
        inc     hl
        ld      d, (hl)
        ld      (tmp_disp), de
        call    st_disp                 ; step slot = value
        ; labels
        call    new_label
        ld      (lb1), hl               ; top
        call    new_label
        ld      (lb2), hl               ; continue
        call    new_label
        ld      (lb3), hl               ; end
        call    new_label
        ld      (lb4), hl               ; body
        call    new_label
        ld      (lb5), hl               ; negate
        call    new_label
        ld      (lb6), hl               ; test done
        ld      hl, (lb1)
        call    def_label
        ; test: diff = v - end, sign-adjusted by step
        call    for_end_ptr
        ld      e, (hl)
        inc     hl
        ld      d, (hl)
        ld      (tmp_disp), de
        call    ld_disp                 ; HL = end
        ld      a, 0e5h                 ; PUSH HL
        call    e_b
        call    for_restore
        call    load_var                ; HL = v
        ld      hl, sq_fsub             ; POP DE/OR A/SBC HL,DE
        ld      b, 4
        call    e_seq                   ; HL = v - end
        ld      a, 0e5h                 ; PUSH HL (diff)
        call    e_b
        call    for_step_ptr
        ld      e, (hl)
        inc     hl
        ld      d, (hl)
        ld      (tmp_disp), de
        call    ld_disp                 ; HL = step
        ld      a, 07ch                 ; LD A, H
        call    e_b
        ld      a, 0b7h                 ; OR A
        call    e_b
        ld      a, 0fah                 ; JP M, neg
        call    e_b
        ld      hl, (lb5)
        call    label_addr
        call    e_wm
        ld      hl, (lb6)
        call    e_jp                    ; JP done
        ld      hl, (lb5)
        call    def_label
        ld      hl, sq_fneg             ; POP HL/negate/PUSH HL
        ld      b, 8
        call    e_seq
        ld      hl, (lb6)
        call    def_label
        ld      a, 0e1h                 ; POP HL (diff)
        call    e_b
        ; exit when diff > 0
        ld      hl, sq_ftst             ; LD A,H/OR L
        ld      b, 2
        call    e_seq
        ld      a, 0cah                 ; JP Z, body
        call    e_b
        ld      hl, (lb4)
        call    label_addr
        call    e_wm
        ld      a, 0fah                 ; JP M, body
        call    e_b
        ld      hl, (lb4)
        call    label_addr
        call    e_wm
        ld      hl, (lb3)
        call    e_jp                    ; JP end
        ld      hl, (lb4)
        call    def_label
        ; body
        ld      a, (fordep)
        inc     a
        ld      (fordep), a
        ld      hl, (lb3)
        ld      de, (lb2)
        call    push_loop
        call    parse_stmts
        call    pop_loop
        ld      a, (fordep)
        dec     a
        ld      (fordep), a
        ld      a, K_NEXT
        call    expect
        ; increment: v = v + step
        ld      hl, (lb2)
        call    def_label
        call    for_restore
        call    load_var
        ld      a, 0e5h                 ; PUSH HL
        call    e_b
        call    for_step_ptr
        ld      e, (hl)
        inc     hl
        ld      d, (hl)
        ld      (tmp_disp), de
        call    ld_disp
        ld      hl, sq_fadd             ; POP DE/ADD HL,DE
        ld      b, 2
        call    e_seq
        call    for_restore
        call    store_var
        ld      hl, (lb1)
        call    e_jp
        ld      hl, (lb3)
        call    def_label
        call    lb_restore
        ret
sq_fsub: db     0d1h, 0b7h, 0edh, 052h  ; POP DE/OR A/SBC HL,DE
sq_fadd: db     0d1h, 019h              ; POP DE/ADD HL,DE
sq_ftst: db     07ch, 0b5h              ; LD A,H/OR L
sq_fneg: db     0e1h, 0afh, 095h, 06fh, 09fh, 094h, 067h, 0e5h
        ; POP HL / XOR A / SUB L / LD L,A / SBC A,A / SUB H
        ; / LD H,A / PUSH HL  (8 bytes; count fixed by caller)

; for_ptr_cur: HL = &forstk[fordep] (8-byte entries).
for_ptr_cur:
        ld      a, (fordep)
        jr      for_ptr_a
; for_ptr_prev: HL = &forstk[fordep - 1].
for_ptr_prev:
        ld      a, (fordep)
        dec     a
for_ptr_a:
        add     a, a
        add     a, a
        add     a, a                    ; *8
        ld      e, a
        ld      d, 0
        ld      hl, forstk
        add     hl, de
        ret

; for_end_ptr / for_step_ptr: HL = address of the end /
; step displacement field of the current for entry.
for_end_ptr:
        call    for_ptr_cur
        ld      de, 4
        add     hl, de
        ret
for_step_ptr:
        call    for_ptr_cur
        ld      de, 6
        add     hl, de
        ret
for_step_ptr_prev:
        call    for_ptr_prev
        ld      de, 6
        add     hl, de
        ret

; for_restore: reload v_kind/v_type/v_disp from the
; current for entry. for_restore_prev: from fordep-1.
for_restore:
        call    for_ptr_cur
        jr      fr_go
for_restore_prev:
        call    for_ptr_prev
fr_go:
        ld      a, (hl)
        ld      (v_kind), a
        inc     hl
        ld      a, (hl)
        ld      (v_type), a
        inc     hl
        ld      e, (hl)
        inc     hl
        ld      d, (hl)
        ld      (v_disp), de
        ret

; ---------------------------------------------------------------
; st_ident: assignment, array store, call statement, or
; print statement. The identifier is in namebuf.
; ---------------------------------------------------------------
st_ident:
        call    lex                     ; '(' or '=' or error
        cp      '('
        jr      z, si_call
        cp      '='
        jr      z, si_assign
        ld      a, E_SYNTAX
        jp      err
si_assign:
        call    find_var
        ld      a, E_UNDEF
        jp      c, err
        ld      a, (v_kind)
        ld      (asv_kind), a
        ld      hl, (v_disp)
        ld      (asv_disp), hl
        ld      a, (v_type)
        ld      (asv_type), a
        call    lex                     ; past '='
        call    parse_expr
        ld      a, (asv_kind)
        ld      (v_kind), a
        ld      hl, (asv_disp)
        ld      (v_disp), hl
        ld      a, (asv_type)
        ld      (v_type), a
        jp      store_var
si_call:
        ; function?
        call    find_func
        jr      nc, si_ufunc
        ; external?
        call    ext_lookup
        jr      nc, si_ext
        ; global array?
        call    find_global
        jr      c, si_undef
        ld      (gv_ptr), hl
        ld      de, 18
        add     hl, de
        ld      a, (hl)
        cp      2
        jr      c, si_undef             ; scalar with '('
        ; array: parse the index expression
        ld      hl, (gv_ptr)
        push    hl
        call    lex                     ; past '('
        call    parse_expr
        ld      a, ')'
        call    expect
        ld      a, (tok)
        cp      '='
        jr      z, si_ast
        ; array load used as a statement: value discarded
        pop     hl
        ld      (gv_ptr), hl
        jp      arr_load
si_ast:
        ; scale the index and push it
        ld      hl, (gv_ptr)
        ld      de, 18
        add     hl, de
        ld      a, (hl)
        cp      2
        jr      nz, si_as1
        ld      a, 029h                 ; ADD HL, HL
        call    e_b
si_as1:
        ld      a, 0e5h                 ; PUSH HL
        call    e_b
        call    lex                     ; past '='
        call    parse_expr
        pop     hl                      ; restore the array entry
        ld      (gv_ptr), hl
        jp      arr_store_tail
si_undef:
        ld      a, E_UNDEF
        jp      err
si_ufunc:
        ld      (cf_fptr), hl
        ld      de, 18
        add     hl, de
        ld      a, (hl)
        ld      (cf_ret), a
        inc     hl
        ld      a, (hl)
        ld      (cf_nargs), a
        xor     a
        ld      (cf_kind), a
        ld      (cf_expr), a
        jp      parse_call
si_ext:
        ld      a, (cf_libid)
        cp      0ffh
        jp      z, stmt_print           ; PRINT is special
        ld      a, 1
        ld      (cf_kind), a
        xor     a
        ld      (cf_expr), a
        jp      parse_call

; ext_lookup: resolve namebuf in the external function
; table. On success sets (cf_libid), (cf_nargs),
; (cf_ret) and clears carry.
ext_lookup:
        ld      hl, exttab
ex_lp:
        ld      a, (hl)
        cp      0eeh
        jr      z, ex_nf
        ld      (cf_libid), a
        inc     hl
        ld      a, (hl)
        ld      (cf_nargs), a
        inc     hl
        ld      a, (hl)
        ld      (cf_ret), a
        inc     hl
        push    hl
        call    name_eq
        jr      z, ex_yes
        pop     hl
ex_skip:
        ld      a, (hl)
        inc     hl
        or      a
        jr      nz, ex_skip
        jr      ex_lp
ex_yes:
        pop     hl
        or      a                       ; clear carry
        ret
ex_nf:
        scf
        ret

; parse_call: the current token is '('. The callee is in
; (cf_kind) 0 = user function (cf_fptr) / 1 = external
; (cf_libid); (cf_nargs) and (cf_ret) describe it.
; (cf_expr) = 1 when the call is used as a value.
; The descriptor is copied to pc_* on entry and the old
; pc_* are saved on the stack: argument parsing recurses
; and may clobber cf_* (and pc_*) freely.
parse_call:
        ld      hl, (pc_fptr)
        push    hl
        ld      hl, (pc_kind)           ; kind | libid << 8
        push    hl
        ld      hl, (pc_nargs)          ; nargs | ret << 8
        push    hl
        ld      hl, (pc_expr)           ; expr | n << 8
        push    hl
        ld      hl, (cf_fptr)
        ld      (pc_fptr), hl
        ld      a, (cf_kind)
        ld      (pc_kind), a
        ld      a, (cf_libid)
        ld      (pc_libid), a
        ld      a, (cf_nargs)
        ld      (pc_nargs), a
        ld      a, (cf_ret)
        ld      (pc_ret), a
        ld      a, (cf_expr)
        ld      (pc_expr), a
        xor     a
        ld      (pc_n), a
        call    lex                     ; past '('
        ld      a, (tok)
        cp      ')'
        jr      z, pc_fin
pc_alp:
        ld      a, (tok)
        cp      T_STR
        jr      z, pc_str               ; strings: print only
        call    parse_expr
        ld      a, 0e5h                 ; PUSH HL
        call    e_b
        ld      a, (pc_n)
        inc     a
        ld      (pc_n), a
        ld      a, (tok)
        cp      ','
        jr      nz, pc_fin
        call    lex
        jr      pc_alp
pc_str:
        ld      a, E_STR
        jp      err
pc_fin:
        ld      a, ')'
        call    expect
        ld      a, (pc_n)
        ld      hl, pc_nargs
        cp      (hl)
        jp      nz, pc_args
        ld      a, (pc_kind)
        or      a
        jr      nz, pc_ext
        ld      a, 0cdh                 ; CALL addr
        call    e_b
        ld      hl, (pc_fptr)
        ld      de, 16
        add     hl, de
        ld      e, (hl)
        inc     hl
        ld      d, (hl)
        ex      de, hl
        call    e_wm
        jr      pc_clean
pc_ext:
        ld      a, (pc_libid)
        call    mark_used
        ld      a, 0cdh
        call    e_b
        ld      a, (pc_libid)
        add     a, a
        ld      l, a
        ld      h, 0
        ld      de, libaddr
        add     hl, de
        ld      e, (hl)
        inc     hl
        ld      d, (hl)
        ex      de, hl
        call    e_wm
pc_clean:
        ld      a, (pc_n)
        or      a
        jr      z, pc_type
        cp      1
        jr      nz, pc_cl2
        ld      a, 0d1h                 ; POP DE (drop 1 arg)
        call    e_b
        jr      pc_type
pc_cl2:
        add     a, a
        ld      (pc_n2), a
        ld      a, 021h                 ; LD HL, 2*nargs
        call    e_b
        ld      hl, (pc_n2)
        ld      h, 0
        call    e_w
        ld      hl, sq_spadj            ; ADD HL,SP / LD SP,HL
        ld      b, 2
        call    e_seq
pc_type:
        ld      a, (pc_ret)
        or      a
        jr      nz, pc_ti
        ; void: an error in expression context
        ld      a, (pc_expr)
        or      a
        jr      nz, pc_verr
        xor     a
        ld      (exprt), a
        jr      pc_out
pc_ti:
        cp      2
        jr      z, pc_tc
        xor     a
        ld      (exprt), a
        jr      pc_out
pc_tc:
        ld      a, 1
        ld      (exprt), a
pc_out:
        pop     hl
        ld      (pc_expr), hl
        pop     hl
        ld      (pc_nargs), hl
        pop     hl
        ld      (pc_kind), hl
        pop     hl
        ld      (pc_fptr), hl
        ret
pc_verr:
        ld      a, E_TYPE
        jp      err
pc_args:
        ld      a, E_ARGS
        jp      err
sq_spadj: db    039h, 0f9h              ; ADD HL,SP / LD SP,HL

; ---------------------------------------------------------------
; stmt_print: print(...) - variadic. Each argument is
; printed by its own library call: strings via x_prints,
; values via x_printi; print() with no arguments emits
; a newline via x_printnl.
; ---------------------------------------------------------------
stmt_print:
        call    lex                     ; past '('
        ld      a, (tok)
        cp      ')'
        jr      z, sp_nl
sp_lp:
        ld      a, (tok)
        cp      T_STR
        jr      z, sp_str
        call    parse_expr
        ld      a, 0e5h                 ; PUSH HL
        call    e_b
        ld      a, 6                      ; x_printi
        call    sp_call
        jr      sp_nx
sp_str:
        ld      a, 021h                 ; LD HL, literal
        call    e_b
        call    lit_addr_hl
        call    e_wm
        call    lit_register
        ld      a, 0e5h                 ; PUSH HL
        call    e_b
        ld      a, 5                      ; x_prints
        call    sp_call
        call    lex                     ; past the string
sp_nx:
        ld      a, (tok)
        cp      ','
        jr      nz, sp_end
        call    lex
        jr      sp_lp
sp_end:
        ld      a, ')'
        jp      expect
sp_nl:
        ld      a, 7                      ; x_printnl
        call    sp_call0
        call    lex                     ; past ')'
        ret

; sp_call: emit CALL to library routine A (the argument
; is already pushed), then drop the argument.
sp_call:
        call    mark_used
        push    af
        ld      a, 0cdh
        call    e_b
        pop     af
        add     a, a
        ld      l, a
        ld      h, 0
        ld      de, libaddr
        add     hl, de
        ld      e, (hl)
        inc     hl
        ld      d, (hl)
        ex      de, hl
        call    e_wm
        ld      a, 0d1h                 ; POP DE
        jp      e_b

; sp_call0: emit CALL to library routine A (no argument).
sp_call0:
        call    mark_used
        push    af
        ld      a, 0cdh
        call    e_b
        pop     af
        add     a, a
        ld      l, a
        ld      h, 0
        ld      de, libaddr
        add     hl, de
        ld      e, (hl)
        inc     hl
        ld      d, (hl)
        ex      de, hl
        jp      e_wm

; lit_addr_hl: HL = the runtime address of the literal
; at the current pool cursor.
lit_addr_hl:
        ld      hl, (litbase)
        ld      de, (litcur)
        add     hl, de
        ld      de, ORIGIN
        add     hl, de
        ret

; lit_register: in the emit pass, copy strbuf (with its
; NUL) into the pool; always advance the pool cursor.
lit_register:
        ld      a, (pass)
        cp      2
        jr      nz, lr_adv
        ld      hl, (litbase)
        ld      de, (litcur)
        add     hl, de
        ld      de, (outbase_v)
        add     hl, de
        ex      de, hl                  ; DE = destination
        ld      hl, strbuf
        ld      a, (tokslen)
        ld      c, a
        ld      b, 0
        inc     bc                      ; include the NUL
        ldir
lr_adv:
        ld      hl, (litcur)
        ld      a, (tokslen)
        ld      e, a
        ld      d, 0
        add     hl, de
        inc     hl
        ld      (litcur), hl
        ret

; ---------------------------------------------------------------
; Expressions. Values are computed into HL; (exprt) is
; the result type (0 = int, 1 = char). Binary operators
; evaluate the left side, PUSH it, evaluate the right
; side, POP DE, and combine (DE = left, HL = right).
; ---------------------------------------------------------------
parse_expr:
        ld      a, 1
        jp      parse_bin

; parse_bin: A = minimum precedence (1..10).
parse_bin:
        push    af                      ; [minp]
        call    parse_unary
pb_lp:
        ld      a, (tok)
        call    op_prec                 ; A = precedence, 0 = none
        or      a
        jr      z, pb_done
        pop     bc                      ; B = minp
        push    bc
        cp      b
        jr      c, pb_done              ; prec < minp: stop
        ld      a, (tok)
        push    af                      ; [minp][op]
        call    lex                     ; consume the operator
        ld      a, 0e5h                 ; PUSH HL (left)
        call    e_b
        pop     af                      ; op token
        push    af
        call    op_prec
        inc     a                       ; right: prec + 1
        call    parse_bin
        pop     af                      ; op token
        ld      (emit_op_tok), a
        ld      a, 0d1h                 ; POP DE (left)
        call    e_b
        call    emit_op
        jr      pb_lp
pb_done:
        pop     af
        ret

; op_prec: A = token -> A = precedence (0 if not a
; binary operator).
op_prec:
        cp      T_OR
        jr      z, op_1
        cp      T_AND
        jr      z, op_2
        cp      '|'
        jr      z, op_3
        cp      '^'
        jr      z, op_4
        cp      '&'
        jr      z, op_5
        cp      T_EQ
        jr      z, op_6
        cp      T_NE
        jr      z, op_6
        cp      '<'
        jr      z, op_7
        cp      '>'
        jr      z, op_7
        cp      T_LE
        jr      z, op_7
        cp      T_GE
        jr      z, op_7
        cp      T_SHL
        jr      z, op_8
        cp      T_SHR
        jr      z, op_8
        cp      '+'
        jr      z, op_9
        cp      '-'
        jr      z, op_9
        cp      '*'
        jr      z, op_10
        cp      '/'
        jr      z, op_10
        cp      '%'
        jr      z, op_10
        xor     a
        ret
op_1:   ld      a, 1
        ret
op_2:   ld      a, 2
        ret
op_3:   ld      a, 3
        ret
op_4:   ld      a, 4
        ret
op_5:   ld      a, 5
        ret
op_6:   ld      a, 6
        ret
op_7:   ld      a, 7
        ret
op_8:   ld      a, 8
        ret
op_9:   ld      a, 9
        ret
op_10:  ld      a, 10
        ret

; emit_op: combine DE (left) and HL (right) per
; (emit_op_tok), result in HL. Sets (exprt) = int.
emit_op:
        xor     a
        ld      (exprt), a
        ld      a, (emit_op_tok)
        cp      '+'
        jr      z, eo_add
        cp      '-'
        jr      z, eo_sub
        cp      '*'
        jr      z, eo_mul
        cp      '/'
        jr      z, eo_div
        cp      '%'
        jr      z, eo_mod
        cp      T_SHL
        jr      z, eo_shl
        cp      T_SHR
        jr      z, eo_shr
        cp      '&'
        jr      z, eo_and
        cp      '|'
        jr      z, eo_or
        cp      '^'
        jp      z, eo_xor
        cp      T_AND
        jp      z, eo_land
        cp      T_OR
        jp      z, eo_lor
        cp      T_EQ
        jp      z, eo_eq
        cp      T_NE
        jp      z, eo_ne
        cp      '<'
        jp      z, eo_lt
        cp      '>'
        jp      z, eo_gt
        cp      T_LE
        jp      z, eo_le
        cp      T_GE
        jp      z, eo_ge
        ret                             ; cannot happen
eo_add:
        ld      a, 019h                 ; ADD HL, DE
        jp      e_b
eo_sub:
        ld      hl, sq_sub              ; EX/OR A/SBC HL,DE
        ld      b, 4
        jp      e_seq
sq_sub: db      0ebh, 0b7h, 0edh, 052h
eo_mul:
        xor     a
        jr      eo_lib
eo_div:
        ld      a, 1
        jr      eo_lib
eo_mod:
        ld      a, 2
        jr      eo_lib
eo_shl:
        ld      a, 3
        jr      eo_lib
eo_shr:
        ld      a, 4
; eo_lib: CALL library routine A (DE, HL -> HL).
eo_lib:
        call    mark_used
        push    af
        ld      a, 0cdh
        call    e_b
        pop     af
        add     a, a
        ld      l, a
        ld      h, 0
        ld      de, libaddr
        add     hl, de
        ld      e, (hl)
        inc     hl
        ld      d, (hl)
        ex      de, hl
        jp      e_wm
eo_and:
        ld      hl, sq_b_and
        ld      b, 6
        jp      e_seq
sq_b_and: db    07ah, 0a4h, 067h, 07bh, 0a5h, 06fh
eo_or:
        ld      hl, sq_b_or
        ld      b, 6
        jp      e_seq
sq_b_or: db     07ah, 0b4h, 067h, 07bh, 0b5h, 06fh
eo_xor:
        ld      hl, sq_b_xor
        ld      b, 6
        jp      e_seq
sq_b_xor: db    07ah, 0ach, 067h, 07bh, 0adh, 06fh
eo_land:
        ld      hl, sq_land
        ld      b, 26
        jp      e_seq
; bool(right) in HL; swap; bool(left) in HL; AND the lows.
sq_land: db     07ch, 0b5h, 021h, 1, 0, 020h, 3, 021h, 0, 0
        db      0ebh, 07ch, 0b5h, 021h, 1, 0, 020h, 3, 021h, 0, 0
        db      07bh, 0a5h, 06fh, 026h, 0
eo_lor:
        ld      hl, sq_lor
        ld      b, 26
        jp      e_seq
sq_lor: db      07ch, 0b5h, 021h, 1, 0, 020h, 3, 021h, 0, 0
        db      0ebh, 07ch, 0b5h, 021h, 1, 0, 020h, 3, 021h, 0, 0
        db      07bh, 0b5h, 06fh, 026h, 0
; comparisons: common prefix computes HL = left - right
; (EX DE,HL / OR A / SBC HL,DE) and sets the flags.
eo_eq:
        call    eo_cmp
        ld      hl, sq_eq               ; HL=1; JR Z,+3; HL=0
        ld      b, 8
        jp      e_seq
sq_eq:  db      021h, 1, 0, 028h, 3, 021h, 0, 0
eo_ne:
        call    eo_cmp
        ld      hl, sq_ne               ; HL=1; JR NZ,+3; HL=0
        ld      b, 8
        jp      e_seq
sq_ne:  db      021h, 1, 0, 020h, 3, 021h, 0, 0
eo_cmp:
        ld      hl, sq_sub
        ld      b, 4
        jp      e_seq
; signed comparisons: after eo_cmp, extract carry =
; sign XOR overflow from the saved flags:
; PUSH AF/POP BC/LD A,C/AND 4/RLCA x5/LD B,A/LD A,C/
; AND 80h/XOR B/RLCA -> carry = (left < right).
eo_lt:
        call    eo_cmp
        call    eo_sgn
        ld      hl, sq_lt               ; HL=1; JR C,+3; HL=0
        ld      b, 8
        jp      e_seq
sq_lt:  db      021h, 1, 0, 038h, 3, 021h, 0, 0
eo_ge:
        call    eo_cmp
        call    eo_sgn
        ld      hl, sq_ge               ; HL=0; JR C,+3; HL=1
        ld      b, 8
        jp      e_seq
sq_ge:  db      021h, 0, 0, 038h, 3, 021h, 1, 0
eo_le:
        call    eo_cmp
        call    eo_sgn
        ld      hl, sq_le
        ld      b, 13
        jp      e_seq
; HL=1; JR C,+8; LD A,C; AND 40h; JR NZ,+3; HL=0
sq_le:  db      021h, 1, 0, 038h, 8, 079h, 0e6h, 040h, 020h, 3
        db      021h, 0, 0
eo_gt:
        call    eo_cmp
        call    eo_sgn
        ld      hl, sq_gt
        ld      b, 13
        jp      e_seq
; HL=0; JR C,+8; LD A,C; AND 40h; JR NZ,+3; HL=1
sq_gt:  db      021h, 0, 0, 038h, 8, 079h, 0e6h, 040h, 020h, 3
        db      021h, 1, 0
eo_sgn:
        ld      hl, sq_sgn
        ld      b, 16
        jp      e_seq
sq_sgn: db      0f5h, 0c1h, 079h, 0e6h, 4, 07h, 07h, 07h, 07h
        db      07h, 047h, 079h, 0e6h, 080h, 0a8h, 07h

; parse_unary: unary operators, then a primary.
parse_unary:
        ld      a, (tok)
        cp      '-'
        jr      z, pu_neg
        cp      '!'
        jr      z, pu_not
        cp      '~'
        jr      z, pu_bnot
        cp      '+'
        jr      z, pu_plus
        jp      parse_primary
pu_neg:
        call    lex
        call    parse_unary
        ld      hl, sq_neg              ; XOR A/SUB L/LD L,A/
        ld      b, 6                    ; SBC A,A/SUB H/LD H,A
        call    e_seq
        xor     a
        ld      (exprt), a
        ret
sq_neg: db      0afh, 095h, 06fh, 09fh, 094h, 067h
pu_not:
        call    lex
        call    parse_unary
        ld      hl, sq_not              ; LD A,H/OR L/HL=1/
        ld      b, 10                   ; JR Z,+3/HL=0
        call    e_seq
        xor     a
        ld      (exprt), a
        ret
sq_not: db      07ch, 0b5h, 021h, 1, 0, 028h, 3, 021h, 0, 0
pu_bnot:
        call    lex
        call    parse_unary
        ld      hl, sq_bnot             ; complement HL
        ld      b, 6
        call    e_seq
        xor     a
        ld      (exprt), a
        ret
sq_bnot: db     07ch, 02fh, 067h, 07dh, 02fh, 06fh
pu_plus:
        call    lex
        jp      parse_unary

; parse_primary: number, parenthesised expression,
; variable, array element, function call.
parse_primary:
        ld      a, (tok)
        cp      T_NUM
        jr      z, pp_num
        cp      '('
        jr      z, pp_par
        cp      T_STR
        jr      z, pp_str
        cp      T_IDENT
        jr      z, pp_id
        ld      a, E_SYNTAX
        jp      err
pp_num:
        ld      a, 021h                 ; LD HL, value
        call    e_b
        ld      hl, (tokval)
        call    e_w
        xor     a
        ld      (exprt), a
        call    lex
        ret
pp_par:
        call    lex
        call    parse_expr
        ld      a, ')'
        jp      expect
pp_str:
        ld      a, E_STR
        jp      err
pp_id:
        ; function call?
        call    find_func
        jr      nc, pp_uf
        call    ext_lookup
        jp      nc, pp_ef
        ; array element?
        call    find_global
        jr      c, pp_var
        ld      (gv_ptr), hl
        ld      de, 18
        add     hl, de
        ld      a, (hl)
        cp      2
        jr      c, pp_var2              ; scalar global
        ; array: '(' index ')'
        call    lex
        cp      '('
        jp      nz, pp_type
        ld      hl, (gv_ptr)
        push    hl                      ; nested parses clobber gv_ptr
        call    lex
        call    parse_expr
        ld      a, ')'
        call    expect
        pop     hl
        ld      (gv_ptr), hl
        call    arr_load
        ld      hl, (gv_ptr)
        ld      de, 18
        add     hl, de
        ld      a, (hl)
        cp      2
        jr      z, pp_ai                ; int array
        ld      a, 1
        ld      (exprt), a
        ret
pp_ai:
        xor     a
        ld      (exprt), a
        ret
pp_var2:
        ; HL was advanced; re-resolve as a variable
        call    find_var
        jr      c, pp_undef
        call    load_var
        ld      a, (v_type)
        ld      (exprt), a
        call    lex
        ret
pp_var:
        call    find_var
        jr      c, pp_undef
        call    load_var
        ld      a, (v_type)
        ld      (exprt), a
        call    lex
        ret
pp_uf:
        ld      (cf_fptr), hl
        ld      de, 18
        add     hl, de
        ld      a, (hl)
        ld      (cf_ret), a
        inc     hl
        ld      a, (hl)
        ld      (cf_nargs), a
        xor     a
        ld      (cf_kind), a
        ld      a, 1
        ld      (cf_expr), a
        call    lex
        cp      '('
        jr      nz, pp_type
        jp      parse_call
pp_ef:
        ld      a, (cf_libid)
        cp      0ffh
        jr      z, pp_type              ; print has no value
        ld      a, 1
        ld      (cf_kind), a
        ld      (cf_expr), a
        call    lex
        cp      '('
        jr      nz, pp_type
        jp      parse_call
pp_type:
        ld      a, E_TYPE
        jp      err
pp_undef:
        ld      a, E_UNDEF
        jp      err

; ---------------------------------------------------------------
; Data.
; ---------------------------------------------------------------
msg_srcp: db    "SRC: ", 0
msg_outp: db    "OUT: ", 0
msg_ok: db      "OK ", 0
msg_err: db     "ERR LINE ", 0
str_main: db    "MAIN", 0

; keyword table: db code, "NAME", 0 ... 0
kwtab:
        db      K_FUNC, "FUNC", 0
        db      K_ENDFUNC, "ENDFUNC", 0
        db      K_INT, "INT", 0
        db      K_CHAR, "CHAR", 0
        db      K_VOID, "VOID", 0
        db      K_DIM, "DIM", 0
        db      K_IF, "IF", 0
        db      K_THEN, "THEN", 0
        db      K_ELSE, "ELSE", 0
        db      K_ENDIF, "ENDIF", 0
        db      K_WHILE, "WHILE", 0
        db      K_ENDWHILE, "ENDWHILE", 0
        db      K_REPEAT, "REPEAT", 0
        db      K_UNTIL, "UNTIL", 0
        db      K_FOR, "FOR", 0
        db      K_TO, "TO", 0
        db      K_STEP, "STEP", 0
        db      K_NEXT, "NEXT", 0
        db      K_BREAK, "BREAK", 0
        db      K_RETURN, "RETURN", 0
        db      0

; external function table: db libid, nargs, ret, "NAME", 0
; ret: 0 = void, 1 = int. libid 0ffh = PRINT (special).
exttab:
        db      0ffh, 0, 0, "PRINT", 0
        db      8, 0, 1, "INPUT", 0
        db      9, 2, 0, "LOCATE", 0
        db      10, 0, 0, "CLS", 0
        db      11, 0, 1, "INKEY", 0
        db      12, 0, 1, "STICK", 0
        db      13, 0, 0, "VSYNC", 0
        db      14, 1, 1, "PEEK", 0
        db      15, 2, 0, "POKE", 0
        db      16, 1, 0, "PUTC", 0
        db      0eeh

; the library tables (generated by tools/mklibtab.py)
        include "libtab.inc"

; ---- variables ----
saved_sp: dw    0
err_code: db    0
pd_val: dw      0
pd_ptr: dw      0
pd_buf: defs    8
find_name: dw   0
name_src: defs  11
name_out: defs  11
linebuf: defs   82
sv_len: dw      0
sv_addr: dw     0
sv_load: dw     0
sv_need: dw     0
sv_entry: dw    0
sv_rec: dw      0
sv_cur: dw      0
sv_ptr: dw      0
sv_left: dw     0
src_rec: dw     0
src_size: dw    0
src_left: dw    0
src_recno: dw   0
src_off: db     0
pb_valid: db    0
pb_ch: db       0
curline: dw     0
tok: db         0
tokval: dw      0
tokslen: db     0
namebuf: defs   16
savname: defs   16
strbuf: defs    64
lx_want: db     0
lx_two: db      0
lx_one: db      0
lx_kwcode: db   0
pass: db        0
outbase_v: dw   0
rt_stub: dw     0
epc: dw         0
label_cnt: dw   0
gcount: db      0
gtotal: dw      0
fcount: db      0
lcount: db      0
gk_kind: db     0
gk_kind2: db    0
gk_elems: dw    0
gk_ret: db      0
gk_fptr: dw     0
gk_nargs: db    0
used_mask: defs 3
libaddr: defs   40
litbase: dw     0
litcur: dw      0
libbase: dw     0
lay_cur: dw     0
lay_id: db      0
lay_val: dw     0
lsw_lo: dw      0
lsw_hi: db      0
bodysize: dw    0
bitmap_size: dw 0
total_file: dw  0
total_padded: dw 0
heap_blocks: dw 0
main_addr: dw   0
el_tab: dw      0
el_len: dw      0
el_cnt: db      0
el_ptr: dw      0
el_wa: dw       0
ef_tr: dw       0
ef_ck: dw       0
ef_val: dw      0
cfun_ret: db    0
cfun_ptr: dw    0
frame_user: dw  0
frame_tot: dw   0
retlab: dw      0
loopdep: db     0
fordep: db      0
loopstk: defs   32
forstk: defs    64
pf_kind: db     0
pf_nargs: db    0
pf_idx: db        0
pf_cum: dw      0
al_kind: db     0
al_param: db    0
lb1: dw         0
lb2: dw         0
lb3: dw         0
lb4: dw         0
lb5: dw         0
lb6: dw         0
v_kind: db      0
v_disp: dw      0
v_type: db      0
tmp_disp: dw    0
gv_ptr: dw      0
cf_kind: db     0
cf_fptr: dw     0
cf_libid: db    0
cf_nargs: db    0
cf_ret: db      0
cf_expr: db     0
pc_fptr: dw     0
pc_kind: db     0
pc_libid: db    0
pc_nargs: db    0
pc_ret: db      0
pc_expr: db     0
pc_n: db        0
pc_n2: dw       0
asv_kind: db    0
asv_disp: dw    0
asv_type: db    0
emit_op_tok: db 0
exprt: db       0
