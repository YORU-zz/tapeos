; ===============================================================
; Tiny Scheme core -- shared source, included by two packages:
;   tsload.z80   the standalone 2-stage tape loader (TSLOAD)
;   scmcore.z80  the embedded core (SCMCORE, roadmap 61), via a
;                generated variant (tools/mkscmcore.py) with a
;                reduced primitive table.
; The packaging supplies the equates (arena bases, error codes,
; SCRBUF, ...), the err_land landing pad and the console / tape
; routines the core's standalone-only parts call.  This file
; must assemble to the identical byte stream in the TSLOAD
; build as the pre-split monolith did (build gate: sha256).
; ===============================================================

; ===============================================================
; Tiny Scheme core
; ===============================================================

; err_throw: a = error code.  Resets the stack and lands in
; err_land (message + native menu).
err_throw:
        ld      (err_code), a
        ld      sp, (save_sp)
        jp      err_land
throw_unbound:
        ld      a, E_UNBOUND
        jr      err_throw
throw_type:
        ld      a, E_TYPE
        jr      err_throw
throw_arity:
        ld      a, E_ARITY
        jr      err_throw
throw_heap:
        ld      a, E_HEAP
        jr      err_throw
throw_read:
        ld      a, E_READ
        jr      err_throw
throw_tape:
        ld      a, E_TAPE
        jr      err_throw

; ---- value helpers ----
; fix_from_int: hl = raw signed int -> hl = fixnum (mod 2^15
; into the signed range, then encoded).
fix_from_int:
        ; clamp modulo: v &= 0x7fff; if v >= 0x4000: v -= 0x8000
        ld      a, h
        and     07fh
        ld      h, a
        bit     6, h                    ; 0x4000 bit
        jr      z, ffi_go
        ld      de, 08000h
        or      a
        sbc     hl, de
ffi_go:
        add     hl, hl
        inc     hl
        ret

; fix_decode: hl = fixnum -> hl = raw signed int
fix_decode:
        sra     h
        rr      l
        ret

; is_fixnum: zero flag set if hl is a fixnum
is_fixnum:
        bit     0, l
        ret

; val_type: classify hl.  Returns a:
;   0 nil, 1 fixnum, 2 bool, 3 cons, 4 symbol, 5 string,
;   6 closure, 7 primitive, 8 other-object
val_type:
        ld      a, h
        or      l
        jr      z, vt_nil
        bit     0, l
        jr      nz, vt_fix
        ld      a, h
        or      a
        jr      nz, vt_notb             ; high byte: not a bool
        ld      a, l
        cp      2
        jr      z, vt_bool
        cp      4
        jr      z, vt_bool
vt_notb:
        bit     1, l
        jr      z, vt_cons
        ; object: type word at hl-2
        push    hl
        dec     hl
        dec     hl
        ld      a, (hl)
        pop     hl
        cp      T_SYM
        jr      z, vt_sym
        cp      T_STR
        jr      z, vt_str
        cp      T_CLO
        jr      z, vt_clo
        cp      T_PRIM
        jr      z, vt_pri
        ld      a, 8
        ret
vt_nil:
        xor     a
        ret
vt_fix:
        ld      a, 1
        ret
vt_bool:
        ld      a, 2
        ret
vt_cons:
        ld      a, 3
        ret
vt_sym:
        ld      a, 4
        ret
vt_str:
        ld      a, 5
        ret
vt_clo:
        ld      a, 6
        ret
vt_pri:
        ld      a, 7
        ret

; truthy: zero flag set if hl is falsy (nil or #f)
truthy:
        ld      a, h
        or      l
        ret     z                       ; nil -> falsy
        ld      a, h
        or      a
        jr      nz, tr_true             ; high byte set: truthy
        ld      a, l
        cp      2
        ret     nz                      ; not #f: truthy (nz)
        xor     a                       ; #f: set zero (falsy)
        ret
tr_true:
        or      1                       ; clear zero: truthy
        ret

; car_hl: hl = car(hl); car of nil = nil.  destroys a only.
car_hl:
        ld      a, h
        or      l
        ret     z
        ld      a, (hl)
        inc     hl
        ld      h, (hl)
        ld      l, a
        ret
; cdr_hl: hl = cdr(hl); cdr of nil = nil.  destroys a only.
cdr_hl:
        ld      a, h
        or      l
        ret     z
        inc     hl
        inc     hl
        ld      a, (hl)
        inc     hl
        ld      h, (hl)
        ld      l, a
        ret
cadr_hl:
        call    cdr_hl
        jr      car_hl
caddr_hl:
        call    cdr_hl
        call    cdr_hl
        jr      car_hl
cadddr_hl:
        call    cdr_hl
        call    cdr_hl
        call    cdr_hl
        jr      car_hl
cddr_hl:
        call    cdr_hl
        jr      cdr_hl
; obj_word4: hl = object value -> hl = word at value+4
; (the third payload word; NOT cddr - objects are not conses)
obj_word4:
        inc     hl
        inc     hl
        inc     hl
        inc     hl
        ld      a, (hl)
        inc     hl
        ld      h, (hl)
        ld      l, a
        ret

; mk_cons: hl = car, de = cdr -> hl = new cons value.
; Uses tmp_car/tmp_cdr (non-reentrant; no recursion inside).
mk_cons:
        ld      (tmp_car), hl
        ld      (tmp_cdr), de
        ld      hl, (cons_ptr)
        push    hl
        ld      de, 4
        add     hl, de
        ld      (cons_ptr), hl
        ld      de, CONS_END
        or      a
        sbc     hl, de
        jp      nc, throw_heap
        pop     hl
        ld      de, (tmp_car)
        ld      (hl), e
        inc     hl
        ld      (hl), d
        inc     hl
        ld      de, (tmp_cdr)
        ld      (hl), e
        inc     hl
        ld      (hl), d
        dec     hl
        dec     hl
        dec     hl
        ret

; obj_new: a = type -> hl = new object value; the object's
; base address is in (obj_addr); payload words are zeroed.
obj_new:
        ld      (tmp_type), a
        ld      hl, (atom_ptr)
        ld      (obj_addr), hl
        push    hl
        ld      de, 8
        add     hl, de
        ld      (atom_ptr), hl
        ld      de, ATOM_END
        or      a
        sbc     hl, de
        jp      nc, throw_heap
        pop     hl
        ld      a, (tmp_type)
        ld      (hl), a
        xor     a
        inc     hl
        ld      (hl), a                 ; type word high byte
        inc     hl
        ld      b, 6
objn_z:
        ld      (hl), a
        inc     hl
        djnz    objn_z
        ld      hl, (obj_addr)
        inc     hl
        inc     hl                      ; value = base + 2
        ret

; str_alloc: de = length -> hl = address in the string arena
; (bump).  Errors on overflow.
str_alloc:
        ld      hl, (str_ptr)
        push    hl
        add     hl, de
        ld      (str_ptr), hl
        ld      de, STR_END
        or      a
        sbc     hl, de
        jp      nc, throw_heap
        pop     hl
        ret

; make_string: hl = source ptr, de = length -> hl = STR value
; (bytes are copied into the string arena).
make_string:
        ld      (tmp_car), hl
        ld      (tmp_cdr), de
        call    str_alloc               ; hl = dest (de = len)
        ld      (tmp_dst), hl
        ex      de, hl
        ld      hl, (tmp_car)
        ld      bc, (tmp_cdr)
        ld      a, b
        or      c
        jr      z, ms_obj
        ldir
ms_obj:
        ld      hl, (tmp_dst)
        ld      de, (tmp_cdr)
        jp      obj_string

; str_obj_addr: hl = STR value -> hl = bytes address
str_obj_addr:
        dec     hl
        dec     hl
        inc     hl
        inc     hl
        ld      a, (hl)
        inc     hl
        ld      h, (hl)
        ld      l, a
        ret
; str_obj_len: hl = STR/SYM value -> hl = length
str_obj_len:
        push    de
        ex      de, hl
        ld      hl, 2
        add     hl, de
        ld      a, (hl)
        inc     hl
        ld      h, (hl)
        ld      l, a
        pop     de
        ret

; intern: hl = name ptr, de = length -> hl = symbol value.
intern:
        ld      (tmp_car), hl
        ld      (tmp_cdr), de
        ld      hl, (symtab)
int_walk:
        ld      a, h
        or      l
        jr      z, int_new
        push    hl
        call    car_hl                  ; hl = symbol value
        ld      (tmp_sym), hl
        ; compare names: symbol word+2 = name addr, +4 = len
        dec     hl
        dec     hl
        inc     hl
        inc     hl
        ld      a, (hl)
        inc     hl
        ld      h, (hl)
        ld      l, a                    ; hl = name addr
        ld      (tmp_np), hl
        ld      hl, (tmp_sym)
        call    str_obj_len             ; (misuse: sym len at +4
        ; symbol layout matches string layout for +2/+4)
        ld      de, (tmp_cdr)
        or      a
        sbc     hl, de
        jr      nz, int_next
        ld      hl, (tmp_np)
        ld      de, (tmp_car)
        ld      bc, (tmp_cdr)
        ld      a, b
        or      c
        jr      z, int_found
int_cmp:
        ld      a, (de)
        cp      (hl)
        jr      nz, int_next
        inc     hl
        inc     de
        dec     bc
        ld      a, b
        or      c
        jr      nz, int_cmp
int_found:
        pop     hl                      ; discard walk cursor
        ld      hl, (tmp_sym)
        ret
int_next:
        pop     hl
        call    cdr_hl
        jr      int_walk
int_new:
        ld      hl, (tmp_car)
        ld      de, (tmp_cdr)
        call    str_alloc               ; hl = name copy dest
        ld      (tmp_dst), hl
        ex      de, hl
        ld      hl, (tmp_car)
        ld      bc, (tmp_cdr)
        ld      a, b
        or      c
        jr      z, int_mk
        ldir
int_mk:
        ld      a, T_SYM
        call    obj_new                 ; hl = value
        ld      (tmp_sym), hl
        ld      de, (obj_addr)
        inc     de
        inc     de
        ld      hl, (tmp_dst)
        ex      de, hl
        ld      (hl), e
        inc     hl
        ld      (hl), d
        inc     hl
        ld      de, (tmp_cdr)
        ld      (hl), e
        inc     hl
        ld      (hl), d
        ; push onto symtab
        ld      hl, (tmp_sym)
        ld      de, (symtab)
        call    mk_cons
        ld      (symtab), hl
        ld      hl, (tmp_sym)
        ret

; ===============================================================
; reader
; ===============================================================
; rd_init: hl = ptr, de = length
rd_init:
        ld      (rd_ptr), hl
        ex      de, hl
        add     hl, de
        ld      (rd_end), hl
        ret

; rd_peek: a = current byte, carry set at end.  preserves hl.
rd_peek:
        push    hl
        ld      hl, (rd_ptr)
        ld      de, (rd_end)
        or      a
        sbc     hl, de
        jr      nc, rdp_end
        ld      hl, (rd_ptr)
        ld      a, (hl)
        pop     hl
        or      a
        ret
rdp_end:
        pop     hl
        scf
        ret

; rd_adv: advance one byte
rd_adv:
        push    hl
        ld      hl, (rd_ptr)
        inc     hl
        ld      (rd_ptr), hl
        pop     hl
        ret

; skip_ws: skip whitespace and ;-comments
skip_ws:
        call    rd_peek
        ret     c
        cp      ' '
        jr      z, sw_adv
        cp      9
        jr      z, sw_adv
        cp      10
        jr      z, sw_adv
        cp      13
        jr      z, sw_adv
        cp      ';'
        jr      z, sw_comment
        ret
sw_adv:
        call    rd_adv
        jr      skip_ws
sw_comment:
        call    rd_adv
        call    rd_peek
        ret     c
        cp      10
        jr      z, skip_ws
        jr      sw_comment

; is_delim: a = char -> carry set if delimiter
is_delim:
        cp      ' '
        jr      z, idl_y
        cp      9
        jr      z, idl_y
        cp      10
        jr      z, idl_y
        cp      13
        jr      z, idl_y
        cp      '('
        jr      z, idl_y
        cp      ')'
        jr      z, idl_y
        cp      '"'
        jr      z, idl_y
        cp      ';'
        jr      z, idl_y
        or      a
        ret
idl_y:
        scf
        ret

; read_expr: -> a = status (0 ok / 1 EOF / 2 RPAREN / 3 DOT),
; hl = value when status 0.
read_expr:
        call    skip_ws
        call    rd_peek
        jr      c, re_eof
        cp      '('
        jr      z, re_list
        cp      ')'
        jr      z, re_rparen
        cp      027h                    ; '
        jr      z, re_quote
        cp      '"'
        jr      z, re_string
        cp      '#'
        jp      z, re_hash
        jp      re_atom
re_eof:
        ld      a, 1
        ret
re_rparen:
        call    rd_adv
        ld      a, 2
        ret
re_quote:
        call    rd_adv
        call    read_expr
        or      a
        jp      nz, throw_read
        ld      de, 0
        call    mk_cons                 ; (x)
        ex      de, hl                  ; de = (x)
        ld      hl, (sym_quote)
        call    mk_cons                 ; (quote x)
        xor     a
        ret
re_list:
        call    rd_adv
        call    read_expr
        cp      1
        jp      z, throw_read           ; EOF inside list
        cp      2
        jr      z, re_list_done         ; () -> nil
        cp      3
        jp      z, throw_read           ; (. x) is invalid
        ; hl = first item; read the rest
        push    hl
        call    read_rest
        pop     de                      ; de = first item... but
        ; read_rest returns the tail list in hl; cons item on
        ex      de, hl                  ; hl=item, de=tail
        call    mk_cons
        xor     a
        ret
re_list_done:
        ld      hl, 0
        xor     a
        ret
; read_rest: read items until ) or . tail.
; -> hl = the list tail (possibly improper).
read_rest:
        call    read_expr
        cp      1
        jp      z, throw_read
        cp      2
        jr      z, rr_nil
        cp      3
        jr      z, rr_dot
        push    hl
        call    read_rest
        pop     de
        ex      de, hl
        call    mk_cons
        ret
rr_nil:
        ld      hl, 0
        ret
rr_dot:
        call    read_expr
        or      a
        jp      nz, throw_read
        push    hl                      ; the tail value
        call    read_expr               ; must be RPAREN
        cp      2
        jp      nz, throw_read
        pop     hl
        ret
