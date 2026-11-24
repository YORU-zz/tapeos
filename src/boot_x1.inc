; boot_x1.inc - X1 entry point (loaded by the compatible
; IPL from FDD at LOADADDR; the IPL is already disconnected).
        org     LOADADDR

start:
        di
        ld      sp, STACKTOP

        ; --- signature for the verification harness ---
        ld      hl, sig_text
        ld      de, SIGADDR
        ld      bc, 15
        ldir

        ; --- clear evidence areas ---
        ld      hl, LASTCMD
        ld      b, 32
        xor     a
clr1:
        ld      (hl), a
        inc     hl
        djnz    clr1
        ld      (KEYLOGN), a
        ld      (KBHEAD), a             ; key ring buffer empty (M4)
        ld      (KBTAIL), a
        ld      (KBPREV), a

        ; --- build the IOCS jump table at IOCS_BASE ---
        ld      hl, iocs_template
        ld      de, IOCS_BASE
        ld      bc, 31 * 3              ; entries 0..30 (roadmap 26)
        ldir

        IF      DEFINED DBGSTUB
        ; --- resident debug stub: page-0 vectors + mailbox ---
        ; Page 0 is RAM once the IPL has disconnected.
        ; 0030h: breakpoint (RST 30H), 0038h: IM 1 key
        ; interrupt, 0066h: NMI (defensive). The mailbox
        ; addresses are literals here; they must match the
        ; DBG_MB_* equates in dbgstub.inc.
        ld      a, 0c3h                 ; JP
        ld      (00030h), a
        ld      hl, dbg_bp
        ld      (00031h), hl
        ld      (00038h), a
        ld      hl, dbg_int
        ld      (00039h), hl
        ld      (00066h), a
        ld      hl, dbg_int
        ld      (00067h), hl
        xor     a
        ld      (0ea00h), a             ; MB_FLAG = idle
        ld      (0ea01h), a             ; MB_STATE = shell
        ld      (0ea02h), a             ; MB_OLEN = 0
        ld      a, 'D'
        ld      (0ea04h), a
        ld      a, 'B'
        ld      (0ea05h), a
        ld      a, 'S'
        ld      (0ea06h), a
        ld      a, 'T'
        ld      (0ea07h), a
        ENDIF

        ; --- screen + banner, through the table from the start ---
        call    clear_gram              ; M3: erase IPL leftovers in GVRAM
        call    C_CLRSCR

        ld      a, 02h                  ; red
        ld      (cur_attr), a
        ld      hl, msg_title
        call    C_PRTMSG

        ld      a, 07h                  ; white
        ld      (cur_attr), a
        ld      hl, msg_sub
        call    C_PRTMSG

        ; fall into the shell loop

