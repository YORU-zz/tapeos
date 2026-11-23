; sccore.inc - the shared Small-C front end for SCC
; (compiler) and SCI (JIT interpreter). Included by both
; drivers; see scc.z80 / sci.z80 for the program shells.
; OUROBO/ROS reproduction project (P2), roadmap item 7.
;
; In the fiction, the C language of the new OS grows out of
; the X1/Z80 TinyBASIC work (ep. 161: the language ancestor
; is TinyBASIC, first written in K&R style); the ANSI-C
; self-hosting compiler of ep. 161 is ROS-era and out of
; scope here. This is the TOS-era Small-C: a Small-C-class
; subset compiler for TOSD programs. The subset definition
; (what is in, what is dropped, and why) is in
; smallc-tos.md.
;
; Architecture (inherited from TinyBASIC's tbcore, see
; tinybasic.md): the source is streamed from FD once per
; pass, like NEWASM. Three passes: pass 1 collects global
; variables and function signatures; pass 2a measures
; (parses everything, computes frame layouts, code sizes,
; the literal pool and the used library set); pass 2b emits
; the identical parse into the output buffer. Every
; emitted instruction has a size that does not depend on
; addresses (absolute jumps and calls everywhere), so the
; two parses agree exactly and no fixup list is needed:
; the TOSR relocation bitmap is written directly during
; emission. The JIT driver (SCI) reuses the same passes
; per function at call time, exactly like TBI.
;
; Differences from the TinyBASIC front end:
;   - C lexer: case-sensitive identifiers, char literals,
;     string escapes, // comments, the full operator set.
;   - A preprocessor: #include (depth <= 2, per-level
;     sector buffers) and object-like #define, expanded
;     through a small expansion stack in the reader.
;   - C statements: blocks with declarations, if/else,
;     while, do/while, for (the increment is parsed twice:
;     the increment's source text is captured at the
;     header and re-parsed after the body), break,
;     continue, return, expression statements.
;   - C expressions: full precedence, assignment as an
;     expression (incl. compound assignment), short-circuit
;     && and ||, ternary ?:, prefix/postfix ++ and --,
;     casts, single-level pointers, 1-D arrays (global and
;     local), string literals as char* values.
;   - Calling convention and TOSR output are identical to
;     TinyBASIC's: the caller pushes 16-bit argument slots
;     in source order (arg1 first) and CALLs; the callee
;     builds an IY frame - saved IY at (IY+0), return
;     address at (IY+2), argN at (IY+4), arg1 at
;     (IY + 4 + 2*(nargs-1)); locals live below IY at
;     negative displacements (assigned incrementally as
;     declarations are parsed, so declarations may appear
;     at the head of any block). The return value is in
;     HL. Globals live in a heap right behind the loaded
;     module, based at IX.
;
; Type codes (globals, locals, params, exprt alike):
;   0 = int, 1 = char, 2 = void, 3 = int*, 4 = char*,
;   5 = void*. Element size: char 1, everything else 2.
;
; Memory map (the tool's own, in the EDAS zone; the
; binary outgrows the 10,240-byte NEWASM/SCED frame --
; see smallc-tos.md for the size accounting):
;   04000h..071ffh  this program (code + data)
;   07200h          DIRBUF: TOSD directory sector
;   07300h..075ffh  SECBUF: source sector buffers, one
;                   256-byte page per include level
;   07600h..07dffh  symbol tables (scratch)
;   07e00h          FORBUF: for-increment capture (192)
;   07ec0h          pnames: pass-1 K&R name slots (128)
;   08000h..        output file image (stub at 08000h,
;                   body at 0806dh); the image is capped
;                   so it ends by 0c800h
;   0c800h..0d1ffh  BITMAP: TOSR relocation bitmap
;   0d200h          MACEXP: macro expansion stack buffer
;   0d300h          MACTAB: macro table (10 x 48)
; The 0c800h+ scratch sits above the largest image and
; below the stack (STACKTOP 0e000h); SCI additionally
; caps module + heap at 0c800h so this scratch survives
; while a JIT-compiled program runs (see layout).

; ---- IOCS (X1) ----
C_CONOUT        equ     0e406h          ; IOCS 2
C_PRTMSG        equ     0e409h          ; IOCS 3
C_CRLF          equ     0e40ch          ; IOCS 4
C_GETLIN        equ     0e40fh          ; IOCS 5
C_DREAD         equ     0e42dh          ; IOCS 15
C_DWRITE        equ     0e430h          ; IOCS 16

MACEXP          equ     0d200h
MACTAB          equ     0d300h
DIRBUF          equ     07200h
SECBUF          equ     07300h
BITMAP          equ     0c800h
; symbol tables live in the tool scratch area (as in
; NEWASM), not in the binary: 07600h..07dffh
forbuf          equ     07e00h          ; for-increment capture (192)
pnames          equ     07ec0h          ; pass-1 K&R names (8 x 16)
toksav          equ     07f40h          ; pending-token save (104)
capstk          equ     0d000h          ; for-capture save stack (512)
gtab            equ     07600h          ; GMAX * 32
ftab            equ     07900h          ; FMAX * 32
ltab            equ     07b00h          ; LMAX * 32
labtab          equ     07d00h          ; LABMAX * 2
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
T_CHR           equ     4
T_EQ            equ     0a0h            ; ==
T_NE            equ     0a1h            ; !=
T_LE            equ     0a2h            ; <=
T_GE            equ     0a3h            ; >=
T_AND           equ     0a4h            ; &&
T_OR            equ     0a5h            ; ||
T_SHL           equ     0a6h            ; <<
T_SHR           equ     0a7h            ; >>
T_INC           equ     0a8h            ; ++
T_DEC           equ     0a9h            ; --
T_AADD          equ     0aah            ; +=
T_ASUB          equ     0abh            ; -=
T_AMUL          equ     0ach            ; *=
T_ADIV          equ     0adh            ; /=
T_AMOD          equ     0aeh            ; %=
T_AAND          equ     0afh            ; &=
T_AOR           equ     0b0h            ; |=
T_AXOR          equ     0b1h            ; ^=
T_ASHL          equ     0b2h            ; <<=
T_ASHR          equ     0b3h            ; >>=
K_INT           equ     081h
K_CHAR          equ     082h
K_VOID          equ     083h
K_IF            equ     084h
K_ELSE          equ     085h
K_WHILE         equ     086h
K_DO            equ     087h
K_FOR           equ     088h
K_BREAK         equ     089h
K_CONTINUE      equ     08ah
K_RETURN        equ     08bh

; ---- error codes (also in the status block) ----
E_SYNTAX        equ     1
E_UNDEF         equ     2
E_DUP           equ     3
E_ARGS          equ     4
E_STR           equ     5
E_BIG           equ     6
E_FILE          equ     7
E_NOMAIN        equ     8
E_LOOP          equ     9               ; break/continue outside loop
E_TYPE          equ     10
E_PP            equ     11              ; preprocessor

; ---- table limits ----
GMAX            equ     24              ; globals
FMAX            equ     16              ; functions
LMAX            equ     16              ; locals + params
LABMAX          equ     128             ; labels
MMAX            equ     10              ; macros
INCMAX          equ     2               ; include nesting

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
; Streaming source reader with #include levels and macro
; expansion. The current level's state is the contiguous
; 12-byte block src_state:
;   src_rec(2) src_size(2) src_left(2) src_recno(2)
;   src_off(1) src_pad(1) curline(2)
; Suspended levels live in istk (INCMAX blocks of 12).
; Each level reads through its own sector buffer page
; (SECBUF + level * 256).
;
; Macro expansion: exp_seg holds EXPMAX segments of
; (start, ptr, end) word triples, offsets into MACEXP;
; (exp_sp) is the next free MACEXP offset. rd_raw serves
; expansion bytes before file bytes; a segment is popped
; when its ptr reaches its end.
; ---------------------------------------------------------------
EXPMAX          equ     4

src_rewind:
        ld      hl, (main_rec)
        ld      (src_rec), hl
        ld      hl, (main_size)
        ld      (src_size), hl
        xor     a
        ld      (inc_depth), a
        ld      (exp_depth), a
        ld      (mcount), a
        ld      hl, 0
        ld      (exp_sp), hl
        ld      hl, SECBUF
        ld      (cur_secbuf), hl
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
        ld      a, 1
        ld      (lx_bol), a             ; start of file = line start
        ret

; rd_raw: next raw byte in A (01ah at EOF). Counts lines.
rd_raw:
        ld      a, (pb_valid)
        or      a
        jr      z, rr_npb
        ld      a, (pb_depth)
        ld      hl, exp_depth
        cp      (hl)
        jr      z, rr_pb
rr_npb:
        ld      a, (exp_depth)
        or      a
        jr      z, rr_file
        ; serve one byte from the top expansion segment
        call    exp_top                 ; HL = &seg (start,ptr,end)
        ld      (seg_tmp), hl
        ld      de, 2
        add     hl, de                  ; &ptr
        ld      e, (hl)
        inc     hl
        ld      d, (hl)                 ; DE = ptr (offset)
        ld      hl, MACEXP
        add     hl, de
        ld      a, (hl)                 ; the byte
        ld      (exp_byte), a
        inc     de
        ld      (exp_newptr), de        ; DE = ptr + 1
        ld      hl, (seg_tmp)
        inc     hl
        inc     hl                      ; &ptr
        ld      (hl), e
        inc     hl
        ld      (hl), d                 ; store ptr + 1
        inc     hl                      ; &end
        ld      e, (hl)
        inc     hl
        ld      d, (hl)                 ; DE = end
        ld      hl, (exp_newptr)
        or      a
        sbc     hl, de
        jr      nz, rr_exp_ret
        call    exp_pop                 ; segment exhausted
rr_exp_ret:
        ld      a, (exp_byte)
        ret
rr_pb:
        xor     a
        ld      (pb_valid), a
        ld      a, (pb_ch)
        ret
rr_file:
rr_fresh:
        ld      hl, (src_left)
        ld      a, h
        or      l
        jr      z, rr_eofpop
        ld      a, (src_off)
        inc     a
        ld      (src_off), a
        jr      nz, rr_have
        ; load the next record into this level's buffer
        push    hl
        ld      hl, (src_rec)
        ld      de, (src_recno)
        add     hl, de
        ex      de, hl
        ld      hl, (cur_secbuf)
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
        ld      de, (cur_secbuf)
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
rr_eofpop:
        ld      a, (inc_depth)
        or      a
        jr      z, rr_eof
        call    inc_pop
        jp      rd_raw                  ; retry at the outer level
rr_eof:
        ld      a, 01ah
        ret

; rd_unget: push back the byte in A (one level).
rd_unget:
        ld      (pb_ch), a
        ld      a, (exp_depth)
        ld      (pb_depth), a
        ld      a, 1
        ld      (pb_valid), a
        ret

; exp_top: HL = &exp_seg[top] (6 bytes per segment).
exp_top:
        ld      a, (exp_depth)
        dec     a
        add     a, a
        ld      e, a
        add     a, a
        add     a, e                    ; *6
        ld      e, a
        ld      d, 0
        ld      hl, exp_seg
        add     hl, de
        ret

; exp_pop: drop the top segment; its buffer space is freed
; (exp_sp returns to the segment start).
exp_pop:
        call    exp_top
        ld      e, (hl)
        inc     hl
        ld      d, (hl)                 ; DE = segment start
        ld      (exp_sp), de
        ld      a, (exp_depth)
        dec     a
        ld      (exp_depth), a
        ret

; exp_push: push a segment whose text is (pp_ptr) of
; length (pp_len) (both set by the caller). Copies the
; text into MACEXP at (exp_sp).
exp_push:
        ld      a, (exp_depth)
        cp      EXPMAX
        jr      nc, xp_big
        ld      hl, (pp_len)
        ld      de, (exp_sp)
        add     hl, de
        ld      a, h
        or      a
        jr      nz, xp_big              ; > 256 bytes total
        ; copy the text
        ld      hl, (pp_ptr)
        ld      de, MACEXP
        ld      bc, (exp_sp)
        ex      de, hl
        add     hl, bc
        ex      de, hl                  ; DE = MACEXP + exp_sp
        ld      bc, (pp_len)
        ld      a, b
        or      c
        jr      z, xp_rec               ; empty: record only
        ldir
xp_rec:
        ; record (start, start, end) in exp_seg[depth]
        call    exp_slot                ; HL = &exp_seg[depth]
        ld      de, (exp_sp)
        ld      (hl), e
        inc     hl
        ld      (hl), d                 ; start
        inc     hl
        ld      (hl), e
        inc     hl
        ld      (hl), d                 ; ptr = start
        inc     hl                      ; &end
        push    hl
        ld      hl, (exp_sp)
        ld      de, (pp_len)
        add     hl, de
        ex      de, hl                  ; DE = end
        pop     hl                      ; &end
        ld      (hl), e
        inc     hl
        ld      (hl), d                 ; end
        ld      (exp_sp), de            ; exp_sp = end
        ld      a, (exp_depth)
        inc     a
        ld      (exp_depth), a
        ret

; exp_slot: HL = &exp_seg[(exp_depth)] (the next free slot).
exp_slot:
        ld      a, (exp_depth)
        add     a, a
        ld      e, a
        add     a, a
        add     a, e                    ; *6
        ld      e, a
        ld      d, 0
        ld      hl, exp_seg
        add     hl, de
        ret
xp_big:
        ld      a, E_BIG
        jp      err

; inc_save: push the current reader state onto istk at
; slot (inc_depth); the caller then sets the new level.
inc_save:
        ld      a, (inc_depth)
        add     a, a
        add     a, a
        ld      e, a
        add     a, a
        add     a, e                    ; *12
        ld      e, a
        ld      d, 0
        ld      hl, istk
        add     hl, de
        ex      de, hl
        ld      hl, src_state
        ld      bc, 12
        ldir
        ret

; inc_pop: restore the level below; (inc_depth) decreases.
inc_pop:
        ld      a, (inc_depth)
        dec     a
        ld      (inc_depth), a
        add     a, a
        add     a, a
        ld      e, a
        add     a, a
        add     a, e                    ; *12
        ld      e, a
        ld      d, 0
        ld      hl, istk
        add     hl, de
        ld      de, src_state
        ld      bc, 12
        ldir
        ; sector buffer for the restored depth
        ld      a, (inc_depth)
        ld      h, 0
        ld      l, a
        add     hl, hl
        add     hl, hl
        add     hl, hl
        add     hl, hl
        add     hl, hl
        add     hl, hl
        add     hl, hl
        add     hl, hl                  ; depth * 256
        ld      de, SECBUF
        add     hl, de
        ld      (cur_secbuf), hl
        xor     a
        ld      (pb_valid), a
        ret