re_string:
        call    rd_adv                  ; opening quote
        ld      hl, tokbuf
        ld      (tok_ptr), hl
        ld      hl, 0
        ld      (tok_len), hl
rs_l:
        call    rd_peek
        jp      c, throw_read
        cp      '"'
        jr      z, rs_done
        cp      05ch                    ; backslash
        jr      z, rs_esc
        call    rd_adv
        call    tok_put
        jr      rs_l
rs_esc:
        call    rd_adv
        call    rd_peek
        jp      c, throw_read
        call    rd_adv
        cp      'n'
        jr      z, rs_nl
        cp      't'
        jr      z, rs_tab
        ; \" \\ and anything else: literal
        call    tok_put
        jr      rs_l
rs_nl:
        ld      a, 10
        call    tok_put
        jr      rs_l
rs_tab:
        ld      a, 9
        call    tok_put
        jr      rs_l
rs_done:
        call    rd_adv                  ; closing quote
        ld      hl, tokbuf
        ld      de, (tok_len)
        call    make_string
        xor     a
        ret
; tok_put: append a to tokbuf (bounded)
tok_put:
        push    hl
        push    de
        ld      hl, (tok_len)
        ld      de, 250
        or      a
        sbc     hl, de
        jr      nc, tp_full
        ld      hl, (tok_len)
        inc     hl
        ld      (tok_len), hl
        ld      hl, (tok_ptr)
        ld      (hl), a
        inc     hl
        ld      (tok_ptr), hl
tp_full:
        pop     de
        pop     hl
        ret
re_hash:
        call    rd_adv
        call    rd_peek
        jp      c, throw_read
        cp      't'
        jr      z, rh_t
        cp      'f'
        jr      z, rh_f
        cp      'x'
        jr      z, rh_x
        cp      05ch                    ; #\ char literal
        jr      z, rh_ch
        jp      throw_read
rh_t:
        call    rd_adv
        ld      hl, 4                   ; #t
        xor     a
        ret
rh_f:
        call    rd_adv
        ld      hl, 2                   ; #f
        xor     a
        ret
rh_ch:
        call    rd_adv
        call    rd_peek
        jp      c, throw_read
        call    rd_adv
        ld      l, a
        ld      h, 0
        add     hl, hl
        inc     hl                      ; fixnum of the code
        xor     a
        ret
rh_x:
        call    rd_adv
        ld      hl, 0                   ; accumulator
        ld      (tmp_acc), hl
        ld      a, 0
        ld      (tmp_any), a
rhx_l:
        call    rd_peek
        jr      c, rhx_done
        call    hex_val
        jr      c, rhx_done
        call    rd_adv
        ld      a, 1
        ld      (tmp_any), a
        push    af                      ; digit value in a? no:
        ; hex_val returns digit in a; combine: acc = acc*16 + a
        ld      a, (tmp_dig)
        ld      e, a
        ld      d, 0
        ld      hl, (tmp_acc)
        add     hl, hl
        add     hl, hl
        add     hl, hl
        add     hl, hl
        add     hl, de
        ld      (tmp_acc), hl
        pop     af
        jr      rhx_l
rhx_done:
        ld      a, (tmp_any)
        or      a
        jp      z, throw_read
        ld      hl, (tmp_acc)
        call    fix_from_int
        xor     a
        ret
; hex_val: a = char -> (tmp_dig) = 0..15, carry set if not hex
hex_val:
        cp      '0'
        jr      c, hv_no
        cp      '9' + 1
        jr      c, hv_d
        cp      'a'
        jr      c, hv_up
        cp      'f' + 1
        jr      c, hv_l
        jr      hv_no
hv_up:
        cp      'A'
        jr      c, hv_no
        cp      'F' + 1
        jr      nc, hv_no
        sub     'A' - 10
        jr      hv_ok
hv_d:
        sub     '0'
        jr      hv_ok
hv_l:
        sub     'a' - 10
hv_ok:
        ld      (tmp_dig), a
        or      a
        ret
hv_no:
        scf
        ret
re_atom:
        ld      hl, tokbuf
        ld      (tok_ptr), hl
        ld      hl, 0
        ld      (tok_len), hl
ra_l:
        call    rd_peek
        jr      c, ra_done
        call    is_delim
        jr      c, ra_done
        call    rd_adv
        call    tok_put
        jr      ra_l
ra_done:
        ld      a, (tok_len)
        or      a
        jp      z, throw_read
        ld      hl, tokbuf
        ld      a, (hl)
        cp      '.'
        jr      nz, ra_num
        ld      a, (tok_len)
        cp      1
        jr      nz, ra_num
        ld      a, (tok_len + 1)
        or      a
        jr      nz, ra_num
        ld      a, 3                    ; DOT
        ret
ra_num:
        ld      hl, tokbuf
        ld      de, (tok_len)
        call    parse_number            ; carry = not a number
        jr      nc, ra_ret
        ld      hl, tokbuf
        ld      de, (tok_len)
        call    intern
ra_ret:
        xor     a
        ret

; parse_number: hl = ptr, de = len -> carry set if not a
; number; else hl = fixnum.  Grammar: [-] digits, value taken
; modulo 2^15 into the signed fixnum range.
parse_number:
        ld      (tmp_car), hl
        ld      (tmp_cdr), de
        ld      a, d
        or      e
        jr      z, pn_no
        xor     a
        ld      (tmp_neg), a
        ld      a, (hl)
        cp      '-'
        jr      nz, pn_first
        ld      a, 1
        ld      (tmp_neg), a
        inc     hl
        dec     de
        ld      a, d
        or      e
        jr      z, pn_no                ; "-" alone: a symbol
        ld      (tmp_car), hl
        ld      (tmp_cdr), de
pn_first:
        ld      hl, (tmp_car)
        ld      a, (hl)
        cp      '0'
        jr      c, pn_no
        cp      '9' + 1
        jr      nc, pn_no
        ld      hl, 0
        ld      (tmp_acc), hl
        ld      hl, (tmp_car)
        ld      de, (tmp_cdr)
pn_l:
        ld      a, (hl)
        inc     hl
        sub     '0'
        cp      10
        jr      nc, pn_no
        push    hl
        push    de
        ld      (tmp_dig), a
        ld      hl, (tmp_acc)
        push    hl                      ; acc
        add     hl, hl                  ; x2
        add     hl, hl                  ; x4
        pop     de                      ; de = acc
        add     hl, de                  ; x5
        add     hl, hl                  ; x10
        ld      a, (tmp_dig)
        ld      e, a
        ld      d, 0
        add     hl, de
        ld      (tmp_acc), hl
        pop     de
        pop     hl
        dec     de
        ld      a, d
        or      e
        jr      nz, pn_l
        ld      hl, (tmp_acc)
        ld      a, (tmp_neg)
        or      a
        jr      z, pn_conv
        ld      a, l                    ; negate hl
        cpl
        ld      l, a
        ld      a, h
        cpl
        ld      h, a
        inc     hl
pn_conv:
        call    fix_from_int
        or      a                       ; carry clear
        ret
pn_no:
        scf
        ret

; ===============================================================
; printer
; ===============================================================
; print_val: hl = value, a = mode (0 display, 1 write)
print_val:
        ld      (prt_mode), a
        jr      pv_go
pv_go:
        call    val_type
        cp      1
        jr      z, pv_fix
        cp      0
        jr      z, pv_nil
        cp      2
        jr      z, pv_bool
        cp      3
        jp      z, pv_cons
        cp      4
        jr      z, pv_sym
        cp      5
        jr      z, pv_str
        cp      6
        jp      z, pv_clo
        cp      7
        jp      z, pv_pri
        ld      hl, str_unknown
        jp      con_puts
pv_fix:
        call    fix_decode
        jp      print_int
pv_nil:
        ld      a, '('
        call    con_putc
        ld      a, ')'
        jp      con_putc
pv_bool:
        ld      a, l
        cp      4
        jr      z, pv_t
        ld      hl, str_false
        jp      con_puts
pv_t:
        ld      hl, str_true
        jp      con_puts
pv_sym:
        ex      de, hl                  ; de = value
        ld      hl, 2
        add     hl, de                  ; hl = value+2? no:
        ; name addr word is at (value), len at (value+2)
        ex      de, hl                  ; hl = value, de = junk
        push    hl
        ld      a, (hl)
        inc     hl
        ld      h, (hl)
        ld      l, a                    ; hl = name addr
        pop     de                      ; de = value
        push    hl                      ; save name addr
        ex      de, hl                  ; hl = value
        call    str_obj_len             ; hl = name len
        ex      de, hl                  ; de = len
        pop     hl                      ; hl = name addr
        jr      pv_bytes
pv_str:
        push    hl
        call    str_obj_addr
        ex      de, hl                  ; de = bytes addr (temp)
        pop     hl
        push    de
        call    str_obj_len
        ex      de, hl                  ; de = len
        pop     hl                      ; hl = bytes addr
        ld      a, (prt_mode)
        or      a
        jr      z, pv_bytes
        ; write mode: quotes + escapes
        push    hl
        push    de
        ld      a, '"'
        call    con_putc
        pop     de
        pop     hl
        call    pv_bytes_esc
        ld      a, '"'
        jp      con_putc
pv_bytes:
        ; hl = addr, de = len: raw output
        ld      a, d
        or      e
        ret     z
        ld      a, (hl)
        inc     hl
        dec     de
        push    hl
        push    de
        call    con_putc
        pop     de
        pop     hl
        jr      pv_bytes
pv_bytes_esc:
        ld      a, d
        or      e
        ret     z
        ld      a, (hl)
        inc     hl
        dec     de
        push    hl
        push    de
        cp      '"'
        jr      z, pve_esc
        cp      05ch
        jr      z, pve_esc
        cp      10
        jr      z, pve_nl
        call    con_putc
        jr      pve_nx
pve_esc:
        push    af
        ld      a, 05ch
        call    con_putc
        pop     af
        call    con_putc
        jr      pve_nx
pve_nl:
        ld      a, 05ch
        call    con_putc
        ld      a, 'n'
        call    con_putc
pve_nx:
        pop     de
        pop     hl
        jr      pv_bytes_esc
pv_clo:
        ld      hl, str_closure
        jp      con_puts
pv_pri:
        ld      hl, str_prim
        jp      con_puts
pv_cons:
        push    hl
        ld      a, '('
        call    con_putc
        pop     hl
pv_cons_l:
        push    hl
        call    car_hl
        ld      a, (prt_mode)
        call    print_val
        pop     hl
        call    cdr_hl
        ld      a, h
        or      l
        jr      z, pv_cons_end
        call    val_type
        cp      3
        jr      z, pv_cons_sp
        ; improper tail
        push    hl
        ld      a, ' '
        call    con_putc
        ld      a, '.'
        call    con_putc
        ld      a, ' '
        call    con_putc
        pop     hl
        ld      a, (prt_mode)
        call    print_val
        jr      pv_cons_end2
pv_cons_sp:
        push    hl
        ld      a, ' '
        call    con_putc
        pop     hl
        jr      pv_cons_l
pv_cons_end:
pv_cons_end2:
        ld      a, ')'
        jp      con_putc

; print_int: print hl as signed decimal
print_int:
        bit     7, h
        jr      z, pi_pos
        push    hl
        ld      a, '-'
        call    con_putc
        pop     hl
        ; negate hl
        ld      a, l
        cpl
        ld      l, a
        ld      a, h
        cpl
        ld      h, a
        inc     hl
pi_pos:
        ld      (div_num), hl
        ; extract digits by repeated divmod by 10
        ; (digit count kept in (tmp_dig): divmod16u eats b)
        ld      hl, numbuf
        ld      (tok_ptr), hl
        xor     a
        ld      (tmp_dig), a
pi_l:
        ld      hl, 10
        ld      (div_div), hl
        call    divmod16u
        ld      a, (div_rem)
        add     a, '0'
        ld      hl, (tok_ptr)
        ld      (hl), a
        inc     hl
        ld      (tok_ptr), hl
        ld      a, (tmp_dig)
        inc     a
        ld      (tmp_dig), a
        ld      hl, (div_quot)
        ld      (div_num), hl
        ld      a, h
        or      l
        jr      nz, pi_l
pi_out:
        ld      hl, (tok_ptr)
        dec     hl
        ld      (tok_ptr), hl
        ld      a, (hl)
        call    con_putc
        ld      a, (tmp_dig)
        dec     a
        ld      (tmp_dig), a
        jr      nz, pi_out
        ret

; divmod16u: (div_num) / (div_div) -> (div_quot), (div_rem)
; unsigned; divisor must be nonzero.
divmod16u:
        ld      hl, 0
        ld      (div_rem), hl
        ld      (div_quot), hl
        ld      b, 16
d16_l:
        ld      hl, (div_num)
        add     hl, hl
        ld      (div_num), hl
        ld      hl, (div_rem)
        adc     hl, hl
        ld      (div_rem), hl
        ld      hl, (div_quot)
        add     hl, hl
        ld      (div_quot), hl
        ld      hl, (div_rem)
        ld      de, (div_div)
        or      a
        sbc     hl, de
        jr      c, d16_nx
        ld      (div_rem), hl
        ld      hl, (div_quot)
        inc     hl
        ld      (div_quot), hl
d16_nx:
        djnz    d16_l
        ret

; ===============================================================
; eval
; ===============================================================
; eval: hl = expr, de = env -> hl = value.
; Preserves de and bc.  Tail positions loop inside eval
; (ev_loop) without growing the stack: the saved env push at
; entry stays on the stack across iterations and is popped by
; ev_ret.

; lookup: hl = symbol, de = env -> hl = value (de preserved)
lookup:
        ld      (lk_sym), hl
        ld      (lk_cur), de
lk_walk:
        ld      hl, (lk_cur)
        ld      a, h
        or      l
        jr      z, lk_global
        call    car_hl                  ; pair
        ld      (lk_pair), hl
        call    car_hl                  ; pair's key
        ld      de, (lk_sym)
        or      a
        sbc     hl, de
        jr      z, lk_found
        ld      hl, (lk_cur)
        call    cdr_hl
        ld      (lk_cur), hl
        jr      lk_walk
lk_global:
        ld      hl, (genv)
        ld      (lk_cur), hl
        ld      a, h
        or      l
        jp      z, throw_unbound
