; shell.inc - monitor shell main loop and dispatch
; (common layer. Tape commands are X1-only; the TOSD file
; commands F/W/R/U/I are common since M7 stage 4; K exists
; only on targets that define HAS_KANJI).
; ---------------------------------------------------------------
; Shell main loop
; ---------------------------------------------------------------
shell_loop:
        ld      hl, msg_prompt
        call    C_PRTMSG
        ld      hl, LINEBUF
        ld      b, SCRW
        call    C_GETLIN                ; a = length

        ; copy the raw line to LASTCMD (harness evidence)
        ld      hl, LINEBUF
        ld      de, LASTCMD
        ld      b, 32
cp_lp:
        ld      a, (hl)
        ld      (de), a
        or      a
        jr      z, cp_done
        inc     hl
        inc     de
        djnz    cp_lp
cp_done:

        ld      hl, LINEBUF
        call    skip_sp
        ld      a, (hl)
        or      a
        jr      z, shell_loop           ; empty line
        inc     hl                      ; hl -> args
        call    upcase
        cp      'H'
        jp      z, cmd_help
        cp      'D'
        jp      z, cmd_dump
        cp      'E'
        jp      z, cmd_exam
        cp      'M'
        jp      z, cmd_mod
        cp      'G'
        jp      z, cmd_go
        cp      'J'
        jp      z, cmd_go
        cp      'V'
        jp      z, cmd_ver
        IF      DEFINED DBGSTUB
        cp      'X'
        jp      z, dbg_main
        ENDIF
        IF      DEFINED EXTIOCS
        cp      'N'
        jp      z, cmd_mem
        ENDIF
        IF      DEFINED TARGET_X1
        cp      'T'
        jp      z, cmd_tape
        cp      'S'
        jp      z, cmd_save
        cp      'A'
        jp      z, cmd_append
        ENDIF
        IF      DEFINED HAS_TAPE
        cp      'T'
        jp      z, cmd_tape
        cp      'S'
        jp      z, cmd_save
        cp      'A'
        jp      z, cmd_append
        ENDIF
        cp      'F'
        jp      z, cmd_fdlist
        cp      'W'
        jp      z, cmd_fdwrite
        cp      'R'
        jp      z, cmd_fdread
        cp      'U'
        jp      z, cmd_usedrv
        cp      'I'
        jp      z, cmd_raminit
        IF      DEFINED HAS_KANJI
        cp      'K'
        jp      z, cmd_kanji
        ENDIF
        IF      DEFINED HAS_DATE
        cp      'C'
        jp      z, cmd_date
        ENDIF
        ld      hl, msg_err
        call    C_PRTMSG
        jp      shell_loop

cmd_help:
        ld      hl, msg_help
        call    C_PRTMSG
        jp      shell_loop

cmd_ver:
        ld      hl, msg_ver
        call    C_PRTMSG
        jp      shell_loop

err_exit:
        ld      hl, msg_err
        call    C_PRTMSG
        jp      shell_loop