; do_include: (incname) holds the file name text. Finds
; the file, pushes the current level, opens the new one.
do_include:
        ld      a, (inc_depth)
        cp      INCMAX
        jr      nc, di_pp
        ld      hl, incname
        ld      de, pp_name
        call    name_copy
        call    dir_load
        ld      hl, pp_name
        ld      (find_name), hl
        call    find_entry
        jr      c, di_file
        ld      de, 10
        add     hl, de
        ld      e, (hl)
        inc     hl
        ld      d, (hl)
        ld      (new_rec), de
        inc     hl
        ld      e, (hl)
        inc     hl
        ld      d, (hl)
        ld      (new_size), de
        call    inc_save
        ld      hl, (new_rec)
        ld      (src_rec), hl
        ld      hl, (new_size)
        ld      (src_size), hl
        ld      (src_left), hl
        ld      hl, 0
        ld      (src_recno), hl
        ld      a, 255
        ld      (src_off), a
        ld      hl, 1
        ld      (curline), hl
        ld      a, (inc_depth)
        inc     a
        ld      (inc_depth), a
        ld      h, 0
        ld      l, a
        add     hl, hl
        add     hl, hl
        add     hl, hl
        add     hl, hl
        add     hl, hl
        add     hl, hl
        add     hl, hl
        add     hl, hl                  ; depth * 256
        ld      de, SECBUF
        add     hl, de
        ld      (cur_secbuf), hl
        xor     a
        ld      (pb_valid), a
        ret
di_pp:
        ld      a, E_PP
        jp      err
di_file:
        ld      a, E_FILE
        jp      err

; ---------------------------------------------------------------
; Preprocessor lines. lex calls pp_line when a '#' is the
; first non-whitespace character of a line ((lx_bol) = 1).
; pp_line consumes the rest of the line (through the
; newline) into linebuf and acts on it:
;   #include "NAME"   -- open NAME as a new reader level
;   #define NAME text -- object-like macro (text <= 31
;                        chars); redefinition replaces
; Anything else is E_PP. Macros are (re)registered in
; every pass, in source order, exactly like the tokens
; they expand to.
; ---------------------------------------------------------------
pp_line:
        ld      hl, linebuf
        ld      b, 80                   ; store at most 80
ppl_rd:
        push    hl
        push    bc
        call    rd_raw
        pop     bc
        pop     hl
        cp      00ah
        jr      z, ppl_dn
        cp      01ah
        jr      z, ppl_dn
        ld      c, a
        ld      a, b
        or      a
        jr      z, ppl_rd               ; full: discard rest
        ld      (hl), c
        inc     hl
        dec     b
        jr      ppl_rd
ppl_dn:
        xor     a
        ld      (hl), a
        ld      a, 1
        ld      (lx_bol), a             ; the newline was consumed
        ; parse the directive word into pp_word
        ld      hl, linebuf
        call    pp_skipsp
        ld      de, pp_word
        ld      b, 7
ppl_w:
        ld      a, (hl)
        cp      'a'
        jr      c, ppl_wd
        cp      'z' + 1
        jr      nc, ppl_wd
        ld      (de), a
        inc     hl
        inc     de
        djnz    ppl_w
ppl_wd:
        xor     a
        ld      (de), a
        ld      (pp_ptr), hl
        ld      hl, pp_word
        ld      de, pp_include
        call    pp_eq
        jr      z, ppl_inc
        ld      hl, pp_word
        ld      de, pp_define
        call    pp_eq
        jr      z, ppl_def
        ld      a, E_PP
        jp      err
ppl_inc:
        ld      hl, (pp_ptr)
        call    pp_skipsp
        ld      a, (hl)
        cp      '"'
        jp      nz, pp_syn
        inc     hl
        ld      de, incname
        ld      b, 15
ppl_iq:
        ld      a, (hl)
        cp      '"'
        jr      z, ppl_id
        or      a
        jp      z, pp_syn
        ld      (de), a
        inc     hl
        inc     de
        djnz    ppl_iq
        jp      pp_syn
ppl_id:
        xor     a
        ld      (de), a
        jp      do_include
ppl_def:
        ld      hl, (pp_ptr)
        call    pp_skipsp
        ; macro name: identifier chars into namebuf
        ; (zero the field first: name_eq compares all
        ; 16 bytes, as in lx_ident; HL = the name's
        ; position in linebuf and is preserved)
        push    hl
        ld      hl, namebuf
        ld      b, 16
ppl_z:
        ld      (hl), 0
        inc     hl
        djnz    ppl_z
        pop     hl
        ld      de, namebuf
        ld      b, 15
ppl_nm:
        ld      a, (hl)
        call    pp_idchar
        jr      c, ppl_nd
        ld      (de), a
        inc     hl
        inc     de
        djnz    ppl_nm
ppl_nd:
        xor     a
        ld      (de), a
        ld      a, b
        cp      15
        jp      z, pp_syn               ; empty name
        call    pp_skipsp
        ld      (pp_ptr), hl            ; replacement starts here
        ; length: to NUL, trailing blanks trimmed
        ld      de, 0
ppl_ll:
        ld      a, (hl)
        or      a
        jr      z, ppl_ld
        inc     hl
        inc     de
        jr      ppl_ll
ppl_ld:
        ; trim: walk back over ' ', '\t', '\r'
        ld      a, d
        or      e
        jr      z, ppl_lz
        dec     hl
        ld      a, (hl)
        cp      ' '
        jr      z, ppl_lt
        cp      009h
        jr      z, ppl_lt
        cp      00dh
        jr      nz, ppl_lz
ppl_lt:
        dec     de
        jr      ppl_ld
ppl_lz:
        ld      (pp_len), de
        ld      a, d
        or      a
        jp      nz, pp_syn              ; replacement > 255
        ld      a, e
        cp      32
        jp      nc, pp_syn              ; replacement > 31 chars
        jp      reg_macro
pp_syn:
        ld      a, E_PP
        jp      err

; pp_skipsp: advance HL past spaces and tabs.
pp_skipsp:
        ld      a, (hl)
        cp      ' '
        jr      z, pps_1
        cp      009h
        ret     nz
pps_1:
        inc     hl
        jr      pp_skipsp

; pp_eq: compare the NUL-terminated strings at HL and DE.
; Z set iff equal. Clobbers A, HL, DE.
pp_eq:
        ld      a, (de)
        cp      (hl)
        ret     nz
        or      a
        ret     z
        inc     hl
        inc     de
        jr      pp_eq

; pp_idchar: carry set iff A is NOT an identifier char.
pp_idchar:
        cp      '_'
        ret     z                       ; '_' ok (carry clear? no:)
pp_idc:
        cp      '0'
        jr      c, pic_no
        cp      '9' + 1
        jr      c, pic_ok
        cp      'A'
        jr      c, pic_no
        cp      'Z' + 1
        jr      c, pic_ok
        cp      'a'
        jr      c, pic_no
        cp      'z' + 1
        jr      c, pic_ok
pic_no:
        scf
        ret
pic_ok:
        or      a
        ret

pp_include: db  "include", 0
pp_define: db   "define", 0

; reg_macro: register namebuf with replacement text at
; (pp_ptr), length (pp_len). Redefinition replaces.
reg_macro:
        call    mac_find
        jr      nc, rm_have             ; HL = entry
        ld      a, (mcount)
        cp      MMAX
        jp      nc, xp_big
        ; append: entry = MACTAB + mcount * 48
        ld      l, a
        ld      h, 0
        add     hl, hl
        add     hl, hl
        add     hl, hl
        add     hl, hl                  ; *16
        ld      e, l
        ld      d, h
        add     hl, hl                  ; *32
        add     hl, de                  ; *48
        ld      de, MACTAB
        add     hl, de
        push    hl
        call    copy_name               ; name (16 bytes) -> entry
        pop     hl
        ld      a, (mcount)
        inc     a
        ld      (mcount), a
rm_have:
        ld      de, 16
        add     hl, de                  ; &repl
        ex      de, hl
        ld      hl, (pp_ptr)
        ld      bc, (pp_len)
        ld      a, b
        or      c
        jr      z, rm_z
        ldir
rm_z:
        xor     a
        ld      (de), a                 ; NUL-terminate
        ret

; mac_find: namebuf in mactab -> HL = entry, carry clear;
; carry set if absent.
mac_find:
        ld      a, (mcount)
        or      a
        jr      z, mf_nf
        ld      b, a
        ld      hl, MACTAB
mf_lp:
        push    hl
        push    bc
        call    name_eq
        jr      z, mf_yes
        pop     bc
        pop     hl
        ld      de, 48
        add     hl, de
        djnz    mf_lp
mf_nf:
        scf
        ret
mf_yes:
        pop     bc
        pop     hl
        or      a
        ret

; ---------------------------------------------------------------
; Lexer. lex reads the next token into (tok); numbers set
; (tokval), identifiers set namebuf (case preserved),
; strings set strbuf and (tokslen), char literals set
; (tokval). The parser's current token is whatever lex
; produced last. Clobbers all registers.
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
        jr      z, lx_nl
        cp      '#'
        jr      z, lx_pp
        cp      '/'
        jr      z, lx_slash
        jr      lx_tok
lx_nl:
        ld      a, 1
        ld      (lx_bol), a
        jr      lx_ws
lx_pp:
        ld      a, (lx_bol)
        or      a
        jp      z, lx_synerr            ; '#' not at line start
        call    pp_line
        jr      lx_ws
lx_slash:
        call    rd_raw                  ; maybe a comment
        cp      '*'
        jr      z, lx_comment
        cp      '/'
        jr      z, lx_lcomment
        call    rd_unget
        ld      a, '/'
        jr      lx_div
lx_comment:
        call    rd_raw
        cp      01ah
        jp      z, lx_synerr
        cp      '*'
        jr      nz, lx_comment
        call    rd_raw
        cp      '/'
        jr      z, lx_ws
        call    rd_unget
        jr      lx_comment
lx_lcomment:
        call    rd_raw
        cp      00ah
        jr      z, lx_nl                ; newline ends it (sets bol)
        cp      01ah
        jr      z, lx_eof
        jr      lx_lcomment
lx_eof:
        xor     a                       ; T_EOF = 0
        ld      (tok), a
        ret
lx_div:
        ; '/' as an operator: '/=' or plain '/'
        xor     a
        ld      (lx_bol), a
        call    rd_raw
        cp      '='
        jr      z, lx_adiv
        call    rd_unget
        ld      a, '/'
        jp      lx_single
lx_adiv:
        ld      a, T_ADIV
        jp      lx_single
lx_tok:
        ; any token character clears the line-start flag
        push    af
        xor     a
        ld      (lx_bol), a
        pop     af
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
        cp      027h                    ; '\''
        jp      z, lx_charlit
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
        jp      z, lx_plus
        cp      '-'
        jp      z, lx_minus
        cp      '*'
        jp      z, lx_star
        cp      '%'
        jp      z, lx_pct
        cp      '^'
        jp      z, lx_caret
        cp      '~'
        jr      z, lx_single
        cp      '('
        jr      z, lx_single
        cp      ')'
        jr      z, lx_single
        cp      ','
        jr      z, lx_single
        cp      ';'
        jr      z, lx_single
        cp      '{'
        jr      z, lx_single
        cp      '}'
        jr      z, lx_single
        cp      '['
        jr      z, lx_single
        cp      ']'
        jr      z, lx_single
        cp      '?'
        jr      z, lx_single
        cp      ':'
        jr      z, lx_single
lx_synerr:
        ld      a, E_SYNTAX
        jp      err
lx_single:
        ld      (tok), a
        ret

; two/three-char operators. lx_op2 tests the next char
; against (lx_want): match -> (lx_two), else unget and
; (lx_one).
lx_eq:
        ld      a, '='
        ld      (lx_want), a
        ld      a, T_EQ
        ld      (lx_two), a
        ld      a, '='
        ld      (lx_one), a
        jp      lx_op2
lx_ne:
        ld      a, '='
        ld      (lx_want), a
        ld      a, T_NE
        ld      (lx_two), a
        ld      a, '!'
        ld      (lx_one), a
        jp      lx_op2
lx_amp:
        call    rd_raw
        cp      '&'
        jr      z, lx_and2
        cp      '='
        jr      z, lx_aand
        call    rd_unget
        ld      a, '&'
        jp      lx_single
lx_and2:
        ld      a, T_AND
        jp      lx_single
lx_aand:
        ld      a, T_AAND
        jp      lx_single
lx_bar:
        call    rd_raw
        cp      '|'
        jr      z, lx_or2
        cp      '='
        jr      z, lx_aor
        call    rd_unget
        ld      a, '|'
        jp      lx_single
lx_or2:
        ld      a, T_OR
        jp      lx_single
lx_aor:
        ld      a, T_AOR
        jp      lx_single
lx_plus:
        call    rd_raw
        cp      '+'
        jr      z, lx_inc
        cp      '='
        jr      z, lx_aadd
        call    rd_unget
        ld      a, '+'
        jp      lx_single
lx_inc:
        ld      a, T_INC
        jp      lx_single
lx_aadd:
        ld      a, T_AADD
        jp      lx_single
lx_minus:
        call    rd_raw
        cp      '-'
        jr      z, lx_mdec
        cp      '='
        jr      z, lx_asub
        call    rd_unget
        ld      a, '-'
        jp      lx_single
lx_mdec:
        ld      a, T_DEC
        jp      lx_single
lx_asub:
        ld      a, T_ASUB
        jp      lx_single
lx_star:
        ld      a, '='
        ld      (lx_want), a
        ld      a, T_AMUL
        ld      (lx_two), a
        ld      a, '*'
        ld      (lx_one), a
        jp      lx_op2
lx_pct:
        ld      a, '='
        ld      (lx_want), a
        ld      a, T_AMOD
        ld      (lx_two), a
        ld      a, '%'
        ld      (lx_one), a
        jp      lx_op2
lx_caret:
        ld      a, '='
        ld      (lx_want), a
        ld      a, T_AXOR
        ld      (lx_two), a
        ld      a, '^'
        ld      (lx_one), a
        jp      lx_op2
lx_lt:
        call    rd_raw
        cp      '<'
        jr      z, lx_shl
        cp      '='
        jr      z, lx_le
        call    rd_unget
        ld      a, '<'
        jp      lx_single
lx_shl:
        call    rd_raw
        cp      '='
        jr      z, lx_ashl
        call    rd_unget
        ld      a, T_SHL
        jp      lx_single
lx_ashl:
        ld      a, T_ASHL
        jp      lx_single
lx_le:
        ld      a, T_LE
        jp      lx_single
lx_gt:
        call    rd_raw
        cp      '>'
        jr      z, lx_shr
        cp      '='
        jr      z, lx_ge
        call    rd_unget
        ld      a, '>'
        jp      lx_single
lx_shr:
        call    rd_raw
        cp      '='
        jr      z, lx_ashr
        call    rd_unget
        ld      a, T_SHR
        jp      lx_single
