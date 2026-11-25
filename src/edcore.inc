; edcore.inc - the shared editor core of SCED and MCED:
; the EMM layer, the free list, the line descriptors, the
; line text operations and the screen rendering. Included
; by tapeos/src/sced.z80 and, through mced/src/splice.py,
; by the generated mced.z80 (the tbcore.inc / sccore.inc
; pattern: one core, two program shells).
;
; ED_MCED (equate in the including file, flipped by
; splice.py for MCED) selects the MCED variant of the few
; routines that genuinely differ - sig_update, stat_build,
; draw_status and the cursor handling in redraw. SCED is
; modal (NORMAL / INSERT / COMMAND) where MCED is modeless,
; and MCED's status line shows a mark indicator instead of
; the mode name; everything else is byte-identical code.
; The includer provides the IOCS equates, the work-area
; equates, the buffer state blocks and the variables.

; ---------------------------------------------------------------
; EMM layer. Ports 0d00h..0d03h (address L/M/H, data with
; auto-increment) - the M5 device, same protocol as the IPL.
; ---------------------------------------------------------------

; emm_probe: write/read test at EMM 010000h, preserving the
; original byte. -> carry set if the device is absent.
emm_probe:
        ld      bc, 00d00h
        xor     a
        out     (c), a                  ; L = 0
        inc     c
        out     (c), a                  ; M = 0
        inc     c
        ld      a, 001h
        out     (c), a                  ; H = 1
        inc     c
        in      a, (c)                  ; original byte
        ld      (probe_sv), a
        call    ep_addr
        inc     c
        ld      a, 0a5h
        out     (c), a
        call    ep_addr
        inc     c
        in      a, (c)
        cp      0a5h
        jp      nz, ep_fail
        call    ep_addr
        inc     c
        ld      a, (probe_sv)
        out     (c), a                  ; restore original
        or      a
        ret
ep_fail:
        scf
        ret
ep_addr:                                ; bc <- 0d00h, addr 010000h
        ld      bc, 00d00h
        xor     a
        out     (c), a
        inc     c
        out     (c), a
        inc     c
        ld      a, 001h
        out     (c), a
        ret

; emm_seek: position the EMM address at the current
; buffer's text offset de. Destroys bc, af.
emm_seek:
        ld      bc, 00d00h
        ld      a, e
        out     (c), a                  ; L = off low
        inc     c
        ld      a, (b_emid)
        add     a, d
        out     (c), a                  ; M = base mid + off high
        inc     c
        ld      a, 001h
        out     (c), a                  ; H = 1
        ret

; emm_read: de = text offset, hl = RAM dest, b = count (>0)
; Destroys af, bc, hl. Preserves de. The byte count is
; kept in memory: with bc = the EMM data port, b is the
; port's high byte and must not double as a counter
; (the trap fd_x1.inc documents for the IOCS itself).
emm_read:
        ld      a, b                    ; count first: emm_seek
        ld      (emm_cnt), a            ; destroys bc (b = 0dh)
        call    emm_seek
        ld      bc, 00d03h
er_lp:
        in      a, (c)
        ld      (hl), a
        inc     hl
        ld      a, (emm_cnt)
        dec     a
        ld      (emm_cnt), a
        jp      nz, er_lp
        ret

; emm_write: de = text offset, hl = RAM src, b = count (>0)
; Destroys af, bc, hl. Preserves de.
emm_write:
        ld      a, b                    ; count first (see above)
        ld      (emm_cnt), a
        call    emm_seek
        ld      bc, 00d03h
ew_lp:
        ld      a, (hl)
        out     (c), a
        inc     hl
        ld      a, (emm_cnt)
        dec     a
        ld      (emm_cnt), a
        jp      nz, ew_lp
        ret

; ---------------------------------------------------------------
; Free list (current buffer). Entries {off u16, size u16}
; at (b_fbase), count (b_flcnt), sorted by off ascending,
; adjacent entries coalesced. First-fit allocation.
; ---------------------------------------------------------------

; fl_alloc: bc = size (>0) -> hl = offset, carry clear;
; carry set if no entry is large enough.
fl_alloc:
        ld      a, (b_flcnt)
        or      a
        jp      z, fa_fail
        ld      hl, (b_fbase)
        ld      (fa_ent), hl
        ld      (fa_cnt), a
fa_lp:
        ld      hl, (fa_ent)
        ld      e, (hl)
        inc     hl
        ld      d, (hl)                 ; de = entry off
        inc     hl
        ld      a, (hl)
        inc     hl
        ld      h, (hl)
        ld      l, a                    ; hl = entry size
        or      a
        sbc     hl, bc                  ; hl = size - want
        jp      nc, fa_fit
        ld      hl, (fa_ent)
        ld      de, 4
        add     hl, de
        ld      (fa_ent), hl
        ld      a, (fa_cnt)
        dec     a
        ld      (fa_cnt), a
        jp      nz, fa_lp
fa_fail:
        scf
        ret
fa_fit:                                 ; hl = leftover, de = off
        ld      (fa_off), de
        ld      a, h
        or      l
        jp      z, fa_remove
        ex      de, hl                  ; hl = off, de = leftover
        add     hl, bc                  ; hl = new entry off
        ld      (fa_noff), hl
        ld      hl, (fa_ent)
        ld      a, (fa_noff)
        ld      (hl), a
        inc     hl
        ld      a, (fa_noff + 1)
        ld      (hl), a
        inc     hl
        ld      (hl), e                 ; size = leftover
        inc     hl
        ld      (hl), d
        ld      hl, (fa_off)
        or      a
        ret
