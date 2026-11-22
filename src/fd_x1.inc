; fd_x1.inc - X1 FD (15-16) and RAMDISK/EMM (24-26) IOCS.
; ---------------------------------------------------------------
; Floppy disk support for IOCS entries 15-16 (M4).
;
; The FDC is a uPD765-style controller at ports 0ff8h-0ffch.
; The compatible IPL disconnects itself at boot, so its disk
; READ entry (021ah) cannot be called; this re-implements the
; same programmed-I/O protocol the IPL uses (LOADFILE /
; FDC_SEEK / FDC_READ in the IPL source): select drive+side
; with the motor on, SEEK (data register = track, command
; 01eh), set the sector register, issue READ (080h) or WRITE
; (0a0h), then move bytes one at a time gated on the DRQ bit.
;
; One difference from the IPL: completion is detected by
; counting the 256 bytes of the sector, not by watching BUSY
; drop -- in this core the status BUSY bit can be held up by
; the seek-sound timer (fddmtr curevent feeds getstat), while
; the error bits in the final status are exact. Polling is
; also kept well under the core's lost-data threshold (the
; data register is touched within a few status reads).
; ---------------------------------------------------------------

; DREAD (15): read one 256-byte sector.
;   de = record number, hl = buffer. carry = error.
;   bc/de/hl preserved.
i_dread:
        push    bc
        push    de
        push    hl
        ld      (fd_rec), de
        ld      (fd_buf), hl
        ld      a, 080h
        ld      (fd_cmd), a
        call    fd_xfer
        pop     hl
        pop     de
        pop     bc
        ret

; DWRITE (16): write one 256-byte sector.
;   de = record number, hl = buffer. carry = error.
;   bc/de/hl preserved.
i_dwrite:
        push    bc
        push    de
        push    hl
        ld      (fd_rec), de
        ld      (fd_buf), hl
        ld      a, 0a0h
        ld      (fd_cmd), a
        call    fd_xfer
        pop     hl
        pop     de
        pop     bc
        ret

; ---------------------------------------------------------------
; RAMDISK on EMM, IOCS entries 24-26 (M5).
;
; Storage is the X1 EMM (expansion memory) at ports
; 0d00h-0d03h, with the interface the compatible IPL uses
; for EMM boot (address L/M/H registers + a data port that
; auto-increments the address). The stock xmil core left
; those ports on dummy handlers; the M5 core patch
; implements the device (1 MB backing store, zeroed on
; reset - see README "EMM (core patch, M5)").
;
; One block is 256 bytes at EMM byte address block * 256.
; RINIT sets the size (dynamic reconfiguration): at most
; RDMAX = 256 blocks = 64 KB, the in-fiction IOCS spec
; shared by the X1/MZ common monitor (ep.19). The EMM
; device itself is larger; TapeOS caps the RAMDISK at the
; spec maximum on purpose.
;
; Byte counting cannot use the b register: with bc = EMM_D,
; b is the port's high byte (0dh). The transfer loops use
; plain in/out with a memory counter (rd_cnt). Block I/O
; instructions are NOT used: in this core (as on a real
; Z80) OUTI decrements b BEFORE the write, so the byte
; would go to port 0c03h instead of 0d03h and be dropped
; (INI reads before decrementing, which is why the IPL's
; ini-based EMM read works). This cost a debugging round
; in M5: RWRITE silently dropped every byte while RREAD
; and the RINIT probe (plain in/out) worked fine.
; ---------------------------------------------------------------

; emm_setblk: point the EMM address at block de (de * 256).
;   destroys af, bc.
emm_setblk:
        ld      bc, EMM_L
        xor     a
        out     (c), a                  ; address low = 0
        inc     c
        out     (c), e                  ; mid = block low byte
        inc     c
        out     (c), d                  ; high = block high byte
        ret

; rd_check: carry set if block de is not usable (RAMDISK
; unconfigured, or de >= configured blocks). destroys hl.
rd_check:
        ld      hl, (rd_blocks)
        ld      a, h
        or      l
        jr      z, rdc_bad              ; unconfigured
        or      a
        sbc     hl, de                  ; blocks - block
        jr      z, rdc_bad              ; block == blocks
        ret                             ; carry = (blocks < block)