lk_gwalk:
        ld      hl, (lk_cur)
        ld      a, h
        or      l
        jp      z, throw_unbound
        call    car_hl
        ld      (lk_pair), hl
        call    car_hl
        ld      de, (lk_sym)
        or      a
        sbc     hl, de
        jr      z, lk_found
        ld      hl, (lk_cur)
        call    cdr_hl
        ld      (lk_cur), hl
        jr      lk_gwalk
lk_found:
        ld      hl, (lk_pair)
        jp      cdr_hl

; lookup_pair: hl = symbol, de = env -> hl = the binding pair
; (env first, then global), or 0 when unbound.  de preserved.
lookup_pair:
        ld      (lk_sym), hl
        ld      (lk_cur), de
        call    lk_scan
        ret     nz                      ; found (hl = pair)
        ld      hl, (genv)
        ld      (lk_cur), hl
        call    lk_scan
        ret     nz
        ld      hl, 0
        ret
; lk_scan: walk (lk_cur) for (lk_sym); zero flag CLEAR and
; hl = pair when found; zero flag SET when not found.
lk_scan:
        ld      hl, (lk_cur)
        ld      a, h
        or      l
        jr      z, lks_no
        call    car_hl
        ld      (lk_pair), hl
        call    car_hl
        ld      de, (lk_sym)
        or      a
        sbc     hl, de
        jr      z, lks_yes
        ld      hl, (lk_cur)
        call    cdr_hl
        ld      (lk_cur), hl
        jr      lk_scan
lks_yes:
        ld      hl, (lk_pair)
        or      1                       ; clear zero
        ret
lks_no:
        xor     a                       ; set zero
        ret

; bind_global: hl = symbol, (bg_val) = value.  Updates the
; existing global pair or pushes a new one.
bind_global:
        ld      (lk_sym), hl
        ld      hl, (genv)
        ld      (lk_cur), hl
        call    lk_scan
        jr      z, bg_new
        ; hl = pair: set its cdr
        ld      de, (bg_val)
        jp      set_cdr_of
bg_new:
        ld      hl, (lk_sym)
        ld      de, (bg_val)
        call    mk_cons                 ; (sym . val)
        ld      de, (genv)
        call    mk_cons                 ; push onto genv
        ld      (genv), hl
        ret

; set_cdr_of: hl = pair, de = value -> (pair+2) = value
set_cdr_of:
        inc     hl
        inc     hl
        ld      (hl), e
        inc     hl
        ld      (hl), d
        ret
; set_car_of: hl = pair, de = value -> (pair) = value
set_car_of:
        ld      (hl), e
        inc     hl
        ld      (hl), d
        ret

; mk_closure: hl = params, (mc_body) = body, de = env
; -> hl = closure value
mk_closure:
        ld      (mc_params), hl
        ld      (mc_env), de
        ld      a, T_CLO
        call    obj_new                 ; hl = value
        ld      de, (obj_addr)
        inc     de
        inc     de                      ; de = base+2 = value
        ld      hl, (mc_params)
        ex      de, hl
        ld      (hl), e
        inc     hl
        ld      (hl), d                 ; params at value+0
        inc     hl
        ld      de, (mc_body)
        ld      (hl), e
        inc     hl
        ld      (hl), d                 ; body at value+2
        inc     hl
        ld      de, (mc_env)
        ld      (hl), e
        inc     hl
        ld      (hl), d                 ; env at value+4
        ld      hl, (obj_addr)
        inc     hl
        inc     hl
        ret