fa_remove:                              ; entry fully consumed
        ld      a, (fa_cnt)
        dec     a                       ; entries after this one
        jp      z, fa_rm_done
        ld      c, a
        ld      b, 0
        sla     c
        rl      b
        sla     c
        rl      b                       ; bc = bytes to move
        ld      hl, (fa_ent)
        ld      d, h
        ld      e, l
        inc     hl
        inc     hl
        inc     hl
        inc     hl                      ; hl = next entry
        ldir
fa_rm_done:
        ld      a, (b_flcnt)
        dec     a
        ld      (b_flcnt), a
        ld      hl, (fa_off)
        or      a
        ret

; fl_free: hl = off, bc = size (>0). Insert in address
; order and coalesce with either neighbour when adjacent.
fl_free:
        ld      (fr_off), hl
        ld      (fr_size), bc
        ld      a, (b_flcnt)
        ld      (fr_cnt), a             ; entries not yet passed
        ld      hl, (b_fbase)
        ld      (fr_ent), hl
        ; find the first entry with off > fr_off
ff_lp:
        ld      a, (fr_cnt)
        or      a
        jp      z, ff_atend
        ld      hl, (fr_ent)
        ld      e, (hl)
        inc     hl
        ld      d, (hl)                 ; de = entry off
        ld      hl, (fr_off)
        or      a
        sbc     hl, de
        jp      c, ff_found             ; fr_off < entry off
        ld      hl, (fr_ent)
        ld      de, 4
        add     hl, de
        ld      (fr_ent), hl
        ld      a, (fr_cnt)
        dec     a
        ld      (fr_cnt), a
        jp      ff_lp
ff_found:                               ; insert before (fr_ent)
        ; try merging into the previous entry first
        ld      hl, (fr_ent)
        ld      de, (b_fbase)
        or      a
        sbc     hl, de
        jp      z, ff_noprev            ; fr_ent is the first
        ld      hl, (fr_ent)
        dec     hl
        dec     hl
        dec     hl
        dec     hl
        ld      (fr_prv), hl
        ld      e, (hl)
        inc     hl
        ld      d, (hl)                 ; de = prev off
        inc     hl
        ld      a, (hl)
        inc     hl
        ld      h, (hl)
        ld      l, a                    ; hl = prev size
        add     hl, de                  ; hl = prev end
        ld      de, (fr_off)
        or      a
        sbc     hl, de
        jp      nz, ff_noprev
        ; prev end == fr_off: grow prev by fr_size
        ld      hl, (fr_prv)
        inc     hl
        inc     hl
        ld      e, (hl)
        inc     hl
        ld      d, (hl)                 ; de = prev size
        ld      hl, (fr_size)
        add     hl, de
        ld      (fr_nsize), hl          ; new prev size
        ld      hl, (fr_prv)
        inc     hl
        inc     hl
        ld      a, (fr_nsize)
        ld      (hl), a
        inc     hl
        ld      a, (fr_nsize + 1)
        ld      (hl), a
        ; does the grown prev now touch the next entry?
        ld      a, (fr_cnt)
        or      a
        ret     z                       ; no next entry
        ld      hl, (fr_prv)
        ld      e, (hl)
        inc     hl
        ld      d, (hl)                 ; de = prev off
        ld      hl, (fr_nsize)
        add     hl, de                  ; hl = grown prev end
        ld      de, (fr_ent)
        ld      a, (de)
        ld      (fr_tmp), a
        inc     de
        ld      a, (de)
        ld      (fr_tmp + 1), a         ; fr_tmp = next off
        ld      de, (fr_tmp)
        or      a
        sbc     hl, de
        ret     nz                      ; not adjacent: done
        ; absorb the next entry into prev, remove next
        ld      hl, (fr_ent)
        inc     hl
        inc     hl
        ld      e, (hl)
        inc     hl
        ld      d, (hl)                 ; de = next size
        ld      hl, (fr_nsize)
        add     hl, de
        ld      (fr_nsize), hl
        ld      hl, (fr_prv)
        inc     hl
        inc     hl
        ld      a, (fr_nsize)
        ld      (hl), a
        inc     hl
        ld      a, (fr_nsize + 1)
        ld      (hl), a
        ld      a, (fr_cnt)
        dec     a                       ; entries after next
        jp      fl_remove_at            ; remove entry at fr_ent
ff_noprev:
        ; try extending the next entry backwards
        ld      a, (fr_cnt)
        or      a
        jp      z, ff_insert            ; no next entry
        ld      hl, (fr_off)
        ld      de, (fr_size)
        add     hl, de                  ; hl = fr end
        ld      de, (fr_ent)
        ld      a, (de)
        ld      (fr_tmp), a
        inc     de
        ld      a, (de)
        ld      (fr_tmp + 1), a
        ld      de, (fr_tmp)            ; de = next off
        or      a
        sbc     hl, de
        jp      nz, ff_insert
        ; fr end == next off: next.off = fr_off, size += fr_size
        ld      hl, (fr_ent)
        ld      a, (fr_off)
        ld      (hl), a
        inc     hl
        ld      a, (fr_off + 1)
        ld      (hl), a
        inc     hl
        ld      e, (hl)
        inc     hl
        ld      d, (hl)                 ; de = next size
        ld      hl, (fr_size)
        add     hl, de
        ex      de, hl                  ; de = new size
        ld      hl, (fr_ent)
        inc     hl
        inc     hl
        ld      (hl), e
        inc     hl
        ld      (hl), d
        ret
