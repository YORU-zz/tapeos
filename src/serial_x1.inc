; serial_x1.inc - X1 serial IOCS bodies, RTGET, BEEP v1, BREAK,
; the C (date) shell command and the reserved seats.
; (roadmap 26, debugger improvement / ep.56 job)
;
; This file exists because the K command and the long help
; text were demoted out of the X1 shell (see debugger-ep56.md):
; the room they freed is spent here.
;
;   IOCS 21 BEEP   - real body v1: PSG channel A, fixed note
;   IOCS 22 RTGET  - real body: sub-CPU calendar (0xED/0xEF)
;   IOCS 23 BREAK  - real body: flag set by kb_poll (see
;                    console.inc), read-and-clear here
;   IOCS 28 SIN    - NEW entry: blocking serial receive
;   IOCS 29 SOUT   - NEW entry: serial transmit
;   IOCS 30 SPOLL  - NEW entry: serial receive poll
;
; The serial bodies sit on the RS-232C board (roadmap 27):
; i8251 at I/O 0x0C00 (data) / 0x0C01 (control/status),
; 9600 bps x16 nominal. The register-level driver mirrors
; rs232c/src/usart.inc; the chip is initialised lazily on
; first use (ser_ready), with the same mode/command words.

SER_DATA        equ     00c00h          ; i8251 data
SER_CTL         equ     00c01h          ; i8251 control/status
PSG_LATCH       equ     01c00h          ; PSG register latch
PSG_DATA        equ     01b00h          ; PSG data write/read

; ---------------------------------------------------------------
; ser_init: one-time i8251 initialisation (mode reset, then
; mode 4eh = x16, 8 data bits, no parity, 1 stop; command
; 37h = TxEN + DTR + RxE + RTS). No registers clobbered
; except af / bc.
; ---------------------------------------------------------------
ser_init:
        ld      a, (ser_ready)
        or      a
        ret     nz
        push    bc
        ld      bc, SER_CTL
        xor     a
        out     (c), a                  ; dummy writes x3
        out     (c), a
        out     (c), a
        ld      a, 040h                 ; internal reset
        out     (c), a
        ld      a, 04eh                 ; mode: x16, 8N1
        out     (c), a
        ld      a, 037h                 ; TxEN|DTR|RxE|RTS
        out     (c), a
        pop     bc
        ld      a, 1
        ld      (ser_ready), a
        ret

; ---------------------------------------------------------------
; IOCS 28 SIN: block until one byte arrives.
;   -> a = byte, l = byte, carry clear. bc / de preserved,
;   h clobbered (l is the documented return mirror, so that
;   the Tiny Scheme (call) primitive can pick the byte up).
; ---------------------------------------------------------------
i_sin:
        push    bc
        call    ser_init
        ld      bc, SER_CTL
isin_w:
        in      a, (c)
        and     002h                    ; RxRDY
        jr      z, isin_w
        ld      bc, SER_DATA
        in      a, (c)
        ld      l, a
        ld      h, 0                    ; HL = the byte, so the
        pop     bc                      ; (call) primitive and
        or      a                       ; other HL readers see a
        ret                             ; clean value; carry clear

; ---------------------------------------------------------------
; IOCS 29 SOUT: transmit one byte.
;   a = byte in, a = byte out. bc / de / hl preserved.
; ---------------------------------------------------------------
i_sout:
        push    bc
        push    hl
        ld      l, a
        call    ser_init
        ld      bc, SER_CTL
isout_w:
        in      a, (c)
        and     001h                    ; TxRDY
        jr      z, isout_w
        ld      bc, SER_DATA
        ld      a, l
        out     (c), a
        pop     hl
        pop     bc
        ret

; ---------------------------------------------------------------
; IOCS 30 SPOLL: is a received byte waiting?
;   -> a = 1 if a byte is ready, 0 if not; carry clear.
;   bc / de / hl preserved.
; ---------------------------------------------------------------
i_spoll:
        push    bc
        call    ser_init
        ld      bc, SER_CTL
        in      a, (c)
        and     002h                    ; RxRDY
        ld      a, 0
        jr      z, ispo_dn
        inc     a
ispo_dn:
        pop     bc
        or      a                       ; carry clear
        ret

