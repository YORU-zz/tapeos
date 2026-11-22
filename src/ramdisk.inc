; ramdisk.inc - RAMDISK IOCS entries 24-26 on a plain-RAM
; backing store (common layer, M7 stage 4; two-segment
; backing since stage 5).
;
; The logic is the X1/EMM version's (fd_x1.inc), shared per
; the common-monitor design (mz-survey.md 4.3/4.4): one block
; is 256 bytes, RINIT configures the size dynamically
; (ep.47) up to RDMAX = 256 blocks = 64 KB, the in-fiction
; IOCS spec of the X1/MZ common monitor (ep.19). What differs
; per machine is only where the backing store lives. Stage 4
; used a single run at 08000h (64 blocks = 16 KB); stage 5
; adds the free RAM below the EDAS load address as a first
; segment, so the backing is two segments:
;   RDBASE  first byte of segment 1 (machine const)
;   RDSEG1  blocks in segment 1 (machine const)
;   RDBASE2 first byte of segment 2 (machine const)
;   RDCAP   total blocks that fit (RDSEG1 + segment 2)
; RINIT clamps the request to min(RDMAX, RDCAP) and reports
; the granted count, so the 64 KB interface stands even on
; machines whose free RAM holds less. The regions were
; chosen per machine so neither segment collides with the
; OS body, the EDAS work area (04000h-07fffh), the IOCS
; table / buffers page (0c000h up) or the FD layer - see
; mz7-stage5.md section 2 for the memory-map audit.
;
; The X1 target does NOT include this file: its backing is
; the EMM device, driven by the port code in fd_x1.inc (the
; user-visible semantics - clamp, query, probe, register
; contract - are identical).
; ---------------------------------------------------------------

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

; rd_addr: hl = backing address of block (rd_blk).
; block < 256 always (rd_check bounds it by rd_blocks,
; which RINIT clamps to RDCAP <= RDMAX = 256). Blocks
; 0..RDSEG1-1 live in segment 1 at RDBASE, the rest in
; segment 2 at RDBASE2.
rd_addr:
        ld      a, (rd_blk)             ; low byte = block
        cp      RDSEG1
        jr      c, rda_seg1
        sub     RDSEG1                  ; block within segment 2
        ld      l, 0
        ld      h, a                    ; hl = block * 256
        ld      de, RDBASE2
        add     hl, de
        ret
rda_seg1:
        ld      l, 0
        ld      h, a
        ld      de, RDBASE
        add     hl, de
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
        call    rd_addr                 ; hl = backing
        ld      de, (rd_buf)
        ld      bc, 256
        ldir
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
        call    rd_addr                 ; hl = backing
        ex      de, hl                  ; de = backing
        ld      hl, (rd_buf)
        ld      bc, 256
        ldir
        or      a                       ; carry clear
rw_exit:
        pop     hl
        pop     de
        pop     bc
        ret

; RINIT (26): configure the RAMDISK size (dynamic
; reconfiguration, ep.47). hl = requested blocks (1..256,
; clamped to RDMAX and to the machine's RDCAP); hl = 0
; reports the current size without changing it.
;   -> hl = configured blocks. carry = error (probe failed).
;   bc/de preserved; hl is the documented exception to the
;   table's register convention.
; Configuring probes the backing with a test pattern at
; block 0 (those bytes are overwritten), mirroring the X1
; EMM probe: an init is not a read-only operation. Stage 5
; probes the first block of segment 2 as well, so a wrong
; segment map is caught at init time, not at first use.
i_rinit:
        push    bc
        push    de
        ld      a, h
        or      l
        jr      z, ri_query             ; hl = 0: report only
        ld      de, RDMAX + 1
        or      a
        sbc     hl, de                  ; hl = requested - 257
        jr      nc, ri_clamp1           ; requested > RDMAX
        add     hl, de                  ; restore requested
        jr      ri_cap
ri_clamp1:
        ld      hl, RDMAX
ri_cap:
        ld      de, RDCAP + 1
        or      a
        sbc     hl, de                  ; hl = requested - (cap+1)
        jr      nc, ri_clamp2           ; requested > RDCAP
        add     hl, de                  ; restore requested
        jr      ri_probe
ri_clamp2:
        ld      hl, RDCAP
ri_probe:
        ld      (rd_req), hl
        ld      hl, RDBASE              ; probe block 0 (segment 1)
        call    rd_probe
        jr      c, ri_fail
        ld      hl, RDBASE2             ; probe segment 2 start
        call    rd_probe
        jr      c, ri_fail
        ld      hl, (rd_req)
        ld      (rd_blocks), hl
ri_query:
        ld      hl, (rd_blocks)
        pop     de
        pop     bc
        or      a                       ; carry clear
        ret
ri_fail:
        pop     de
        pop     bc
        scf
        ret

; rd_probe: write the A5 5A C3 3C test pattern at hl and
; read it back. carry set on mismatch. hl preserved.
rd_probe:
        push    hl
        ld      (hl), 0a5h
        inc     hl
        ld      (hl), 05ah
        inc     hl
        ld      (hl), 0c3h
        inc     hl
        ld      (hl), 03ch
        pop     hl
        ld      a, (hl)
        cp      0a5h
        jr      nz, rdp_fail
        inc     hl
        ld      a, (hl)
        cp      05ah
        jr      nz, rdp_fail
        inc     hl
        ld      a, (hl)
        cp      0c3h
        jr      nz, rdp_fail
        inc     hl
        ld      a, (hl)
        cp      03ch
        jr      nz, rdp_fail
        or      a                       ; carry clear
        ret
rdp_fail:
        scf
        ret