ff_insert:                              ; plain insert at fr_ent
        ld      a, (b_flcnt)
        cp      FLMAX
        ret     nc                      ; list full: leak (rare)
        ld      a, (fr_cnt)             ; entries to shift up
        jp      fl_insert_at
ff_atend:                               ; append after last entry
        ; fr_ent = one past the last entry; prev merge?
        ld      a, (b_flcnt)
        or      a
        jp      z, ff_insert            ; empty list: insert
        ld      hl, (fr_ent)
        dec     hl
        dec     hl
        dec     hl
        dec     hl
        ld      (fr_prv), hl
        ld      e, (hl)
        inc     hl
        ld      d, (hl)
        inc     hl
        ld      a, (hl)
        inc     hl
        ld      h, (hl)
        ld      l, a                    ; hl = prev size
        add     hl, de                  ; hl = prev end
        ld      de, (fr_off)
        or      a
        sbc     hl, de
        jp      nz, ff_insert
        ; grow prev (which is the last entry)
        ld      hl, (fr_prv)
        inc     hl
        inc     hl
        ld      e, (hl)
        inc     hl
        ld      d, (hl)
        ld      hl, (fr_size)
        add     hl, de
        ex      de, hl
        ld      hl, (fr_prv)
        inc     hl
        inc     hl
        ld      (hl), e
        inc     hl
        ld      (hl), d
        ret

; fl_remove_at: remove the entry at (fr_ent); a = number
; of entries after it. Decrements (b_flcnt).
fl_remove_at:
        or      a
        jp      z, fra_done
        ld      c, a
        ld      b, 0
        sla     c
        rl      b
        sla     c
        rl      b                       ; bc = bytes to move
        ld      hl, (fr_ent)
        ld      d, h
        ld      e, l
        inc     hl
        inc     hl
        inc     hl
        inc     hl
        ldir
fra_done:
        ld      a, (b_flcnt)
        dec     a
        ld      (b_flcnt), a
        ret

; fl_insert_at: insert (fr_off, fr_size) at (fr_ent);
; a = number of entries at/after fr_ent to shift up.
; Increments (b_flcnt). Caller checked capacity.
fl_insert_at:
        or      a
        jp      z, fia_write
        ld      c, a
        ld      b, 0
        sla     c
        rl      b
        sla     c
        rl      b                       ; bc = bytes to move
        ld      hl, (fr_ent)
        add     hl, bc                  ; hl = end of block
        dec     hl                      ; last byte src
        ld      d, h
        ld      e, l
        inc     de
        inc     de
        inc     de
        inc     de                      ; de = last byte dst
        lddr
fia_write:
        ld      hl, (fr_ent)
        ld      a, (fr_off)
        ld      (hl), a
        inc     hl
        ld      a, (fr_off + 1)
        ld      (hl), a
        inc     hl
        ld      a, (fr_size)
        ld      (hl), a
        inc     hl
        ld      a, (fr_size + 1)
        ld      (hl), a
        ld      a, (b_flcnt)
        inc     a
        ld      (b_flcnt), a
        ret

; ---------------------------------------------------------------
; Line descriptors (current buffer). 8 bytes per line at
; (b_lbase), in document order, prev/next links maintained.
; ---------------------------------------------------------------

; desc_ptr: hl = index -> hl = descriptor pointer.
; Destroys de.
desc_ptr:
        ld      de, (b_lbase)
        add     hl, hl
        add     hl, hl
        add     hl, hl                  ; index * 8
        add     hl, de
        ret

; desc_get: hl = index -> de = text offset, a = length.
; Destroys hl.
desc_get:
        call    desc_ptr
        ld      e, (hl)
        inc     hl
        ld      d, (hl)
        inc     hl
        ld      a, (hl)
        ret

; relink: rebuild prev/next links for all descriptors.
relink:
        ld      hl, (b_nlines)
        ld      (rl_n), hl
        ld      hl, 0
        ld      (rl_i), hl
        ld      hl, (b_lbase)
        ld      (rl_p), hl
rl_lp:
        ld      hl, (rl_i)
        ld      de, (rl_n)
        or      a
        sbc     hl, de
        ret     nc                      ; i >= n: done
        ld      hl, (rl_i)
        dec     hl
        ld      (rl_v), hl              ; prev = i-1 (ffff if 0)
        ld      hl, (rl_p)
        inc     hl
        inc     hl
        inc     hl                      ; &prev
        ld      a, (rl_v)
        ld      (hl), a
        inc     hl
        ld      a, (rl_v + 1)
        ld      (hl), a
        ld      hl, (rl_i)
        inc     hl                      ; next candidate = i+1
        ld      de, (rl_n)
        or      a
        sbc     hl, de
        jp      c, rl_nx_ok
        ld      hl, 0ffffh
        jp      rl_nx_st
rl_nx_ok:
        ld      hl, (rl_i)
        inc     hl
