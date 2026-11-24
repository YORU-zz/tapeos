; console.inc - IOCS console routines (common layer):
; key ring buffer, INKEY/KEYIN, CONOUT, PRTMSG, CRLF,
; GETLIN, CLRSCR, LOCATE, HEXOUT, HEX16. Built on kb_raw
; and the machine display primitives.
; kb_pop: take one code from the ring.
;   -> a = code (carry clear), or carry set if empty.
;   destroys de, hl.
kb_pop:
        ld      a, (KBTAIL)
        ld      hl, KBHEAD
        cp      (hl)
        jr      z, kbp_empty
        ld      hl, KBBUF
        ld      e, a
        ld      d, 0
        add     hl, de
        ld      a, (hl)
        push    af
        ld      a, (KBTAIL)
        inc     a
        and     00fh
        ld      (KBTAIL), a
        pop     af
        or      a                       ; codes are never 0
        ret
kbp_empty:
        scf
        ret

; kb_poll: one cooperative scan, called from CONOUT for every
; output character. On a press edge the code is pushed into
; the ring (dropped if the ring is full). Preserves all
; registers.
kb_poll:
        push    af
        push    bc
        push    de
        push    hl
        call    kb_raw                  ; a = code now down (or 0)
        ld      hl, KBPREV
        ld      c, (hl)                 ; c = previous code
        ld      (hl), a                 ; prev = current
        or      a
        jr      z, kbpl_ret
        ld      b, a                    ; b = code
        ld      a, c
        or      a
        jr      nz, kbpl_ret            ; held, not an edge
        IF      DEFINED BRKFLAG
        ld      a, b
        cp      01bh                    ; ESC press edge: set the
        jr      nz, kbpl_nobrk          ; BREAK flag (roadmap 26,
        ld      a, 1                    ; addendum 4: CONOUT's
        ld      (brk_flag), a           ; polling carries it)
kbpl_nobrk:
        ENDIF
        ld      a, (KBHEAD)
        inc     a
        and     00fh
        ld      (kb_tmp), a             ; next head candidate
        ld      hl, KBTAIL
        cp      (hl)
        jr      z, kbpl_ret             ; ring full: drop newest
        ld      a, (KBHEAD)
        ld      hl, KBBUF
        ld      e, a
        ld      d, 0
        add     hl, de
        ld      (hl), b
        ld      a, (kb_tmp)
        ld      (KBHEAD), a
kbpl_ret:
        pop     hl
        pop     de
        pop     bc
        pop     af
        ret

; INKEY (0): non-blocking key read.
;   -> a = ASCII code, or 0 if no key is down.
;   Buffered codes (captured during output) come first.
i_inkey:
        push    de
        push    hl
        call    kb_pop
        jr      nc, ik_popd
        call    kb_raw
        ld      (KBPREV), a
ik_popd:
        pop     hl
        pop     de
        ret

