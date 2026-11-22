; tape_x1.inc - X1 CMT tape IOCS entries 10-14.
; ---------------------------------------------------------------
; Tape (CMT) support for IOCS entries 10-14.
;
; The deck is driven through the sub-CPU (command 0e9h + arg),
; exactly like the compatible IPL's MT_CTRL. Tape data in is
; 8255 port B bit1; tape data out is 8255 port C bit0.
; Bit encoding (the format the IPL reads and HuBASIC writes):
; every bit cell starts with a rising edge; the level ~185us
; after the edge decides the bit (still high = 1, low = 0).
;   write: 1 = high ~380us + low ~280us
;          0 = high ~100us + low ~280us
; The TAPE4800 build variant (tape-4800) quarters all
; three phases and the reader's sample point (~46us),
; and must run against the core's XMIL_TAPE_BPS=4800;
; the default build below is byte-for-byte the classic
; code, and the IPL CMT boot path stays at this classic
; speed (the IPL ROM is not ours to change).
; Byte format: start bit (1) + 8 data bits, MSB first.
; Block format (IPL compatible):
;   info: leader 1s, 40 zeros, 41 ones, 32B info + 2B checksum
;   data: leader 1s, 20 zeros, 21 ones, payload
; ---------------------------------------------------------------

; tp_waitwr: wait until the sub-CPU accepts a command byte
; (8255 port B bit6 = 0). destroys af, bc
tp_waitwr:
        ld      bc, PPIB
tpw_w:
        in      a, (c)
        and     040h
        jr      nz, tpw_w
        ret

; tp_ctrl: send deck command a (0=eject 1=stop 2=play 3=ff
; 4=rew 0ah=rec) through the sub-CPU. This copies the
; compatible IPL's MT_CTRL exactly, including the ei and the
; double ready-wait after the 0e9h header: with a single wait
; the command byte can be written before the sub-CPU has
; consumed the header, and the deck never starts (verified by
; src/tpdiag.z80). destroys af, bc
tp_ctrl:
        push    af
        ei
        call    tp_waitwr
        ld      bc, SUBCPU
        ld      a, 0e9h
        out     (c), a
        call    tp_waitwr
        di
        call    tp_waitwr
        ld      bc, SUBCPU
        pop     af
        out     (c), a
        ret

; TCTRL (14): a = deck command
i_tctrl:
        push    bc
        call    tp_ctrl
        pop     bc
        ret

; tp_edge: wait for the tape signal to go low, then for the
; rising edge. bc = PPIB throughout; hl = timeout in polls
; (~17us each, 0 = ~1.1s). carry set on timeout.
; destroys af, hl
tp_edge:
tpe_wl:
        in      a, (c)
        and     002h
        jr      z, tpe_wr
        dec     hl
        ld      a, h
        or      l
        jr      nz, tpe_wl
        scf
        ret
tpe_wr:
        in      a, (c)
        and     002h
        jr      nz, tpe_ok
        dec     hl
        ld      a, h
        or      l
        jr      nz, tpe_wr
        scf
        ret
tpe_ok:
        or      a
        ret

; tp_rdbit: read one bit -> a = 0/1, carry = error.
; destroys af, bc, hl
tp_rdbit:
        IF      DEFINED TAPE4800
        jp      tp_rdbit48
        ELSE
        ld      bc, PPIB
        ld      hl, 0
        call    tp_edge
        ret     c
        ld      a, 40                   ; ~185us (16 clk per loop)
tprd_d:
        dec     a
        jr      nz, tprd_d
        in      a, (c)
        and     002h
        jr      nz, tprd_1
        xor     a
        ret
tprd_1:
        ld      a, 1
        ret
        ENDIF

; tp_rdbyte: read one byte -> a = byte, carry = error.
; destroys af, bc; preserves de, hl (tp_rdbit clobbers hl as
; its timeout counter, so hl is sheltered on the stack)
tp_rdbyte:
        push    hl
        call    tp_rdbit                ; start bit (must be 1)
        jr      c, tprb_x
        or      a
        jr      nz, tprb_go
        scf
        jr      tprb_x
tprb_go:
        xor     a
        ld      (tp_byte), a
        ld      b, 8
tprb_l:
        ld      a, (tp_byte)
        add     a, a
        ld      (tp_byte), a
        push    bc
        call    tp_rdbit
        pop     bc
        jr      c, tprb_x
        ld      c, a
        ld      a, (tp_byte)
        or      c
        ld      (tp_byte), a
        djnz    tprb_l
        ld      a, (tp_byte)
        and     a                       ; carry = 0