rl_nx_st:
        ld      (rl_v), hl
        ld      hl, (rl_p)
        ld      de, D_NEXT
        add     hl, de                  ; &next
        ld      a, (rl_v)
        ld      (hl), a
        inc     hl
        ld      a, (rl_v + 1)
        ld      (hl), a
        ld      hl, (rl_p)
        ld      de, 8
        add     hl, de
        ld      (rl_p), hl
        ld      hl, (rl_i)
        inc     hl
        ld      (rl_i), hl
        jp      rl_lp

; ins_desc: hl = index -> insert a blank descriptor there.
; Carry set if the array is full (256 lines).
ins_desc:
        ld      (id_i), hl
        ld      hl, (b_nlines)
        ld      a, h
        cp      MAXLINES >> 8
        jp      nz, id_go
        ld      a, l
        or      a
        jp      z, id_full              ; nlines == 256
id_go:
        ; bytes to move = (nlines - idx) * 8
        ld      hl, (b_nlines)
        ld      de, (id_i)
        or      a
        sbc     hl, de
        add     hl, hl
        add     hl, hl
        add     hl, hl
        ld      (id_bc), hl             ; byte count
        ld      a, h
        or      l
        jp      z, id_write             ; nothing above
        ; src end = base + nlines*8 - 1, dst end = src + 8
        ld      hl, (b_nlines)
        add     hl, hl
        add     hl, hl
        add     hl, hl
        ld      de, (b_lbase)
        add     hl, de
        dec     hl                      ; hl = src last byte
        ld      d, h
        ld      e, l
        inc     de
        inc     de
        inc     de
        inc     de
        inc     de
        inc     de
        inc     de
        inc     de                      ; de = dst last byte
        ld      bc, (id_bc)
        lddr
id_write:
        ld      hl, (id_i)
        call    desc_ptr
        ld      a, 0ffh
        ld      (hl), a                 ; off = ffffh
        inc     hl
        ld      (hl), a
        inc     hl
        xor     a
        ld      (hl), a                 ; len = 0
        ld      hl, (b_nlines)
        inc     hl
        ld      (b_nlines), hl
        call    relink
        or      a
        ret
id_full:
        scf
        ret

; del_desc: hl = index -> remove that descriptor.
del_desc:
        ld      (id_i), hl
        ld      hl, (b_nlines)
        ld      de, (id_i)
        or      a
        sbc     hl, de
        dec     hl                      ; entries after = n-idx-1
        ld      a, h
        or      l
        jp      z, dd_nomore
        add     hl, hl
        add     hl, hl
        add     hl, hl
        ld      (id_bc), hl
        ld      hl, (id_i)
        call    desc_ptr                ; hl = dst
        ld      d, h
        ld      e, l
        inc     hl
        inc     hl
        inc     hl
        inc     hl
        inc     hl
        inc     hl
        inc     hl
        inc     hl                      ; hl = src (next desc)
        ld      bc, (id_bc)
        ldir
dd_nomore:
        ld      hl, (b_nlines)
        dec     hl
        ld      (b_nlines), hl
        call    relink
        ret

; ---------------------------------------------------------------
; Line text operations. All go through RAM copies; the
; checksum (b_chk) is maintained incrementally.
; ---------------------------------------------------------------

; line_read: hl = index, de = RAM dest -> a = length.
line_read:
        ld      (lr_dest), de
        call    desc_get                ; de = off, a = len
        ld      b, a
        or      a
        ret     z
        push    bc
        ld      hl, (lr_dest)
        call    emm_read                ; de = off, b = len
        pop     bc
        ld      a, b
        ret

; line_sum: hl = index -> hl = sum of the line's bytes.
line_sum:
        ld      de, RLINE
        call    line_read               ; a = len
        or      a
        jp      z, lsum_zero
        ld      hl, 0
        ld      de, RLINE
        ld      b, a
lsum_lp:
        ld      a, (de)
        inc     de
        add     a, l
        ld      l, a
        jp      nc, lsum_nc
        inc     h
lsum_nc:
        djnz    lsum_lp
        ret
lsum_zero:
        ld      hl, 0
        ret