; append_lists: hl = a, de = b -> hl = append(a, b)
; (copies a's spine).  Preserves de.
append_lists:
        ld      a, h
        or      l
        jr      nz, ap_go
        ex      de, hl
        ret
ap_go:
        ld      (ap_b), de
        push    hl
        call    car_hl
        ld      de, 0
        call    mk_cons                 ; head cell (car . nil)
        ld      (ap_head), hl
        ld      (ap_tail), hl
        pop     hl
        call    cdr_hl
ap_l:
        ld      a, h
        or      l
        jr      z, ap_fin
        push    hl
        call    car_hl
        ld      de, 0
        call    mk_cons
        ex      de, hl                  ; de = new cell
        ld      hl, (ap_tail)
        call    set_cdr_of              ; tail.cdr = new cell
        ld      (ap_tail), de
        pop     hl
        call    cdr_hl
        jr      ap_l
ap_fin:
        ld      de, (ap_b)
        ld      hl, (ap_tail)
        call    set_cdr_of
        ld      hl, (ap_head)
        ret

; evlist: hl = exprs, de = env -> hl = values (de preserved)
evlist:
        ld      a, h
        or      l
        ret     z
        push    hl                      ; [exprs]
        call    car_hl                  ; hl = first expr
        call    eval                    ; hl = value (de kept)
        ex      (sp), hl                ; hl = exprs, [value]
        call    cdr_hl                  ; hl = rest exprs
        call    evlist                  ; hl = rest values
        pop     de                      ; de = first value
        ex      de, hl
        jp      mk_cons

; ev_seq_tail: hl = exprs, de = env -> hl = last expr (not
; evaluated); evaluates all preceding ones.  de preserved.
ev_seq_tail:
        ld      a, h
        or      l
        ret     z
est_loop:
        push    hl                      ; [exprs]
        call    cdr_hl                  ; hl = rest
        ld      a, h
        or      l
        jr      nz, est_more
        pop     hl
        jp      car_hl                  ; hl = last expr
est_more:
        ex      (sp), hl                ; hl = exprs, [rest]
        call    car_hl
        call    eval                    ; discard
        pop     hl                      ; hl = rest
        jr      est_loop

; ev_seq: hl = exprs, de = env -> hl = value of the last
ev_seq:
        ld      a, h
        or      l
        jr      z, evs_nil
        call    ev_seq_tail
        jp      eval
evs_nil:
        ld      hl, 0
        ret

; bind_params: hl = params, de = args, (bp_env) = env
; -> de = new env.  Errors on arity mismatch.
bind_params:
        ld      (bp_p), hl
        ld      (bp_a), de
        ld      hl, (bp_env)
        ld      (bp_e), hl
bp_l:
        ld      hl, (bp_p)
        ld      a, h
        or      l
        jr      z, bp_end
        call    val_type
        cp      4
        jr      z, bp_rest              ; params is a symbol
        cp      3
        jp      nz, throw_arity
        ld      hl, (bp_a)
        ld      a, h
        or      l
        jp      z, throw_arity
        call    val_type
        cp      3
        jp      nz, throw_arity
        ld      hl, (bp_p)
        call    car_hl
        ld      (bp_k), hl
        ld      hl, (bp_a)
        call    car_hl
        ex      de, hl                  ; de = arg value
        ld      hl, (bp_k)
        call    mk_cons                 ; (name . value)
        call    mk_cons2_env            ; bp_e = cons(pair, bp_e)
        ld      hl, (bp_p)
        call    cdr_hl
        ld      (bp_p), hl
        ld      hl, (bp_a)
        call    cdr_hl
        ld      (bp_a), hl
        jr      bp_l
bp_rest:
        ld      hl, (bp_p)
        ld      de, (bp_a)
        call    mk_cons                 ; (sym . args)
        call    mk_cons2_env
        jr      bp_done
bp_end:
        ld      hl, (bp_a)
        ld      a, h
        or      l
        jp      nz, throw_arity
bp_done:
        ld      de, (bp_e)
        ret
; mk_cons2_env: hl = pair -> bp_e = cons(pair, bp_e)
mk_cons2_env:
        ld      de, (bp_e)
        call    mk_cons
        ld      (bp_e), hl
        ret

; apply_proc: hl = proc, de = args -> hl = value
apply_proc:
        call    val_type
        cp      7
        jr      z, prim_apply           ; hl=proc, de=args
        cp      6
        jp      nz, throw_notproc
        ld      (ap_proc), hl
        ld      (ap_args), de
        call    car_hl                  ; params (word at +0)
        ld      (bp_p), hl
        ld      hl, (ap_proc)
        call    cdr_hl                  ; body (word at +2)
        ld      (ap_body), hl
        ld      hl, (ap_proc)
        call    obj_word4               ; env (word at +4)
        ld      (bp_env), hl
        ld      hl, (bp_p)
        ld      de, (ap_args)
        call    bind_params             ; de = new env
        ld      hl, (ap_body)
        jp      ev_seq
throw_notproc:
        ld      a, E_NOTPROC
        jp      err_throw

; prim_apply: hl = proc, de = args -> hl = value.
; The primitive receives the args list in hl.
prim_apply:
        ld      (pa_args), de
        call    car_hl                  ; index (word at +0)
        ld      a, l
        ld      hl, prim_table
        ld      e, a
        ld      d, 0
        add     hl, de
        add     hl, de
        ld      a, (hl)
        inc     hl
        ld      h, (hl)
        ld      l, a                    ; target address
        push    hl
        ld      hl, (pa_args)
        ret                             ; jump to the primitive

eval:
        push    de                      ; saved env (see header)
ev_loop:
        bit     0, l
        jr      nz, ev_ret              ; fixnum: self
        ld      a, h
        or      l
        jr      z, ev_ret               ; nil
        ld      a, h
        or      a
        jr      nz, ev_notb             ; high byte: not a bool
        ld      a, l
        cp      2
        jr      z, ev_ret               ; #f
        cp      4
        jr      z, ev_ret               ; #t
ev_notb:
        bit     1, l
        jr      z, ev_form              ; cons: a form
        ; object: symbol -> lookup, else self-evaluating
        call    val_type
        cp      4
        jr      nz, ev_ret
        call    lookup                  ; hl = sym, de = env
        jr      ev_ret
ev_ret:
        pop     de
        ret

ev_form:
        ld      (cur_expr), hl
        ld      (cur_env), de           ; dispatch compares
        ; clobber de, so handlers reload the env from here
        call    car_hl                  ; operator position
        ld      (cur_op), hl
        call    val_type
        cp      4
        jp      nz, f_call              ; not a symbol: call
        ; special-form dispatch on the symbol
        ld      hl, (cur_op)
        ld      de, (sym_quote)
        or      a
        sbc     hl, de
        jp      z, f_quote
        ld      hl, (cur_op)
        ld      de, (sym_if)
        or      a
        sbc     hl, de
        jp      z, f_if
        ld      hl, (cur_op)
        ld      de, (sym_define)
        or      a
        sbc     hl, de
        jp      z, f_define
        ld      hl, (cur_op)
        ld      de, (sym_set)
        or      a
        sbc     hl, de
        jp      z, f_set
        ld      hl, (cur_op)
        ld      de, (sym_lambda)
        or      a
        sbc     hl, de
        jp      z, f_lambda
        ld      hl, (cur_op)
        ld      de, (sym_let)
        or      a
        sbc     hl, de
        jp      z, f_let
        ld      hl, (cur_op)
        ld      de, (sym_letstar)
        or      a
        sbc     hl, de
        jp      z, f_letstar
        ld      hl, (cur_op)
        ld      de, (sym_begin)
        or      a
        sbc     hl, de
        jp      z, f_begin
        ld      hl, (cur_op)
        ld      de, (sym_cond)
        or      a
        sbc     hl, de
        jp      z, f_cond
        ld      hl, (cur_op)
        ld      de, (sym_and)
        or      a
        sbc     hl, de
        jp      z, f_and
        ld      hl, (cur_op)
        ld      de, (sym_or)
        or      a
        sbc     hl, de
        jp      z, f_or
        ld      hl, (cur_op)
        ld      de, (sym_when)
        or      a
        sbc     hl, de
        jp      z, f_when
        jp      f_call

f_quote:
        ld      hl, (cur_expr)
        call    cadr_hl
        jp      ev_ret

f_if:
        ld      de, (cur_env)
        ld      hl, (cur_expr)
        push    hl                      ; [form]
        call    cadr_hl
        call    eval                    ; test value (de kept)
        call    truthy
        pop     hl                      ; hl = form (flags kept)
        jr      z, f_if_else
        call    caddr_hl
        jp      ev_loop                 ; tail
f_if_else:
        call    cddr_hl
        call    cdr_hl                  ; cdddr
        ld      a, h
        or      l
        jr      z, f_if_none
        call    car_hl
        jp      ev_loop                 ; tail
f_if_none:
        ld      hl, 0
        jp      ev_ret

f_define:
        ld      de, (cur_env)
        ld      hl, (cur_expr)
        call    cadr_hl                 ; name or (name . params)
        ld      (df_name), hl
        call    val_type
        cp      3
        jr      z, f_define_fn
        cp      4
        jp      nz, throw_type
        ld      hl, (df_name)
        push    hl                      ; [name]
        ld      hl, (cur_expr)
        call    caddr_hl
        call    eval                    ; value (de kept)
        ld      (bg_val), hl
        pop     hl
        ld      (df_name), hl
        ld      hl, (df_name)
        call    bind_global
        ld      hl, (bg_val)
        jp      ev_ret
f_define_fn:
        ld      hl, (df_name)
        call    car_hl
        ld      (df_name), hl           ; function name
        ld      hl, (cur_expr)
        call    cddr_hl
        ld      (mc_body), hl
        ld      hl, (cur_expr)
        call    cadr_hl
        call    cdr_hl                  ; params in hl
        ; de = current env (closure capture)
        call    mk_closure              ; hl=params de=env
        ld      (bg_val), hl
        ld      hl, (df_name)
        call    bind_global
        ld      hl, (bg_val)
        jp      ev_ret

f_set:
        ld      de, (cur_env)
        ld      hl, (cur_expr)
        call    cadr_hl
        push    hl                      ; [name]
        ld      hl, (cur_expr)
        call    caddr_hl
        call    eval                    ; value (de kept)
        ld      (bg_val), hl
        pop     hl
        ld      (df_name), hl
        ld      hl, (df_name)
        ; de = env (preserved by eval)
        call    lookup_pair             ; hl = pair or 0
        ld      a, h
        or      l
        jp      z, throw_unbound
        ld      de, (bg_val)
        call    set_cdr_of
        ld      hl, (bg_val)
        jp      ev_ret

f_lambda:
        ld      de, (cur_env)
        ld      hl, (cur_expr)
        call    cddr_hl
        ld      (mc_body), hl
        ld      hl, (cur_expr)
        call    cadr_hl                 ; params
        call    mk_closure              ; de = env
        jp      ev_ret

f_begin:
        ld      de, (cur_env)
        ld      hl, (cur_expr)
        call    cdr_hl                  ; body exprs
        call    ev_seq_tail
        jp      ev_loop                 ; tail (hl may be 0)

f_when:
        ld      de, (cur_env)
        ld      hl, (cur_expr)
        push    hl                      ; [form]
        call    cadr_hl
        call    eval
        call    truthy
        pop     hl                      ; hl = form (flags kept)
        jp      z, f_if_none            ; -> nil
        call    cddr_hl
        call    ev_seq_tail
        jp      ev_loop

f_let:
        ld      de, (cur_env)
        ld      hl, (cur_expr)
        call    cadr_hl
        ld      (lt_binds), hl
        ld      (lt_cur), hl
        ld      hl, 0
        ld      (lt_acc), hl
f_let_l:
        ld      hl, (lt_cur)
        ld      a, h
        or      l
        jr      z, f_let_done
        ld      hl, (lt_acc)
        push    hl
        ld      hl, (lt_cur)
        push    hl
        call    car_hl                  ; binding (name init)
        ld      (lt_b), hl
        call    cadr_hl                 ; init expr
        ld      de, (cur_env)
        call    eval                    ; value in OUTER env
        ex      (sp), hl                ; hl = saved binds
        ld      (lt_cur), hl
        pop     hl                      ; hl = value
        ld      (lt_v), hl
        pop     hl                      ; hl = saved acc
        ld      (lt_acc), hl
        ld      hl, (lt_b)
        call    car_hl                  ; name
        ld      de, (lt_v)
        call    mk_cons                 ; (name . value)
        ld      de, (lt_acc)
        call    mk_cons
        ld      (lt_acc), hl
        ld      hl, (lt_cur)
        call    cdr_hl
        ld      (lt_cur), hl
        jr      f_let_l
f_let_done:
        ld      hl, (lt_acc)
        ld      de, (cur_env)           ; outer env
        call    append_lists            ; hl = acc ++ env
        ex      de, hl                  ; de = new env
        ld      hl, (cur_expr)
        call    cddr_hl                 ; body
        call    ev_seq_tail
        jp      ev_loop

f_letstar:
        ld      de, (cur_env)
        ld      (lt_env), de            ; growing env accumulator
        ld      hl, (cur_expr)
        call    cadr_hl
        ld      (lt_cur), hl
f_ls_l:
        ld      hl, (lt_cur)
        ld      a, h
        or      l
        jr      z, f_ls_done
        ld      hl, (lt_env)
        push    hl
        ld      hl, (lt_cur)
        push    hl
        call    car_hl                  ; binding (name init)
        ld      (lt_b), hl
        call    cadr_hl                 ; init expr
        ld      de, (lt_env)
        call    eval                    ; in the growing env
        ex      (sp), hl                ; hl = saved binds
        ld      (lt_cur), hl
        pop     hl                      ; hl = value
        ld      (lt_v), hl
        pop     hl                      ; hl = saved env
        ld      (lt_env), hl
        ld      hl, (lt_b)
        call    car_hl                  ; name
        ld      de, (lt_v)
        call    mk_cons                 ; (name . value)
        ld      de, (lt_env)
        call    mk_cons                 ; push onto the env
        ld      (lt_env), hl
        ld      hl, (lt_cur)
        call    cdr_hl
        ld      (lt_cur), hl
        jr      f_ls_l
f_ls_done:
        ld      de, (lt_env)            ; body env
        ld      hl, (cur_expr)
        call    cddr_hl                 ; body
        call    ev_seq_tail
        jp      ev_loop

f_cond:
        ld      de, (cur_env)
        ld      hl, (cur_expr)
        call    cdr_hl
        ld      (cd_cur), hl
f_cond_l:
        ld      hl, (cd_cur)
        ld      a, h
        or      l
        jp      z, f_if_none            ; no clause: nil
        call    car_hl                  ; clause
        ld      (cd_cl), hl
        call    car_hl                  ; test
        ld      (cd_t), hl
        ld      de, (sym_else)
        or      a
        sbc     hl, de
        jr      z, f_cond_body
        ld      hl, (cd_cur)
        push    hl
        ld      hl, (cd_cl)
        push    hl
        ld      hl, (cd_t)
        call    eval
        ex      (sp), hl                ; hl = saved clause
        ld      (cd_cl), hl
        pop     hl                      ; hl = test value
        ld      (cd_v), hl
        pop     hl                      ; hl = saved clause list
        ld      (cd_cur), hl
        ld      hl, (cd_v)              ; test value for truthy
        call    truthy
        jr      z, f_cond_next
        ld      hl, (cd_cl)
        call    cdr_hl
        ld      a, h
        or      l
        jr      nz, f_cond_seq
        ld      hl, (cd_v)              ; (test) alone: value
        jp      ev_ret
f_cond_seq:
        ld      hl, (cd_cl)
        call    cdr_hl
        call    ev_seq_tail
        jp      ev_loop
f_cond_body:                            ; else clause
        ld      hl, (cd_cl)
        call    cdr_hl
        call    ev_seq_tail
        jp      ev_loop
f_cond_next:
        ld      hl, (cd_cur)
        call    cdr_hl
        ld      (cd_cur), hl
        jr      f_cond_l

f_and:
        ld      de, (cur_env)
        ld      hl, (cur_expr)
        call    cdr_hl
        ld      (ad_cur), hl
        ld      a, h
        or      l
        jr      nz, f_and_l
        ld      hl, 4                   ; (and) = #t
        jp      ev_ret
f_and_l:
        ld      hl, (ad_cur)
        call    cdr_hl
        ld      a, h
        or      l
        jr      nz, f_and_more
        ld      hl, (ad_cur)            ; last: tail-evaluate
        call    car_hl
        jp      ev_loop
f_and_more:
        ld      hl, (ad_cur)
        push    hl
        call    car_hl
        call    eval
        ex      (sp), hl
        ld      (ad_cur), hl
        pop     hl
        call    truthy
        jp      z, ev_ret               ; falsy value in hl
        ld      hl, (ad_cur)
        call    cdr_hl
        ld      (ad_cur), hl
        jr      f_and_l

f_or:
        ld      de, (cur_env)
        ld      hl, (cur_expr)
        call    cdr_hl
        ld      (ad_cur), hl
        ld      a, h
        or      l
        jr      nz, f_or_l
        ld      hl, 2                   ; (or) = #f
        jp      ev_ret
f_or_l:
        ld      hl, (ad_cur)
        call    cdr_hl
        ld      a, h
        or      l
        jr      nz, f_or_more
        ld      hl, (ad_cur)
        call    car_hl
        jp      ev_loop
f_or_more:
        ld      hl, (ad_cur)
        push    hl
        call    car_hl
        call    eval
        ex      (sp), hl
        ld      (ad_cur), hl
        pop     hl
        call    truthy
        jp      nz, ev_ret              ; truthy value in hl
        ld      hl, (ad_cur)
        call    cdr_hl
        ld      (ad_cur), hl
        jr      f_or_l

f_call:
        ld      de, (cur_env)
        ld      hl, (cur_expr)
        push    hl                      ; [form] (nested evals
        ; clobber the cur_* globals, so keep state on the stack)
        call    car_hl                  ; operator expr
        call    eval                    ; proc (de kept)
        ex      (sp), hl                ; hl = form, [proc]
        call    cdr_hl                  ; arg exprs
        call    evlist                  ; values (de kept)
        pop     de                      ; de = proc
        ld      (cur_args), hl
        ex      de, hl                  ; hl = proc
        call    val_type
        cp      7
        jr      z, f_call_prim
        cp      6
        jp      nz, throw_notproc
        ; closure: bind in its captured env, tail-call body
        ld      (ap_proc), hl
        call    car_hl                  ; params
        ld      (bp_p), hl
        ld      hl, (ap_proc)
        call    cdr_hl                  ; body
        ld      (ap_body), hl
        ld      hl, (ap_proc)
        call    obj_word4               ; captured env
        ld      (bp_env), hl
        ld      hl, (bp_p)
        ld      de, (cur_args)
        call    bind_params             ; de = new env
        ld      hl, (ap_body)
        call    ev_seq_tail             ; hl = last body expr
        jp      ev_loop                 ; tail (de = new env)
f_call_prim:
        ld      de, (cur_args)
        call    prim_apply              ; hl=proc de=args
        jp      ev_ret

; ===============================================================
; primitives
; ===============================================================
; Convention: args list in hl; result in hl.  pa_arg1 pops one
; argument into hl and leaves the rest in (pa_rest).

pa_arg1:
        ld      a, h
        or      l
        jp      z, throw_arity
        push    hl
        call    cdr_hl
        ld      (pa_rest), hl
        pop     hl
        jp      car_hl
pa_end:
        ld      hl, (pa_rest)
        ld      a, h
        or      l
        jp      nz, throw_arity
        ret
need_fix:
        bit     0, l
        jp      z, throw_type
        jp      fix_decode
need_str:
        call    val_type
        cp      5
        jp      nz, throw_type
        ret
need_sym:
        call    val_type
        cp      4
        jp      nz, throw_type
        ret
ret_true:
        ld      hl, 4
        ret
ret_false:
        ld      hl, 2
        ret
; bool_of: zero flag set -> #f, else #t
bool_of:
        jr      z, ret_false
        jr      ret_true
; bool_zt: zero flag set -> #t, else #f
bool_zt:
        jr      z, ret_true
        jr      ret_false

; cmp_sgn: hl vs de (signed) -> carry if hl < de, zero if =
cmp_sgn:
        ld      a, h
        xor     d
        jp      p, cmp_same
        ; signs differ: hl < de iff hl is negative
        ld      a, h
        and     080h
        jr      nz, cmp_lt
        or      1                       ; not less, not equal
        ret
cmp_lt:
        scf
        ret
cmp_same:
        or      a
        sbc     hl, de
        ret

; mul16u: hl = hl * de (low 16 bits)
mul16u:
        ld      (mul_a), hl
        ld      (mul_b), de
        ld      hl, 0
        ld      (mul_r), hl
        ld      b, 16
mul_l:
        ld      hl, (mul_b)
        srl     h
        rr      l
        ld      (mul_b), hl
        jr      nc, mul_s
        ld      hl, (mul_r)
        ld      de, (mul_a)
        add     hl, de
        ld      (mul_r), hl
mul_s:
        ld      hl, (mul_a)
        add     hl, hl
        ld      (mul_a), hl
        djnz    mul_l
        ld      hl, (mul_r)
        ret

; abs_hl: hl = |hl| (raw int)
abs_hl:
        bit     7, h
        ret     z
        ld      a, l
        cpl
        ld      l, a
        ld      a, h
        cpl
        ld      h, a
        inc     hl
        ret

; sdiv16: (sd_a) / (sd_b) -> (sd_q), (sd_r); signed.
; Division by zero throws E_DIV0.
sdiv16:
        ld      hl, (sd_b)
        ld      a, h
        or      l
        jr      nz, sd_go
        ld      a, E_DIV0
        jp      err_throw
sd_go:
        ; sign_q in (sd_sq), sign_r in (sd_sr)
        ld      hl, (sd_a)
        call    abs_hl
        ld      (div_num), hl
        ld      a, (sd_a + 1)
        and     080h
        ld      (sd_sr), a
        ld      hl, (sd_b)
        call    abs_hl
        ld      (div_div), hl
        ; sign_q = sign(a) XOR sign(b):
        ld      a, (sd_a + 1)
        and     080h
        ld      b, a
        ld      a, (sd_b + 1)
        and     080h
        xor     b
        ld      (sd_sq), a
        call    divmod16u
        ld      hl, (div_quot)
        ld      a, (sd_sq)
        or      a
        jr      z, sd_qok
        ld      a, l
        cpl
        ld      l, a
        ld      a, h
        cpl
        ld      h, a
        inc     hl
sd_qok:
        ld      (sd_q), hl
        ld      hl, (div_rem)
        ld      a, (sd_sr)
        or      a
        jr      z, sd_rok
        ld      a, l
        cpl
        ld      l, a
        ld      a, h
        cpl
        ld      h, a
        inc     hl
sd_rok:
        ld      (sd_r), hl
        ret

; rev_list: hl = list -> hl = reversed list
rev_list:
        ld      de, 0
rev_l:
        ld      a, h
        or      l
        jr      z, rev_done
        push    hl
        push    de
        call    car_hl
        pop     de
        call    mk_cons                 ; (car . acc)
        ex      de, hl                  ; de = new acc
        pop     hl
        call    cdr_hl
        jr      rev_l
rev_done:
        ex      de, hl
        ret

; equal_val: hl, de -> zero flag set if structurally equal
equal_val:
        ld      a, h
        xor     d
        jr      nz, eqv_d
        ld      a, l
        xor     e
        ret     z                       ; identical -> equal
eqv_d:
        push    hl
        push    de
        call    val_type
        ld      (eqv_t), a
        ex      de, hl
        call    val_type
        ld      b, a
        ld      a, (eqv_t)
        cp      b
        jr      nz, eqv_no
        cp      3
        jr      z, eqv_cons
        cp      5
        jr      z, eqv_str
eqv_no:
        pop     de
        pop     hl
        or      1                       ; not equal
        ret
eqv_cons:
        pop     de                      ; de = B
        pop     hl                      ; hl = A
        push    hl
        push    de
        call    car_hl                  ; hl = car A
        ex      de, hl                  ; de = car A, hl = B
        call    car_hl                  ; hl = car B
        ex      de, hl                  ; hl = car A, de = car B
        call    equal_val
        jr      nz, eqv_cons_no
        pop     de
        pop     hl
        call    cdr_hl                  ; hl = cdr A
        ex      de, hl                  ; de = cdr A, hl = B
        call    cdr_hl                  ; hl = cdr B
        ex      de, hl                  ; hl = cdr A, de = cdr B
        jp      equal_val
eqv_cons_no:
        pop     de
        pop     hl
        or      1
        ret
eqv_str:
        pop     de                      ; de = B
        pop     hl                      ; hl = A
        push    hl
        push    de
        call    str_obj_len             ; hl = len A
        ld      (eqv_n), hl
        ex      de, hl                  ; de = len A, hl = B
        call    str_obj_len             ; hl = len B
        or      a
        sbc     hl, de
        jr      nz, eqv_no
        pop     de                      ; de = B
        pop     hl                      ; hl = A
        push    de
        call    str_obj_addr            ; hl = addr A
        ex      de, hl                  ; de = addr A, hl = B
        call    str_obj_addr            ; hl = addr B
        ex      de, hl                  ; hl = addr A, de = addr B
        ld      bc, (eqv_n)
eqv_cl:
        ld      a, b
        or      c
        jr      z, eqv_yes
        ld      a, (de)
        cp      (hl)
        jr      nz, eqv_no4
        inc     hl
        inc     de
        dec     bc
        jr      eqv_cl
eqv_yes:
        pop     de                      ; discard saved B
        xor     a                       ; equal
        ret
eqv_no4:
        pop     de
        or      1
        ret

; ---- the primitives themselves ----
p_cons:
        call    pa_arg1
        ld      (op_a), hl
        ld      hl, (pa_rest)
        call    pa_arg1
        ex      de, hl
        ld      hl, (op_a)
        call    mk_cons
        push    hl
        call    pa_end
        pop     hl
        ret
p_car:
        call    pa_arg1
        push    hl
        call    pa_end
        pop     hl
        ld      a, h
        or      l
        ret     z                       ; (car nil) = nil
        call    val_type
        cp      3
        jp      nz, throw_type
        jp      car_hl
p_cdr:
        call    pa_arg1
        push    hl
        call    pa_end
        pop     hl
        ld      a, h
        or      l
        ret     z
        call    val_type
        cp      3
        jp      nz, throw_type
        jp      cdr_hl
p_setcar:
        call    pa_arg1
        ld      (op_a), hl
        ld      hl, (pa_rest)
        call    pa_arg1
        ld      (op_b), hl
        call    pa_end
        ld      hl, (op_a)
        call    val_type
        cp      3
        jp      nz, throw_type
        ld      hl, (op_a)
        ld      de, (op_b)
        call    set_car_of
        ld      hl, (op_b)
        ret
p_setcdr:
        call    pa_arg1
        ld      (op_a), hl
        ld      hl, (pa_rest)
        call    pa_arg1
        ld      (op_b), hl
        call    pa_end
        ld      hl, (op_a)
        call    val_type
        cp      3
        jp      nz, throw_type
        ld      hl, (op_a)
        ld      de, (op_b)
        call    set_cdr_of
        ld      hl, (op_b)
        ret
p_pairp:
        call    pa_arg1
        push    hl
        call    pa_end
        pop     hl
        call    val_type
        cp      3
        jp      bool_zt
p_nullp:
        call    pa_arg1
        push    hl
        call    pa_end
        pop     hl
        ld      a, h
        or      l
        jp      bool_zt                 ; z (nil) -> #t
p_symp:
        call    pa_arg1
        push    hl
        call    pa_end
        pop     hl
        call    val_type
        cp      4
        jp      bool_zt
p_strp:
        call    pa_arg1
        push    hl
        call    pa_end
        pop     hl
        call    val_type
        cp      5
        jp      bool_zt
p_nump:
        call    pa_arg1
        push    hl
        call    pa_end
        pop     hl
        call    val_type
        cp      1
        jp      bool_zt
p_procp:
        call    pa_arg1
        push    hl
        call    pa_end
        pop     hl
        call    val_type
        cp      6
        jp      z, ret_true
        cp      7
        jp      z, ret_true
        jp      ret_false
p_boolp:
        call    pa_arg1
        push    hl
        call    pa_end
        pop     hl
        call    val_type
        cp      2
        jp      bool_zt
p_eq:
        call    pa_arg1
        ld      (op_a), hl
        ld      hl, (pa_rest)
        call    pa_arg1
        ld      de, (op_a)
        or      a
        sbc     hl, de
        push    af
        call    pa_end
        pop     af
        jp      bool_zt                 ; z -> #t
p_equal:
        call    pa_arg1
        ld      (op_a), hl
        ld      hl, (pa_rest)
        call    pa_arg1
        ld      de, (op_a)
        ex      de, hl                  ; hl=a, de=b
        call    equal_val
        push    af
        call    pa_end
        pop     af
        jp      bool_zt
p_not:
        call    pa_arg1
        push    hl
        call    pa_end
        pop     hl
        call    truthy
        jp      z, ret_true
        jp      ret_false
p_add:
        ld      hl, 0
        ld      (acc_i), hl
        ld      hl, (pa_args)           ; args were in hl at
        ; entry; prim_apply left them in (pa_args) and in hl.
        ; (hl already = args here)
p_add_l:
        ld      a, h
        or      l
        jr      z, p_add_done
        push    hl
        call    car_hl
        call    need_fix                ; hl = int
        ld      de, (acc_i)
        add     hl, de
        ld      (acc_i), hl
        pop     hl
        call    cdr_hl
        jr      p_add_l
p_add_done:
        ld      hl, (acc_i)
        jp      fix_from_int
p_sub:
        call    pa_arg1
        call    need_fix
        ld      (acc_i), hl
        ld      hl, (pa_rest)
        ld      a, h
        or      l
        jr      nz, p_sub_l
        ; single arg: negate
        ld      hl, (acc_i)
        ld      a, l
        cpl
        ld      l, a
        ld      a, h
        cpl
        ld      h, a
        inc     hl
        jp      fix_from_int
p_sub_l:
        ld      a, h
        or      l
        jr      z, p_add_done           ; same finish as add
        push    hl
        call    car_hl
        call    need_fix
        ex      de, hl                  ; de = subtrahend
        ld      hl, (acc_i)
        or      a
        sbc     hl, de
        ld      (acc_i), hl
        pop     hl
        call    cdr_hl
        jr      p_sub_l
p_mul:
        ld      hl, 1
        ld      (acc_i), hl
p_mul_s:
        ld      hl, (pa_args)
p_mul_l:
        ld      a, h
        or      l
        jr      z, p_add_done
        push    hl
        call    car_hl
        call    need_fix
        ld      de, (acc_i)
        call    mul16u
        ld      (acc_i), hl
        pop     hl
        call    cdr_hl
        jr      p_mul_l
p_quot:
        call    pa_arg1
        call    need_fix
        ld      (sd_a), hl
        ld      hl, (pa_rest)
        call    pa_arg1
        call    need_fix
        ld      (sd_b), hl
        call    pa_end
        call    sdiv16
        ld      hl, (sd_q)
        jp      fix_from_int
p_rem:
        call    pa_arg1
        call    need_fix
        ld      (sd_a), hl
        ld      hl, (pa_rest)
        call    pa_arg1
        call    need_fix
        ld      (sd_b), hl
        call    pa_end
        call    sdiv16
        ld      hl, (sd_r)
        jp      fix_from_int
p_numeq:
        call    pa_arg1
        call    need_fix
        ld      (op_a), hl
        ld      hl, (pa_rest)
        call    pa_arg1
        call    need_fix
        ld      de, (op_a)
        ex      de, hl                  ; hl = a, de = b
        or      a
        sbc     hl, de
        push    af
        call    pa_end
        pop     af
        jp      bool_zt
p_lt:
        call    pa_arg1
        call    need_fix
        ld      (op_a), hl
        ld      hl, (pa_rest)
        call    pa_arg1
        call    need_fix
        ld      (op_b), hl
        call    pa_end
        ld      hl, (op_a)
        ld      de, (op_b)
        call    cmp_sgn                 ; carry iff a < b
        jp      c, ret_true
        jp      ret_false
p_gt:
        call    pa_arg1
        call    need_fix
        ld      (op_a), hl
        ld      hl, (pa_rest)
        call    pa_arg1
        call    need_fix
        ld      (op_b), hl
        call    pa_end
        ld      hl, (op_b)
        ld      de, (op_a)
        call    cmp_sgn                 ; carry iff b < a (a > b)
        jp      c, ret_true
        jp      ret_false
p_le:
        call    pa_arg1
        call    need_fix
        ld      (op_a), hl
        ld      hl, (pa_rest)
        call    pa_arg1
        call    need_fix
        ld      (op_b), hl
        call    pa_end
        ld      hl, (op_b)
        ld      de, (op_a)
        call    cmp_sgn                 ; b < a -> false... le
        ; a <= b  <=>  not (b < a): carry clear -> #t
        jp      c, ret_false
        jp      ret_true
p_ge:
        call    pa_arg1
        call    need_fix
        ld      (op_a), hl
        ld      hl, (pa_rest)
        call    pa_arg1
        call    need_fix
        ld      (op_b), hl
        call    pa_end
        ld      hl, (op_a)
        ld      de, (op_b)
        call    cmp_sgn                 ; a < b -> false
        jp      c, ret_false
        jp      ret_true
p_zerop:
        call    pa_arg1
        call    need_fix
        push    hl
        call    pa_end
        pop     hl
        ld      a, h
        or      l
        jp      bool_zt
p_abs:
        call    pa_arg1
        call    need_fix
        push    hl
        call    pa_end
        pop     hl
        call    abs_hl
        jp      fix_from_int
p_length:
        call    pa_arg1
        push    hl
        call    pa_end
        pop     hl
        ld      de, 0
plen_l:
        ld      a, h
        or      l
        jr      z, plen_done
        call    val_type
        cp      3
        jp      nz, throw_type
        inc     de
        call    cdr_hl
        jr      plen_l
plen_done:
        ex      de, hl
        jp      fix_from_int

; obj_string: hl = bytes addr, de = length -> hl = STR value
; pointing at the existing bytes (no copy).
obj_string:
        ld      (tmp_dst), hl
        ld      (tmp_cdr), de
        ld      a, T_STR
        call    obj_new                 ; hl = value
        ld      de, (obj_addr)
        inc     de
        inc     de                      ; de = value position
        ld      hl, (tmp_dst)
        ex      de, hl
        ld      (hl), e
        inc     hl
        ld      (hl), d                 ; bytes addr at value+0
        inc     hl
        ld      de, (tmp_cdr)
        ld      (hl), e
        inc     hl
        ld      (hl), d                 ; length at value+2
        ld      hl, (obj_addr)
        inc     hl
        inc     hl
        ret

; ---- string primitives ----
p_strlen:
        call    pa_arg1
        call    need_str
        push    hl
        call    pa_end
        pop     hl
        call    str_obj_len
        jp      fix_from_int
p_strref:
        call    pa_arg1
        call    need_str
        ld      (op_a), hl
        ld      hl, (pa_rest)
        call    pa_arg1
        call    need_fix                ; hl = index
        ld      (op_b), hl
        call    pa_end
        ld      hl, (op_a)
        call    str_obj_len
        ld      de, (op_b)
        or      a
        sbc     hl, de                  ; len - idx
        jp      c, throw_type           ; idx > len-? (idx==len
        jr      z, p_strref_bad         ;   also bad)
        ld      hl, (op_b)
        bit     7, h
        jr      nz, p_strref_bad
        ld      hl, (op_a)
        call    str_obj_addr
        ld      de, (op_b)
        add     hl, de
        ld      a, (hl)
        ld      l, a
        ld      h, 0
        add     hl, hl
        inc     hl
        ret
p_strref_bad:
        jp      throw_type
p_substring:
        call    pa_arg1
        call    need_str
        ld      (op_a), hl
        ld      hl, (pa_rest)
        call    pa_arg1
        call    need_fix
        ld      (op_b), hl              ; start
        ld      hl, (pa_rest)
        call    pa_arg1
        call    need_fix                ; end
        ld      (tmp_acc), hl
        call    pa_end
        ; validate 0 <= start <= end <= len
        ld      hl, (op_b)
        bit     7, h
        jp      nz, throw_type
        ld      hl, (tmp_acc)
        bit     7, h
        jp      nz, throw_type
        ld      de, (op_b)
        or      a
        sbc     hl, de                  ; end - start
        jp      c, throw_type
        ld      (tmp_cdr), hl           ; result length
        ld      hl, (op_a)
        call    str_obj_len
        ld      de, (tmp_acc)
        or      a
        sbc     hl, de                  ; len - end
        jp      c, throw_type
        ld      hl, (op_a)
        call    str_obj_addr
        ld      de, (op_b)
        add     hl, de                  ; source = addr + start
        ld      de, (tmp_cdr)
        jp      make_string
p_strappend:
        ld      hl, (pa_args)
        ld      de, 0
        ld      (sa_total), de
psa_1:
        ld      a, h
        or      l
        jr      z, psa_alloc
        push    hl
        call    car_hl
        call    need_str
        call    str_obj_len
        ld      de, (sa_total)
        add     hl, de
        ld      (sa_total), hl
        pop     hl
        call    cdr_hl
        jr      psa_1
psa_alloc:
        ld      de, (sa_total)
        call    str_alloc               ; hl = dest
        ld      (sa_dst), hl
        ld      hl, (pa_args)
psa_2:
        ld      a, h
        or      l
        jr      z, psa_done
        push    hl
        call    car_hl
        ld      (op_a), hl
        call    str_obj_addr
        ex      de, hl                  ; de = src
        ld      hl, (op_a)
        call    str_obj_len
        ld      b, h
        ld      c, l                    ; bc = len
        ld      hl, (sa_dst)
        ld      a, b
        or      c
        jr      z, psa_2n
        ex      de, hl                  ; hl = src, de = dst
        ldir
        ld      (sa_dst), de            ; de advanced past copy
psa_2n:
        pop     hl
        call    cdr_hl
        jr      psa_2
psa_done:
        ld      hl, (sa_dst)            ; end of the block; the
        ld      de, (sa_total)          ; start = end - total
        or      a
        sbc     hl, de
        ex      de, hl                  ; de = start... obj_string
        ; obj_string wants hl=addr, de=len:
        ld      hl, 0
        add     hl, de
        ld      de, (sa_total)
        jp      obj_string
p_strtolist:
        call    pa_arg1
        call    need_str
        ld      (op_a), hl
        call    pa_end
        ld      hl, (op_a)
        call    str_obj_addr
        ld      (tmp_car), hl
        ld      hl, (op_a)
        call    str_obj_len
        ld      (tmp_cdr), hl
        ld      hl, 0                   ; acc (reversed order ok:
        ; we push chars front to back, then reverse)
        ld      (tmp_acc), hl
        ld      bc, (tmp_cdr)
pst_l:
        ld      a, b
        or      c
        jr      z, pst_done
        ld      hl, (tmp_car)
        ld      a, (hl)
        inc     hl
        ld      (tmp_car), hl
        ld      l, a
        ld      h, 0
        add     hl, hl
        inc     hl                      ; fixnum char code
        ld      de, (tmp_acc)
        call    mk_cons
        ld      (tmp_acc), hl
        dec     bc
        jr      pst_l
pst_done:
        ld      hl, (tmp_acc)
        jp      rev_list
p_listtostr:
        call    pa_arg1
        ld      (op_a), hl
        call    pa_end
        ; count + validate
        ld      hl, (op_a)
        ld      de, 0
plt_c:
        ld      a, h
        or      l
        jr      z, plt_alloc
        push    hl
        push    de
        call    car_hl
        call    need_fix                ; decoded (any int; low
        pop     de                      ;   byte is used)
        pop     hl
        inc     de
        call    cdr_hl
        jr      plt_c
plt_alloc:
        ld      (tmp_cdr), de
        call    str_alloc               ; hl = dest (de = len)
        ld      (tmp_dst), hl
        ld      hl, (op_a)
plt_f:
        ld      a, h
        or      l
        jr      z, plt_done
        push    hl
        call    car_hl
        call    need_fix                ; hl = int
        ld      a, l                    ; low byte
        ld      hl, (tmp_dst)
        ld      (hl), a
        inc     hl
        ld      (tmp_dst), hl
        pop     hl
        call    cdr_hl
        jr      plt_f
plt_done:
        ld      hl, (tmp_dst)
        ld      de, (tmp_cdr)
        or      a
        sbc     hl, de                  ; start of the block
        ld      de, (tmp_cdr)
        jp      obj_string
p_numtostr:
        call    pa_arg1
        call    need_fix                ; hl = int
        push    hl
        call    pa_end
        pop     hl
        ; sign
        ld      a, 0
        ld      (tmp_neg), a
        bit     7, h
        jr      z, pnt_pos
        ld      a, 1
        ld      (tmp_neg), a
        call    abs_hl
pnt_pos:
        ld      (div_num), hl
        ld      hl, tokbuf
        ld      (tok_ptr), hl
        ld      b, 0
pnt_l:
        ld      hl, 10
        ld      (div_div), hl
        push    bc
        call    divmod16u
        pop     bc
        ld      a, (div_rem)
        add     a, '0'
        ld      hl, (tok_ptr)
        ld      (hl), a
        inc     hl
        ld      (tok_ptr), hl
        inc     b
        ld      hl, (div_quot)
        ld      (div_num), hl
        ld      a, h
        or      l
        jr      nz, pnt_l
        ; tokbuf holds digits reversed; copy into numbuf fwd
        ld      hl, numbuf
        ld      a, (tmp_neg)
        or      a
        jr      z, pnt_cp
        ld      (hl), '-'
        inc     hl
pnt_cp:
        ld      (tmp_dst), hl           ; write cursor
        ld      e, b                    ; digit count
        ld      d, 0
        ld      hl, (tok_ptr)
        or      a
        sbc     hl, de                  ; first digit pos (fwd
        ; digits are stored LSD-first from tokbuf; forward
        ; string = read them backwards from tok_ptr-1 down)
        ld      hl, (tok_ptr)
pnt_r:
        dec     hl
        ld      a, (hl)
        push    hl
        ld      hl, (tmp_dst)
        ld      (hl), a
        inc     hl
        ld      (tmp_dst), hl
        pop     hl
        djnz    pnt_r
        ; length = digits + sign
        ld      hl, (tmp_dst)
        ld      de, numbuf
        or      a
        sbc     hl, de
        ex      de, hl                  ; de = length
        ld      hl, numbuf
        jp      make_string
p_strtonum:
        call    pa_arg1
        call    need_str
        ld      (op_a), hl
        call    pa_end
        ld      hl, (op_a)
        call    str_obj_addr
        ex      de, hl
        ld      hl, (op_a)
        call    str_obj_len
        ex      de, hl                  ; hl = addr, de = len
        call    parse_number
        jp      c, ret_false
        ret
p_symtostr:
        call    pa_arg1
        call    need_sym
        push    hl
        call    pa_end
        pop     hl
        push    hl
        call    str_obj_len             ; name len (sym layout)
        ex      de, hl                  ; de = len
        pop     hl
        ld      a, (hl)
        inc     hl
        ld      h, (hl)
        ld      l, a                    ; name addr (word at value)
        jp      make_string
p_streq:
        call    pa_arg1
        call    need_str
        ld      (op_a), hl
        ld      hl, (pa_rest)
        call    pa_arg1
        call    need_str
        ld      de, (op_a)
        ex      de, hl
        call    equal_val
        push    af
        call    pa_end
        pop     af
        jp      bool_zt

; ---- I/O and system primitives ----
p_display:
        call    pa_arg1
        push    hl
        call    pa_end
        pop     hl
        xor     a
        call    print_val
        jp      ret_true
p_write:
        call    pa_arg1
        push    hl
        call    pa_end
        pop     hl
        ld      a, 1
        call    print_val
        jp      ret_true
p_newline:
        call    con_crlf
        jp      ret_true
p_readline:
        call    getline                 ; a = length
        ld      e, a
        ld      d, 0
        ld      hl, linebuf
        jp      make_string
p_readkey:
        call    kb_getkey
        ld      l, a
        ld      h, 0
        add     hl, hl
        inc     hl
        ret
p_clear:
        call    clrscr
        jp      ret_true
p_exitmenu:
        ld      sp, (save_sp)
        jp      menu
p_poke:
        call    pa_arg1
        ld      (op_a), hl
        ld      hl, (pa_rest)
        call    pa_arg1
        ld      (op_b), hl
        call    pa_end
        ld      hl, (op_a)
        call    need_fix                ; hl = address pattern
        ld      (pk_addr), hl
        ld      hl, (op_b)
        call    need_fix
        ld      a, l
        ld      hl, (pk_addr)
        ld      (hl), a
        jp      ret_true
p_peek:
        call    pa_arg1
        ld      (op_a), hl
        call    pa_end
        ld      hl, (op_a)
        call    need_fix
        ld      a, (hl)
        ld      l, a
        ld      h, 0
        add     hl, hl
        inc     hl
        ret
p_eval:
        call    pa_arg1
        ld      (op_a), hl
        call    pa_end
        ld      hl, (op_a)
        ld      de, (genv)
        jp      eval
p_apply:
        call    pa_arg1
        ld      (ap_proc), hl
        ld      hl, (pa_rest)
        ld      a, h
        or      l
        jp      z, throw_arity
        push    hl
        call    cdr_hl
        ld      a, h
        or      l
        jr      z, pap_single
        pop     hl
        ld      (ap_cur), hl
        ld      hl, 0
        ld      (ap_acc), hl
pap_l:
        ld      hl, (ap_cur)
        call    cdr_hl
        ld      a, h
        or      l
        jr      z, pap_last
        ld      hl, (ap_cur)
        call    car_hl
        ld      de, (ap_acc)
        call    mk_cons
        ld      (ap_acc), hl
        ld      hl, (ap_cur)
        call    cdr_hl
        ld      (ap_cur), hl
        jr      pap_l
pap_last:
        ld      hl, (ap_cur)
        call    car_hl                  ; the final arg (a list)
        ld      (ap_args), hl
        ld      hl, (ap_acc)
        call    rev_list
        ld      de, (ap_args)
        call    append_lists
        ex      de, hl                  ; de = full args
        ld      hl, (ap_proc)
        jp      apply_proc
pap_single:
        pop     hl
        call    car_hl
        ex      de, hl                  ; de = the arg list
        ld      hl, (ap_proc)
        jp      apply_proc
p_list:
        ret                             ; args are already a list
p_readfromstr:
        call    pa_arg1
        call    need_str
        ld      (op_a), hl
        call    pa_end
        ld      hl, (rd_ptr)
        ld      (rd_savep), hl
        ld      hl, (rd_end)
        ld      (rd_savee), hl
        ld      hl, (op_a)
        call    str_obj_addr
        ex      de, hl
        ld      hl, (op_a)
        call    str_obj_len
        ex      de, hl                  ; hl = addr, de = len
        call    rd_init
        call    read_expr
        ld      (op_b), hl
        ld      b, a
        ld      hl, (rd_savep)
        ld      (rd_ptr), hl
        ld      hl, (rd_savee)
        ld      (rd_end), hl
        ld      a, b
        or      a
        jp      nz, ret_false           ; EOF / ) / . -> #f
        ld      hl, (op_b)
        ret
