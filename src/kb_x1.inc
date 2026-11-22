; kb_x1.inc - X1 raw keyboard access (sub-CPU protocol).
; kb_raw contract: -> a = ASCII code or 0, preserves bc.
; ---------------------------------------------------------------
; IOCS routines (reached only through the jump table)
; Register contracts are documented per routine; unless noted,
; bc / de / hl are preserved.
; ---------------------------------------------------------------

; ---------------------------------------------------------------
; Key input (M4): cooperative ring buffer.
;
; The X1 keyboard is level-based with no queue anywhere: a key
; pressed and released while the shell is busy printing (a
; scrolling dump keeps it busy for ~100 frames) used to be
; lost. The interrupt path exists in the core (sub-CPU command
; 0e4h sets a vector, key events then raise Z80_INTERRUPT),
; but a staged key transaction makes the core's subcpu_o drop
; command bytes, which races with the tape deck control that
; shares the same command channel (tp_ctrl's ei window) -- so
; TapeOS instead polls cooperatively: CONOUT runs kb_poll for
; every output character, and a press edge (previous poll 0,
; now a code) is pushed into the 16-byte KBBUF ring. The
; sub-CPU transaction is fully synchronous in this core, so a
; poll costs only a few dozen cycles. KEYIN/INKEY drain the
; ring first and fall back to direct polling.
; ---------------------------------------------------------------

; kb_raw: one raw sub-CPU key transaction (the IPL's KBHIT).
;   -> a = ASCII code of the key currently down, or 0.
;   preserves bc.
kb_raw:
        push    bc
        ld      bc, PPIB
ik_w1:
        in      a, (c)
        and     040h
        jr      nz, ik_w1               ; wait while write-busy
        ld      bc, SUBCPU
        ld      a, 0e6h                 ; e6 = key input command
        out     (c), a
        ld      bc, PPIB
ik_w2:
        in      a, (c)
        and     040h
        jr      nz, ik_w2
        call    ik_read                 ; function-key byte (discard)
        call    ik_read                 ; ASCII byte -> a
        pop     bc
        ret

ik_read:
        ld      bc, PPIB
ikr_w:
        in      a, (c)
        and     020h
        jr      nz, ikr_w               ; wait while not ready
        ld      bc, SUBCPU
        in      a, (c)
        ret