tprb_x:
        pop     hl
        ret

; tp_skip1s: skip a leader run of >=255 ones and any further
; ones up to (and including) the first 0. carry = error.
; destroys af, bc, de, hl
tp_skip1s:
        ld      d, 0ffh
tps1_l:
        call    tp_rdbit
        ret     c
        or      a
        jr      z, tp_skip1s
        dec     d
        jr      nz, tps1_l
tps1_r:
        call    tp_rdbit
        ret     c
        or      a
        jr      nz, tps1_r
        ret

; tp_skip0 / tp_skip1: skip exactly d more 0s / 1s.
; carry = error (wrong bit or read error).
; destroys af, bc, de, hl
tp_skip0:
        call    tp_rdbit
        ret     c
        or      a
        jr      nz, tps_err
        dec     d
        jr      nz, tp_skip0
        ret
tp_skip1:
        call    tp_rdbit
        ret     c
        or      a
        jr      z, tps_err
        dec     d
        jr      nz, tp_skip1
        ret
tps_err:
        scf
        ret

; tp_readblk: read de bytes to hl. carry = error.
; destroys af, bc, hl; de counts down to 0
tp_readblk:
        call    tp_rdbyte
        ret     c
        ld      (hl), a
        inc     hl
        dec     de
        ld      a, d
        or      e
        jr      nz, tp_readblk
        and     a
        ret

; tp_skipdat: skip one data block of de bytes (the tape is
; positioned right after an info block). Used by T <name> to
; pass over non-matching programs. carry = error.
; bc/de/hl preserved.
tp_skipdat:
        push    bc
        push    hl
        push    de
        ld      a, 2
        call    tp_ctrl                 ; PLAY
        call    tp_skip1s
        jr      c, tsd_err
        ld      d, 19
        call    tp_skip0
        jr      c, tsd_err
        ld      d, 21
        call    tp_skip1
        jr      c, tsd_err
        pop     de
        push    de
tsd_lp:
        ld      a, d
        or      e
        jr      z, tsd_done
        call    tp_rdbyte               ; read and discard
        jr      c, tsd_err
        dec     de
        jr      tsd_lp
tsd_done:
        ld      a, 1
        call    tp_ctrl                 ; STOP
        pop     de
        pop     hl
        pop     bc
        and     a
        ret
tsd_err:
        ld      a, 1
        call    tp_ctrl                 ; STOP
        pop     de
        pop     hl
        pop     bc
        scf
        ret

; TINF (10): read the tape info block (32 bytes + 2 checksum
; bytes) to hl. carry = error. bc/de/hl preserved.
i_tinf:
        push    bc
        push    de
        push    hl
        ld      a, 2
        call    tp_ctrl                 ; PLAY
        call    tp_skip1s
        jr      c, tpi_err
        ld      d, 39
        call    tp_skip0
        jr      c, tpi_err
        ld      d, 41
        call    tp_skip1
        jr      c, tpi_err
        pop     hl
        push    hl
        ld      de, 34
        call    tp_readblk
        jr      c, tpi_err
        ld      a, 1
        call    tp_ctrl                 ; STOP
        pop     hl
        pop     de
        pop     bc
        and     a
        ret
tpi_err:
        ld      a, 1
        call    tp_ctrl                 ; STOP
        pop     hl
        pop     de
        pop     bc
        scf
        ret

; TDAT (11): read the tape data block. hl = dest, de = count.
; carry = error. bc/de/hl preserved.
i_tdat:
        push    bc
        push    hl
        push    de
        ld      a, 2
        call    tp_ctrl                 ; PLAY
        call    tp_skip1s
        jr      c, tpd_err
        ld      d, 19
        call    tp_skip0
        jr      c, tpd_err
        ld      d, 21
        call    tp_skip1
        jr      c, tpd_err
        pop     de
        pop     hl
        push    hl
        push    de
        call    tp_readblk
        jr      c, tpd_err
        ld      a, 1
        call    tp_ctrl                 ; STOP
        pop     de
        pop     hl
        pop     bc
        and     a
        ret
tpd_err:
        ld      a, 1
        call    tp_ctrl                 ; STOP
        pop     de
        pop     hl
        pop     bc
        scf
        ret

; tp_shadow_init: capture 8255 port C into the shadow byte
; (bit0 cleared). Tape writes preserve all other bits; in
; particular bit5 never falls, so the one-shot GVRAM write
; latch is never armed by tape output (see README M3 notes).
tp_shadow_init:
        push    bc
        ld      bc, PPIC
        in      a, (c)
        and     0feh
        ld      (tp_shadow), a
        pop     bc
        ret