; line_set: hl = index, de = RAM src, a = new length.
; Replaces the line's text, reallocating its storage when
; the length changed (the fiction's rule) and writing in
; place when it did not. Carry set on allocation failure
; (the line is then unchanged).
line_set:
        ld      (ls_idx), hl
        ld      (ls_src), de
        ld      (ls_nlen), a
        call    desc_get                ; de = old off, a = old len
        ld      (ls_ooff), de
        ld      (ls_olen), a
        ld      hl, (ls_idx)
        call    line_sum
        ld      (ls_osum), hl
        ; new sum, from the RAM source
        ld      hl, 0
        ld      a, (ls_nlen)
        or      a
        jp      z, ls_ns_done
        ld      de, (ls_src)
        ld      b, a
ls_ns_lp:
        ld      a, (de)
        inc     de
        add     a, l
        ld      l, a
        jp      nc, ls_ns_nc
        inc     h
ls_ns_nc:
        djnz    ls_ns_lp
ls_ns_done:
        ld      (ls_nsum), hl
        ; same length?
        ld      a, (ls_nlen)
        ld      b, a
        ld      a, (ls_olen)
        cp      b
        jp      nz, ls_realloc
        or      a
        jp      z, ls_upd               ; both zero
        ld      de, (ls_ooff)           ; in-place overwrite
        ld      hl, (ls_src)
        ld      a, (ls_nlen)
        ld      b, a
        call    emm_write
        jp      ls_upd
ls_realloc:
        ld      a, (ls_nlen)
        or      a
        jp      z, ls_zero_new
        ld      b, 0
        ld      c, a
        call    fl_alloc                ; -> hl = new off
        jp      c, ls_fail
        ld      (ls_noff), hl
        ex      de, hl                  ; de = new off
        ld      hl, (ls_src)
        ld      a, (ls_nlen)
        ld      b, a
        call    emm_write
        ld      a, (ls_olen)
        or      a
        jp      z, ls_setdesc
        ld      hl, (ls_ooff)
        ld      b, 0
        ld      c, a
        call    fl_free
        jp      ls_setdesc
ls_zero_new:
        ld      hl, (ls_ooff)
        ld      a, (ls_olen)
        ld      b, 0
        ld      c, a
        call    fl_free
        ld      hl, 0ffffh
        ld      (ls_noff), hl
ls_setdesc:
        ld      hl, (ls_idx)
        call    desc_ptr
        ld      a, (ls_noff)
        ld      (hl), a
        inc     hl
        ld      a, (ls_noff + 1)
        ld      (hl), a
        inc     hl
        ld      a, (ls_nlen)
        ld      (hl), a
ls_upd:
        ld      hl, (b_chk)
        ld      de, (ls_osum)
        or      a
        sbc     hl, de
        ld      de, (ls_nsum)
        add     hl, de
        ld      (b_chk), hl
        or      a
        ret
ls_fail:
        scf
        ret

; load_cur_line: (WLINE) <- current line, (wlen) = length.
load_cur_line:
        ld      hl, (b_line)
        ld      de, WLINE
        call    line_read
        ld      (wlen), a
        ret

; commit_cur_line: write (WLINE)/(wlen) back to the
; current line. On allocation failure, show AREA FULL and
; reload the line (the edit is reverted).
commit_cur_line:
        ld      hl, (b_line)
        ld      de, WLINE
        ld      a, (wlen)
        call    line_set
        ret     nc
        ld      hl, msg_full
        ld      a, 004h
        call    set_msg
        call    load_cur_line
        or      a
        ret

; ---------------------------------------------------------------
; Screen rendering. Text VRAM is written directly through
; ports (char plane page 030h, attr plane page 020h, cell
; offset = y*40 + x) - the OS's own internal scheme. Only
; attribute values 000h..007h are used (foreground colour;
; higher bits hide or blink the cell on this hardware, as
; verified by a probe program - see ../sced.md).
; ---------------------------------------------------------------

; set_msg: hl = message (0-terminated), a = attribute.
; The message replaces the status line for one redraw.
set_msg:
        ld      (msgptr), hl
        ld      (msgattr), a
        ret

; putcell: hl = cell offset, a = char, attr in (cattr).
; Destroys bc.
putcell:
        push    af
        ld      c, l
        ld      a, h
        add     a, 030h
        ld      b, a
        pop     af
        out     (c), a                  ; char plane
        ld      c, l
        ld      a, h
        add     a, 020h
        ld      b, a
        ld      a, (cattr)
        out     (c), a                  ; attribute plane
        ret

; cell_cursor: hl = cell offset, a = attribute. Rewrites
; the cell's existing char with the given attribute (the
; visible cursor). Destroys bc.
cell_cursor:
        ld      (cattr), a
        push    hl
        ld      c, l
        ld      a, h
        add     a, 030h
        ld      b, a
        in      a, (c)                  ; char under the cursor
        pop     hl
        jp      putcell

; desc_next: hl = index -> hl = its next link (or ffffh).
desc_next:
        call    desc_ptr
        ld      de, D_NEXT
        add     hl, de
        ld      a, (hl)
        inc     hl
        ld      h, (hl)
        ld      l, a
        ret

; adjust_view: scroll (b_top)/(b_left) so the cursor is
; visible: rows 0..23 show lines top..top+23, cols show
; left..left+39.
adjust_view:
        ld      hl, (b_line)
        ld      de, (b_top)
        or      a
        sbc     hl, de
        jp      nc, av_below            ; line >= top
        ld      hl, (b_line)
        ld      (b_top), hl
        jp      av_h
av_below:                               ; hl = line - top
        ld      a, h
        or      a
        jp      nz, av_far
        ld      a, l
        cp      24
        jp      c, av_h
av_far:
        ld      hl, (b_line)
        ld      de, 23
        or      a
        sbc     hl, de
        ld      (b_top), hl
av_h:
        ld      a, (b_left)
        ld      b, a
        ld      a, (b_col)
        cp      b                       ; col - left
        jp      nc, av_h2               ; col >= left
        ld      a, (b_col)
        ld      (b_left), a
        ret
av_h2:
        sub     b                       ; a = col - left
        cp      40
        ret     c
        ld      a, (b_col)
        sub     39
        ld      (b_left), a
        ret

; sig_update: refresh the verification block at SIGBLK:
        if ED_MCED
; "MCED", curbuf, 0, buf1 mod, buf2 mod, buf1 lines,
        else
; "SCED", curbuf, mode, buf1 mod, buf2 mod, buf1 lines,
        endif