; ---------------------------------------------------------------
; IOCS 22 RTGET: read the sub-CPU calendar into the 6-byte
; buffer at hl: +0 year (two digits), +1 month 1..12,
; +2 day 1..31, +3 hour 0..23, +4 minute, +5 second, all
; binary. Carry set if the calendar answers nonsense.
; (The buffer contract was fixed by Tiny Scheme's (date) /
; (time), roadmap 32; this is the body they waited for.)
; Wire order of the sub-CPU replies (datcnt counts down):
; 0xED gives year, (month<<4)|weekday, day; 0xEF gives
; hour, minute, second - all BCD except the month nibble.
; ---------------------------------------------------------------
i_rtget:
        push    bc
        push    de
        push    hl
        ld      (rt_ptr), hl
        ld      hl, rt_buf
        ld      e, 0edh                 ; date get
        call    rt_xfer
        ld      hl, rt_buf + 3
        ld      e, 0efh                 ; time get
        call    rt_xfer
        ld      hl, (rt_ptr)
        ld      a, (rt_buf)             ; year (BCD)
        call    bcd2bin
        ld      (hl), a
        inc     hl
        ld      a, (rt_buf + 1)         ; month = high nibble
        and     0f0h
        rrca
        rrca
        rrca
        rrca
        ld      (hl), a
        cp      1
        jr      c, rt_bad
        cp      13
        jr      nc, rt_bad
        inc     hl
        ld      a, (rt_buf + 2)         ; day (BCD)
        call    bcd2bin
        ld      (hl), a
        inc     hl
        ld      a, (rt_buf + 3)         ; hour (BCD)
        call    bcd2bin
        ld      (hl), a
        inc     hl
        ld      a, (rt_buf + 4)         ; minute (BCD)
        call    bcd2bin
        ld      (hl), a
        inc     hl
        ld      a, (rt_buf + 5)         ; second (BCD)
        call    bcd2bin
        ld      (hl), a
        pop     hl
        pop     de
        pop     bc
        or      a                       ; carry clear
        ret
rt_bad:
        pop     hl
        pop     de
        pop     bc
        scf
        ret

; rt_xfer: sub-CPU transaction, e = command, hl = dest for
; the 3 reply bytes (wire order). Mirrors kb_raw's waits:
; PPIB bit6 = write busy, bit5 = no data pending.
rt_xfer:
        push    bc
        ld      bc, PPIB
rtx_w1:
        in      a, (c)
        and     040h
        jr      nz, rtx_w1
        ld      bc, SUBCPU
        ld      a, e
        out     (c), a
        ld      bc, PPIB
rtx_w0:
        in      a, (c)
        and     040h
        jr      nz, rtx_w0
        ld      b, 3
rtx_lp:
        push    bc
        ld      bc, PPIB
rtx_w2:
        in      a, (c)
        and     020h
        jr      nz, rtx_w2
        ld      bc, SUBCPU
        in      a, (c)
        ld      (hl), a
        inc     hl
        pop     bc
        djnz    rtx_lp
        pop     bc
        ret

; bcd2bin: a = packed BCD -> a = binary. bc preserved.
bcd2bin:
        push    bc
        ld      b, a
        and     00fh
        ld      c, a                    ; low digit
        ld      a, b
        and     0f0h
        rrca
        rrca                            ; a = high digit * 4
        ld      b, a
        rrca
        rrca                            ; a = high digit
        add     a, b                    ; * 5
        add     a, a                    ; * 10
        add     a, c
        pop     bc
        ret

; ---------------------------------------------------------------
; IOCS 21 BEEP v1: one fixed note on PSG channel A
; (about 880 Hz, about 0.15 s), then mute. The full sound
; driver is wish 17's later work; this body only proves the
; seat. PSG: latch = 0x1C00, data = 0x1B00. Register values
; stay readable after the call (period in regs 0/1, volume
; reg 8 back to 0), which is how the test program verifies
; the body ran. All registers preserved except af.
; ---------------------------------------------------------------
i_beep:
        push    bc
        push    de
        push    hl
        ld      a, 0                    ; reg 0: period low
        ld      e, 08eh                 ; 142 -> ~880 Hz @2 MHz
        call    psg_w
        ld      a, 1                    ; reg 1: period high
        ld      e, 0
        call    psg_w
        ld      a, 7                    ; reg 7: tone A only
        ld      e, 03eh
        call    psg_w
        ld      a, 8                    ; reg 8: volume A = 15
        ld      e, 00fh
        call    psg_w
        ld      hl, 9000                ; ~0.15 s at 4 MHz