; tp_wset: set the tape-out line (port C bit0) to a (0/1),
; other bits from the shadow. destroys af, bc
tp_wset:
        push    bc
        ld      c, a
        ld      a, (tp_shadow)
        and     0feh
        or      c
        ld      bc, PPIC
        out     (c), a
        pop     bc
        ret

; tp_delay: busy wait, a = count (16 clocks each = ~4us).
; destroys af
tp_delay:
        dec     a
        jr      nz, tp_delay
        ret

; tp_wrbit: write one bit (a = 0/1) as a pulse:
; high (bit ? ~380us : ~100us), then low ~280us.
; The TAPE4800 build uses tp_wrbit48 instead (inline line
; drive; see there). destroys af; preserves bc, de, hl
tp_wrbit:
        IF      DEFINED TAPE4800
        jp      tp_wrbit48
        ELSE
        or      a
        ld      a, 25
        jr      z, tpw_h
        ld      a, 95
tpw_h:
        push    af
        ld      a, 1
        call    tp_wset
        pop     af
        call    tp_delay
        xor     a
        call    tp_wset
        ld      a, 70
        call    tp_delay
        ret
        ENDIF

; tp_wrbyte: write a (start bit + 8 bits, MSB first).
; destroys af; preserves bc, de, hl
tp_wrbyte:
        push    bc
        ld      c, a
        ld      a, 1
        call    tp_wrbit                ; start bit
        ld      b, 8
tpwb_l:
        sla     c
        ld      a, 0
        jr      nc, tpwb_s
        ld      a, 1
tpwb_s:
        call    tp_wrbit
        djnz    tpwb_l
        pop     bc
        ret

; tp_wr_run: write hl copies of bit c.
; destroys af, hl; preserves bc, de
tp_wr_run:
        ld      a, c
        push    bc
        push    hl
        call    tp_wrbit
        pop     hl
        pop     bc
        dec     hl
        ld      a, h
        or      l
        jr      nz, tp_wr_run
        ret

; tp_wrblk: write b bytes from hl.
; destroys af, bc, hl; preserves de
tp_wrblk:
        ld      a, (hl)
        inc     hl
        push    hl
        push    bc
        call    tp_wrbyte
        pop     bc
        pop     hl
        djnz    tp_wrblk
        ret

; tp_wrblk16: write de bytes from hl.
; destroys af, de, hl; preserves bc
tp_wrblk16:
        ld      a, (hl)
        inc     hl
        push    hl
        push    de
        call    tp_wrbyte
        pop     de
        pop     hl
        dec     de
        ld      a, d
        or      e
        jr      nz, tp_wrblk16
        ret

; tp_wr_leader: write a block leader. a = zeros count
; (40 for info, 20 for data); the ones count is zeros + 1.
; destroys af, bc, de, hl
tp_wr_leader:
        ld      (tp_zcnt), a
        xor     a
        call    tp_wset                 ; line low: known baseline
        ld      a, 250
        call    tp_delay                ; ~1ms settle
        ld      c, 1
        ld      hl, 800
        call    tp_wr_run
        ld      a, (tp_zcnt)
        ld      l, a
        ld      h, 0
        ld      c, 0
        call    tp_wr_run
        ld      a, (tp_zcnt)
        inc     a
        ld      l, a
        ld      h, 0
        ld      c, 1
        call    tp_wr_run
        ret

; TWINF (12): write the tape info block. hl = 32-byte info
; block; the 2-byte checksum (byte sum, LE) is appended by
; this routine. bc/de/hl preserved.
i_twinf:
        push    bc
        push    de
        push    hl
        ld      de, 0                   ; checksum = byte sum
        ld      b, 32
        push    hl
tpwi_s:
        ld      a, (hl)
        inc     hl
        add     a, e
        ld      e, a
        jr      nc, tpwi_nc
        inc     d
tpwi_nc:
        djnz    tpwi_s
        pop     hl
        ld      a, e
        ld      (tp_cksum), a
        ld      a, d
        ld      (tp_cksum + 1), a
        call    tp_shadow_init
        ld      a, 0ah
        call    tp_ctrl                 ; REC
        ld      a, 40
        call    tp_wr_leader
        pop     hl
        push    hl
        ld      b, 32
        call    tp_wrblk
        ld      hl, tp_cksum
        ld      b, 2
        call    tp_wrblk
        ld      a, 1
        call    tp_ctrl                 ; STOP
        pop     hl
        pop     de
        pop     bc
        and     a
        ret