; KEYIN (1): blocking key read, one code per physical press.
; Buffered codes are returned first; otherwise waits for the
; previous key to be released, then for the next press (like
; the IPL's IPLKEY). Every accepted code goes to KEYLOG.
;   -> a = ASCII code
i_keyin:
        push    bc
        push    de
        push    hl
        call    kb_pop
        jr      nc, ki_have
; Wait for a NEW press: poll the raw state and accept any code
; that differs from the last seen one (KBPREV). The old release-
; wait swallowed a key that was already down when KEYIN was
; entered - with scripted/fast typing whole characters of long
; lines vanished (found by EDAS line input in M6). A held key
; still yields exactly one code: while the code equals KBPREV
; the wait continues, and a release (0) re-arms the edge.
ki_wait:
        call    kb_raw                  ; a = code now down (or 0)
        ld      hl, KBPREV
        ld      c, (hl)                 ; c = last seen code
        ld      (hl), a                 ; last seen = current
        or      a
        jr      z, ki_wait              ; nothing down: keep waiting
        cp      c
        jr      z, ki_wait              ; same key held: keep waiting
ki_have:
        ld      c, a                    ; c = key code
        ld      a, (KEYLOGN)
        cp      64
        jr      nc, ki_done
        ld      hl, KEYLOG
        ld      e, a
        ld      d, 0
        add     hl, de
        ld      (hl), c
        inc     a
        ld      (KEYLOGN), a
ki_done:
        ld      a, c
        pop     hl
        pop     de
        pop     bc
        ret

; CONOUT (2): output one character at the cursor.
;   a = char. CR (0dh) homes x, LF (0ah) advances a line
;   (scrolling at the bottom), BS (08h) moves the cursor left,
;   other control codes and codes >= 07fh are ignored.
i_conout:
        push    bc
        push    de
        push    hl
        IF      DEFINED HAS_CURSOR_SEAT
        call    cursor_hook             ; wish 23 seat (roadmap 26):
        ENDIF                           ; a = char, must preserve all
        call    kb_poll                 ; M4: scan keys while printing
        cp      00dh
        jr      z, co_cr
        cp      00ah
        jr      z, co_lf
        cp      008h
        jr      z, co_bs
        cp      020h
        jr      c, co_out
        cp      07fh
        jr      nc, co_out
        push    af
        call    cur_offset
        pop     af
        call    wr_cell
        ld      a, (cur_x)
        inc     a
        cp      SCRW
        jr      c, co_setx
        xor     a
        ld      (cur_x), a
        jr      co_lf
co_setx:
        ld      (cur_x), a
        jr      co_out
co_cr:
        xor     a
        ld      (cur_x), a
        jr      co_out
co_bs:
        ld      a, (cur_x)
        or      a
        jr      z, co_out
        dec     a
        ld      (cur_x), a
        jr      co_out
co_lf:
        ld      a, (cur_y)
        inc     a
        cp      SCRH
        jr      c, co_sety
        call    scroll_up
        ld      a, SCRH - 1
co_sety:
        ld      (cur_y), a
co_out:
        pop     hl
        pop     de
        pop     bc
        ret

; PRTMSG (3): print a 0-terminated string.
;   hl = string (ends past the terminator)
i_prtmsg:
        ld      a, (hl)
        or      a
        ret     z
        inc     hl
        call    C_CONOUT
        jr      i_prtmsg

; CRLF (4): print CR + LF
i_crlf:
        push    af
        ld      a, 00dh
        call    C_CONOUT
        ld      a, 00ah
        call    C_CONOUT
        pop     af
        ret

; GETLIN (5): line input with echo and backspace editing.
;   hl = buffer, b = max chars -> a = length, buffer
;   0-terminated, hl = buffer on return.
i_getlin:
        push    bc
        push    de
        push    hl
        ld      d, h
        ld      e, l                    ; de = write pointer
        ld      c, 0                    ; c = length
gl_loop:
        call    C_KEYIN
        cp      00dh
        jr      z, gl_enter
        cp      008h
        jr      z, gl_bs
        cp      07fh
        jr      z, gl_bs
        cp      020h
        jr      c, gl_loop              ; ignore control codes
        cp      07fh
        jr      nc, gl_loop             ; ignore >= 07fh
        push    af
        ld      a, c
        cp      b
        jr      nc, gl_full             ; buffer full: drop char
        pop     af
        ld      (de), a
        inc     de
        inc     c
        call    C_CONOUT                ; echo
        jr      gl_loop
gl_full:
        pop     af
        jr      gl_loop
gl_bs:
        ld      a, c
        or      a
        jr      z, gl_loop
        dec     c
        dec     de
        ld      a, 008h
        call    C_CONOUT
        ld      a, ' '
        call    C_CONOUT
        ld      a, 008h
        call    C_CONOUT
        jr      gl_loop
gl_enter:
        xor     a
        ld      (de), a
        call    C_CRLF
        ld      a, c
        pop     hl
        pop     de
        pop     bc
        ret

; CLRSCR (6): clear the text screen, cursor to (0,0)
i_clrscr:
        push    bc
        push    de
        push    hl
        ld      hl, 0
cs_loop:
        push    hl
        ld      a, SPACE_CODE           ; raw display code of blank
        call    wr_chr2
        pop     hl
        push    hl
        ld      a, 07h
        call    wr_att2
        pop     hl
        inc     hl
        ld      a, h
        cp      08h                     ; 2048 cells done
        jr      nz, cs_loop
        xor     a
        ld      (cur_x), a
        ld      (cur_y), a
        pop     hl
        pop     de
        pop     bc
        ret

; LOCATE (7): set the cursor. h = x (0..39), l = y (0..24)
i_locate:
        ld      a, h
        ld      (cur_x), a
        ld      a, l
        ld      (cur_y), a
        ret

; HEXOUT (8): print a as two hex digits
i_hexout:
        push    af
        rrca
        rrca
        rrca
        rrca
        call    hex_digit
        pop     af
        call    hex_digit
        ret

hex_digit:
        and     00fh
        cp      00ah
        jr      nc, hd_alpha
        add     a, '0'
        jr      hd_out
hd_alpha:
        add     a, 'A' - 10
hd_out:
        jp      C_CONOUT

; HEX16 (9): print hl as four hex digits (hl preserved)
i_hex16:
        push    af
        ld      a, h
        call    C_HEXOUT
        ld      a, l
        call    C_HEXOUT
        pop     af
        ret

