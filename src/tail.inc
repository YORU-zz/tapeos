; tail.inc - DELAY (20) and the unimplemented-entry stub
; (common layer).
; DELAY (20): busy wait, hl = loop count (hl destroyed)
i_delay:
        dec     hl
        ld      a, h
        or      l
        jr      nz, i_delay
        ret

; Stub for unimplemented entries: returns with carry set.
i_stub:
        scf
        ret