lx_ashr:
        ld      a, T_ASHR
        jp      lx_single
lx_ge:
        ld      a, T_GE
        jp      lx_single
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
; case preserved, at most 15 chars (extras consumed).
; Then: keyword lookup, macro expansion, else T_IDENT.
lx_ident:
        ; zero all 16 bytes of namebuf first: name_eq
        ; compares the full field, so a tail left by a
        ; longer earlier identifier must not survive
        push    af
        ld      hl, namebuf
        ld      b, 16
lx_id_z:
        ld      (hl), 0
        inc     hl
        djnz    lx_id_z
        pop     af
        ld      hl, namebuf
        ld      b, 0                    ; stored length
lx_id_lp:
        cp      '_'
        jr      z, lx_id_st
        cp      'A'
        jr      c, lx_id_num            ; digits (and enders)
        cp      'Z' + 1
        jr      c, lx_id_st
        cp      'a'
        jr      c, lx_id_end            ; between 'Z' and 'a'
        cp      'z' + 1
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
lx_id_num:
        cp      '0'
        jr      c, lx_id_end
        cp      '9' + 1
        jr      nc, lx_id_end
        jr      lx_id_st
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
        ; macro? expand and re-lex
        call    mac_find
        jr      c, lx_id_tok
        ; HL = macro entry; replacement at +16
        ld      de, 16
        add     hl, de
        ld      (pp_ptr), hl
        ; length = strlen(repl)
        ld      de, 0
lx_ml:
        ld      a, (hl)
        or      a
        jr      z, lx_md
        inc     hl
        inc     de
        jr      lx_ml
lx_md:
        ld      (pp_len), de
        ld      a, d
        or      e
        jp      z, lex                  ; empty expansion
        call    exp_push
        jp      lex
lx_id_tok:
        ld      a, T_IDENT
        ld      (tok), a
        ret

; number: first digit in A. Decimal, 0x hex, or 0-octal.
; A trailing l/L/u/U suffix is consumed and ignored.
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
        cp      '0'
        jr      c, lx_num_end0          ; just "0"
        cp      '7' + 1
        jr      c, lx_oct
        jr      lx_num_end0             ; 8/9 after 0: end
lx_oct:
        sub     '0'
        ld      e, a
        ld      d, 0
        ld      hl, (tokval)
        add     hl, hl
        add     hl, hl
        add     hl, hl                  ; *8
        add     hl, de
        ld      (tokval), hl
        call    rd_raw
        cp      '0'
        jr      c, lx_num_end0
        cp      '7' + 1
        jr      c, lx_oct
        jr      lx_num_end0
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
lx_num_end0:
        ; suffix letters are consumed
        cp      'l'
        jr      z, lx_sfx
        cp      'L'
        jr      z, lx_sfx
        cp      'u'
        jr      z, lx_sfx
        cp      'U'
        jr      z, lx_sfx
        call    rd_unget
lx_num_ok:
        ld      a, T_NUM
        ld      (tok), a
        ret
lx_sfx:
        call    rd_raw
        jr      lx_num_end0
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
        jp      lx_num_end0
lx_hex_end2:
        jp      lx_num_end0

; escape: the char after a backslash is in A; returns the
; escaped value in A.
lx_escape:
        cp      'n'
        jr      z, lxe_n
        cp      't'
        jr      z, lxe_t
        cp      'r'
        jr      z, lxe_r
        cp      '0'
        jr      z, lxe_0
        ret                             ; \\, \', \" and any
                                        ; other char = itself
lxe_n:
        ld      a, 10
        ret
lxe_t:
        ld      a, 9
        ret
lxe_r:
        ld      a, 13
        ret
lxe_0:
        xor     a
        ret

; char literal: '\'' was consumed. One char (or escape),
; then the closing '\''. Value in (tokval), type int (C).
lx_charlit:
        call    rd_raw
        cp      027h
        jr      z, lx_str_err           ; '' is invalid
        cp      01ah
        jr      z, lx_str_err
        cp      '\'
        jr      nz, lx_cl_v
        call    rd_raw
        call    lx_escape
lx_cl_v:
        ld      l, a
        ld      h, 0
        ld      (tokval), hl
        call    rd_raw
        cp      027h
        jr      nz, lx_str_err
        ld      a, T_CHR
        ld      (tok), a
        ret

; string literal: collect into strbuf with escapes
; processed (max 63 chars; longer is E_STR).
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
        cp      '\'
        jr      nz, lx_str_st
        push    hl
        call    rd_raw
        call    lx_escape
        pop     hl
lx_str_st:
        ld      c, a
        ld      a, b
        cp      63
        jr      nc, lx_str_err          ; too long
        ld      (hl), c
        inc     hl
        inc     b
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
; high byte. In pass 0 (the no-emit scan used for the
; for-increment) e_b returns at once: nothing is counted
; or stored, but parsing, label allocation and library
; marking proceed exactly as in the real passes.
; All e_* routines preserve registers.
; ---------------------------------------------------------------
e_b:
        push    hl
        push    de
        push    af                      ; the byte to emit
        ld      a, (pass)
        or      a
        jr      z, e_b_done             ; pass 0: scan only
        ld      hl, (epc)
        ld      de, 03e00h
        or      a
        sbc     hl, de
        jr      nc, e_big
        ld      a, (pass)
        cp      2
        jr      nz, e_b_cnt             ; measure: count only
        ld      hl, (epc)
        ld      de, (outbase_v)
        add     hl, de
        pop     af                      ; the byte
        push    af
        ld      (hl), a
e_b_cnt:
        ld      hl, (epc)
        inc     hl
        ld      (epc), hl
e_b_done:
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

; e_seq: emit B bytes from HL.
e_seq:
        ld      a, (hl)
        call    e_b
        inc     hl
        djnz    e_seq
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
; bytes, NUL padded).
; Globals: +16 off, +18 type, +19 kind (0 scalar,
; 1 array), +20 elems.
; Functions: +16 addr, +18 ret type, +19 nargs,
; +20 ptypes[8], +28 frame size, +30 literal start.
; The parallel byte array fdef marks defined functions.
; Locals: +16 disp (signed), +18 type, +19 isparam,
; +20 block depth, +21 kind (0 scalar, 1 array),
; +22 elems.
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

ftab_ptr:                               ; A = index -> HL
        ld      l, a
        ld      h, 0
        add     hl, hl
        add     hl, hl
        add     hl, hl
        add     hl, hl
        add     hl, hl                  ; *32
        ld      de, ftab
        add     hl, de
        ret

ltab_ptr:                               ; A = index -> HL
        ld      l, a
        ld      h, 0
        add     hl, hl
        add     hl, hl
        add     hl, hl
        add     hl, hl
        add     hl, hl                  ; *32
        ld      de, ltab
        add     hl, de
        ret

; name_eq: compare namebuf with the 16-byte name at HL.
; Z set iff equal. Clobbers A, DE, HL.
name_eq:
        ld      de, namebuf
        ld      b, 16
ne_lp:
        ld      a, (de)
        cp      (hl)
        ret     nz
        inc     hl
        inc     de
        djnz    ne_lp
        xor     a
        ret

; find_global: namebuf -> HL = entry, carry clear; or
; carry set. (Adapted from TinyBASIC: its finders return
; the entry in HL with A = index preserved on the stack
; dance; here only HL matters.)
find_global:
        ld      a, (gcount)
        or      a
        jr      z, fg_nf
        ld      b, a
        xor     a
        ld      (find_i), a
        ld      hl, gtab
fg_lp:
        push    hl
        push    bc
        call    name_eq
        jr      z, fg_yes
        pop     bc
        pop     hl
        ld      de, 32
        add     hl, de
        ld      a, (find_i)
        inc     a
        ld      (find_i), a
        djnz    fg_lp
fg_nf:
        scf
        ret
fg_yes:
        pop     bc
        pop     hl
        or      a
        ret

; find_local: namebuf -> HL = entry, or carry set.
find_local:
        ld      a, (lcount)
        or      a
        jr      z, fg_nf
        ld      b, a
        ld      hl, ltab
fl_lp:
        push    hl
        push    bc
        call    name_eq
        jr      z, fg_yes
        pop     bc
        pop     hl
        ld      de, 32
        add     hl, de
        djnz    fl_lp
        scf
        ret

; find_func: namebuf -> HL = entry, A = index, or carry.
find_func:
        ld      a, (fcount)
        or      a
        jr      z, fg_nf
        ld      b, a
        xor     a
        ld      (find_i), a
        ld      hl, ftab
ffu_lp:
        push    hl
        push    bc
        call    name_eq
        jr      z, fg_yes
        pop     bc
        pop     hl
        ld      de, 32
        add     hl, de
        ld      a, (find_i)
        inc     a
        ld      (find_i), a
        djnz    ffu_lp
        scf
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
; Library table access (clibtab.inc, generated from
; clib.z80). Entry = 16 bytes: off, len, selfidx, nself,
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
; Type helpers.
; ---------------------------------------------------------------
; type_size: A = type -> A = element size (1 char, else 2).
type_size:
        cp      1
        jr      z, ts_1
        ld      a, 2
        ret
ts_1:
        ld      a, 1
        ret

; ptr_base: A = pointer type -> A = pointee type.
; (Caller guarantees A >= 3.)
ptr_base:
        sub     3
        ret

; make_ptr: A = base type -> A = pointer type.
make_ptr:
        add     a, 3
        ret