beep_w:
        dec     hl
        ld      a, h
        or      l
        jr      nz, beep_w
        ld      a, 8                    ; mute
        ld      e, 0
        call    psg_w
        pop     hl
        pop     de
        pop     bc
        ret

; psg_w: a = register, e = value.
psg_w:
        push    bc
        ld      bc, PSG_LATCH
        out     (c), a
        ld      bc, PSG_DATA
        ld      a, e
        out     (c), a
        pop     bc
        ret

; ---------------------------------------------------------------
; IOCS 23 BREAK: report the break flag.
;   -> a = 1 if ESC was pressed during CONOUT output since
;   the last BREAK call, else 0; the flag is cleared either
;   way. Carry clear. The flag itself is set by kb_poll
;   (console.inc, X1 builds only) when it sees the ESC press
;   edge while a program prints - no interrupt conversion,
;   the CONOUT polling path carries it (brief addendum 4).
;   The key is NOT consumed: INKEY still sees it.
; ---------------------------------------------------------------
i_break:
        ld      a, (brk_flag)
        push    af
        xor     a
        ld      (brk_flag), a
        pop     af
        or      a                       ; carry clear
        ret

; ---------------------------------------------------------------
; C - print the date and time (shell command, X1 only).
; Dogfoods RTGET through the table, like everything else.
; Format: 20YY-MM-DD HH:MM:SS
; ---------------------------------------------------------------
cmd_date:
        ld      hl, date_buf
        call    C_RTGET
        jp      c, err_exit
        ld      hl, msg_cent
        call    C_PRTMSG
        ld      a, (date_buf)
        call    dec2
        ld      a, '-'
        call    C_CONOUT
        ld      a, (date_buf + 1)
        call    dec2
        ld      a, '-'
        call    C_CONOUT
        ld      a, (date_buf + 2)
        call    dec2
        ld      a, ' '
        call    C_CONOUT
        ld      a, (date_buf + 3)
        call    dec2
        ld      a, ':'
        call    C_CONOUT
        ld      a, (date_buf + 4)
        call    dec2
        ld      a, ':'
        call    C_CONOUT
        ld      a, (date_buf + 5)
        call    dec2
        call    C_CRLF
        jp      shell_loop

; dec2: print a as two decimal digits (zero padded).
dec2:
        push    bc
        ld      b, '0'
dc2_t:
        cp      10
        jr      c, dc2_u
        sub     10
        inc     b
        jr      dc2_t
dc2_u:
        push    af
        ld      a, b
        call    C_CONOUT
        pop     af
        add     a, '0'
        call    C_CONOUT
        pop     bc
        ret

msg_cent:
        db      "20", 0

; ---------------------------------------------------------------
; Reserved seats (space held in the shell body, addresses
; are part of the build, contents are placeholders).
;
; Cursor seat (wish 23): CONOUT calls cursor_hook once per
; character (X1 builds). The hook contract: preserve all
; registers, return quickly; a = the character about to be
; printed. The seat behind it is where the cursor drawing
; implementation will live.
; ---------------------------------------------------------------
cursor_hook:
        ret
        ds      125                     ; cursor seat (reserved)

; ---------------------------------------------------------------
; Resident Scheme seat (wishes 22/24): room for the entry
; stub of a shell-resident Tiny Scheme core. The gate
; convention of roadmap 61 (init/eval/call) is what will
; sit here; until then the seat is zeros.
; ---------------------------------------------------------------
scheme_seat:
        ds      128                     ; Scheme seat (reserved)

; ---------------------------------------------------------------
; Work area (inside the shell body, like the other X1 vars)
; ---------------------------------------------------------------
ser_ready:
        db      0
brk_flag:
        db      0
rt_ptr:
        dw      0
rt_buf:
        ds      6
date_buf:
        ds      6