; buf2 lines, buf1 checksum, buf2 checksum.
sig_update:
        ld      hl, sig_magic
        ld      de, SIGBLK
        ld      bc, 4
        ldir
        ld      a, (curbuf)
        ld      (SIGBLK + 4), a
        ld      a, (mode)
        ld      (SIGBLK + 5), a
        if ED_MCED
        ld      a, 1
        call    su_ptr                  ; buffer 1's state
        xor     a
        ld      (su_p2), a              ; delta 0 = buffer 1
        ld      de, SIGBLK + 6
        call    sig_fields
        ld      a, 2
        call    su_ptr                  ; buffer 2's state
        ld      a, 1
        ld      (su_p2), a              ; delta 1 = buffer 2
        ld      de, SIGBLK + 7
        jp      sig_fields
; su_ptr: a = buffer number -> hl = its state block
; (bstate when it is the current buffer, else ostate).
su_ptr:
        ld      hl, bstate
        ld      b, a
        ld      a, (curbuf)
        cp      b
        ret     z
        ld      hl, ostate
        ret
; sig_fields: hl = state, de = the state's mod slot in
; SIGBLK (6 or 7), (su_p2) = 0/1: write the modified
; flag at de+0, the line count at de+2+delta, the
; checksum a further 3 bytes on.
sig_fields:
        push    hl
        ld      bc, S_MOD
        add     hl, bc
        ld      a, (hl)
        ld      (de), a
        pop     hl
        ld      a, (su_p2)
        add     a, 2
        add     a, e
        ld      e, a
        jp      nc, sf_1
        inc     d
sf_1:
        ld      a, (hl)
        ld      (de), a
        inc     hl
        inc     de
        ld      a, (hl)
        ld      (de), a
        ld      a, 3
        add     a, e
        ld      e, a
        jp      nc, sf_2
        inc     d
sf_2:
        ld      bc, S_CHK - 1
        add     hl, bc
        ld      a, (hl)
        ld      (de), a
        inc     hl
        inc     de
        ld      a, (hl)
        ld      (de), a
        ret
        else
        ld      a, (curbuf)
        cp      1
        jp      z, su_cur1
        ld      hl, ostate              ; cur = 2: buf1 = ostate
        ld      de, bstate
        jp      su_go
su_cur1:
        ld      hl, bstate
        ld      de, ostate
su_go:                                  ; hl = buf1 state, de = buf2 state
        ld      (su_p2), de
        push    hl
        ld      de, S_MOD
        add     hl, de
        ld      a, (hl)
        ld      (SIGBLK + 6), a
        pop     hl
        push    hl
        ld      de, S_NLINES
        add     hl, de
        ld      a, (hl)
        ld      (SIGBLK + 8), a
        inc     hl
        ld      a, (hl)
        ld      (SIGBLK + 9), a
        pop     hl
        ld      de, S_CHK
        add     hl, de
        ld      a, (hl)
        ld      (SIGBLK + 12), a
        inc     hl
        ld      a, (hl)
        ld      (SIGBLK + 13), a
        ld      hl, (su_p2)             ; buf2 state
        push    hl
        ld      de, S_MOD
        add     hl, de
        ld      a, (hl)
        ld      (SIGBLK + 7), a
        pop     hl
        push    hl
        ld      de, S_NLINES
        add     hl, de
        ld      a, (hl)
        ld      (SIGBLK + 10), a
        inc     hl
        ld      a, (hl)
        ld      (SIGBLK + 11), a
        pop     hl
        ld      de, S_CHK
        add     hl, de
        ld      a, (hl)
        ld      (SIGBLK + 14), a
        inc     hl
        ld      a, (hl)
        ld      (SIGBLK + 15), a
        ret
        endif
sig_magic:
        if ED_MCED
        db      "MCED"
        else
        db      "SCED"
        endif

; div10: hl = value -> hl = value / 10, a = remainder.
; Preserves bc and de (the quotient is counted in bc).
; (Values divided here are <= 999, so repeated subtraction
; is at most ~100 iterations.)
div10:
        push    bc
        ld      bc, 0
dv_lp:
        ld      a, h
        or      a
        jp      nz, dv_sub
        ld      a, l
        cp      10
        jp      c, dv_done
dv_sub:
        ld      a, l
        sub     10
        ld      l, a
        jp      nc, dv_nc
        dec     h
dv_nc:
        inc     bc
        jp      dv_lp
dv_done:
        ld      a, l                    ; remainder
        ld      h, b
        ld      l, c                    ; hl = quotient
        pop     bc
        ret

; dec16: hl = value, de = dest -> writes decimal digits
; (no leading zeros), de advanced past them.
dec16:
        ld      b, 0
dc_div:
        call    div10
        push    af
        inc     b
        ld      a, h
        or      l
        jp      nz, dc_div
dc_wr:
        pop     af
        add     a, '0'
        ld      (de), a
        inc     de
        djnz    dc_wr
        ret

; stat_build: build the 40-char status line in STATBUF
        if ED_MCED
; and set (stat_attr). A one-shot message (set_msg)
; replaces the status for one redraw; otherwise the
; status shows buffer, name, modified mark, position,
; and the SKK / mark indicators.
        else
; and set (stat_attr). Handles the message override and
; COMMAND mode; otherwise the normal status.
        endif
stat_build:
        ld      hl, (msgptr)
        ld      a, h
        or      l
        if ED_MCED
        jp      z, sb_status
        else
        jp      z, sb_normal
        endif
        ; one-shot message
        ld      a, (msgattr)
        ld      (stat_attr), a
        ld      de, STATBUF
        ld      b, 40