; TWDAT (13): write the tape data block. hl = source,
; de = byte count. bc/de/hl preserved.
i_twdat:
        push    bc
        push    hl
        push    de
        call    tp_shadow_init
        ld      a, 0ah
        call    tp_ctrl                 ; REC
        ld      a, 20
        call    tp_wr_leader
        pop     de
        pop     hl
        push    hl
        push    de
        call    tp_wrblk16
        ld      a, 1
        call    tp_ctrl                 ; STOP
        pop     de
        pop     hl
        pop     bc
        and     a
        ret


; ---------------------------------------------------------------
; TAPE4800-only reader/writer (the default build emits none of
; this; see tape-4800.md for the measurements behind the design).
;
; Reader: at the first 4800 format (the classic timings
; quartered) the classic sample-after-delay reader merged
; "0" cells pairwise. Counting readers were tried next -
; rise-to-rise period, high-phase width against a leader-
; calibrated threshold, and a streaming engine - and all
; of them failed on this core: between polls the counts
; drift, grow and slip at ~200us cells, though a bare
; diagnostic loop always saw a clean waveform. The wire
; format was therefore changed so the classes differ in
; BOTH phases ("1" = 130us high + 70us low, "0" = 35us
; high + 130us low; the core player timings match), and
; the final reader (tp_rdbit48, below) goes back to the
; classic structure - sync to the edge, fixed delay,
; sample once - which needs no counting at all.
;
; Writer: the classic tp_wrbit drives the line through
; tp_wset calls whose overhead (~40us) exceeds the whole
; 25us "0" high phase at 4800, so every bit decoded as "1"
; at the core recorder's 46us sample point. tp_wrbit48
; drives the line inline; the delay counts below include
; the measured inline overhead (~17us rise-to-fall).
; ---------------------------------------------------------------
        IF      DEFINED TAPE4800

; tp_rdbit48: read one bit in the 4800 width format by the
; classic reader's method: sync to the rising edge, wait a
; fixed delay, sample once.  A "1" (high 130us) is still high
; at the sample point (~65us after the rise); a "0" (high
; 35us) is already low.  Counting readers (period, width and
; streaming variants) all failed on this core -- their poll
; counts drift, grow and slip at 200us cells -- while edge +
; delay + single sample is the structure the classic reader
; proves on the same core at 1200bps.
tp_rdbit48:
        ld      bc, PPIB
        ld      hl, 0                   ; timeout budget (wraps)
tw48_f: in      a, (c)                  ; wait for the fall
        and     002h
        jr      z, tw48_r
        dec     hl
        ld      a, h
        or      l
        jr      nz, tw48_f
        scf
        ret
tw48_r: in      a, (c)                  ; wait for the rise
        and     002h
        jr      nz, tw48_s
        dec     hl
        ld      a, h
        or      l
        jr      nz, tw48_r
        scf
        ret
tw48_s: ld      a, 14                   ; ~65us from the rise
tw48_d: dec     a
        jr      nz, tw48_d
        in      a, (c)
        and     002h
        jr      nz, tw48_1
        xor     a
        ret
tw48_1: ld      a, 1
        ret

; tp_wrbit48: write one bit (a = 0/1) in the 4800 width
; format with the line driven inline. high ~130us ("1",
; 28 counts) / ~37us ("0", 5 counts: under the recorder's
; 46us sample point, above the reader's "0" width class),
; low short (2 counts; neither consumer measures the low).
; Counts are tp_delay units (~4us) plus the measured
; inline overhead (~17us rise-to-fall).
; destroys af; preserves bc, de, hl
tp_wrbit48:
        push    bc                      ; bc is live in tp_wrbyte /
        push    de                      ; tp_wrblk loop state
        or      a
        ld      a, 5
        ld      e, 2                    ; "0" low count
        jr      z, tpw48_h
        ld      a, 28
        ld      e, 6                    ; "1" low count (~70us
tpw48_h:                                ; with overhead, matching
        push    af                      ; the player's low phase:
        ld      a, (tp_shadow)          ; the reader's loop-back
        or      001h                    ; must fit inside it)
        ld      bc, PPIC
        out     (c), a
        pop     af
        call    tp_delay
        ld      a, (tp_shadow)
        and     0feh
        ld      bc, PPIC
        out     (c), a
        ld      a, e
        call    tp_delay
        pop     de
        pop     bc
        ret

        ENDIF