p_loadstring:
        call    pa_arg1
        call    need_str
        ld      (op_a), hl
        call    pa_end
        ld      hl, (rd_ptr)
        ld      (rd_savep), hl
        ld      hl, (rd_end)
        ld      (rd_savee), hl
        ld      hl, (op_a)
        call    str_obj_addr
        ex      de, hl
        ld      hl, (op_a)
        call    str_obj_len
        ex      de, hl
        call    rd_init
        ld      hl, 2                   ; last value = #f if none
        ld      (op_b), hl
pls_l:
        call    read_expr
        cp      1
        jr      z, pls_done
        or      a
        jp      nz, throw_read
        ld      de, (genv)
        call    eval
        ld      (op_b), hl
        jr      pls_l
pls_done:
        ld      hl, (rd_savep)
        ld      (rd_ptr), hl
        ld      hl, (rd_savee)
        ld      (rd_end), hl
        ld      hl, (op_b)
        ret
p_tapescan:
        call    scan_files
        ld      hl, 0
        ld      (ts_acc), hl
        ld      hl, ftab
        ld      (ts_cur), hl
        ld      a, (nfiles)
        ld      (ts_i), a
pts_l:
        ld      a, (ts_i)
        or      a
        jr      z, pts_done
        dec     a
        ld      (ts_i), a
        ; name string from the entry (NUL-terminated)
        ld      hl, (ts_cur)
        call    strlen                  ; de = length
        ld      hl, (ts_cur)
        call    make_string
        ld      (ts_name), hl
        ; fields
        ld      hl, (ts_cur)
        ld      de, 18
        add     hl, de
        ld      a, (hl)
        inc     hl
        ld      h, (hl)
        ld      l, a                    ; size
        call    fix_from_int
        ld      (ts_f1), hl
        ld      hl, (ts_cur)
        ld      de, 20
        add     hl, de
        ld      a, (hl)
        inc     hl
        ld      h, (hl)
        ld      l, a                    ; load
        call    fix_from_int
        ld      (ts_f2), hl
        ld      hl, (ts_cur)
        ld      de, 22
        add     hl, de
        ld      a, (hl)
        inc     hl
        ld      h, (hl)
        ld      l, a                    ; exec
        call    fix_from_int
        ; build (name size load exec)
        ld      de, 0
        call    mk_cons                 ; (exec)
        ex      de, hl
        ld      hl, (ts_f2)
        call    mk_cons                 ; (load exec)
        ex      de, hl
        ld      hl, (ts_f1)
        call    mk_cons                 ; (size load exec)
        ex      de, hl
        ld      hl, (ts_name)
        call    mk_cons                 ; entry list
        ld      de, (ts_acc)
        call    mk_cons
        ld      (ts_acc), hl
        ld      hl, (ts_cur)
        ld      de, 26
        add     hl, de
        ld      (ts_cur), hl
        jr      pts_l