sb_msg_lp:
        ld      a, (hl)
        or      a
        if ED_MCED
        jp      z, sb_fill
        else
        jp      z, sb_pad
        endif
        ld      (de), a
        inc     hl
        inc     de
        djnz    sb_msg_lp
        jp      sb_done
        if ED_MCED
        else
sb_pad:
        ld      a, ' '
        ld      (de), a
        inc     de
        djnz    sb_pad
        jp      sb_done
sb_normal:
        ld      a, 006h
        ld      (stat_attr), a
        ld      a, (mode)
        cp      M_COMMAND
        jp      nz, sb_status
        ; command line: ':' + CMDBUF
        ld      de, STATBUF
        ld      a, ':'
        ld      (de), a
        inc     de
        ld      hl, CMDBUF
        ld      a, (cmdlen)
        or      a
        jp      z, sb_cpad
        ld      b, a
sb_clp:
        ld      a, (hl)
        ld      (de), a
        inc     hl
        inc     de
        djnz    sb_clp
sb_cpad:
sb_cpad2:
        ; pad from de to STATBUF+40
        ld      hl, STATBUF + 40
        or      a
        sbc     hl, de
        ret     z
        ld      b, l
sb_cpl:
        ld      a, ' '
        ld      (de), a
        inc     de
        djnz    sb_cpl
        jp      sb_done
        endif
sb_status:
        if ED_MCED
        ld      a, 006h                 ; (SCED sets this in
        ld      (stat_attr), a          ; sb_normal instead)
        endif
        ld      de, STATBUF
        ld      a, (curbuf)
        add     a, '0'
        ld      (de), a
        inc     de
        ld      a, ':'
        ld      (de), a
        inc     de
        ; name: if first byte is a space -> "UNNAMED"
        ld      a, (b_name)
        cp      ' '
        jp      nz, sb_nm
        ld      hl, str_unnamed
        ld      bc, 7
        ldir
        jp      sb_nm_done
sb_nm:
        ld      hl, b_name
        ld      b, 10
sb_nmlp:
        ld      a, (hl)
        cp      ' '
        jp      z, sb_nm_done
        ld      (de), a
        inc     hl
        inc     de
        djnz    sb_nmlp
sb_nm_done:
        ld      a, ' '
        ld      (de), a
        inc     de
        ld      a, (b_mod)
        or      a
        jp      z, sb_nomark
        ld      a, '+'
        jp      sb_mark
sb_nomark:
        ld      a, ' '
sb_mark:
        ld      (de), a
        inc     de
        ld      a, ' '
        ld      (de), a
        inc     de
        ld      hl, (b_line)
        inc     hl
        call    dec16                   ; line (1-based)
        ld      a, ','
        ld      (de), a
        inc     de
        ld      a, (b_col)
        inc     a
        ld      l, a
        ld      h, 0
        call    dec16                   ; col (1-based)
        ld      a, ' '
        ld      (de), a
        inc     de
        if ED_MCED
        else
        ld      a, (mode)
        cp      M_INSERT
        jp      z, sb_ins
        ld      hl, str_normal
        ld      bc, 6
        ldir
        jp      sb_skk
sb_ins:
        ld      hl, str_insert
        ld      bc, 6
        ldir
        endif
sb_skk:
        ld      a, (skk_on)
        or      a
        if ED_MCED
        jp      z, sb_mrk
        else
        jp      z, sb_fill
        endif
        ld      hl, str_skk
        ld      bc, 4
        ldir
        ld      a, (pend_len)
        or      a
        if ED_MCED
        jp      z, sb_mrk
        else
        jp      z, sb_fill
        endif
        ld      a, '['
        ld      (de), a
        inc     de
        ld      hl, pend_buf
        ld      a, (pend_len)
        ld      b, a
sb_pendlp:
        ld      a, (hl)
        ld      (de), a
        inc     hl
        inc     de
        djnz    sb_pendlp
        ld      a, ']'
        ld      (de), a
        inc     de
        if ED_MCED
sb_mrk:
        ld      a, (b_mset)
        or      a
        jp      z, sb_fill
        ld      hl, str_mrk
        ld      bc, 4
        ldir
sb_fill:
        call    pad_stat
        else
sb_fill:
        ld      hl, STATBUF + 40
        or      a
        sbc     hl, de
        jp      z, sb_done
        ld      b, l
sb_flp:
        ld      a, ' '
        ld      (de), a
        inc     de
        djnz    sb_flp
        endif
sb_done:
        ret

str_unnamed:
        db      "UNNAMED"
        if ED_MCED
        else
str_normal:
        db      "NORMAL"
str_insert:
        db      "INSERT"
        endif
str_skk:
        db      " SKK"
        if ED_MCED
str_mrk:
        db      " MRK"
        endif

; draw_status: write STATBUF (40 chars) to row 24 with
        if ED_MCED
; (stat_attr).
        else
; (stat_attr); in COMMAND mode, put the cursor at the end
; of the typed command.
        endif
draw_status:
        call    stat_build
        if ED_MCED
        ld      a, (stat_attr)
        ld      (cattr), a
        ld      a, 24
        ld      hl, STATBUF
        call    put_row
        ld      hl, 0                   ; one-shot message
        ld      (msgptr), hl            ; consumed
        ret
        else
        ld      hl, 24 * 40
        ld      (ds_off), hl
        ld      hl, STATBUF
        ld      (ds_src), hl
        ld      a, (stat_attr)
        ld      (cattr), a
        ld      b, 40