; ---------------------------------------------------------------
; Pass 1: collect globals and function signatures.
; Top-level grammar:
;   [type] [*] name ( params ) [{ | kr-decls { | ;]
;   type [*] name [ [ n ] ] (, [*] name [ [ n ] ])* ;
; A function with no type keyword is implicit int (K&R).
; Parameter lists may be ANSI (typed) or K&R (names only,
; types declared between ) and {). Prototypes (header
; ending in ';') register the signature only.
; ---------------------------------------------------------------
pass1:
        xor     a
        ld      (gcount), a
        ld      (fcount), a
        ld      (mcount), a
        ld      hl, 0
        ld      (gtotal), hl
        ld      hl, fdef
        ld      de, fdef + 1
        ld      bc, FMAX - 1
        ld      (hl), 0
        ldir
        call    src_rewind
        call    lex
p1_lp:
        ld      a, (tok)
        or      a
        ret     z                       ; EOF: done
        call    p1_item
        jr      p1_lp

; p1_item: one top-level item. The current token is a
; type keyword or an identifier (implicit-int function).
p1_item:
        ; optional base type
        ld      a, (tok)
        cp      K_INT
        jr      z, p1_ti
        cp      K_CHAR
        jr      z, p1_tc
        cp      K_VOID
        jr      z, p1_tv
        cp      T_IDENT
        jr      z, p1_impl              ; implicit int func
        jp      p1_syn
p1_ti:
        xor     a
        jr      p1_thave
p1_tc:
        ld      a, 1
        jr      p1_thave
p1_tv:
        ld      a, 2
p1_thave:
        ld      (gk_base), a
        call    lex                     ; first declarator start
        jr      p1_decl
p1_impl:
        xor     a
        ld      (gk_base), a            ; int
        ; namebuf already holds the name; tok is T_IDENT
        ld      a, (gk_base)
        ld      (gk_type), a
        jp      p1_after_name
p1_decl:
        ; pointer stars
        ld      a, (gk_base)
        ld      (gk_type), a
p1_star:
        ld      a, (tok)
        cp      '*'
        jr      nz, p1_name
        ld      a, (gk_type)
        cp      3
        jp      nc, p1_type_err         ; pointer to pointer
        add     a, 3
        ld      (gk_type), a
        call    lex
        jp      p1_star
p1_name:
        ld      a, (tok)
        cp      T_IDENT
        jp      nz, p1_syn
p1_after_name:
        call    lex                     ; token after the name
        ld      a, (tok)
        cp      '('
        jp      z, p1_func
        ; global declarator(s): the first is in
        ; namebuf/(gk_type); more may follow after ','
p1_gitem:
        ld      a, (gk_type)
        cp      2
        jp      z, p1_type_err          ; void variable
        xor     a
        ld      (gk_arr), a
        ld      hl, 1
        ld      (gk_elems), hl
        ld      a, (tok)
        cp      '['
        jr      nz, p1_greg
        call    lex
        ld      a, (tok)
        cp      T_NUM
        jp      nz, p1_syn
        ld      hl, (tokval)
        ld      a, h
        or      l
        jp      z, p1_syn               ; size must be >= 1
        ld      (gk_elems), hl
        ld      a, 1
        ld      (gk_arr), a
        call    lex
        ld      a, (tok)
        cp      ']'
        jp      nz, p1_syn
        call    lex
p1_greg:
        call    reg_global
        ld      a, (tok)
        cp      ','
        jr      nz, p1_gend
        call    lex                     ; next declarator
        ld      a, (gk_base)
        ld      (gk_type), a
        jp      p1_star
p1_gend:
        cp      ';'
        jp      nz, p1_syn
        call    lex
        ret
p1_syn:
        ld      a, E_SYNTAX
        jp      err
p1_type_err:
        ld      a, E_TYPE
        jp      err

; reg_global: register namebuf as a global with type
; (gk_type), array flag (gk_arr), count (gk_elems).
reg_global:
        call    find_global
        jp      nc, p1_dup
        call    find_func
        jp      nc, p1_dup
        ld      a, (gcount)
        cp      GMAX
        jp      nc, p1_big
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
        ld      a, (gk_type)
        ld      (hl), a                 ; type
        inc     hl
        ld      a, (gk_arr)
        ld      (hl), a                 ; kind: 0 scalar 1 array
        inc     hl
        ld      de, (gk_elems)
        ld      (hl), e
        inc     hl
        ld      (hl), d                 ; elems
        ; byte size = elems * element size
        ld      a, (gk_type)
        call    type_size
        ld      e, a
        ld      d, 0
        ld      hl, (gk_elems)
        call    mul_hl_de               ; HL = elems * size
        ld      de, (gtotal)
        add     hl, de
        ld      (gtotal), hl
        ld      a, (gcount)
        inc     a
        ld      (gcount), a
        ret
p1_dup:
        ld      a, E_DUP
        jp      err
p1_big:
        ld      a, E_BIG
        jp      err

; mul_hl_de: HL = HL * DE (small values; clobbers A, BC).
mul_hl_de:
        ld      b, h
        ld      c, l
        ld      hl, 0
        ld      a, 16
mhd_lp:
        srl     b
        rr      c
        jr      nc, mhd_no
        add     hl, de
mhd_no:
        sla     e
        rl      d
        dec     a
        jr      nz, mhd_lp
        ret

; p1_func: the current token is '(' after a declarator
; name; (gk_type) is the return type. Parses the
; parameter list (ANSI or K&R), registers the signature,
; then handles ';' (prototype), K&R declarations, and
; the body skip.
p1_func:
        ; find or create the function entry
        call    find_func
        jr      nc, p1f_have
        ld      a, (fcount)
        cp      FMAX
        jp      nc, p1_big
        call    ftab_ptr
        ld      (gk_fptr), hl
        call    copy_name
        ld      a, (fcount)
        inc     a
        ld      (fcount), a
        jr      p1f_sig
p1f_have:
        ld      (gk_fptr), hl
        ld      a, (find_i)
        ld      (gk_fidx), a
        ; a prototype after the definition is fine if it
        ; matches; a second definition is caught below.
        jr      p1f_sig2
p1f_sig:
        ld      a, (fcount)
        dec     a
        ld      (gk_fidx), a
p1f_sig2:
        xor     a
        ld      (gk_nargs), a
        ld      hl, pnames
        ld      (pn_ptr), hl
        call    lex                     ; first param token
        ld      a, (tok)
        cp      ')'
        jp      z, p1f_done             ; no params
        cp      K_VOID
        jr      nz, p1f_plp
        call    lex                     ; maybe "(void)"
        ld      a, (tok)
        cp      ')'
        jp      z, p1f_done
        jp      p1_syn
p1f_plp:
        ; one parameter: [type] [*] [name]
        ld      a, (tok)
        cp      K_INT
        jr      z, p1f_pi
        cp      K_CHAR
        jr      z, p1f_pc
        cp      K_VOID
        jr      z, p1f_pv
        cp      T_IDENT
        jr      z, p1f_pkr              ; K&R: name only
        jp      p1_syn
p1f_pi:
        xor     a
        jr      p1f_pt
p1f_pc:
        ld      a, 1
        jr      p1f_pt
p1f_pv:
        ld      a, 2
p1f_pt:
        ld      (gk_ptype), a
        call    lex
        jr      p1f_pstar
p1f_pkr:
        xor     a
        ld      (gk_ptype), a           ; default int (K&R)
        jr      p1f_pname
p1f_pstar:
        ld      a, (tok)
        cp      '*'
        jr      nz, p1f_pname
        ld      a, (gk_ptype)
        cp      3
        jp      nc, p1_type_err
        add     a, 3
        ld      (gk_ptype), a
        call    lex
        jr      p1f_pstar
p1f_pname:
        ld      a, (tok)
        cp      T_IDENT
        jr      nz, p1f_pnoname
        ; save the name in pnames (16 bytes each)
        ld      hl, (pn_ptr)
        ld      de, namebuf
        ld      bc, 16
        ex      de, hl
        ldir
        ld      (pn_ptr), de
        call    lex
        jr      p1f_preg
p1f_pnoname:
        ; unnamed parameter (prototype style): zero its
        ; pnames slot so K&R matching can never hit it
        ld      hl, (pn_ptr)
        ld      b, 16
p1f_pnn:
        ld      (hl), 0
        inc     hl
        djnz    p1f_pnn
        ld      (pn_ptr), hl
p1f_preg:
        ld      a, (gk_nargs)
        cp      8
        jp      nc, p1_big
        ld      hl, (gk_fptr)
        ld      de, 20
        add     hl, de
        ld      e, a
        ld      d, 0
        add     hl, de
        ld      a, (gk_ptype)
        ld      (hl), a                 ; ptypes[nargs]
        ld      a, (gk_nargs)
        inc     a
        ld      (gk_nargs), a
        ld      a, (tok)
        cp      ','
        jr      nz, p1f_pend
        call    lex
        jp      p1f_plp
p1f_pend:
        cp      ')'
        jp      nz, p1_syn
p1f_done:
        ; store ret + nargs (checking an earlier prototype)
        ld      hl, (gk_fptr)
        ld      de, 18
        add     hl, de
        ld      a, (gk_type)
        ld      (hl), a                 ; ret type
        inc     hl
        ld      a, (gk_nargs)
        ld      (hl), a                 ; nargs
        call    lex                     ; token after ')'
        ld      a, (tok)
        cp      ';'
        jr      z, p1f_proto
        cp      '{'
        jr      z, p1f_body
        ; K&R declarations: type [*] name (, [*] name)* ;
        ; repeated until '{'
p1f_kr:
        ld      a, (tok)
        cp      K_INT
        jr      z, p1f_ki
        cp      K_CHAR
        jr      z, p1f_kc
        jp      p1_syn
p1f_ki:
        xor     a
        jr      p1f_kt
p1f_kc:
        ld      a, 1
p1f_kt:
        ld      (gk_ptype), a
        ld      (gk_kbase), a
        call    lex
p1f_klp:
        ld      a, (tok)
        cp      '*'
        jr      nz, p1f_kn
        ld      a, (gk_ptype)
        cp      3
        jp      nc, p1_type_err
        add     a, 3
        ld      (gk_ptype), a
        call    lex
        jr      p1f_klp
p1f_kn:
        ld      a, (tok)
        cp      T_IDENT
        jp      nz, p1_syn
        call    kr_assign               ; type -> ptypes[name]
        call    lex
        cp      ','
        jr      nz, p1f_kend
        call    lex
        ld      a, (gk_kbase)
        ld      (gk_ptype), a           ; pointer part resets
        jr      p1f_klp
p1f_kend:
        cp      ';'
        jp      nz, p1_syn
        call    lex
        ld      a, (tok)
        cp      '{'
        jr      z, p1f_body
        jr      p1f_kr
p1f_proto:
        call    lex
        ret
p1f_body:
        ; a second definition is an error
        ld      a, (gk_fidx)
        ld      l, a
        ld      h, 0
        ld      de, fdef
        add     hl, de
        ld      a, (hl)
        or      a
        jp      nz, p1_dup
        ld      (hl), 1
        ; skip the body by brace counting (the depth
        ; lives in memory: lex clobbers all registers)
        ld      a, 1
        ld      (p1_depth), a
p1f_sk:
        call    lex
        ld      a, (tok)
        or      a
        jp      z, p1_syn               ; EOF inside function
        cp      '{'
        jr      z, p1f_ski
        cp      '}'
        jr      z, p1f_skd
        jr      p1f_sk
p1f_ski:
        ld      a, (p1_depth)
        inc     a
        ld      (p1_depth), a
        jr      p1f_sk
p1f_skd:
        ld      a, (p1_depth)
        dec     a
        ld      (p1_depth), a
        jr      nz, p1f_sk
        call    lex                     ; past '}'
        ret

; kr_assign: namebuf names a K&R parameter; set its
; ptypes entry to (gk_ptype). Unknown name is E_UNDEF.
kr_assign:
        ld      hl, pnames
        ld      a, (gk_nargs)
        or      a
        jr      z, ka_undef
        ld      b, a
        ld      c, 0                    ; index
ka_lp:
        push    hl
        push    bc
        call    name_eq
        jr      z, ka_yes
        pop     bc
        pop     hl
        ld      de, 16
        add     hl, de
        inc     c
        djnz    ka_lp
ka_undef:
        ld      a, E_UNDEF
        jp      err
ka_yes:
        pop     bc
        pop     hl
        ld      hl, (gk_fptr)
        ld      de, 20
        add     hl, de
        ld      e, c
        ld      d, 0
        add     hl, de
        ld      a, (gk_ptype)
        ld      (hl), a
        ret

; ---------------------------------------------------------------
; Layout (between the measure and emit passes): compute
; the literal pool base, close the used-library set over
; dependencies, assign library addresses, and derive all
; TOSR sizes. (Same algorithm as TinyBASIC's layout.)
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
        ; bitmap_size = (stub + bodysize + 7) / 8
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
        ; the heap must fit behind the module at origin:
        ; total_padded + gtotal <= (heap_limit), a driver
        ; supplied cap (SCC 06000h as in TinyBASIC; SCI
        ; 04800h so the module stays below the 0c800h
        ; scratch while it runs)
        ld      hl, (total_padded)
        ld      de, (gtotal)
        add     hl, de
        ld      de, (heap_limit)
        inc     de
        or      a
        sbc     hl, de
        jr      nc, lay_big             ; over the cap: too big
        ; find main
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
; (Same algorithm as TinyBASIC's emit_library.)
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
        ld      a, (hl)                 ; tgtid
        ld      (el_tgt), a
        inc     hl
        inc     hl                      ; skip pad
        ld      (el_ptr), hl
        ; word address = OUTBASE + cur + pos
        ld      hl, (lay_cur)
        add     hl, de
        ld      (el_wa), hl
        ld      de, (outbase_v)
        add     hl, de
        ex      de, hl                  ; DE = memory address
        ; value = libaddr[tgtid]
        ld      a, (el_tgt)
        add     a, a
        ld      l, a
        ld      h, 0
        push    de
        ld      de, libaddr
        add     hl, de
        ld      e, (hl)
        inc     hl
        ld      d, (hl)
        ex      de, hl
        pop     de
        ex      de, hl
        ld      (hl), e
        inc     hl
        ld      (hl), d
        ; mark the high byte
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
        ret

; ---------------------------------------------------------------
; emit_rtentry: the runtime entry at body offset 0.
;   LD HL, entry   (marked: after relocation HL = the
;                   runtime address of this instruction)
;   LD DE, -109    -> HL = module base
;   LD DE, total_padded ; ADD HL, DE -> heap base
;   PUSH HL / POP IX
;   zero the heap (globals area)
;   CALL main (marked)
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
        call    e_wm                    ; CALL main
        ld      a, 0c9h
        call    e_b                     ; RET
        ret
er_zloop:
        db      036h, 0, 023h, 00bh, 078h, 0b1h, 020h, 0f8h

; ---------------------------------------------------------------
; parse_program (measure and emit passes): the same
; top-level walk as pass 1; globals are consumed, each
; function is compiled by parse_function.
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
        call    lex
        call    emit_rtentry
pp_lp:
        ld      a, (tok)
        or      a
        ret     z                       ; EOF
        call    p2_item
        jr      pp_lp

; p2_item: parse the declarator head (type, stars, name)
; into (gk_*) and namebuf, then dispatch.
p2_item:
        ld      a, (tok)
        cp      K_INT
        jr      z, p2_ti
        cp      K_CHAR
        jr      z, p2_tc
        cp      K_VOID
        jr      z, p2_tv
        cp      T_IDENT
        jr      z, p2_impl
        jp      syn_err
p2_ti:
        xor     a
        jr      p2_thave
p2_tc:
        ld      a, 1
        jr      p2_thave
p2_tv:
        ld      a, 2
p2_thave:
        ld      (gk_base), a
        call    lex
        jr      p2_decl
p2_impl:
        xor     a
        ld      (gk_base), a
        ld      (gk_type), a
        jr      p2_after_name
p2_decl:
        ld      a, (gk_base)
        ld      (gk_type), a
p2_star:
        ld      a, (tok)
        cp      '*'
        jr      nz, p2_name
        ld      a, (gk_type)
        cp      3
        jp      nc, p1_type_err
        add     a, 3
        ld      (gk_type), a
        call    lex
        jr      p2_star
p2_name:
        ld      a, (tok)
        cp      T_IDENT
        jp      nz, syn_err
p2_after_name:
        call    lex
        ld      a, (tok)
        cp      '('
        jp      z, parse_function
        jp      parse_global2

; parse_global2: consume the rest of a global declaration
; (the first declarator's name/type are in namebuf /
; (gk_type); the current token follows the name).
parse_global2:
pg2_lp:
        ld      a, (tok)
        cp      '['
        jr      nz, pg2_nx
        call    lex                     ; size
        ld      a, (tok)
        cp      T_NUM
        jp      nz, syn_err
        call    lex
        ld      a, (tok)
        cp      ']'
        jp      nz, syn_err
        call    lex
pg2_nx:
        ld      a, (tok)
        cp      ','
        jr      nz, pg2_end
        call    lex                     ; next declarator
        ld      a, (gk_base)
        ld      (gk_type), a
pg2_star:
        ld      a, (tok)
        cp      '*'
        jr      nz, pg2_nm
        ld      a, (gk_type)
        cp      3
        jp      nc, p1_type_err
        add     a, 3
        ld      (gk_type), a
        call    lex
        jr      pg2_star
pg2_nm:
        ld      a, (tok)
        cp      T_IDENT
        jp      nz, syn_err
        call    lex
        jr      pg2_lp
pg2_end:
        cp      ';'
        jp      nz, syn_err
        call    lex
        ret

syn_err:
        ld      a, E_SYNTAX
        jp      err
type_err:
        ld      a, E_TYPE
        jp      err

; ---------------------------------------------------------------
; parse_function: compile one function. On entry the
; current token is '(' and (gk_type) is the declared
; return type; pass 1 registered the signature, which is
; authoritative for parameter types and the frame size.
; ---------------------------------------------------------------
parse_function:
        call    find_func
        ld      a, E_UNDEF
        jp      c, err
        ld      (cfun_ptr), hl
        ; return type from the entry (+18)
        ld      de, 18
        add     hl, de
        ld      a, (hl)
        ld      (cfun_ret), a
        inc     hl
        ld      a, (hl)
        ld      (cfun_nargs), a
        ; record the function address: ORIGIN + epc
        ld      hl, (cfun_ptr)
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
        ; locals: none yet; frame accumulator zero
        xor     a
        ld      (lcount), a
        ld      (cur_bdepth), a
        ld      hl, 0
        ld      (pf_cum), hl
        ; ---- parameters ----
        call    lex                     ; first param token
        xor     a
        ld      (pf_idx), a
        ld      a, (tok)
        cp      ')'
        jp      z, pf_pdone
        cp      K_VOID
        jr      nz, pf_plp
        call    lex                     ; "(void)" form
        ld      a, (tok)
        cp      ')'
        jp      nz, syn_err
        jr      pf_pdone
pf_plp:
        ; one parameter: [type] [*] name, or a K&R name.
        ; The TYPE always comes from ftab ptypes[idx].
        ld      a, (tok)
        cp      K_INT
        jr      z, pf_pskip
        cp      K_CHAR
        jr      z, pf_pskip
        cp      K_VOID
        jr      z, pf_pskip
        jr      pf_pname
pf_pskip:
        call    lex                     ; past the type word
pf_pstar:
        ld      a, (tok)
        cp      '*'
        jr      nz, pf_pname
        call    lex
        jr      pf_pstar
pf_pname:
        ld      a, (tok)
        cp      T_IDENT
        jp      nz, syn_err
        ; type = ptypes[pf_idx]
        ld      hl, (cfun_ptr)
        ld      de, 20
        add     hl, de
        ld      a, (pf_idx)
        ld      e, a
        ld      d, 0
        add     hl, de
        ld      a, (hl)
        ld      (al_type), a
        ; disp = 4 + 2*(nargs-1-idx)
        ld      a, (cfun_nargs)
        dec     a
        ld      hl, pf_idx
        sub     (hl)
        add     a, a
        add     a, 4
        ld      e, a
        ld      d, 0
        ld      (al_disp), de
        ld      a, 1
        ld      (al_param), a
        xor     a
        ld      (al_arr), a
        ld      hl, 1
        ld      (al_elems), hl
        call    add_local
        ld      a, (pf_idx)
        inc     a
        ld      (pf_idx), a
        call    lex
        cp      ','
        jr      nz, pf_pend
        call    lex
        jr      pf_plp
pf_pend:
        cp      ')'
        jp      nz, syn_err
pf_pdone:
        ld      a, (pf_idx)
        ld      hl, cfun_nargs
        cp      (hl)
        jp      nz, pf_args_err
        call    lex                     ; token after ')'
        ld      a, (tok)
        cp      '{'
        jr      z, pf_body
        cp      ';'
        jr      z, pf_proto             ; prototype: no code
        ; K&R declarations: consume until '{'
pf_kr:
        ld      a, (tok)
        cp      K_INT
        jr      z, pf_kr1
        cp      K_CHAR
        jr      z, pf_kr1
        jp      syn_err
pf_kr1:
        call    lex
pf_krlp:
        ld      a, (tok)
        cp      '*'
        jr      nz, pf_krn
        call    lex
        jr      pf_krlp
pf_krn:
        ld      a, (tok)
        cp      T_IDENT
        jp      nz, syn_err
        call    lex
        cp      ','
        jr      nz, pf_krend
        call    lex
        jr      pf_krlp
pf_krend:
        cp      ';'
        jp      nz, syn_err
        call    lex
        ld      a, (tok)
        cp      '{'
        jr      z, pf_body
        jr      pf_kr
pf_proto:
        call    lex
        ret
pf_body:
        ; frame size: the emit pass uses the value the
        ; measure pass stored at +28; the measure pass
        ; emits a placeholder (byte counts are identical).
        ld      hl, (cfun_ptr)
        ld      de, 28
        add     hl, de
        ld      e, (hl)
        inc     hl
        ld      d, (hl)
        ld      (pf_frame), de
        ; prologue: PUSH IY / LD IY,0 / ADD IY,SP /
        ; LD HL,-frame / ADD HL,SP / LD SP,HL
        ld      hl, pf_pro1
        ld      b, 9
        call    e_seq
        ld      hl, (pf_frame)
        ld      a, h
        cpl
        ld      h, a
        ld      a, l
        cpl
        ld      l, a
        inc     hl                      ; HL = -frame
        call    e_w
        ld      hl, pf_pro2
        ld      b, 2
        call    e_seq
        ; body block
        call    new_label
        ld      (retlab), hl
        call    parse_block
        ; measure pass: store the final frame size
        ld      a, (pass)
        cp      1
        jr      nz, pf_epi2
        ld      hl, (cfun_ptr)
        ld      de, 28
        add     hl, de
        ld      de, (pf_cum)
        ld      (hl), e
        inc     hl
        ld      (hl), d
pf_epi2:
        ld      hl, (retlab)
        call    def_label
        ld      hl, pf_epi
        ld      b, 5
        call    e_seq                   ; LD SP,IY/POP IY/RET
        ret
pf_args_err:
        ld      a, E_ARGS
        jp      err
pf_pro1:
        db      0fdh, 0e5h, 0fdh, 021h, 0, 0, 0fdh, 039h, 021h
pf_pro2:
        db      039h, 0f9h
pf_epi:
        db      0fdh, 0f9h, 0fdh, 0e1h, 0c9h

; add_local: register namebuf as a local. (al_type),
; (al_param), (al_arr), (al_elems) describe it; for
; parameters (al_disp) is preset, for locals the
; displacement is allocated from (pf_cum).
add_local:
        call    find_local
        jp      nc, p1_dup              ; no shadowing at all
        ld      a, (lcount)
        cp      LMAX
        jp      nc, p1_big
        call    ltab_ptr
        ld      (al_ep), hl
        call    copy_name
        ; displacement
        ld      a, (al_param)
        or      a
        jr      nz, al_pd
        ld      a, (al_arr)
        or      a
        jr      nz, al_arr2
        ld      a, (al_type)
        call    type_size
        ld      e, a
        ld      d, 0
        jr      al_alc
al_arr2:
        ld      a, (al_type)
        call    type_size
        ld      e, a
        ld      d, 0
        ld      hl, (al_elems)
        call    mul_hl_de
        ex      de, hl                  ; DE = byte size
al_alc:
        ld      hl, (pf_cum)
        add     hl, de
        ld      (pf_cum), hl            ; HL = new cum
        xor     a
        sub     l
        ld      e, a
        ld      a, 0
        sbc     a, h
        ld      d, a                    ; DE = -cum
        jr      al_st
al_pd:
        ld      de, (al_disp)
al_st:
        ld      hl, (al_ep)
        ld      bc, 16
        add     hl, bc                  ; +16
        ld      (hl), e
        inc     hl
        ld      (hl), d                 ; disp
        inc     hl
        ld      a, (al_type)
        ld      (hl), a                 ; +18 type
        inc     hl
        ld      a, (al_param)
        ld      (hl), a                 ; +19 isparam
        inc     hl
        ld      a, (cur_bdepth)
        ld      (hl), a                 ; +20 block depth
        inc     hl
        ld      a, (al_arr)
        ld      (hl), a                 ; +21 kind
        inc     hl
        ld      de, (al_elems)
        ld      (hl), e                 ; +22 elems
        inc     hl
        ld      (hl), d
        ld      a, (lcount)
        inc     a
        ld      (lcount), a
        ret

; ---------------------------------------------------------------
; Blocks and statements.
; Label registers lb1..lb6 are per-statement scratch:
; nested statements clobber them, so every handler that
; uses them saves them on entry and restores on exit.
; (Same pattern as TinyBASIC.)
; ---------------------------------------------------------------
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
        ; the stack holds lb6..lb1 (lb6 on top) with the
        ; statement routine's return address beneath them
        ; (lb_save left its own return address on top for
        ; its ret, and the routine's calls balanced out).
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
        ret

; expect: A = token that must be current; then advance.
expect:
        ld      hl, tok
        cp      (hl)
        jp      nz, syn_err
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
sq_jz:
        db      07ch, 0b5h, 0cah

; e_jnz: emit "LD A,H / OR L / JP NZ, label HL".
e_jnz:
        push    hl
        ld      hl, sq_jnz
        ld      b, 3
        call    e_seq
        pop     hl
        call    label_addr
        jp      e_wm
sq_jnz:
        db      07ch, 0b5h, 0c2h

; e_jp: emit "JP label HL".
e_jp:
        push    hl
        ld      a, 0c3h
        call    e_b
        pop     hl
        call    label_addr
        jp      e_wm

; push_loop: break label in HL, continue label in DE.
push_loop:
        ld      a, (loopdep)
        cp      8
        jp      nc, p1_big
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

; loop_cont: HL = the innermost continue label.
loop_cont:
        ld      a, (loopdep)
        dec     a
        add     a, a
        add     a, a
        ld      e, a
        ld      d, 0
        ld      hl, loopstk
        add     hl, de
        inc     hl
        inc     hl
        ld      e, (hl)
        inc     hl
        ld      d, (hl)
        ex      de, hl
        ret

; ---------------------------------------------------------------
; parse_block: the current token is '{'. Declarations may
; appear at the head of the block (and, as in C99, before
; any statement of the block -- here: anywhere before the
; closing brace, since each is registered as it is met);
; locals are function-scoped (no shadowing).
; ---------------------------------------------------------------
parse_block:
        ld      a, (blkdep)
        cp      8
        jp      nc, p1_big
        ld      hl, blkstk
        ld      e, a
        ld      d, 0
        add     hl, de
        ld      a, (lcount)
        ld      (hl), a                 ; save lcount
        ld      a, (blkdep)
        inc     a
        ld      (blkdep), a
        ld      a, (cur_bdepth)
        inc     a
        ld      (cur_bdepth), a
        call    lex                     ; past '{'
pb_lp:
        ld      a, (tok)
        cp      '}'
        jr      z, pb_done
        or      a
        jp      z, syn_err              ; EOF inside block
        cp      K_INT
        jr      z, pb_decl
        cp      K_CHAR
        jr      z, pb_decl
        cp      K_VOID
        jr      z, pb_decl
        call    parse_stmt
        jr      pb_lp
pb_decl:
        call    parse_local_decl
        jr      pb_lp
pb_done:
        call    lex                     ; past '}'
        ld      a, (cur_bdepth)
        dec     a
        ld      (cur_bdepth), a
        ld      a, (blkdep)
        dec     a
        ld      (blkdep), a
        ld      hl, blkstk
        ld      e, a
        ld      d, 0
        add     hl, de
        ld      a, (hl)
        ld      (lcount), a             ; restore lcount
        ret

; parse_local_decl: the current token is a type keyword.
parse_local_decl:
        ld      a, (tok)
        cp      K_INT
        jr      z, pld_i
        cp      K_CHAR
        jr      z, pld_c
        ld      a, 2                    ; void
        jr      pld_t
pld_i:
        xor     a
        jr      pld_t
pld_c:
        ld      a, 1
pld_t:
        ld      (gk_base), a
        call    lex
pld_lp:
        ld      a, (gk_base)
        ld      (al_type), a
pld_star:
        ld      a, (tok)
        cp      '*'
        jr      nz, pld_nm
        ld      a, (al_type)
        cp      3
        jp      nc, type_err
        add     a, 3
        ld      (al_type), a
        call    lex
        jr      pld_star
pld_nm:
        ld      a, (tok)
        cp      T_IDENT
        jp      nz, syn_err
        xor     a
        ld      (al_param), a
        ld      (al_arr), a
        ld      hl, 1
        ld      (al_elems), hl
        call    lex
        ld      a, (tok)
        cp      '['
        jr      nz, pld_reg
        call    lex
        ld      a, (tok)
        cp      T_NUM
        jp      nz, syn_err
        ld      hl, (tokval)
        ld      a, h
        or      l
        jp      z, syn_err
        ld      (al_elems), hl
        ld      a, 1
        ld      (al_arr), a
        call    lex
        ld      a, (tok)
        cp      ']'
        jp      nz, syn_err
        call    lex
pld_reg:
        ld      a, (al_type)
        cp      2
        jp      z, type_err             ; void variable
        call    add_local
        ld      a, (tok)
        cp      ','
        jr      nz, pld_end
        call    lex
        jr      pld_lp
pld_end:
        cp      ';'
        jp      nz, syn_err
        call    lex
        ret

; ---------------------------------------------------------------
; parse_stmt: the current token starts a statement.
; ---------------------------------------------------------------
parse_stmt:
        ld      a, (tok)
        cp      '{'
        jp      z, parse_block
        cp      K_IF
        jp      z, st_if
        cp      K_WHILE
        jp      z, st_while
        cp      K_DO
        jp      z, st_do
        cp      K_FOR
        jp      z, st_for
        cp      K_BREAK
        jp      z, st_break
        cp      K_CONTINUE
        jp      z, st_continue
        cp      K_RETURN
        jp      z, st_return
        cp      ';'
        jr      z, st_empty
        jp      st_expr
st_empty:
        call    lex
        ret

st_if:
        call    lb_save
        call    lex                     ; '('
        ld      a, '('
        call    expect
        call    parse_expr
        call    lv_load
        ld      a, ')'
        call    expect
        call    new_label
        ld      (lb1), hl               ; else label
        call    new_label
        ld      (lb2), hl               ; end label
        ld      hl, (lb1)
        call    e_jz
        call    parse_stmt
        ld      a, (tok)
        cp      K_ELSE
        jr      nz, si_noelse
        call    lex
        ld      hl, (lb2)
        call    e_jp
        ld      hl, (lb1)
        call    def_label
        call    parse_stmt
        ld      hl, (lb2)
        call    def_label
        jp      lb_restore
si_noelse:
        ld      hl, (lb1)
        call    def_label
        jp      lb_restore

st_while:
        call    lb_save
        call    new_label
        ld      (lb1), hl               ; top
        call    new_label
        ld      (lb2), hl               ; end
        ld      hl, (lb1)
        call    def_label
        call    lex                     ; '('
        ld      a, '('
        call    expect
        call    parse_expr
        call    lv_load
        ld      a, ')'
        call    expect
        ld      hl, (lb2)
        call    e_jz
        ld      hl, (lb2)
        ld      de, (lb1)
        call    push_loop               ; break=end, cont=top
        call    parse_stmt
        call    pop_loop
        ld      hl, (lb1)
        call    e_jp
        ld      hl, (lb2)
        call    def_label
        jp      lb_restore

st_do:
        call    lb_save
        call    new_label
        ld      (lb1), hl               ; top
        call    new_label
        ld      (lb2), hl               ; cond
        call    new_label
        ld      (lb3), hl               ; end
        ld      hl, (lb1)
        call    def_label
        call    lex                     ; past DO
        ld      hl, (lb3)
        ld      de, (lb2)
        call    push_loop               ; break=end, cont=cond
        call    parse_stmt
        call    pop_loop
        ld      a, K_WHILE
        call    expect
        ld      a, '('
        call    expect
        ld      hl, (lb2)
        call    def_label
        call    parse_expr
        call    lv_load
        ld      a, ')'
        call    expect
        ld      a, ';'
        call    expect
        ld      hl, (lb1)
        call    e_jnz
        ld      hl, (lb3)
        call    def_label
        jp      lb_restore

st_for:
        call    lb_save
        call    new_label
        ld      (lb1), hl               ; top (condition)
        call    new_label
        ld      (lb2), hl               ; increment
        call    new_label
        ld      (lb3), hl               ; end
        call    lex                     ; '('
        ld      a, '('
        call    expect
        ld      a, (tok)
        cp      ';'
        jr      z, sf_noinit
        call    parse_expr              ; init
        call    lv_load
sf_noinit:
        ld      a, ';'
        call    expect
        ld      hl, (lb1)
        call    def_label
        ld      a, (tok)
        cp      ';'
        jr      z, sf_nocond
        call    parse_expr              ; condition
        call    lv_load
        ld      hl, (lb3)
        call    e_jz
sf_nocond:
        ; the current token must be the ';' after the
        ; condition. Do NOT lex here: ';' is a single-
        ; char token with no lookahead, so the raw
        ; stream sits right after it, at the increment.
        ld      a, (tok)
        cp      ';'
        jp      nz, syn_err
        ; Capture the increment's raw source bytes into
        ; FORBUF (paren/quote aware), consuming the ')'.
        ; After the body, the captured text plus a ')'
        ; is pushed as a macro expansion segment and the
        ; increment is parsed from it; the file reader is
        ; already positioned past the real ')'.
        call    cap_incr
        ; a nested for in the body would overwrite
        ; FORBUF with its own capture: stack ours away
        ; until the body is parsed
        call    fsv_push
        call    lex                     ; body's first token
        ld      hl, (lb3)
        ld      de, (lb2)
        call    push_loop               ; break=end, cont=incr
        call    parse_stmt              ; body
        call    pop_loop
        call    fsv_pop                 ; our capture back
        ; The body's parse has already lexed the first
        ; token of the NEXT statement (the parser always
        ; holds one token of lookahead). The segment
        ; re-parse below would discard it, so save the
        ; whole pending-token state first and restore it
        ; at sf_noincr instead of lexing there.
        call    toksave
        ; push FORBUF (+ ')') as an expansion segment
        ld      hl, forbuf
        ld      (pp_ptr), hl
        ld      hl, (cap_len)
        inc     hl                      ; room for the ')'
        ld      (pp_len), hl
        call    exp_push
        ld      hl, (lb2)
        call    def_label
        call    lex                     ; first incr token
        ld      a, (tok)
        cp      ')'
        jr      z, sf_noincr
        call    parse_expr              ; the increment
        call    lv_load
sf_noincr:
        ld      a, (tok)
        cp      ')'
        jp      nz, syn_err
        call    tokrestore              ; the saved token
        ld      hl, (lb1)
        call    e_jp
        ld      hl, (lb3)
        call    def_label
        jp      lb_restore

; cap_incr: copy raw source bytes (via rd_raw) into
; FORBUF until the ')' that closes the for-header at
; paren depth 0; that ')' is consumed but not stored.
; A ')' byte is appended at forbuf + (cap_len) by the
; caller through (pp_len); here (cap_len) counts only
; the captured bytes. String/char literals and nested
; parens are tracked so a ')' inside them is copied.
; FORBUF is 192 bytes in the scratch page; a longer
; increment is E_BIG.
cap_incr:
        ld      hl, 0
        ld      (cap_len), hl
        xor     a
        ld      (cap_dep), a
        ld      (cap_str), a
        ld      (cap_chr), a
        ld      (cap_esc), a
ci_lp:
        call    rd_raw
        cp      01ah
        jp      z, syn_err              ; EOF in for-header
        ld      (cap_c), a
        ld      a, (cap_esc)
        or      a
        jr      z, ci_nesc
        xor     a
        ld      (cap_esc), a
        jr      ci_store
ci_nesc:
        ld      a, (cap_str)
        or      a
        jr      z, ci_nstr
        ld      a, (cap_c)
        cp      '\'
        jr      z, ci_esc1
        cp      '"'
        jr      nz, ci_store
        xor     a
        ld      (cap_str), a
        jr      ci_store
ci_nstr:
        ld      a, (cap_chr)
        or      a
        jr      z, ci_nchr
        ld      a, (cap_c)
        cp      '\'
        jr      z, ci_esc1
        cp      027h
        jr      nz, ci_store
        xor     a
        ld      (cap_chr), a
        jr      ci_store
ci_esc1:
        ld      a, 1
        ld      (cap_esc), a
        jr      ci_store
ci_nchr:
        ld      a, (cap_c)
        cp      '"'
        jr      z, ci_str1
        cp      027h
        jr      z, ci_chr1
        cp      '('
        jr      z, ci_op
        cp      ')'
        jr      z, ci_cl
        jr      ci_store
ci_str1:
        ld      a, 1
        ld      (cap_str), a
        jr      ci_store
ci_chr1:
        ld      a, 1
        ld      (cap_chr), a
        jr      ci_store
ci_op:
        ld      a, (cap_dep)
        inc     a
        ld      (cap_dep), a
        jr      ci_store
ci_cl:
        ld      a, (cap_dep)
        or      a
        jr      z, ci_done              ; the header's ')'
        dec     a
        ld      (cap_dep), a
ci_store:
        ld      hl, (cap_len)
        ld      a, h
        or      a
        jp      nz, p1_big
        ld      a, l
        cp      191
        jp      nc, p1_big
        ld      de, forbuf
        add     hl, de
        ld      a, (cap_c)
        ld      (hl), a
        ld      hl, (cap_len)
        inc     hl
        ld      (cap_len), hl
        jp      ci_lp
ci_done:
        ; append the synthetic ')' the re-parse expects
        ld      hl, (cap_len)
        ld      de, forbuf
        add     hl, de
        ld      (hl), ')'
        ret

; fsv_push / fsv_pop: stack the current for-capture
; (cap_len + FORBUF[0..cap_len], the appended ')'
; included) in the capture stack at capstk (0d000h,
; 512 bytes below MACEXP; the relocation bitmap ends
; by 0cfc0h at the largest body, so this is free).
; Entry: [cap_len(2)][bytes][total(2)], total =
; cap_len + 5. Overflow is E_BIG.
fsv_push:
        ld      hl, (cap_len)
        ld      de, 5
        add     hl, de                  ; HL = total
        ld      (fsv_len), hl
        ld      de, (fsv_sp)
        add     hl, de
        ld      a, h
        cp      2
        jp      nc, p1_big              ; > 512 bytes
        ld      (fsv_sp), hl
        ld      hl, capstk
        add     hl, de                  ; HL = entry start
        ld      de, (cap_len)
        ld      (hl), e
        inc     hl
        ld      (hl), d
        inc     hl
        ex      de, hl
        ld      hl, forbuf
        ld      bc, (cap_len)
        inc     bc                      ; + the ')'
        ldir
        ex      de, hl
        ld      de, (fsv_len)
        ld      (hl), e
        inc     hl
        ld      (hl), d
        ret
fsv_pop:
        ; (fsv_sp) is an OFFSET into capstk, not a
        ; pointer: form the absolute entry-end first
        ld      hl, (fsv_sp)
        ld      de, capstk
        add     hl, de                  ; HL = entry end
        dec     hl
        ld      d, (hl)                 ; total, high byte
        dec     hl
        ld      e, (hl)                 ; total, low byte
        inc     hl
        inc     hl                      ; HL = entry end
        or      a
        sbc     hl, de                  ; HL = entry start
        push    hl
        ld      de, capstk
        or      a
        sbc     hl, de                  ; HL = entry offset
        ld      (fsv_sp), hl
        pop     hl                      ; HL = entry start
        ld      e, (hl)
        inc     hl
        ld      d, (hl)                 ; DE = cap_len
        ld      (cap_len), de
        inc     hl                      ; HL = the bytes
        ld      de, forbuf
        ld      bc, (cap_len)
        inc     bc                      ; + the ')'
        ldir
        ret

; toksave / tokrestore: copy the pending-token state
; to / from the save area at 07f40h (free scratch after
; pnames). The state is the contiguous BSS span from
; pb_valid through strbuf (pb_valid, pb_ch, lx_bol, tok,
; tokval, tokslen, namebuf, savname, strbuf = 103
; bytes); lx_bol and savname ride along harmlessly.
; Used by st_for around the increment re-parse.
toksave:
        ld      hl, pb_valid
        ld      de, toksav
        ld      bc, 104
        ldir
        ; the saved pushback belongs to the token stream
        ; after the re-parse; the segment parse must
        ; start with a clean pushback
        xor     a
        ld      (pb_valid), a
        ret
tokrestore:
        ld      hl, toksav
        ld      de, pb_valid
        ld      bc, 104
        ldir
        ret

st_break:
        ld      a, (loopdep)
        or      a
        jr      z, st_looperr
        call    loop_break
        call    e_jp
        call    lex
        ld      a, ';'
        jp      expect
st_continue:
        ld      a, (loopdep)
        or      a
        jr      z, st_looperr
        call    loop_cont
        call    e_jp
        call    lex
        ld      a, ';'
        jp      expect
st_looperr:
        ld      a, E_LOOP
        jp      err

st_return:
        call    lex                     ; past RETURN
        ld      a, (tok)
        cp      ';'
        jr      z, sr_done
        ld      a, (cfun_ret)
        cp      2
        jp      z, type_err             ; value in void func
        call    parse_expr
        call    lv_load
        ld      a, (cfun_ret)
        cp      1
        jr      nz, sr_jp               ; not char: keep HL
        ld      a, (exprt)
        cp      1
        jr      z, sr_jp                ; already char (H=0)
        ld      hl, sq_ldh0             ; truncate: LD H, 0
        ld      b, 2
        call    e_seq
sr_jp:
        ld      hl, (retlab)
        call    e_jp
sr_done:
        ld      a, ';'
        jp      expect

st_expr:
        call    parse_expr
        call    lv_load
        ld      a, ';'
        jp      expect

; ---------------------------------------------------------------
; Expressions. Values are computed into HL; (exprt) is
; the result type (C type codes). Binary operators
; evaluate the left side, PUSH it, evaluate the right
; side, POP DE, and combine (DE = left, HL = right).
;
; Lvalues: a designator (variable, array subscript, *p)
; is parsed in "address mode": the emitted code computes
; the ADDRESS in HL, and (lv_valid) = 1 with (lv_type)
; the element type and (lv_isarray) marking an array
; designator (whose address is its base). lv_load turns
; an lvalue into its value (for arrays: decay to a
; pointer, no code). Every consumer of a value calls
; lv_load first; assignment and ++/-- use the address
; form directly.
; ---------------------------------------------------------------

; chk_val: a void value used as a value is E_TYPE.
chk_val:
        ld      a, (exprt)
        cp      2
        ret     nz
        jp      type_err

; lv_load: if (lv_valid), emit the load through HL (or
; decay an array to a pointer). Sets (exprt).
lv_load:
        ld      a, (lv_valid)
        or      a
        ret     z
        xor     a
        ld      (lv_valid), a
        ld      a, (lv_isarray)
        or      a
        jr      nz, ll_decay
        ld      a, (lv_type)
        ld      (exprt), a
        cp      1
        jr      z, ll_c
        ld      hl, sq_ldw              ; LD E,(HL)/INC/LD D/EX
        ld      b, 4
        jp      e_seq
ll_c:
        ld      hl, sq_ldb              ; LD L,(HL)/LD H,0
        ld      b, 3
        jp      e_seq
ll_decay:
        xor     a
        ld      (lv_isarray), a
        ld      a, (lv_type)
        add     a, 3                    ; pointer to element
        ld      (exprt), a
        ret
sq_ldw: db      05eh, 023h, 056h, 0ebh
sq_ldb: db      06eh, 026h, 0

; lv_store_de: emit a store of HL through the address in
; DE, at the element size of (lv_type). HL is preserved
; (it is the assignment's result value).
lv_store_de:
        ld      a, (lv_type)
        cp      1
        jr      z, lsd_c
        ld      a, 07dh                 ; LD A, L
        call    e_b
        ld      a, 012h                 ; LD (DE), A
        call    e_b
        ld      a, 013h                 ; INC DE
        call    e_b
        ld      a, 07ch                 ; LD A, H
        call    e_b
        ld      a, 012h                 ; LD (DE), A
        jp      e_b
lsd_c:
        ld      a, 07dh                 ; LD A, L
        call    e_b
        ld      a, 012h                 ; LD (DE), A
        jp      e_b

; lv_step: emit INC/DEC of HL by the step for (lv_type):
; int* steps by 2, everything else by 1. A = 0: increment,
; A = 1: decrement.
lv_step:
        or      a
        jr      nz, lvs_dec
        ld      a, (lv_type)
        cp      3                       ; int*
        jr      z, lvs_i2
        ld      a, 023h                 ; INC HL
        jp      e_b
lvs_i2:
        ld      a, 023h
        call    e_b
        ld      a, 023h
        jp      e_b
lvs_dec:
        ld      a, (lv_type)
        cp      3
        jr      z, lvs_d2
        ld      a, 02bh                 ; DEC HL
        jp      e_b
lvs_d2:
        ld      a, 02bh
        call    e_b
        ld      a, 02bh
        jp      e_b

; find_var2: resolve namebuf as a variable (locals first,
; then globals). On success: (lv_kind) 0 = local /
; 1 = global, (lv_disp) = IY displacement or IX offset,
; (lv_type), (lv_isarray); carry clear. Carry set if not
; found.
find_var2:
        call    find_local
        jr      nc, fv2_loc
        call    find_global
        ret     c
        ; global: HL = entry
        ld      de, 16
        add     hl, de
        ld      e, (hl)
        inc     hl
        ld      d, (hl)
        ld      (lv_disp), de
        inc     hl
        ld      a, (hl)                 ; type
        ld      (lv_type), a
        inc     hl
        ld      a, (hl)                 ; kind (array?)
        ld      (lv_isarray), a
        ld      a, 1
        ld      (lv_kind), a
        or      a
        ret
fv2_loc:
        ld      de, 16
        add     hl, de
        ld      e, (hl)
        inc     hl
        ld      d, (hl)
        ld      (lv_disp), de
        inc     hl
        ld      a, (hl)                 ; type
        ld      (lv_type), a
        ld      de, 3
        add     hl, de                  ; +21 kind
        ld      a, (hl)
        ld      (lv_isarray), a
        xor     a
        ld      (lv_kind), a
        or      a
        ret

; emit_addr: emit code computing the address of the
; variable described by (lv_kind)/(lv_disp) into HL.
emit_addr:
        ld      a, 021h                 ; LD HL, disp/off
        call    e_b
        ld      hl, (lv_disp)
        call    e_w
        ld      a, (lv_kind)
        or      a
        jr      nz, ea_g
        ld      hl, sq_iyde             ; PUSH IY/POP DE/ADD
        ld      b, 4
        jp      e_seq
ea_g:
        ld      hl, sq_ixde             ; PUSH IX/POP DE/ADD
        ld      b, 4
        jp      e_seq
sq_iyde: db     0fdh, 0e5h, 0d1h, 019h
sq_ixde: db     0ddh, 0e5h, 0d1h, 019h

; parse_expr: assignment level (the full expression).
parse_expr:
        jp      parse_assign

; parse_assign: conditional expression, then an optional
; assignment (right-associative).
parse_assign:
        call    parse_cond
        ld      a, (tok)
        cp      '='
        jr      z, pa_eq
        cp      T_AADD
        jr      nc, pa_comp             ; compound ops are
        ret                             ; AADD..ASHR (0aah+)
pa_eq:
        ld      a, (lv_valid)
        or      a
        jp      z, type_err
        ld      a, (lv_isarray)
        or      a
        jp      nz, type_err
        ld      a, (lv_type)
        ld      (asv_type), a
        call    lex                     ; past '='
        ld      a, 0e5h                 ; PUSH HL (address)
        call    e_b
        call    parse_assign            ; right side
        call    lv_load
        call    chk_val
        ld      a, 0d1h                 ; POP DE (address)
        call    e_b
        ld      a, (asv_type)
        ld      (lv_type), a
        call    lv_store_de
        ld      a, (asv_type)
        ld      (exprt), a
        xor     a
        ld      (lv_valid), a
        ret
pa_comp:
        cp      T_ASHR + 1
        ret     nc                      ; not an assignment
        ld      (asv_op), a
        ld      a, (lv_valid)
        or      a
        jp      z, type_err
        ld      a, (lv_isarray)
        or      a
        jp      nz, type_err
        ld      a, (lv_type)
        ld      (asv_type), a
        cp      3
        jr      c, pac_ok               ; int/char target
        ; pointer target: only += and -=
        ld      a, (asv_op)
        cp      T_AADD
        jr      z, pac_ok
        cp      T_ASUB
        jp      nz, type_err
pac_ok:
        call    lex                     ; past the operator
        ld      a, 0e5h                 ; PUSH HL (address)
        call    e_b
        call    lv_load                 ; old value into HL
        ld      a, 0e5h                 ; PUSH HL (old value)
        call    e_b
        call    parse_assign            ; right side
        call    lv_load
        call    chk_val
        ; scale the right side for int* += / -=
        ld      a, (asv_type)
        cp      3
        jr      nz, pac_ns
        ld      a, (asv_op)
        cp      T_AADD
        jr      z, pac_sc
        cp      T_ASUB
        jr      nz, pac_ns
pac_sc:
        ld      a, 029h                 ; ADD HL, HL
        call    e_b
pac_ns:
        ld      a, 0d1h                 ; POP DE (old value)
        call    e_b
        ld      a, (asv_op)
        call    asv_map                 ; A = binary op token
        ld      (emit_op_tok), a
        call    emit_op                 ; DE op HL -> HL
        ld      a, 0d1h                 ; POP DE (address)
        call    e_b
        ld      a, (asv_type)
        ld      (lv_type), a
        call    lv_store_de
        ld      a, (asv_type)
        ld      (exprt), a
        xor     a
        ld      (lv_valid), a
        ret

; asv_map: compound-assignment token in A -> binary op
; token in A.
asv_map:
        cp      T_AADD
        jr      z, am_add
        cp      T_ASUB
        jr      z, am_sub
        cp      T_AMUL
        jr      z, am_mul
        cp      T_ADIV
        jr      z, am_div
        cp      T_AMOD
        jr      z, am_mod
        cp      T_AAND
        jr      z, am_and
        cp      T_AOR
        jr      z, am_or
        cp      T_AXOR
        jr      z, am_xor
        cp      T_ASHL
        jr      z, am_shl
        ld      a, T_SHR
        ret
am_add:
        ld      a, '+'
        ret
am_sub:
        ld      a, '-'
        ret
am_mul:
        ld      a, '*'
        ret
am_div:
        ld      a, '/'
        ret
am_mod:
        ld      a, '%'
        ret
am_and:
        ld      a, '&'
        ret
am_or:
        ld      a, '|'
        ret
am_xor:
        ld      a, '^'
        ret
am_shl:
        ld      a, T_SHL
        ret

; parse_cond: ternary ?: over parse_lor.
parse_cond:
        call    parse_lor
        ld      a, (tok)
        cp      '?'
        ret     nz
        call    lb_save
        call    new_label
        ld      (lb1), hl               ; else label
        call    new_label
        ld      (lb2), hl               ; end label
        call    lv_load
        call    chk_val
        ld      hl, (lb1)
        call    e_jz
        call    lex                     ; past '?'
        call    parse_expr              ; middle (full expr)
        call    lv_load
        call    chk_val
        ld      a, (exprt)
        ld      (cond_mt), a
        ld      a, ':'
        call    expect
        ld      hl, (lb2)
        call    e_jp
        ld      hl, (lb1)
        call    def_label
        call    parse_cond              ; right branch
        call    lv_load
        call    chk_val
        ld      hl, (lb2)
        call    def_label
        ; result type: equal types win, else int (unless a
        ; pointer is involved, which wins over int)
        ld      a, (cond_mt)
        ld      hl, exprt
        cp      (hl)
        jr      z, pcd_done
        cp      3
        jr      nc, pcd_done            ; middle is pointer
        ld      a, (exprt)
        cp      3
        jr      nc, pcd_done            ; right is pointer
        xor     a
        ld      (exprt), a
pcd_done:
        jp      lb_restore

; parse_lor: short-circuit || over parse_land.
parse_lor:
        call    parse_land
        ld      a, (tok)
        cp      T_OR
        ret     nz
        call    lb_save
        call    new_label
        ld      (lb1), hl               ; true label
        call    new_label
        ld      (lb2), hl               ; end label
plo_lp:
        call    lv_load
        call    chk_val
        ld      hl, (lb1)
        call    e_jnz
        call    lex                     ; past '||'
        call    parse_land
        ld      a, (tok)
        cp      T_OR
        jr      z, plo_lp
        call    lv_load
        call    chk_val
        ld      hl, (lb1)
        call    e_jnz
        ld      a, 021h                 ; LD HL, 0
        call    e_b
        ld      hl, 0
        call    e_w
        ld      hl, (lb2)
        call    e_jp
        ld      hl, (lb1)
        call    def_label
        ld      a, 021h                 ; LD HL, 1
        call    e_b
        ld      hl, 1
        call    e_w
        ld      hl, (lb2)
        call    def_label
        xor     a
        ld      (exprt), a
        jp      lb_restore

; parse_land: short-circuit && over parse_bin(1).
parse_land:
        call    parse_bin1
        ld      a, (tok)
        cp      T_AND
        ret     nz
        call    lb_save
        call    new_label
        ld      (lb1), hl               ; false label
        call    new_label
        ld      (lb2), hl               ; end label
pla_lp:
        call    lv_load
        call    chk_val
        ld      hl, (lb1)
        call    e_jz
        call    lex                     ; past '&&'
        call    parse_bin1
        ld      a, (tok)
        cp      T_AND
        jr      z, pla_lp
        call    lv_load
        call    chk_val
        ld      hl, (lb1)
        call    e_jz
        ld      a, 021h                 ; LD HL, 1
        call    e_b
        ld      hl, 1
        call    e_w
        ld      hl, (lb2)
        call    e_jp
        ld      hl, (lb1)
        call    def_label
        ld      a, 021h                 ; LD HL, 0
        call    e_b
        ld      hl, 0
        call    e_w
        ld      hl, (lb2)
        call    def_label
        xor     a
        ld      (exprt), a
        jp      lb_restore

parse_bin1:
        ld      a, 1
        jp      parse_bin

; parse_bin: precedence climbing over the binary levels
; | ^ & == != relational shift additive multiplicative
; (&& and || live one level up: they short-circuit).
; A = minimum precedence (1..8).
parse_bin:
        push    af                      ; [minp]
        call    parse_unary
pbn_lp:
        ld      a, (tok)
        call    op_prec                 ; A = precedence, 0 = none
        or      a
        jr      z, pbn_done
        pop     bc                      ; B = minp
        push    bc
        cp      b
        jr      c, pbn_done              ; prec < minp: stop
        ld      (pb_prec), a
        call    lv_load                 ; left becomes a value
        call    chk_val
        ld      a, (tok)
        push    af                      ; [minp][op]
        call    lex                     ; consume the operator
        ld      a, 0e5h                 ; PUSH HL (left)
        call    e_b
        ld      a, (exprt)
        push    af                      ; [minp][op][ltype]
        ld      a, (pb_prec)
        inc     a                       ; right: prec + 1
        call    parse_bin
        pop     af
        ld      (op_lt), a              ; left type
        pop     af                      ; op token
        ld      (emit_op_tok), a
        call    lv_load                 ; right becomes a value
        call    chk_val
        ld      a, 0d1h                 ; POP DE (left)
        call    e_b
        call    binop_pre               ; scaling + type rules
        call    emit_op
        call    binop_post              ; ptr-diff scale, exprt
        jr      pbn_lp
pbn_done:
        pop     af
        ret

; op_prec: A = token -> A = precedence (0 if not a
; binary operator at this level).
op_prec:
        cp      '|'
        jr      z, op_1
        cp      '^'
        jr      z, op_2
        cp      '&'
        jr      z, op_3
        cp      T_EQ
        jr      z, op_4
        cp      T_NE
        jr      z, op_4
        cp      '<'
        jr      z, op_5
        cp      '>'
        jr      z, op_5
        cp      T_LE
        jr      z, op_5
        cp      T_GE
        jr      z, op_5
        cp      T_SHL
        jr      z, op_6
        cp      T_SHR
        jr      z, op_6
        cp      '+'
        jr      z, op_7
        cp      '-'
        jr      z, op_7
        cp      '*'
        jr      z, op_8
        cp      '/'
        jr      z, op_8
        cp      '%'
        jr      z, op_8
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

; binop_pre: type rules and pointer scaling for the
; operator (emit_op_tok), left type (op_lt), right type
; (exprt). DE = left value, HL = right value. Sets
; (binop_rt) = result type and (ptrdiff) = 0/1/2.
; Pointer comparisons use the signed compare of emit_op
; (a documented limitation, shared with TinyBASIC).
binop_pre:
        xor     a
        ld      (ptrdiff), a
        ld      a, (emit_op_tok)
        cp      '+'
        jr      z, bp_add
        cp      '-'
        jr      z, bp_sub
        cp      T_EQ
        jr      z, bp_cmp
        cp      T_NE
        jr      z, bp_cmp
        cp      '<'
        jr      z, bp_cmp
        cp      '>'
        jr      z, bp_cmp
        cp      T_LE
        jr      z, bp_cmp
        cp      T_GE
        jr      z, bp_cmp
        ; arithmetic/bitwise/shift: pointers forbidden
        ld      a, (op_lt)
        cp      3
        jp      nc, type_err
        ld      a, (exprt)
        cp      3
        jp      nc, type_err
        xor     a
        ld      (binop_rt), a
        ret
bp_cmp:
        xor     a
        ld      (binop_rt), a
        ret
bp_add:
        ld      a, (op_lt)
        cp      3
        jr      nc, bp_add_lp           ; left is a pointer
        ld      a, (exprt)
        cp      3
        jr      nc, bp_add_rp           ; right is a pointer
        xor     a
        ld      (binop_rt), a
        ret
bp_add_lp:
        ld      a, (exprt)
        cp      3
        jp      nc, type_err            ; ptr + ptr
        ld      a, (op_lt)
        ld      (binop_rt), a
        cp      3 + 0                   ; int* (type 3)?
        ret     nz                      ; char*/void*: step 1
        ld      a, 029h                 ; ADD HL, HL (scale R)
        jp      e_b
bp_add_rp:
        ld      a, (exprt)
        ld      (binop_rt), a
        cp      3
        ret     nz
        ; scale the left (DE) by 2: EX/ADD/EX
        ld      a, 0ebh                 ; EX DE, HL
        call    e_b
        ld      a, 029h                 ; ADD HL, HL
        call    e_b
        ld      a, 0ebh                 ; EX DE, HL
        jp      e_b
bp_sub:
        ld      a, (op_lt)
        cp      3
        jr      nc, bp_sub_lp
        ld      a, (exprt)
        cp      3
        jp      nc, type_err            ; int - ptr
        xor     a
        ld      (binop_rt), a
        ret
bp_sub_lp:
        ld      a, (exprt)
        cp      3
        jr      nc, bp_sub_pp           ; ptr - ptr
        ld      a, (op_lt)
        ld      (binop_rt), a
        cp      3
        ret     nz
        ld      a, 029h                 ; ADD HL, HL (scale R)
        jp      e_b
bp_sub_pp:
        ld      hl, op_lt
        cp      (hl)
        jp      nz, type_err            ; mismatched pointers
        ld      a, (op_lt)
        cp      3
        jr      nz, bp_sp1
        ld      a, 2
        ld      (ptrdiff), a            ; divide by 2 after
bp_sp1:
        xor     a
        ld      (binop_rt), a
        ret

; binop_post: after emit_op. Applies the pointer
; difference scale and sets the final (exprt).
binop_post:
        ld      a, (ptrdiff)
        cp      2
        jr      nz, bpo_t
        ld      a, 0cbh
        call    e_b
        ld      a, 02ch                 ; SRA H
        call    e_b
        ld      a, 0cbh
        call    e_b
        ld      a, 01dh                 ; RR L
        call    e_b
bpo_t:
        ld      a, (binop_rt)
        ld      (exprt), a
        ret

; emit_op: combine DE (left) and HL (right) per
; (emit_op_tok), result in HL. Sets (exprt) = int.
; (Sequences as in TinyBASIC's emit_op.)
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
sq_le:  db      021h, 1, 0, 038h, 8, 079h, 0e6h, 040h, 020h, 3
        db      021h, 0, 0
eo_gt:
        call    eo_cmp
        call    eo_sgn
        ld      hl, sq_gt
        ld      b, 13
        jp      e_seq
sq_gt:  db      021h, 0, 0, 038h, 8, 079h, 0e6h, 040h, 020h, 3
        db      021h, 1, 0
eo_sgn:
        ld      hl, sq_sgn
        ld      b, 16
        jp      e_seq
sq_sgn: db      0f5h, 0c1h, 079h, 0e6h, 4, 07h, 07h, 07h, 07h
        db      07h, 047h, 079h, 0e6h, 080h, 0a8h, 07h

; parse_unary: prefix operators and casts, then a
; postfix expression.
parse_unary:
        ld      a, (tok)
        cp      '-'
        jp      z, pu_neg
        cp      '!'
        jp      z, pu_not
        cp      '~'
        jp      z, pu_bnot
        cp      '+'
        jp      z, pu_plus
        cp      '&'
        jp      z, pu_addr
        cp      '*'
        jp      z, pu_deref
        cp      T_INC
        jp      z, pu_preinc
        cp      T_DEC
        jp      z, pu_predec
        cp      '('
        jp      z, pu_paren
        jp      parse_postfix
pu_neg:
        call    lex
        call    parse_unary
        call    lv_load
        call    chk_val
        ld      a, (exprt)
        cp      3
        jp      nc, type_err
        ld      hl, sq_neg
        ld      b, 6
        call    e_seq
        xor     a
        ld      (exprt), a
        ret
sq_neg: db      0afh, 095h, 06fh, 09fh, 094h, 067h
pu_not:
        call    lex
        call    parse_unary
        call    lv_load
        call    chk_val
        ld      hl, sq_not
        ld      b, 10
        call    e_seq
        xor     a
        ld      (exprt), a
        ret
sq_not: db      07ch, 0b5h, 021h, 1, 0, 028h, 3, 021h, 0, 0
pu_bnot:
        call    lex
        call    parse_unary
        call    lv_load
        call    chk_val
        ld      a, (exprt)
        cp      3
        jp      nc, type_err
        ld      hl, sq_bnot
        ld      b, 6
        call    e_seq
        xor     a
        ld      (exprt), a
        ret
sq_bnot: db     07ch, 02fh, 067h, 07dh, 02fh, 06fh
pu_plus:
        call    lex
        call    parse_unary
        call    lv_load
        call    chk_val
        ld      a, (exprt)
        cp      3
        jp      nc, type_err
        ret
pu_addr:
        call    lex
        call    parse_unary
        ld      a, (lv_valid)
        or      a
        jp      z, type_err             ; & of a non-lvalue
        ; an array designator's address is itself
        xor     a
        ld      (lv_valid), a
        ld      (lv_isarray), a
        ld      a, (lv_type)
        add     a, 3
        ld      (exprt), a
        ret
pu_deref:
        call    lex
        call    parse_unary
        call    lv_load                 ; the pointer value
        call    chk_val
        ld      a, (exprt)
        cp      3
        jp      c, type_err             ; * of a non-pointer
        sub     3
        ld      (lv_type), a            ; pointee type
        ld      a, 1
        ld      (lv_valid), a
        xor     a
        ld      (lv_isarray), a
        ret
pu_preinc:
        xor     a
        jr      pu_pre
pu_predec:
        ld      a, 1
pu_pre:
        ld      (pre_dec), a
        call    lex
        call    parse_unary
        ld      a, (lv_valid)
        or      a
        jp      z, type_err
        ld      a, (lv_isarray)
        or      a
        jp      nz, type_err
        ld      a, 0e5h                 ; PUSH HL (address)
        call    e_b
        call    lv_load                 ; value
        ld      a, (pre_dec)
        call    lv_step                 ; INC/DEC HL
        ld      a, 0d1h                 ; POP DE (address)
        call    e_b
        call    lv_store_de
        ld      a, (lv_type)
        ld      (exprt), a
        xor     a
        ld      (lv_valid), a
        ret
pu_paren:
        call    lex                     ; token after '('
        ld      a, (tok)
        cp      K_INT
        jr      z, pu_cast
        cp      K_CHAR
        jr      z, pu_cast
        cp      K_VOID
        jr      z, pu_cast
        ; a parenthesised expression: parse_primary will
        ; find (paren_pend) set
        ld      a, 1
        ld      (paren_pend), a
        jp      parse_postfix
pu_cast:
        cp      K_INT
        jr      z, puc_i
        cp      K_CHAR
        jr      z, puc_c
        ld      a, 2
        jr      puc_t
puc_i:
        xor     a
        jr      puc_t
puc_c:
        ld      a, 1
puc_t:
        ld      (cast_type), a
        call    lex
puc_star:
        ld      a, (tok)
        cp      '*'
        jr      nz, puc_rp
        ld      a, (cast_type)
        cp      3
        jp      nc, type_err
        add     a, 3
        ld      (cast_type), a
        call    lex
        jr      puc_star
puc_rp:
        ld      a, ')'
        call    expect
        call    parse_unary
        call    lv_load
        ld      a, (cast_type)
        cp      1
        jr      nz, puc_set               ; to char: truncate
        ld      a, (exprt)
        cp      1
        jr      z, puc_set                ; already char
        ld      hl, sq_ldh0               ; LD H, 0
        ld      b, 2
        call    e_seq
puc_set:
        ld      a, (cast_type)
        ld      (exprt), a
        xor     a
        ld      (lv_valid), a
        ret
sq_ldh0: db     026h, 0

; parse_postfix: a primary, then subscripts and
; postfix ++/--.
parse_postfix:
        call    parse_primary
ppf_lp:
        ld      a, (tok)
        cp      '['
        jr      z, ppf_sub
        cp      T_INC
        jr      z, ppf_inc
        cp      T_DEC
        jr      z, ppf_dec
        ret
ppf_sub:
        ; element type into (sub_elem)
        ld      a, (lv_isarray)
        or      a
        jr      nz, pps_arr
        call    lv_load                 ; pointer value
        call    chk_val
        ld      a, (exprt)
        cp      3
        jp      c, type_err
        sub     3
        ld      (sub_elem), a
        jr      pps_go
pps_arr:
        ld      a, (lv_type)
        ld      (sub_elem), a
pps_go:
        ld      a, 0e5h                 ; PUSH HL (base)
        call    e_b
        call    lex                     ; past '['
        call    parse_expr              ; index
        call    lv_load
        call    chk_val
        ld      a, ']'
        call    expect
        ld      a, (sub_elem)
        call    type_size
        cp      2
        jr      nz, pps_ns
        ld      a, 029h                 ; ADD HL, HL (scale)
        call    e_b
pps_ns:
        ld      a, 0d1h                 ; POP DE (base)
        call    e_b
        ld      a, 019h                 ; ADD HL, DE
        call    e_b
        ld      a, (sub_elem)
        ld      (lv_type), a
        ld      a, 1
        ld      (lv_valid), a
        xor     a
        ld      (lv_isarray), a
        jp      ppf_lp
ppf_inc:
        xor     a
        jr      ppf_id
ppf_dec:
        ld      a, 1
ppf_id:
        ld      (pre_dec), a
        ld      a, (lv_valid)
        or      a
        jp      z, type_err
        ld      a, (lv_isarray)
        or      a
        jp      nz, type_err
        call    lex                     ; past ++/--
        ld      a, 0e5h                 ; PUSH HL (address)
        call    e_b
        call    lv_load                 ; value into HL
        ld      a, 060h                 ; LD H, B opcode base:
        ; save the old value in BC: LD B,H / LD C,L
        ld      a, 044h                 ; LD B, H
        call    e_b
        ld      a, 04dh                 ; LD C, L
        call    e_b
        ld      a, (pre_dec)
        call    lv_step                 ; INC/DEC HL (new)
        ld      a, 0d1h                 ; POP DE (address)
        call    e_b
        call    lv_store_de             ; store the new value
        ld      a, 060h                 ; LD H, B
        call    e_b
        ld      a, 069h                 ; LD L, C
        call    e_b
        ld      a, (lv_type)
        ld      (exprt), a
        xor     a
        ld      (lv_valid), a
        jp      ppf_lp

; parse_primary: number, char literal, string literal,
; parenthesised expression (via (paren_pend)), variable
; designator, function call.
parse_primary:
        ld      a, (paren_pend)
        or      a
        jr      nz, pp_paren
        ld      a, (tok)
        cp      T_NUM
        jr      z, pp_num
        cp      T_CHR
        jr      z, pp_num
        cp      T_STR
        jr      z, pp_str
        cp      T_IDENT
        jr      z, pp_id
        jp      syn_err
pp_paren:
        xor     a
        ld      (paren_pend), a
        call    parse_expr
        ld      a, ')'
        jp      expect
pp_num:
        ld      a, 021h                 ; LD HL, value
        call    e_b
        ld      hl, (tokval)
        call    e_w
        xor     a
        ld      (exprt), a              ; int (also for 'c')
        ld      (lv_valid), a
        call    lex
        ret
pp_str:
        ld      a, 021h                 ; LD HL, literal
        call    e_b
        call    lit_addr_hl
        call    e_wm
        call    lit_register
        ld      a, 4                    ; char*
        ld      (exprt), a
        xor     a
        ld      (lv_valid), a
        call    lex
        ret
pp_id:
        ; function call (user function)?
        call    find_func
        jr      nc, pp_uf
        call    ext_lookup
        jp      nc, pp_ef
        ; variable designator
        call    find_var2
        jp      c, pp_undef
        call    emit_addr               ; address into HL
        ld      a, 1
        ld      (lv_valid), a
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
        jp      nz, type_err
        jp      parse_call
pp_ef:
        ld      a, (cf_libid)
        cp      6
        jr      z, pp_printf            ; printf is variadic
        ld      a, 1
        ld      (cf_kind), a
        ld      (cf_expr), a
        call    lex
        cp      '('
        jp      nz, type_err
        jp      parse_call
pp_printf:
        ld      a, 1
        ld      (cf_kind), a
        ld      (cf_expr), a
        call    lex
        cp      '('
        jp      nz, type_err
        jp      parse_call
pp_undef:
        ld      a, E_UNDEF
        jp      err

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
        call    ex_cmp
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

; ex_cmp: compare namebuf with the NUL-terminated name
; at HL (exttab names are not 16-byte fields, so this
; stops at the shared NUL, unlike name_eq). Z iff equal.
; Clobbers A, DE, HL.
ex_cmp:
        ld      de, namebuf
ex_cmp_lp:
        ld      a, (de)
        cp      (hl)
        ret     nz
        or      a
        ret     z
        inc     hl
        inc     de
        jr      ex_cmp_lp

; parse_call: the current token is '('. The callee is in
; (cf_kind) 0 = user function (cf_fptr) / 1 = external
; (cf_libid); (cf_nargs) and (cf_ret) describe it. For
; printf (libid 6) (cf_nargs) is 0ffh: any count >= 1,
; and the argument count is passed in A.
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
        call    parse_expr
        call    lv_load
        call    chk_val
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
pc_fin:
        ld      a, ')'
        call    expect
        ld      a, (pc_kind)
        or      a
        jr      nz, pc_fin_ext          ; external callee
        ld      a, (pc_n)
        ld      hl, pc_nargs
        cp      (hl)
        jp      nz, pc_args
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
pc_fin_ext:
        ld      a, (pc_libid)
        cp      6
        jr      z, pc_printf2           ; variadic printf
        ld      a, (pc_n)
        ld      hl, pc_nargs
        cp      (hl)
        jp      nz, pc_args
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
        jr      pc_clean
pc_printf2:
        ld      a, (pc_n)
        or      a
        jp      z, pc_args              ; printf needs >= 1
        dec     a
        ld      (pc_n2), a
        ld      a, 6
        call    mark_used
        ld      a, 03eh                 ; LD A, nargs - 1
        call    e_b
        ld      a, (pc_n2)
        call    e_b
        ld      a, 0cdh                 ; CALL x_printf
        call    e_b
        ld      a, 6
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
        ; drop the argument slots with POP DE, one per
        ; argument: the LD HL,2n / ADD HL,SP form used
        ; before destroyed the return value in HL
        ld      a, (pc_n)
        or      a
        jr      z, pc_type
        ld      b, a
pc_clp:
        push    bc
        ld      a, 0d1h                 ; POP DE (drop 1 arg)
        call    e_b
        pop     bc
        djnz    pc_clp
pc_type:
        ; the result type is the callee's type code; a void
        ; call sets (exprt) = 2 and value consumers reject
        ; it via chk_val.
        ld      a, (pc_ret)
        ld      (exprt), a
        xor     a
        ld      (lv_valid), a
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
pc_args:
        ld      a, E_ARGS
        jp      err
sq_spadj: db    039h, 0f9h              ; ADD HL,SP / LD SP,HL

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
; In pass 0 (the for-increment scan) do nothing at all:
; the scan must not disturb the pool state.
lit_register:
        ld      a, (pass)
        or      a
        ret     z
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
; Data.
; ---------------------------------------------------------------
msg_srcp: db    "SRC: ", 0
msg_outp: db    "OUT: ", 0
msg_ok: db      "OK ", 0
msg_err: db     "ERR LINE ", 0
str_main: db    "main", 0

; keyword table: db code, "name", 0 ... 0 (lowercase:
; C is case-sensitive)
kwtab:
        db      K_INT, "int", 0
        db      K_CHAR, "char", 0
        db      K_VOID, "void", 0
        db      K_IF, "if", 0
        db      K_ELSE, "else", 0
        db      K_WHILE, "while", 0
        db      K_DO, "do", 0
        db      K_FOR, "for", 0
        db      K_BREAK, "break", 0
        db      K_CONTINUE, "continue", 0
        db      K_RETURN, "return", 0
        db      0

; external function table: db libid, nargs, ret, "name", 0
; ret is a C type code (0 int, 2 void). libid 6 (printf)
; is variadic: nargs 0ffh, handled specially.
exttab:
        db      6, 0ffh, 0, "printf", 0
        db      5, 1, 0, "puts", 0
        db      16, 1, 0, "putchar", 0
        db      7, 0, 0, "getchar", 0
        db      8, 0, 0, "input", 0
        db      9, 2, 2, "locate", 0
        db      10, 0, 2, "cls", 0
        db      11, 0, 0, "inkey", 0
        db      12, 0, 0, "stick", 0
        db      13, 0, 2, "vsync", 0
        db      14, 1, 0, "peek", 0
        db      15, 2, 2, "poke", 0
        db      0eeh

; the library tables (generated by tools/mkclibtab.py)
        include "clibtab.inc"

; ---- variables ----
saved_sp: dw    0
err_code: db    0
pd_val: dw      0
pd_ptr: dw      0
pd_buf: defs    8
find_name: dw   0
find_i: db      0
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
main_rec: dw    0
main_size: dw   0
src_state:
src_rec: dw     0
src_size: dw    0
src_left: dw    0
src_recno: dw   0
src_off: db     0
src_pad: db     0
curline: dw     0
istk: defs      24
inc_depth: db   0
cur_secbuf: dw  0
new_rec: dw     0
new_size: dw    0
incname: defs   16
pp_name: defs   11
pp_word: defs   8
pp_ptr: dw      0
pp_len: dw      0
mcount: db      0
exp_seg: defs   24
exp_depth: db   0
exp_sp: dw      0
exp_byte: db    0
exp_newptr: dw  0
seg_tmp: dw     0
pb_valid: db    0
pb_ch: db       0
pb_depth: db    0
lx_bol: db      0
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
sv_pass: db     0
outbase_v: dw   0
rt_stub: dw     0
epc: dw         0
label_cnt: dw   0
gcount: db      0
gtotal: dw      0
fcount: db      0
fdef: defs      16
lcount: db      0
gk_base: db     0
gk_type: db     0
gk_arr: db      0
gk_elems: dw    0
gk_fptr: dw     0
gk_fidx: db     0
gk_nargs: db    0
gk_ptype: db    0
gk_kbase: db    0
pn_ptr: dw      0
p1_depth: db    0
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
heap_limit: dw  0
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
el_tgt: db      0
ef_tr: dw       0
ef_ck: dw       0
ef_val: dw      0
cfun_ret: db    0
cfun_nargs: db  0
cfun_ptr: dw    0
pf_cum: dw      0
pf_frame: dw    0
pf_idx: db      0
al_type: db     0
al_param: db    0
al_arr: db      0
al_elems: dw    0
al_disp: dw     0
al_ep: dw       0
retlab: dw      0
loopdep: db     0
loopstk: defs   32
blkdep: db      0
blkstk: defs    8
cur_bdepth: db  0
cap_len: dw     0
cap_c: db       0
cap_dep: db     0
cap_str: db     0
cap_chr: db     0
cap_esc: db     0
fsv_sp: dw      0
fsv_len: dw     0
lb1: dw         0
lb2: dw         0
lb3: dw         0
lb4: dw         0
lb5: dw         0
lb6: dw         0
lv_valid: db    0
lv_isarray: db  0
lv_kind: db     0
lv_type: db     0
lv_disp: dw     0
asv_type: db    0
asv_op: db      0
emit_op_tok: db 0
op_lt: db       0
binop_rt: db    0
ptrdiff: db     0
pb_prec: db     0
cond_mt: db     0
sub_elem: db    0
pre_dec: db     0
cast_type: db   0
paren_pend: db  0
exprt: db       0
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