pts_done:
        ld      hl, (ts_acc)
        jp      rev_list
; strlen: hl = NUL-terminated string -> de = length
strlen:
        ld      de, 0
stl_l:
        ld      a, (hl)
        or      a
        ret     z
        inc     hl
        inc     de
        jr      stl_l
p_boot:
        call    pa_arg1
        ld      (op_a), hl
        call    pa_end
        ld      hl, (op_a)
        call    need_fix                ; hl = 1-based index
        ld      a, h
        or      l
        jp      z, ret_false
        bit     7, h
        jp      nz, ret_false
        dec     hl
        ld      a, l
        call    load_file_n             ; returns only on error
        jp      ret_false
p_scripttext:
        ld      hl, SCRBUF + 8
        ld      de, (SCRBUF + 4)        ; length from the header
        jp      obj_string
p_scriptsave:
        call    pa_arg1
        call    need_str
        ld      (op_a), hl
        call    pa_end
        ld      hl, (op_a)
        call    str_obj_len
        ld      de, SCRPSIZE - 8
        or      a
        sbc     hl, de
        jr      c, pss_go
        jr      z, pss_go
        ld      a, E_BIG
        jp      err_throw
pss_go:
        ld      hl, (op_a)
        call    str_obj_len
        ld      (scr_new_len), hl
        ld      hl, (op_a)
        call    str_obj_addr
        ld      (scr_new_ptr), hl
        call    save_script_native
        jr      nc, pss_rec
        ld      a, (save_err)
        or      a
        jr      nz, pss_err
        ld      a, E_TAPE
pss_err:
        jp      err_throw
pss_rec:
        call    record_tape
        jp      ret_true

p_scriptlen:
        ld      hl, (SCRBUF + 4)
        jp      fix_from_int
p_scriptsavecur:
        ld      hl, SCRBUF + 8
        ld      (scr_new_ptr), hl
        ld      hl, (SCRBUF + 4)
        ld      (scr_new_len), hl
        call    save_script_native
        jr      nc, psc_rec
        ld      a, (save_err)
        or      a
        jr      nz, psc_err
        ld      a, E_TAPE
psc_err:
        jp      err_throw
psc_rec:
        call    record_tape
        jp      ret_true

; (block-ref i): byte i of the script block payload
; (SCRBUF + i); i must be a fixnum in [0, SCRPSIZE).
p_blockref:
        call    pa_arg1
        ld      (op_a), hl
        call    pa_end
        ld      hl, (op_a)
        call    need_fix
        ld      de, SCRBUF
        add     hl, de
        ld      a, (hl)
        ld      l, a
        ld      h, 0
        add     hl, hl
        inc     hl
        ret
; (block-set! i b): set payload byte i to b (0..255).
p_blockset:
        call    pa_arg1
        ld      (op_a), hl
        ld      hl, (pa_rest)
        call    pa_arg1
        ld      (op_b), hl
        call    pa_end
        ld      hl, (op_a)
        call    need_fix
        ld      de, SCRBUF
        add     hl, de
        ld      (pk_addr), hl
        ld      hl, (op_b)
        call    need_fix
        ld      a, l
        ld      hl, (pk_addr)
        ld      (hl), a
        jp      ret_true