rdc_bad:
        scf
        ret

; RREAD (24): read one 256-byte block from the RAMDISK.
;   de = block, hl = buffer. carry = error. bc/de/hl kept.
i_rread:
        push    bc
        push    de
        push    hl
        ld      (rd_blk), de
        ld      (rd_buf), hl
        call    rd_check
        jr      c, rr_exit
        ld      de, (rd_blk)
        call    emm_setblk
        ld      hl, (rd_buf)
        ld      bc, EMM_D
        xor     a
        ld      (rd_cnt), a
rr_lp:
        in      a, (c)                  ; (hl) <- EMM
        ld      (hl), a
        inc     hl
        ld      a, (rd_cnt)
        inc     a
        ld      (rd_cnt), a
        jr      nz, rr_lp               ; 256 bytes (wraps to 0)
        or      a                       ; carry clear
rr_exit:
        pop     hl
        pop     de
        pop     bc
        ret

; RWRITE (25): write one 256-byte block to the RAMDISK.
;   de = block, hl = buffer. carry = error. bc/de/hl kept.
i_rwrite:
        push    bc
        push    de
        push    hl
        ld      (rd_blk), de
        ld      (rd_buf), hl
        call    rd_check
        jr      c, rw_exit
        ld      de, (rd_blk)
        call    emm_setblk
        ld      hl, (rd_buf)
        ld      bc, EMM_D
        xor     a
        ld      (rd_cnt), a
rw_lp:
        ld      a, (hl)                 ; EMM <- (hl)
        inc     hl
        out     (c), a
        ld      a, (rd_cnt)
        inc     a
        ld      (rd_cnt), a
        jr      nz, rw_lp               ; 256 bytes (wraps to 0)
        or      a                       ; carry clear
rw_exit:
        pop     hl
        pop     de
        pop     bc
        ret

; RINIT (26): configure the RAMDISK size (dynamic
; reconfiguration, ep.47). hl = requested blocks (1..256,
; clamped to RDMAX); hl = 0 reports the current size
; without changing it.
;   -> hl = configured blocks. carry = error (no EMM).
;   bc/de preserved; hl is the documented exception to the
;   table's register convention.
; Configuring probes the EMM with a test pattern at block 0
; (those bytes are overwritten) so that a core without the
; M5 EMM patch fails cleanly instead of silently losing
; data. The probe is also why RINIT belongs to an init:
; it is not a read-only operation.
i_rinit:
        push    bc
        push    de
        ld      a, h
        or      l
        jr      z, ri2_query            ; hl = 0: report only
        ld      de, RDMAX + 1
        or      a
        sbc     hl, de                  ; hl = requested - 257
        jr      nc, ri2_clamp           ; requested > RDMAX
        add     hl, de                  ; restore requested
        jr      ri2_probe
ri2_clamp:
        ld      hl, RDMAX
ri2_probe:
        ld      (rd_req), hl
        ld      de, 0
        call    emm_setblk
        ld      bc, EMM_D
        ld      a, 0a5h
        out     (c), a
        ld      a, 05ah
        out     (c), a
        ld      a, 0c3h
        out     (c), a
        ld      a, 03ch
        out     (c), a
        ld      de, 0
        call    emm_setblk
        ld      bc, EMM_D
        in      a, (c)
        cp      0a5h
        jr      nz, ri2_fail
        in      a, (c)
        cp      05ah
        jr      nz, ri2_fail
        in      a, (c)
        cp      0c3h
        jr      nz, ri2_fail
        in      a, (c)
        cp      03ch
        jr      nz, ri2_fail
        ld      hl, (rd_req)
        ld      (rd_blocks), hl
ri2_query:
        ld      hl, (rd_blocks)
        pop     de
        pop     bc
        or      a                       ; carry clear
        ret
ri2_fail:
        pop     de
        pop     bc
        scf
        ret

