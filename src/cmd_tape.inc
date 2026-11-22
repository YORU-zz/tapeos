; cmd_tape.inc - shell commands T/S/A (tape), common layer.
; (Was cmd_tape_x1.inc until M7 stage 5. The commands only
; ever spoke IOCS - C_TINF / C_TDAT / C_TWINF / C_TWDAT /
; C_TCTRL, plus the machine layer's tp_skipdat - so once
; the MZ targets grew real tape IOCS entries in stage 5,
; the file became common unchanged. The X1 binary is
; byte-identical for the move; the suite checks it.)
; ---------------------------------------------------------------
; T [name] - load a program from tape (M4: optional name).
; Without a name, loads the first program on the tape. With a
; name, scans info blocks from the start of the tape, skipping
; non-matching programs' data blocks, and loads the match.
; Data goes to the load address stored in the info block.
; ---------------------------------------------------------------
cmd_tape:
        call    skip_sp
        call    parse_tname             ; TNAMEBUF filled; carry = no name
        jr      c, ct_first
        ld      a, 1
        ld      (tp_named), a
        jr      ct_rew
ct_first:
        xor     a
        ld      (tp_named), a
ct_rew:
        ld      a, 4
        call    C_TCTRL                 ; REW
ct_scan:
        ld      hl, TAPEBUF
        call    C_TINF
        jp      c, err_exit             ; end of tape / read error
        ld      a, (tp_named)
        or      a
        jr      z, ct_load
        ld      hl, TAPEBUF + 1
        ld      de, TNAMEBUF
        ld      b, 13
ct_cmp:
        ld      a, (de)
        cp      (hl)
        jr      nz, ct_skip
        inc     hl
        inc     de
        djnz    ct_cmp
ct_load:
        ld      hl, (TAPEBUF + 014h)    ; load address
        ld      de, (TAPEBUF + 012h)    ; size
        call    C_TDAT
        jp      c, err_exit
        xor     a
        ld      (TAPEBUF + 011h), a     ; name becomes 0-terminated
        ld      hl, TAPEBUF + 1
        call    C_PRTMSG
        ld      hl, msg_tload
        call    C_PRTMSG
        jp      shell_loop
ct_skip:
        ld      de, (TAPEBUF + 012h)    ; size of the skipped program
        call    tp_skipdat
        jp      c, err_exit
        jr      ct_scan

; ---------------------------------------------------------------
; S addr len [name] - save a memory block to tape as a NEW tape
; (EJECT first, so the recording replaces the tape content).
; A addr len [name] - append a program at the current tape
; position (no EJECT). In the (patched) xmil core a REC that
; directly follows a REC..STOP segment appends to the tape,
; so A right after S/A appends; after any PLAY/REW/EJECT the
; chain is broken and A starts a fresh tape instead.
; The name defaults to TAPEOS and is stored space-padded in
; the info block, HuBASIC style.
; ---------------------------------------------------------------
cmd_save:
        xor     a
        ld      (sv_append), a
        jr      save_common
cmd_append:
        ld      a, 1
        ld      (sv_append), a
save_common:
        call    skip_sp
        call    parse_hex               ; de = addr
        jp      c, err_exit
        ld      (sv_addr), de
        call    skip_sp
        call    parse_hex               ; de = len
        jp      c, err_exit
        ld      (sv_len), de
        call    skip_sp
        call    parse_tname_d           ; name (or default) -> TNAMEBUF
        jp      c, err_exit
        ; build the info block at TAPEBUF
        ld      hl, TAPEBUF
        ld      b, 32
        xor     a
cs_clr:
        ld      (hl), a
        inc     hl
        djnz    cs_clr
        ld      a, 001h
        ld      (TAPEBUF), a            ; boot flag
        ld      hl, TNAMEBUF
        ld      de, TAPEBUF + 1
        ld      bc, 13
        ldir
        ld      hl, sv_ext
        ld      de, TAPEBUF + 0eh
        ld      bc, 3
        ldir
        ld      de, (sv_len)
        ld      (TAPEBUF + 012h), de    ; size
        ld      de, (sv_addr)
        ld      (TAPEBUF + 014h), de    ; load address
        ld      (TAPEBUF + 016h), de    ; exec address
        ld      a, (sv_append)
        or      a
        jr      nz, sc_rec              ; append: keep tape + position
        xor     a
        call    C_TCTRL                 ; EJECT (fresh-tape semantics)
sc_rec:
        ld      hl, TAPEBUF
        call    C_TWINF
        ld      hl, (sv_addr)
        ld      de, (sv_len)
        call    C_TWDAT
        ld      hl, msg_tsave
        call    C_PRTMSG
        jp      shell_loop