; (block-splice! a b str): replace block text bytes [a, b)
; with str's bytes; updates the header length.  Native
; memmove: Scheme-level per-byte loops would exhaust the
; no-GC arenas on a 4 KB block.  E_BIG if the result does
; not fit.
p_blocksplice:
        call    pa_arg1
        ld      (bs_a), hl
        ld      hl, (pa_rest)
        call    pa_arg1
        ld      (bs_b), hl
        ld      hl, (pa_rest)
        call    pa_arg1
        call    need_str
        ld      (bs_s), hl
        call    pa_end
        ld      hl, (bs_a)
        call    need_fix
        ld      (bs_a), hl
        ld      hl, (bs_b)
        call    need_fix
        ld      (bs_b), hl
        ld      hl, (bs_s)
        call    str_obj_len             ; hl = insert length
        ld      (bs_il), hl
        ld      hl, (SCRBUF + 4)        ; n = current length
        ld      (bs_n), hl
        ; d = il - (b - a); newn = n + d
        ld      hl, (bs_b)
        ld      de, (bs_a)
        or      a
        sbc     hl, de                  ; hl = b - a
        ld      de, (bs_il)
        ex      de, hl
        or      a
        sbc     hl, de                  ; hl = d (signed)
        ld      (bs_d), hl
        ld      de, (bs_n)
        add     hl, de                  ; newn
        ld      (bs_nn), hl
        bit     7, h
        jp      nz, bs_big
        ld      de, SCRPSIZE - 8
        or      a
        sbc     hl, de
        jp      z, bs_fit
        jp      nc, bs_big
bs_fit:
        ; move the tail [b, n) to [b+d, n+d)
        ld      hl, (bs_d)
        ld      a, h
        or      l
        jp      z, bs_copy
        bit     7, h
        jp      nz, bs_fwd
        ; d > 0: copy backwards from the end
        ld      hl, (bs_n)
        ld      de, SCRBUF + 8
        add     hl, de                  ; src end (exclusive)
        ld      (bs_src), hl
        ld      hl, (bs_nn)
        ld      de, SCRBUF + 8
        add     hl, de                  ; dst end
        ld      (bs_dst), hl
        ld      hl, (bs_n)
        ld      de, (bs_b)
        or      a
        sbc     hl, de                  ; tail count
        ld      (bs_cnt), hl
bs_bk:
        ld      hl, (bs_cnt)
        ld      a, h
        or      l
        jr      z, bs_copy
        dec     hl
        ld      (bs_cnt), hl
        ld      hl, (bs_src)
        dec     hl
        ld      (bs_src), hl
        ld      a, (hl)
        ld      hl, (bs_dst)
        dec     hl
        ld      (bs_dst), hl
        ld      (hl), a
        jr      bs_bk
bs_fwd:
        ; d < 0: copy forwards
        ld      hl, (bs_b)
        ld      de, SCRBUF + 8
        add     hl, de
        ld      (bs_src), hl
        ld      hl, (bs_b)
        ld      de, (bs_d)
        add     hl, de
        ld      de, SCRBUF + 8
        add     hl, de
        ld      (bs_dst), hl
        ld      hl, (bs_n)
        ld      de, (bs_b)
        or      a
        sbc     hl, de
        ld      (bs_cnt), hl
bs_fw:
        ld      hl, (bs_cnt)
        ld      a, h
        or      l
        jr      z, bs_copy
        dec     hl
        ld      (bs_cnt), hl
        ld      hl, (bs_src)
        ld      a, (hl)
        inc     hl
        ld      (bs_src), hl
        ld      hl, (bs_dst)
        ld      (hl), a
        inc     hl
        ld      (bs_dst), hl
        jr      bs_fw
bs_copy:
        ; copy the insert string to SCRBUF+8+a
        ld      hl, (bs_s)
        call    str_obj_addr
        ld      (bs_src), hl
        ld      hl, (bs_a)
        ld      de, SCRBUF + 8
        add     hl, de
        ld      (bs_dst), hl
        ld      hl, (bs_il)
        ld      (bs_cnt), hl
bs_cp:
        ld      hl, (bs_cnt)
        ld      a, h
        or      l
        jr      z, bs_len
        dec     hl
        ld      (bs_cnt), hl
        ld      hl, (bs_src)
        ld      a, (hl)
        inc     hl
        ld      (bs_src), hl
        ld      hl, (bs_dst)
        ld      (hl), a
        inc     hl
        ld      (bs_dst), hl
        jr      bs_cp
bs_len:
        ld      hl, (bs_nn)
        ld      (SCRBUF + 4), hl
        jp      ret_true
bs_big:
        ld      a, E_BIG
        jp      err_throw

; (block-find i byte): first text index >= i holding byte,
; or -1.  Native scan (see block-splice! for why).
p_blockfind:
        call    pa_arg1
        ld      (op_a), hl
        ld      hl, (pa_rest)
        call    pa_arg1
        ld      (op_b), hl
        call    pa_end
        ld      hl, (op_b)
        call    need_fix
        ld      a, l
        ld      (bs_byt), a
        ld      hl, (op_a)
        call    need_fix               ; hl = i (decoded)
        ld      (bs_idx), hl
        ld      de, (SCRBUF + 4)        ; len
        or      a
        sbc     hl, de
        jr      nc, bf_none             ; i >= len
        add     hl, de                  ; hl = i again
        ld      de, SCRBUF + 8
        add     hl, de
        ld      (bs_src), hl            ; scan ptr
        ld      hl, (SCRBUF + 4)
        ld      de, (bs_idx)
        or      a
        sbc     hl, de                  ; remaining = len - i
        ld      (bs_cnt), hl
bf_l:
        ld      hl, (bs_cnt)
        ld      a, h
        or      l
        jr      z, bf_none
        dec     hl
        ld      (bs_cnt), hl
        ld      hl, (bs_src)
        ld      a, (bs_byt)
        cp      (hl)
        jr      z, bf_hit
        inc     hl
        ld      (bs_src), hl
        ld      hl, (bs_idx)
        inc     hl
        ld      (bs_idx), hl
        jr      bf_l
bf_hit:
        ld      hl, (bs_idx)
        jp      fix_from_int
bf_none:
        ld      hl, -1
        jp      fix_from_int

; (block-line-start n): text index where 1-based line n
; starts, or -1 if the block has fewer lines.
p_blockls:
        call    pa_arg1
        call    need_fix
        ld      (bs_n), hl
        call    pa_end
        ld      hl, (bs_n)
        ld      a, h
        or      l
        jr      z, bls_none             ; line 0: none
        dec     hl
        ld      (bs_n), hl              ; newlines to pass
        ld      hl, SCRBUF + 8
        ld      (bs_src), hl
        ld      hl, (SCRBUF + 4)
        ld      (bs_cnt), hl
        ld      hl, 0
        ld      (bs_idx), hl
bls_l:
        ld      hl, (bs_n)
        ld      a, h
        or      l
        jr      z, bls_hit              ; passed enough NLs
        ld      hl, (bs_cnt)
        ld      a, h
        or      l
        jr      z, bls_none
        dec     hl
        ld      (bs_cnt), hl
        ld      hl, (bs_src)
        ld      a, (hl)
        inc     hl
        ld      (bs_src), hl
        ld      hl, (bs_idx)
        inc     hl
        ld      (bs_idx), hl
        cp      10
        jr      nz, bls_l
        ld      hl, (bs_n)
        dec     hl
        ld      (bs_n), hl
        jr      bls_l
bls_hit:
        ld      hl, (bs_idx)
        jp      fix_from_int
bls_none:
        ld      hl, -1
        jp      fix_from_int

; rnd_tick_inc: bump the key-wait polling counter.  Called on
; every kb_raw poll inside kb_getkey, so (rnd_tick) measures
; how long the user took to press/release keys -- the entropy
; source for (random ...).  Preserves all registers.
rnd_tick_inc:
        push    hl
        ld      hl, (rnd_tick)
        inc     hl
        ld      (rnd_tick), hl
        pop     hl
        ret

; (random n): integer in [0, n); n must be a positive fixnum.
; Generator: xorshift16 (shifts 7, 9, 8; full period 65535),
; state in (rnd_state).  Seeded lazily on first use:
; seed = (rnd_tick) XOR script-block checksum XOR 0xACE1,
; remapped to 0xACE1 if zero (xorshift sticks at zero).
p_random:
        call    pa_arg1
        call    need_fix
        ld      (rnd_n), hl
        call    pa_end
        ld      hl, (rnd_n)
        ld      a, h
        or      l
        jp      z, throw_type           ; n = 0
        bit     7, h
        jp      nz, throw_type          ; n < 0
        ld      hl, (rnd_state)
        ld      a, h
        or      l
        jr      nz, rnd_mix
        ld      hl, (rnd_tick)
        ld      de, (SCRBUF + 6)        ; script checksum
        ld      a, l
        xor     e
        ld      l, a
        ld      a, h
        xor     d
        ld      h, a
        ld      a, l
        xor     0e1h
        ld      l, a
        ld      a, h
        xor     0ach
        ld      h, a
        ld      a, h
        or      l
        jr      nz, rnd_step
        ld      hl, 0ace1h
        jr      rnd_step
rnd_mix:
        ; state ^= current key-wait tick: any key activity
        ; since the last call perturbs the stream
        ld      de, (rnd_tick)
        ld      a, l
        xor     e
        ld      l, a
        ld      a, h
        xor     d
        ld      h, a
        ld      a, h
        or      l
        jr      nz, rnd_step
        ld      hl, 0ace1h
rnd_step:
        ; x ^= x << 7
        ld      d, h
        ld      e, l
        ld      b, 7
rnd_s1:
        add     hl, hl
        djnz    rnd_s1
        ld      a, l
        xor     e
        ld      l, a
        ld      a, h
        xor     d
        ld      h, a
        ; x ^= x >> 9  (x>>9 = high byte >> 1, in the low byte)
        ld      a, h
        srl     a
        xor     l
        ld      l, a
        ; x ^= x << 8  (high byte ^= low byte)
        ld      a, h
        xor     l
        ld      h, a
        ld      (rnd_state), hl
        ; result = state mod n (unsigned)
        ld      (div_num), hl
        ld      hl, (rnd_n)
        ld      (div_div), hl
        call    divmod16u
        ld      hl, (div_rem)
        jp      fix_from_int

; ===============================================================
; date / time via the IOCS RTGET seat (entry 22, 0xE442)
; ===============================================================
; The RTGET body is still a stub (scf; ret) at this point;
; its real implementation is the debugger job's task (brief
; 追記2).  These primitives are therefore built to fail
; safely -- they return #f -- until a conforming RTGET
; exists:
;   1. the seat is only called if it holds a JP opcode
;      (the Tiny Scheme environment has no resident IOCS;
;      on a bare boot 0xE442 is uninitialised RAM);
;   2. HL = rtc_buf (6 bytes) is passed, the IOCS house
;      convention "carry = error" is honoured (the stub
;      always sets carry);
;   3. the buffer is sentinel-filled first and the returned
;      fields are range-checked as binary values
;      (+0 year, +1 month 1..12, +2 day 1..31, +3 hour
;      0..23, +4 minute 0..59, +5 second 0..59).
; If the future RTGET adopts a different buffer layout,
; only rtget_fetch's offsets need to change.
RTGET_SEAT      equ     0e442h

rtget_fetch:
        ld      a, (RTGET_SEAT)
        cp      0c3h
        jr      nz, rtf_bad
        ld      hl, rtc_buf
        ld      b, 6
        ld      a, 0ffh
rtf_fill:
        ld      (hl), a
        inc     hl
        djnz    rtf_fill
        ld      hl, rtc_buf
        call    RTGET_SEAT
        jr      c, rtf_bad
        ld      a, (rtc_buf + 1)
        or      a
        jr      z, rtf_bad
        cp      13
        jr      nc, rtf_bad
        ld      a, (rtc_buf + 2)
        or      a
        jr      z, rtf_bad
        cp      32
        jr      nc, rtf_bad
        ld      a, (rtc_buf + 3)
        cp      24
        jr      nc, rtf_bad
        ld      a, (rtc_buf + 4)
        cp      60
        jr      nc, rtf_bad
        ld      a, (rtc_buf + 5)
        cp      60
        jr      nc, rtf_bad
        or      a                       ; carry clear: valid
        ret
rtf_bad:
        scf
        ret

; rtc_fix: a = byte -> hl = fixnum
rtc_fix:
        ld      l, a
        ld      h, 0
        add     hl, hl
        inc     hl
        ret

; (date) -> (year month day) or #f
p_date:
        ld      a, h
        or      l
        jp      nz, throw_arity
        call    rtget_fetch
        jp      c, ret_false
        ld      a, (rtc_buf + 2)
        call    rtc_fix
        ld      de, 0
        call    mk_cons
        push    hl
        ld      a, (rtc_buf + 1)
        call    rtc_fix
        pop     de
        call    mk_cons
        push    hl
        ld      a, (rtc_buf + 0)
        call    rtc_fix
        pop     de
        jp      mk_cons

; (call addr) / (call addr hlval): call the machine-code
; routine at addr (a fixnum address, same convention as
; peek/poke) and return when it RETs.  The optional second
; argument is loaded into HL before the call (default 0);
; on return, carry set yields #f (the IOCS error
; convention), otherwise the returned HL is the result
; (as a fixnum).  Other registers are the callee's to use;
; the callee must keep SP balanced.  This is the bridge
; primitive for prototyping: with peek/poke a Scheme
; program can plant bytes anywhere and run them, and can
; reach the IOCS jump table (0xE400) or any other resident
; routine.  Deliberately dangerous, like poke: policy is
; the Scheme side's business.
p_call:
        call    pa_arg1
        call    need_fix
        ld      (cl_addr), hl
        ld      hl, (pa_rest)
        ld      a, h
        or      l
        jr      z, pc_hl0
        call    pa_arg1
        call    need_fix
        push    hl
        call    pa_end
        pop     hl
        jr      pc_go
pc_hl0:
        ld      hl, 0
pc_go:
        ld      ix, (cl_addr)
        call    pc_tramp
        jp      c, ret_false
        jp      fix_from_int
pc_tramp:
        jp      (ix)

; (time) -> (hour minute second) or #f
p_time:
        ld      a, h
        or      l
        jp      nz, throw_arity
        call    rtget_fetch
        jp      c, ret_false
        ld      a, (rtc_buf + 5)
        call    rtc_fix
        ld      de, 0
        call    mk_cons
        push    hl
        ld      a, (rtc_buf + 4)
        call    rtc_fix
        pop     de
        call    mk_cons
        push    hl
        ld      a, (rtc_buf + 3)
        call    rtc_fix
        pop     de
        jp      mk_cons