; fd_xfer: transfer the sector named by (fd_rec) to/from
; (fd_buf) using command (fd_cmd). carry set on error.
; destroys af, bc, de, hl.
fd_xfer:
        ; --- split the record: ((track*2 + side) << 4) | sec0 ---
        ld      hl, (fd_rec)
        ld      a, l
        and     00fh
        inc     a
        ld      (fd_sec), a             ; sector (1-origin)
        ld      hl, (fd_rec)
        srl     h
        rr      l
        srl     h
        rr      l
        srl     h
        rr      l
        srl     h
        rr      l                       ; hl = record >> 4
        ld      a, l
        and     001h
        ld      (fd_side), a            ; side = bit 0
        srl     l
        ld      a, l
        ld      (fd_trk), a             ; track = record >> 5
        ; --- select drive 0 + side, motor on ---
        ld      a, (fd_side)
        add     a, a
        add     a, a
        add     a, a
        add     a, a                    ; side << 4
        or      080h                    ; motor on
        ld      bc, FDNO
        out     (c), a
        ; --- seek ---
        ld      bc, FDDR
        ld      a, (fd_trk)
        out     (c), a                  ; data register = track
        ld      bc, FDCR
        ld      a, 01eh                 ; seek command
        out     (c), a
        call    fd_wait
        jr      c, fd_fail
        in      a, (c)                  ; bc = FDCR (fd_wait keeps it)
        and     098h                    ; NOTREADY / SEEKERR / CRCERR
        jr      nz, fd_fail
        ; --- sector register, then the transfer command ---
        ld      bc, FDSR
        ld      a, (fd_sec)
        out     (c), a
        call    fd_wait
        jr      c, fd_fail
        ld      hl, (fd_buf)
        xor     a
        ld      (fd_bcnt), a            ; bytes moved (wraps at 256)
        ld      bc, FDCR
        ld      a, (fd_cmd)
        out     (c), a
fdx_byte:
        ld      de, 0                   ; DRQ wait timeout
fdx_poll:
        dec     de
        ld      a, d
        or      e
        jr      z, fd_fail
        in      a, (c)                  ; status (bc = FDCR)
        rrca
        jr      nc, fd_fail             ; BUSY gone mid-sector
        rrca
        jr      nc, fdx_poll            ; DRQ not set yet
        ld      c, 0fbh                 ; data register (bc = 0ffbh)
        ld      a, (fd_cmd)
        cp      0a0h
        jr      z, fdx_wr
        in      a, (c)                  ; read: DR -> (hl)
        ld      (hl), a
        inc     hl
        jr      fdx_next
fdx_wr:
        ld      a, (hl)                 ; write: (hl) -> DR
        inc     hl
        out     (c), a
fdx_next:
        ld      c, 0f8h                 ; command/status register
        ld      a, (fd_bcnt)
        inc     a
        ld      (fd_bcnt), a
        jr      nz, fdx_byte            ; 256 bytes -> wraps to 0
        ; --- final status: mask the error bits ---
        in      a, (c)
        ld      e, a
        ld      a, (fd_cmd)
        cp      0a0h
        ld      a, 09ch                 ; read: NRDY/LOST/CRC/RNF
        jr      nz, fdx_m2
        ld      a, 0fch                 ; write: + WRITEP/WFAULT
fdx_m2:
        and     e
        jr      nz, fd_fail
        xor     a                       ; motor off, carry = 0
        ld      bc, FDNO
        out     (c), a
        ret
fd_fail:
        xor     a                       ; motor off
        ld      bc, FDNO
        out     (c), a
        scf
        ret

; fd_wait: wait until (status & 081h) == 0 (bounded).
; bc = FDCR on exit. carry set on timeout.
fd_wait:
        ld      bc, FDCR
        ld      de, 0
fdw_lp:
        dec     de
        ld      a, d
        or      e
        jr      z, fdw_to
        in      a, (c)
        and     081h
        jr      nz, fdw_lp
        or      a
        ret
fdw_to:
        scf
        ret

