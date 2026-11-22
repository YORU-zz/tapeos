; cmd_core.inc - core shell commands D/E/M/G (common layer).
; ---------------------------------------------------------------
; D [addr] - dump 128 bytes (hex + ASCII)
; ---------------------------------------------------------------
cmd_dump:
        call    skip_sp
        call    parse_hex               ; de = value, carry if none
        jr      c, cd_use
        ex      de, hl
        ld      (dump_ptr), hl
        ex      de, hl
cd_use:
        ld      hl, (dump_ptr)
        ld      b, 8                    ; 8 lines x 16 bytes
cd_line:
        push    bc
        call    C_HEX16                 ; address (hl preserved)
        ld      a, ':'
        call    C_CONOUT
        ld      a, ' '
        call    C_CONOUT
        push    hl                      ; line start (for ASCII pass)
        ld      b, 16
cd_hex:
        ld      a, (hl)
        inc     hl
        push    hl
        push    bc
        call    C_HEXOUT
        ld      a, ' '
        call    C_CONOUT
        pop     bc
        pop     hl
        djnz    cd_hex
        pop     hl                      ; line start
        ld      b, 16
cd_asc:
        ld      a, (hl)
        inc     hl
        cp      020h
        jr      c, cd_dot
        cp      07fh
        jr      c, cd_ok
cd_dot:
        ld      a, '.'
cd_ok:
        push    hl
        push    bc
        call    C_CONOUT
        pop     bc
        pop     hl
        djnz    cd_asc
        call    C_CRLF
        pop     bc
        djnz    cd_line
        ld      (dump_ptr), hl
        jp      shell_loop

; ---------------------------------------------------------------
; E addr - examine/modify bytes interactively
;   per byte: shows "XXXX: XX " and reads a line:
;   empty = next, hex byte = write + next, "." = quit
; ---------------------------------------------------------------
cmd_exam:
        call    skip_sp
        call    parse_hex
        jp      c, err_exit
        ex      de, hl                  ; hl = address
ce_loop:
        push    hl
        call    C_HEX16
        ld      a, ':'
        call    C_CONOUT
        ld      a, ' '
        call    C_CONOUT
        pop     hl
        push    hl
        ld      a, (hl)
        call    C_HEXOUT
        ld      a, ' '
        call    C_CONOUT
        ld      hl, SUBBUF
        ld      b, 8
        call    C_GETLIN                ; a = length
        pop     hl                      ; hl = address
        or      a
        jr      z, ce_next              ; empty -> next byte
        push    hl
        ld      hl, SUBBUF
        ld      a, (hl)
        cp      '.'
        jr      z, ce_quit
        call    parse_hex
        jr      c, ce_bad
        pop     hl
        ld      a, e
        ld      (hl), a
        jr      ce_next2
ce_bad:
        pop     hl
        jr      ce_loop                 ; re-prompt same address
ce_quit:
        pop     hl
        jp      shell_loop
ce_next:
        inc     hl
        jr      ce_loop
ce_next2:
        inc     hl
        jr      ce_loop

; ---------------------------------------------------------------
; M addr bb [bb ...] - write bytes
; (target pointer kept in BC; parse_hex preserves BC)
; ---------------------------------------------------------------
cmd_mod:
        call    skip_sp
        call    parse_hex
        jp      c, err_exit
        ld      b, d
        ld      c, e                    ; bc = target address
cm_loop:
        call    skip_sp
        ld      a, (hl)
        or      a
        jp      z, shell_loop           ; end of line -> done
        call    parse_hex
        jp      c, err_exit
        ld      a, e
        ld      (bc), a
        inc     bc
        jr      cm_loop

; ---------------------------------------------------------------
; G [addr] / J [addr] - call a routine (RET returns to the shell)
; With no address, calls the built-in demo routine.
; ---------------------------------------------------------------
cmd_go:
        call    skip_sp
        call    parse_hex
        jr      c, cg_demo
        ex      de, hl                  ; hl = target
        jr      cg_call
cg_demo:
        ld      hl, demo_g
cg_call:
        ld      bc, shell_loop
        push    bc
        jp      (hl)

demo_g:
        ld      hl, msg_demo
        call    C_PRTMSG
        ret