; ===============================================================
; scheme_init / run_source
; ===============================================================
scheme_init:
        ld      hl, CONS_BASE
        ld      (cons_ptr), hl
        ld      hl, ATOM_BASE
        ld      (atom_ptr), hl
        ld      hl, STR_BASE
        ld      (str_ptr), hl
        ld      hl, 0
        ld      (symtab), hl
        ld      (genv), hl
        ld      (rnd_state), hl
        ld      (rnd_tick), hl
        ; intern the special-form symbols
        ld      hl, spec_name_ptrs
        ld      (init_np), hl
        ld      hl, spec_var_ptrs
        ld      (init_vp), hl
        ld      b, SPEC_COUNT
si_spec:
        push    bc
        ld      hl, (init_np)
        ld      a, (hl)
        inc     hl
        ld      h, (hl)
        ld      l, a                    ; name string ptr
        push    hl
        call    strlen                  ; de = length
        pop     hl
        call    intern                  ; hl = symbol
        ex      de, hl                  ; de = symbol
        ld      hl, (init_vp)
        ld      a, (hl)
        inc     hl
        ld      h, (hl)
        ld      l, a                    ; var address
        ld      (hl), e
        inc     hl
        ld      (hl), d
        ld      hl, (init_np)
        inc     hl
        inc     hl
        ld      (init_np), hl
        ld      hl, (init_vp)
        inc     hl
        inc     hl
        ld      (init_vp), hl
        pop     bc
        djnz    si_spec
        ; bind the primitives into the global env
        ld      hl, prim_name_ptrs
        ld      (init_np), hl
        ld      a, 0
        ld      (init_i), a
        ld      b, PRIM_COUNT
si_prim:
        push    bc
        ld      hl, (init_np)
        ld      a, (hl)
        inc     hl
        ld      h, (hl)
        ld      l, a
        push    hl
        call    strlen
        pop     hl
        call    intern
        ld      (init_sym), hl
        ld      a, T_PRIM
        call    obj_new                 ; hl = value
        ld      (bg_val), hl
        ld      de, (obj_addr)
        inc     de
        inc     de
        ld      a, (init_i)             ; index word at value+0
        ld      (de), a
        inc     de
        xor     a
        ld      (de), a
        ld      hl, (init_sym)
        call    bind_global
        ld      hl, (init_np)
        inc     hl
        inc     hl
        ld      (init_np), hl
        ld      a, (init_i)
        inc     a
        ld      (init_i), a
        pop     bc
        djnz    si_prim
        ret

; run_source: hl = text ptr, de = length.  Reads and evaluates
; every top-level form in the global env.
run_source:
        call    rd_init
rsrc_l:
        call    read_expr
        cp      1
        ret     z                       ; EOF: done
        or      a
        jp      nz, throw_read
        ld      de, (genv)
        call    eval
        jr      rsrc_l

; ===============================================================
; tables
; ===============================================================
SPEC_COUNT      equ     13
spec_name_ptrs:
        dw      sn_quote, sn_if, sn_define, sn_set, sn_lambda
        dw      sn_let, sn_letstar, sn_begin, sn_cond, sn_and
        dw      sn_or, sn_when, sn_else
spec_var_ptrs:
        dw      sym_quote, sym_if, sym_define, sym_set
        dw      sym_lambda, sym_let, sym_letstar, sym_begin
        dw      sym_cond, sym_and, sym_or, sym_when, sym_else
sn_quote:       db      "quote", 0
sn_if:          db      "if", 0
sn_define:      db      "define", 0
sn_set:         db      "set!", 0
sn_lambda:      db      "lambda", 0
sn_let:         db      "let", 0
sn_letstar:     db      "let*", 0
sn_begin:       db      "begin", 0
sn_cond:        db      "cond", 0
sn_and:         db      "and", 0
sn_or:          db      "or", 0
sn_when:        db      "when", 0
sn_else:        db      "else", 0

PRIM_COUNT      equ     66
prim_name_ptrs:
        dw      pn_cons, pn_car, pn_cdr, pn_setcar, pn_setcdr
        dw      pn_pairp, pn_nullp, pn_symp, pn_strp, pn_nump
        dw      pn_procp, pn_eq, pn_equal, pn_not, pn_add
        dw      pn_sub, pn_mul, pn_quot, pn_rem, pn_numeq
        dw      pn_lt, pn_gt, pn_le, pn_ge, pn_zerop
        dw      pn_abs, pn_length, pn_display, pn_write
        dw      pn_newline, pn_readline, pn_strlen, pn_strref
        dw      pn_substr, pn_strapp, pn_str2list, pn_list2str
        dw      pn_num2str, pn_str2num, pn_sym2str, pn_streq
        dw      pn_readstr, pn_eval, pn_apply, pn_list
        dw      pn_tapescan, pn_boot, pn_scripttext
        dw      pn_scriptsave, pn_readkey, pn_poke, pn_peek
        dw      pn_clear, pn_exitmenu, pn_loadstring
        dw      pn_scriptlen, pn_scriptsavecur
        dw      pn_blockref, pn_blockset
        dw      pn_blocksplice, pn_blockfind, pn_blockls
        dw      pn_random, pn_date, pn_time, pn_call
prim_table:
        dw      p_cons, p_car, p_cdr, p_setcar, p_setcdr
        dw      p_pairp, p_nullp, p_symp, p_strp, p_nump
        dw      p_procp, p_eq, p_equal, p_not, p_add
        dw      p_sub, p_mul, p_quot, p_rem, p_numeq
        dw      p_lt, p_gt, p_le, p_ge, p_zerop
        dw      p_abs, p_length, p_display, p_write
        dw      p_newline, p_readline, p_strlen, p_strref
        dw      p_substring, p_strappend, p_strtolist
        dw      p_listtostr, p_numtostr, p_strtonum, p_symtostr
        dw      p_streq, p_readfromstr, p_eval, p_apply, p_list
        dw      p_tapescan, p_boot, p_scripttext, p_scriptsave
        dw      p_readkey, p_poke, p_peek, p_clear, p_exitmenu
        dw      p_loadstring
        dw      p_scriptlen, p_scriptsavecur
        dw      p_blockref, p_blockset
        dw      p_blocksplice, p_blockfind, p_blockls
        dw      p_random, p_date, p_time, p_call
pn_cons:        db      "cons", 0
pn_car:         db      "car", 0
pn_cdr:         db      "cdr", 0
pn_setcar:      db      "set-car!", 0
pn_setcdr:      db      "set-cdr!", 0
pn_pairp:       db      "pair?", 0
pn_nullp:       db      "null?", 0
pn_symp:        db      "symbol?", 0
pn_strp:        db      "string?", 0
pn_nump:        db      "number?", 0
pn_procp:       db      "procedure?", 0
pn_eq:          db      "eq?", 0
pn_equal:       db      "equal?", 0
pn_not:         db      "not", 0
pn_add:         db      "+", 0
pn_sub:         db      "-", 0
pn_mul:         db      "*", 0
pn_quot:        db      "quotient", 0
pn_rem:         db      "remainder", 0
pn_numeq:       db      "=", 0
pn_lt:          db      "<", 0
pn_gt:          db      ">", 0
pn_le:          db      "<=", 0
pn_ge:          db      ">=", 0
pn_zerop:       db      "zero?", 0
pn_abs:         db      "abs", 0
pn_length:      db      "length", 0
pn_display:     db      "display", 0
pn_write:       db      "write", 0
pn_newline:     db      "newline", 0
pn_readline:    db      "read-line", 0
pn_strlen:      db      "string-length", 0
pn_strref:      db      "string-ref", 0
pn_substr:      db      "substring", 0
pn_strapp:      db      "string-append", 0
pn_str2list:    db      "string->list", 0
pn_list2str:    db      "list->string", 0
pn_num2str:     db      "number->string", 0
pn_str2num:     db      "string->number", 0
pn_sym2str:     db      "symbol->string", 0
pn_streq:       db      "string=?", 0
pn_readstr:     db      "read-from-string", 0
pn_eval:        db      "eval", 0
pn_apply:       db      "apply", 0
pn_list:        db      "list", 0
pn_tapescan:    db      "tape-scan", 0
pn_boot:        db      "boot", 0
pn_scripttext:  db      "script-text", 0
pn_scriptsave:  db      "script-save", 0
pn_readkey:     db      "read-key", 0
pn_poke:        db      "poke", 0
pn_peek:        db      "peek", 0
pn_clear:       db      "clear", 0
pn_exitmenu:    db      "exit-menu", 0
pn_loadstring:  db      "load-string", 0
pn_scriptlen:   db      "script-len", 0
pn_scriptsavecur: db    "script-save-current", 0
pn_blockref:    db      "block-ref", 0
pn_blockset:    db      "block-set!", 0
pn_blocksplice: db      "block-splice!", 0
pn_blockfind:   db      "block-find", 0
pn_blockls:     db      "block-line-start", 0
pn_random:      db      "random", 0
pn_date:        db      "date", 0
pn_time:        db      "time", 0
pn_call:        db      "call", 0

; ===============================================================
; strings and data
; ===============================================================
msg_title:      db      "TSLOAD - TINY SCHEME 2-STAGE", 10, 0
msg_menu:       db      "TS MENU (NO VALID SCRIPT)", 10, 0
msg_sel:        db      "SELECT 1-9, S=SCRIPT, Q=HALT", 10, "> ", 0
msg_nofiles:    db      "NO FILES ON TAPE", 10, 0
msg_loaderr:    db      "LOAD ERROR", 10, 0
msg_err:        db      "SCHEME ERR ", 0
str_true:       db      "#t", 0
str_false:      db      "#f", 0
str_closure:    db      "#<closure>", 0
str_prim:       db      "#<primitive>", 0
str_unknown:    db      "#<?>", 0
str_script:     db      "SCRIPT"
str_script16:   db      "SCRIPT       "
str_tsload:     db      "TSLOAD       "
sig_menu:       db      "TSMN"

cur_x:          ds      1
cur_y:          ds      1
cur_attr:       ds      1
cpd_started:    ds      1
tp_byte:        ds      1
tp_shadow:      ds      1
tp_cksum:       ds      2
tp_zcnt:        ds      1
save_sp:        ds      2
err_code:       ds      1
save_err:       ds      1
loader_size:    ds      2
scr_len:        ds      2
rnd_state:      ds      2
rnd_tick:       ds      2
rnd_n:          ds      2
rtc_buf:        ds      6
cl_addr:        ds      2
scr_new_ptr:    ds      2
scr_new_len:    ds      2
nfiles:         ds      1
ftab_ptr:       ds      2
stage_ptr:      ds      2
want_idx:       ds      1
infobuf:        ds      36
ftab:           ds      12 * 26
linebuf:        ds      128
cons_ptr:       ds      2
atom_ptr:       ds      2
str_ptr:        ds      2
symtab:         ds      2
genv:           ds      2
sym_quote:      ds      2
sym_if:         ds      2
sym_define:     ds      2
sym_set:        ds      2
sym_lambda:     ds      2
sym_let:        ds      2
sym_letstar:    ds      2
sym_begin:      ds      2
sym_cond:       ds      2
sym_and:        ds      2
sym_or:         ds      2
sym_when:       ds      2
sym_else:       ds      2
rd_ptr:         ds      2
rd_end:         ds      2
rd_savep:       ds      2
rd_savee:       ds      2
tokbuf:         ds      256
tok_ptr:        ds      2
tok_len:        ds      2
numbuf:         ds      8
prt_mode:       ds      1
div_num:        ds      2
div_div:        ds      2
div_quot:       ds      2
div_rem:        ds      2
tmp_car:        ds      2
tmp_cdr:        ds      2
tmp_dst:        ds      2
tmp_sym:        ds      2
tmp_np:         ds      2
tmp_acc:        ds      2
tmp_dig:        ds      1
tmp_any:        ds      1
tmp_neg:        ds      1
tmp_type:       ds      1
obj_addr:       ds      2
cur_expr:       ds      2
cur_env:        ds      2
cur_op:         ds      2
cur_proc:       ds      2
cur_args:       ds      2
lk_sym:         ds      2
lk_cur:         ds      2
lk_pair:        ds      2
bg_val:         ds      2
df_name:        ds      2
mc_body:        ds      2
mc_params:      ds      2
mc_env:         ds      2
ap_b:           ds      2
ap_head:        ds      2
ap_tail:        ds      2
ap_proc:        ds      2
ap_args:        ds      2
ap_body:        ds      2
ap_cur:         ds      2
ap_acc:         ds      2
bp_p:           ds      2
bp_a:           ds      2
bp_e:           ds      2
bp_env:         ds      2
bp_k:           ds      2
pa_args:        ds      2
pa_rest:        ds      2
lt_binds:       ds      2
lt_cur:         ds      2
lt_acc:         ds      2
lt_b:           ds      2
lt_v:           ds      2
lt_env:         ds      2
lt_pair:        ds      1
cd_cur:         ds      2
cd_cl:          ds      2
cd_t:           ds      2
cd_v:           ds      2
ad_cur:         ds      2
op_a:           ds      2
op_b:           ds      2
acc_i:          ds      2
sd_a:           ds      2
sd_b:           ds      2
sd_q:           ds      2
sd_r:           ds      2
sd_sq:          ds      1
sd_sr:          ds      1
mul_a:          ds      2
mul_b:          ds      2
mul_r:          ds      2
eqv_t:          ds      1
eqv_n:          ds      2
sa_total:       ds      2
sa_dst:         ds      2
pk_addr:        ds      2
ts_acc:         ds      2
ts_cur:         ds      2
ts_i:           ds      1
ts_name:        ds      2
ts_f1:          ds      2
ts_f2:          ds      2
bs_a:           ds      2
bs_b:           ds      2
bs_s:           ds      2
bs_il:          ds      2
bs_n:           ds      2
bs_d:           ds      2
bs_nn:          ds      2
bs_src:         ds      2
bs_dst:         ds      2
bs_cnt:         ds      2
bs_idx:         ds      2
bs_byt:         ds      1
init_np:        ds      2
init_vp:        ds      2
init_i:         ds      1
init_sym:       ds      2