ds_lp:
        push    bc
        ld      hl, (ds_src)
        ld      a, (hl)
        inc     hl
        ld      (ds_src), hl
        ld      hl, (ds_off)
        push    af
        call    putcell
        pop     af
        ld      hl, (ds_off)
        inc     hl
        ld      (ds_off), hl
        pop     bc
        djnz    ds_lp
        ; one-shot message consumed
        ld      hl, 0
        ld      (msgptr), hl
        ld      a, (mode)
        cp      M_COMMAND
        ret     nz
        ld      a, (cmdlen)
        inc     a                       ; past the ':'
        ld      l, a
        ld      h, 0
        ld      de, 24 * 40
        add     hl, de
        ld      a, 002h
        jp      cell_cursor
        endif

; redraw: full redraw of the text area (rows 0..23),
; the status row, and the cursor cell.
redraw:
        call    adjust_view
        call    sig_update
        ld      hl, (b_top)
        ld      (rd_idx), hl
        xor     a
        ld      (rd_row), a
rd_lp:
        ld      a, (rd_row)
        cp      24
        jp      z, rd_status
        ; offset = row * 40
        ld      l, a
        ld      h, 0
        add     hl, hl                  ; *2
        add     hl, hl                  ; *4
        add     hl, hl                  ; *8
        ld      (rd_off), hl
        add     hl, hl                  ; *16
        add     hl, hl                  ; *32
        ld      de, (rd_off)
        add     hl, de                  ; *40
        ld      (rd_off), hl
        ; valid line?
        ld      hl, (rd_idx)
        ld      a, h
        cp      0ffh
        jp      z, rd_tilde
        ld      de, (b_nlines)
        or      a
        sbc     hl, de
        jp      nc, rd_tilde
        ld      hl, (rd_idx)
        ld      de, RLINE
        call    line_read               ; a = len
        ld      (rd_len), a
        ; draw 40 cells from leftcol
        ld      a, 007h
        ld      (cattr), a
        xor     a
        ld      (rd_x), a
rd_cell_lp:
        ld      a, (rd_x)
        cp      40
        jp      z, rd_row_done
        ld      a, (b_left)
        ld      b, a
        ld      a, (rd_x)
        add     a, b                    ; a = source pos
        ld      (rd_pos), a
        ld      b, a
        ld      a, (rd_len)
        cp      b
        jp      c, rd_blank             ; pos > len
        jp      z, rd_blank             ; pos == len
        ld      hl, RLINE
        ld      e, b
        ld      d, 0
        add     hl, de
        ld      a, (hl)                 ; the char
        cp      020h
        jp      c, rd_sp                ; control: show space
        cp      07fh
        jp      z, rd_sp
        jp      rd_put
rd_sp:
        ld      a, ' '
        jp      rd_put
rd_blank:
        ld      a, ' '
rd_put:
        ld      (rd_ch), a
        ; cursor cell?
        if ED_MCED
        ; (modeless: the cursor is never suppressed)
        else
        ; (suppressed in COMMAND mode: the cursor lives
        ; on the status line there)
        ld      a, (mode)
        cp      M_COMMAND
        jp      z, rd_nc
        endif
        ld      hl, (rd_idx)
        ld      de, (b_line)
        or      a
        sbc     hl, de
        jp      nz, rd_nc
        ld      a, (rd_pos)
        ld      b, a
        ld      a, (b_col)
        cp      b
        jp      nz, rd_nc
        if ED_MCED
        ld      a, 002h
        else
        ld      a, (mode)
        cp      M_INSERT
        jp      z, rd_cins
        ld      a, 002h
        jp      rd_cset
rd_cins:
        ld      a, 004h
        endif
rd_cset:
        ld      (cattr), a
        ld      a, (rd_ch)
        ld      hl, (rd_off)
        call    putcell
        ld      a, 007h
        ld      (cattr), a
        jp      rd_nx
rd_nc:
        ld      a, (rd_ch)
        ld      hl, (rd_off)
        call    putcell
rd_nx:
        ld      hl, (rd_off)
        inc     hl
        ld      (rd_off), hl
        ld      a, (rd_x)
        inc     a
        ld      (rd_x), a
        jp      rd_cell_lp
rd_row_done:
        ld      hl, (rd_idx)
        call    desc_next
        ld      (rd_idx), hl
        ld      a, (rd_row)
        inc     a
        ld      (rd_row), a
        jp      rd_lp
rd_tilde:
        ld      a, 001h
        ld      (cattr), a
        ld      a, '~'
        ld      hl, (rd_off)
        call    putcell
        ld      a, 007h
        ld      (cattr), a
        ld      b, 39
rd_tl_lp:
        push    bc
        ld      hl, (rd_off)
        inc     hl
        ld      (rd_off), hl
        ld      a, ' '                  ; putcell clobbers a
        call    putcell
        pop     bc
        djnz    rd_tl_lp
        ld      hl, 0ffffh
        ld      (rd_idx), hl
        ld      a, (rd_row)
        inc     a
        ld      (rd_row), a
        jp      rd_lp
rd_status:
        jp      draw_status
