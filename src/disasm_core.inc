; disasm_core.inc - DISASM decode core: source layer (getb),
; label management, the three passes, the structured x/y/z
; decoder and the NEWASM/EDAS-subset formatter, plus all
; shared variables. Shared by the standalone DISASM
; (disasm/src/disasm.z80) and the EDAS disassembler module
; EDIS (tapeos/src/edis.z80) - roadmap item 60.
;
; Expects the including file to have defined the C_* IOCS
; entries, the work-area EQUs (SECBUF / OUTBUF / LINEBUF /
; LABTAB / LABMAX / FDBITS / DRTAB / DRMAX / FDMAXREC /
; DIRREC) and the E_* error codes, and to provide: err,
; dir_load, find_entry, find_free (the last three via
; disasm_util.inc).
;
; When EDISMOD is defined (the EDIS build), two seams
; change: gb_mem translates through (inp_origin) so the
; input can be staged in a private buffer, and emit_ch
; gains the out_mode = 2 sink, which jumps to ec_cb_hook
; (provided by the EDIS front end). The standalone build
; is unaffected and stays byte-identical.

; ---------------------------------------------------------------
; Source layer: getb reads the byte at an absolute
; address, from the file (sector-cached) or from
; memory, per src_mode.
; ---------------------------------------------------------------

; getb: HL = address -> A = byte. Addresses outside
; the input range read as 0 (only happens for the
; operand prefetch past the last instruction).
getb:
        ld      a, (src_mode)
        or      a
        jr      nz, gb_mem
        push    bc
        push    de
        push    hl
        ld      de, (src_base)
        or      a
        sbc     hl, de                  ; HL = offset
        jr      c, gb_zero
        ld      (gb_off), hl
        ld      de, (src_size)
        or      a
        sbc     hl, de
        jr      nc, gb_zero             ; past EOF: 0
        ld      hl, (gb_off)
        ld      e, h                    ; sector index = off>>8
        ld      d, 0
        ld      hl, (src_rec0)
        add     hl, de
        ex      de, hl                  ; DE = record number
        ld      hl, (cache_rec)
        or      a
        sbc     hl, de
        jr      z, gb_hit
        ld      (cache_rec), de
        ld      hl, SECBUF
        call    C_DREAD
        jp      c, err_file
gb_hit:
        ld      a, (gb_off)             ; low byte of offset
        ld      l, a
        ld      h, SECBUF / 256
        ld      a, (hl)
        pop     hl
        pop     de
        pop     bc
        ret
gb_zero:
        xor     a
        pop     hl
        pop     de
        pop     bc
        ret
gb_mem:
        IF      DEFINED EDISMOD
        ; EDIS build: the input is staged in a private
        ; buffer; translate the logical address through
        ; (inp_origin) - (src_base).
        push    de
        push    hl
        ld      de, (src_base)
        or      a
        sbc     hl, de
        ld      de, (inp_origin)
        add     hl, de
        ld      a, (hl)
        pop     hl
        pop     de
        ret
        ELSE
        ld      a, (hl)
        ret
        ENDIF
err_file:
        ld      a, E_FILE
        jp      err

; ---------------------------------------------------------------
; Output layer. Lines are assembled in LINEBUF by the
; pc_* helpers (pass 2 only) and flushed by emit_line
; to the screen (CONOUT) or to the OUT file (sector
; buffered, allocated at next-free on close-out).
; ---------------------------------------------------------------

; lb_reset: start a fresh output line.
lb_reset:
        ld      hl, LINEBUF
        ld      (lb_ptr), hl
        ld      (hl), 0
        ret

; pc_ch: append A to the line (no-op unless emit=1).
pc_ch:
        push    hl
        ld      hl, emit
        bit     0, (hl)
        jr      z, pc_dn
        ld      hl, (lb_ptr)
        ld      (hl), a
        inc     hl
        ld      (lb_ptr), hl
        ld      (hl), 0
pc_dn:
        pop     hl
        ret

; pc_str: append the NUL-terminated string at HL.
pc_str:
        ld      a, (hl)
        or      a
        ret     z
        call    pc_ch
        inc     hl
        jr      pc_str

; pc_sp: append a space.
pc_sp:
        ld      a, ' '
        jp      pc_ch

; pc_comma: append a comma.
pc_comma:
        ld      a, ','
        jp      pc_ch

; emit_line: send LINEBUF + newline to the sink.
; A no-op during pass2d dry walks.
emit_line:
        ld      a, (dry)
        or      a
        ret     nz
        ld      hl, LINEBUF
el_lp:
        ld      a, (hl)
        or      a
        jr      z, el_nl
        push    hl
        call    emit_ch
        pop     hl
        inc     hl
        jr      el_lp
el_nl:
        ld      a, (out_mode)
        or      a
        jr      z, el_scr
        ld      a, 00ah                 ; file: LF only
        jp      emit_ch
el_scr:
        call    C_CRLF
        ret

; emit_ch: A = char to the sink. Every emitted byte
; is counted in out_len, whichever sink is active.
emit_ch:
        ld      (ec_c), a
        ld      hl, (out_len)
        inc     hl
        ld      (out_len), hl
        ld      a, h
        or      l
        jr      nz, ec_nz
        ld      hl, (out_len + 2)
        inc     hl
        ld      (out_len + 2), hl
ec_nz:
        ld      a, (out_mode)
        IF      DEFINED EDISMOD
        cp      2
        jr      z, ec_cb2               ; EDIS: text sink
        ENDIF
        or      a
        jr      nz, ec_file
        ld      a, (ec_c)
        jp      C_CONOUT
        IF      DEFINED EDISMOD
ec_cb2:
        ld      a, (ec_c)
        jp      ec_cb_hook              ; provided by EDIS
        ENDIF
ec_file:
        ld      hl, (out_pos)
        ld      de, OUTBUF
        add     hl, de
        ld      a, (ec_c)
        ld      (hl), a
        ld      hl, (out_pos)
        inc     hl
        ld      (out_pos), hl
        ld      a, h
        cp      1
        ret     nz                      ; pos < 256
        ; sector full: write it out
        ld      hl, (out_rec)
        ld      de, FDMAXREC
        or      a
        sbc     hl, de
        jp      nc, err_disk
        ld      de, (out_rec)
        ld      hl, OUTBUF
        call    C_DWRITE
        jp      c, err_file
        ld      hl, (out_rec)
        inc     hl
        ld      (out_rec), hl
        ld      hl, 0
        ld      (out_pos), hl
        ret
err_disk:
        ld      a, E_BIG
        jp      err

; out_open_file: prepare the OUT file: free an old
; entry of the same name, reserve a free entry, and
; start writing at the next-free record. DIRBUF is
; written back only by out_close_file, so an aborted
; run leaves the disk directory untouched.
out_open_file:
        call    dir_load
        ld      hl, name_out
        ld      (find_name), hl
        call    find_entry
        jr      c, oo_nf
        ld      (hl), 0                 ; free the old entry
oo_nf:
        call    find_free
        jr      nc, oo_ok
        ld      a, E_DIR
        jp      err
oo_ok:
        ld      (out_entry), hl
        ld      hl, (DIRBUF + 6)
        ld      (out_rec), hl
        ld      (out_rec0), hl
        ld      hl, 0
        ld      (out_pos), hl
        ld      (out_len), hl
        ld      (out_len + 2), hl
        ret

; out_close_file: flush the tail sector, fill the
; directory entry, advance next-free, write DIRBUF.
out_close_file:
        ld      hl, (out_pos)
        ld      a, h
        or      l
        jr      z, oc_nf
        ld      de, (out_rec)
        ld      hl, OUTBUF
        call    C_DWRITE
        jp      c, err_file
        ld      hl, (out_rec)
        inc     hl
        ld      (out_rec), hl
oc_nf:
        ld      hl, (out_len + 2)
        ld      a, h
        or      l
        jr      z, oc_szok
        ld      a, E_BIG                ; > 64 KiB of source
        jp      err
oc_szok:
        ld      hl, (out_entry)
        ld      de, name_out
        ld      b, 10
oc_nm:
        ld      a, (de)
        ld      (hl), a
        inc     hl
        inc     de
        djnz    oc_nm
        ld      de, (out_rec0)
        ld      (hl), e
        inc     hl
        ld      (hl), d
        inc     hl
        ld      de, (out_len)
        ld      (hl), e
        inc     hl
        ld      (hl), d
        inc     hl
        ld      de, 08000h              ; text load address
        ld      (hl), e
        inc     hl
        ld      (hl), d
        ld      hl, (out_rec)
        ld      (DIRBUF + 6), hl        ; advance next-free
        ld      de, DIRREC
        ld      hl, DIRBUF
        jp      C_DWRITE

; ---------------------------------------------------------------
; Label table: LABTAB holds the sorted, deduplicated
; absolute addresses that need a label. label_insert
; keeps it sorted; label_exists binary-searches it.
; ---------------------------------------------------------------

; label_exists: HL = address -> carry set if present.
; Preserves HL.
label_exists:
        push    bc
        push    de
        push    hl
        ld      hl, 0
        ld      (le_lo), hl
        ld      hl, (lab_cnt)
        ld      (le_hi), hl
le_lp:
        ld      hl, (le_hi)
        ld      de, (le_lo)
        or      a
        sbc     hl, de
        jr      z, le_nf                ; empty: not found
        ; mid = (lo + hi) / 2
        ld      hl, (le_lo)
        ld      de, (le_hi)
        add     hl, de
        srl     h
        rr      l                       ; HL = mid
        ld      (le_mid), hl
        add     hl, hl
        ld      de, LABTAB
        add     hl, de
        ld      e, (hl)
        inc     hl
        ld      d, (hl)                 ; DE = table[mid]
        pop     hl
        push    hl                      ; HL = key
        or      a
        sbc     hl, de                  ; key - table[mid]
        jr      z, le_found
        jr      c, le_left              ; key < table[mid]
        ld      hl, (le_mid)            ; lo = mid + 1
        inc     hl
        ld      (le_lo), hl
        jr      le_lp
le_left:
        ld      hl, (le_mid)            ; hi = mid
        ld      (le_hi), hl
        jr      le_lp
le_found:
        pop     hl
        pop     de
        pop     bc
        scf
        ret
le_nf:
        pop     hl
        pop     de
        pop     bc
        or      a
        ret

; label_insert: HL = address. Adds it if absent.
; Errors (E_LAB) when the table is full.
label_insert:
        push    hl
        call    label_exists
        pop     hl
        ret     c                       ; already present
        push    hl
        ld      hl, (lab_cnt)
        ld      de, LABMAX
        or      a
        sbc     hl, de
        jr      c, li_room
        ld      a, E_LAB
        jp      err
li_room:
        pop     hl
        ld      (li_key), hl
        ; find the insertion point (first entry > key)
        ld      hl, 0
        ld      (le_lo), hl
li_lp:
        ld      hl, (le_lo)
        ld      de, (lab_cnt)
        or      a
        sbc     hl, de
        jr      nc, li_at               ; pos == count
        ld      hl, (le_lo)
        add     hl, hl
        ld      de, LABTAB
        add     hl, de
        ld      e, (hl)
        inc     hl
        ld      d, (hl)                 ; DE = entry
        ld      hl, (li_key)
        or      a
        sbc     hl, de
        jr      c, li_at                ; key < entry: here
        ld      hl, (le_lo)
        inc     hl
        ld      (le_lo), hl
        jr      li_lp
li_at:
        ; shift entries [pos, cnt) up one slot
        ld      hl, (lab_cnt)
        ld      de, (le_lo)
        or      a
        sbc     hl, de                  ; HL = entries to move
        jr      z, li_store
        add     hl, hl                  ; bytes to move
        ld      b, h
        ld      c, l
        ; lddr from the block's top byte:
        ; src = LABTAB + 2*cnt - 1, dst = src + 2
        ld      hl, (lab_cnt)
        add     hl, hl
        ld      de, LABTAB - 1
        add     hl, de
        push    hl
        pop     de
        inc     de
        inc     de                      ; DE = dst, HL = src
        lddr
li_store:
        ld      a, 1
        ld      (changed), a
        ld      hl, (le_lo)
        add     hl, hl
        ld      de, LABTAB
        add     hl, de
        ld      de, (li_key)
        ld      (hl), e
        inc     hl
        ld      (hl), d
        ld      hl, (lab_cnt)
        inc     hl
        ld      (lab_cnt), hl
        ret

; ---------------------------------------------------------------
; Data ranges (DRTAB: dr_cnt entries of lo,hi LE16,
; inclusive) and the forced-DB bitset (FDBITS).
; ---------------------------------------------------------------

; in_data: HL = address -> carry set if inside a
; data range; then DE = offset just past that
; range's end (relative to src_base). Preserves HL.
in_data:
        push    hl
        push    bc
        ld      (id_addr), hl
        ld      a, (dr_cnt)
        or      a
        jr      z, id_no
        ld      b, a
        ld      hl, DRTAB
id_lp:
        ld      e, (hl)
        inc     hl
        ld      d, (hl)                 ; DE = lo
        inc     hl
        push    hl
        ld      hl, (id_addr)
        or      a
        sbc     hl, de
        pop     hl
        jr      c, id_nx                ; addr < lo
        ld      e, (hl)
        inc     hl
        ld      d, (hl)                 ; DE = hi
        inc     hl
        push    hl
        ld      hl, (id_addr)
        or      a
        sbc     hl, de
        pop     hl
        jr      c, id_hit               ; addr < hi: inside
        jr      z, id_hit               ; addr == hi: inside
        jr      id_nx2
id_nx:
        inc     hl
        inc     hl
id_nx2:
        djnz    id_lp
id_no:
        pop     bc
        pop     hl
        or      a
        ret
id_hit:
        ; DE = hi (still); end offset = hi - base + 1
        ld      hl, (src_base)
        ex      de, hl
        or      a
        sbc     hl, de
        inc     hl
        ex      de, hl                  ; DE = offset past end
        pop     bc
        pop     hl
        scf
        ret

; fd_off: HL = address -> HL = FDBITS byte pointer,
; A = bit mask for the offset's bit.
fd_off:
        ld      de, (src_base)
        or      a
        sbc     hl, de                  ; HL = offset
        ld      a, l
        and     7
        ld      (fd_bit), a
        srl     h
        rr      l
        srl     h
        rr      l
        srl     h
        rr      l                       ; HL = offset / 8
        ld      de, FDBITS
        add     hl, de
        ld      a, (fd_bit)
        ld      de, fd_masks
        add     a, e
        ld      e, a
        jr      nc, fd_nm
        inc     d
fd_nm:
        ld      a, (de)
        ret
fd_masks:
        db      001h, 002h, 004h, 008h
        db      010h, 020h, 040h, 080h

; fd_set: mark the address HL as forced-DB (sets
; changed when the bit was clear).
fd_set:
        push    hl
        call    fd_off
        push    af
        and     (hl)
        jr      nz, fd_s2
        ld      a, 1
        ld      (changed), a
fd_s2:
        pop     af
        or      (hl)
        ld      (hl), a
        pop     hl
        ret

; fd_test: HL = address -> carry set if forced-DB.
fd_test:
        push    hl
        call    fd_off
        and     (hl)
        pop     hl
        jr      nz, ft_y
        or      a
        ret
ft_y:
        scf
        ret

; ---------------------------------------------------------------
; The three passes. w_off is the current offset into
; the input range; d_addr = src_base + w_off.
; ---------------------------------------------------------------

; w_done: carry set while w_off < src_size.
w_done:
        ld      hl, (w_off)
        ld      de, (src_size)
        or      a
        sbc     hl, de
        ret

; w_addr: HL = d_addr for the current w_off.
w_addr:
        ld      hl, (src_base)
        ld      de, (w_off)
        add     hl, de
        ld      (d_addr), hl
        ret

; w_adv: advance w_off by d_len.
w_adv:
        ld      hl, (w_off)
        ld      a, (d_len)
        ld      e, a
        ld      d, 0
        add     hl, de
        ld      (w_off), hl
        ret

; w_adv1: advance w_off by 1.
w_adv1:
        ld      hl, (w_off)
        inc     hl
        ld      (w_off), hl
        ret

; w_over: carry set if the decoded instruction at
; w_off would run past the end of the range
; (off + len > size). An exact fit is not overflow.
w_over:
        ld      hl, (w_off)
        ld      a, (d_len)
        ld      e, a
        ld      d, 0
        add     hl, de                  ; HL = off + len
        ex      de, hl                  ; DE = off + len
        ld      hl, (src_size)
        or      a
        sbc     hl, de                  ; carry iff size < off+len
        ret

; pass1: collect label targets.
pass1:
        xor     a
        ld      (emit), a
        ld      hl, 0
        ld      (w_off), hl
p1_lp:
        call    w_done
        ret     nc
        call    w_addr
        call    in_data
        jr      c, p1_skip
        call    decode
        call    w_over
        jr      c, p1_adv1              ; truncated tail: step 1
        ld      a, (d_kind)
        or      a
        jr      nz, p1_adv1
        ld      a, (d_tflg)
        or      a
        jr      z, p1_adv
        ld      hl, (d_tgt)
        ld      de, (src_base)
        or      a
        sbc     hl, de
        jr      c, p1_adv               ; target < base
        ld      de, (src_size)
        or      a
        sbc     hl, de
        jr      nc, p1_adv              ; target >= end
        ld      hl, (d_tgt)
        call    label_insert
p1_adv:
        call    w_adv
        jr      p1_lp
p1_adv1:
        call    w_adv1
        jr      p1_lp
p1_skip:
        ld      (w_off), de             ; DE from in_data
        jr      p1_lp

; pass15: mark instructions that contain a label
; target in their interior as forced-DB, so pass 2
; can place the label at a byte boundary. Marking
; changes pass 2's walk (a forced-DB instruction is
; re-decoded byte by byte), and the re-decode can
; expose new control-flow targets and new containing
; instructions, so the dry walk (pass2d, the pass-2
; walk with output suppressed) repeats until a full
; walk adds neither a label nor a mark: a fixpoint.
; Only then is every referenced label guaranteed to
; sit at a walk boundary of the real pass 2.
pass15:
        ld      hl, FDBITS
        ld      de, FDBITS + 1
        ld      bc, RANGEMAX / 8 - 1
        ld      (hl), 0
        ldir
p15_lp:
        xor     a
        ld      (changed), a
        call    pass2d
        ld      a, (changed)
        or      a
        jr      nz, p15_lp
        ret

; pass2d: the pass-2 walk with output suppressed
; (dry = 1). Collects control-flow targets and
; forced-DB marks exactly as the real walk exposes
; them; label_insert / fd_set record any addition
; in changed. Used by pass15's fixpoint loop.
pass2d:
        ld      a, 1
        ld      (dry), a
        jr      p2_main
; pass2: emit the source.
pass2:
        xor     a
        ld      (dry), a
p2_main:
        ld      a, (dry)
        xor     1
        ld      (emit), a               ; emit = 1 only when real
        call    lb_reset
        ld      hl, msg_hdr
        call    pc_str
        call    emit_line
        call    lb_reset
        ld      hl, msg_org
        call    pc_str
        ld      hl, (src_base)
        call    pr_hex16
        call    emit_line
        ld      hl, 0
        ld      (w_off), hl
p2_lp:
        call    w_done
        jr      c, p2_body
        ; END line
        call    lb_reset
        ld      hl, msg_end
        call    pc_str
        call    emit_line
        ret
p2_body:
        call    w_addr
        call    in_data
        jp      c, p2_db
        ld      hl, (d_addr)
        call    fd_test
        jp      c, p2_db
        xor     a
        ld      (emit), a
        call    decode                  ; measure pass
        ld      a, (dry)
        or      a
        jr      nz, p2_norst
        ld      a, 1
        ld      (emit), a               ; restore for db_run / emit
p2_norst:
        ld      a, (d_kind)
        or      a
        jp      nz, p2_db
        call    w_over
        jp      c, p2_db                ; truncated tail as DB
        ld      a, (dry)
        or      a
        jr      nz, p2_drybody
        call    lb_reset
        call    p2_label
        call    decode                  ; emit pass
        call    emit_line
        call    w_adv
        jp      p2_lp
p2_drybody:
        ; collect an in-range target (as pass 1 does)
        ld      a, (d_tflg)
        or      a
        jr      z, p2d_mark
        ld      hl, (d_tgt)
        ld      de, (src_base)
        or      a
        sbc     hl, de
        jr      c, p2d_mark             ; target < base
        ld      de, (src_size)
        or      a
        sbc     hl, de
        jr      nc, p2d_mark            ; target >= end
        ld      hl, (d_tgt)
        call    label_insert
p2d_mark:
        ; mark when a label sits in the interior
        ld      a, (d_len)
        cp      2
        jr      c, p2d_adv              ; length 1: no interior
        ld      b, a
        dec     b
        ld      hl, (d_addr)
p2d_k:
        inc     hl
        push    hl
        push    bc
        call    label_exists
        pop     bc
        pop     hl
        jr      c, p2d_set
        djnz    p2d_k
        jr      p2d_adv
p2d_set:
        ld      hl, (d_addr)
        call    fd_set
p2d_adv:
        call    w_adv
        jp      p2_lp
p2_db:
        call    db_run
        jp      p2_lp

; p2_label: if a label exists at d_addr, print
; "Lxxxx " into the line.
p2_label:
        ld      hl, (d_addr)
        call    label_exists
        ret     nc
        ld      a, 'L'
        call    pc_ch
        ld      hl, (d_addr)
        call    pr_hex4raw
        jp      pc_sp

; dbpos: HL = address -> carry set if the byte at
; the address is emitted as DB (data range, forced,
; or a non-canonical decode). Clobbers d_*.
dbpos:
        push    hl
        call    in_data
        pop     hl
        ret     c
        push    hl
        call    fd_test
        pop     hl
        ret     c
        ld      (d_addr), hl
        xor     a
        ld      (emit), a
        call    decode
        ld      a, (dry)
        xor     1
        ld      (emit), a               ; restore (0 in dry walks)
        ld      a, (d_kind)
        or      a
        jr      nz, dbp_y
        ; truncated instruction at the range end is DB too
        ld      hl, (d_addr)
        ld      de, (src_base)
        or      a
        sbc     hl, de                  ; HL = offset of d_addr
        ld      a, (d_len)
        ld      e, a
        ld      d, 0
        add     hl, de                  ; HL = off + len
        ld      de, (src_size)
        or      a
        sbc     hl, de                  ; carry iff off+len < size
        jr      c, dbp_n                ; fits: not DB
        jr      z, dbp_n                ; exact fit: not DB
dbp_y:
        scf                             ; overruns the end: DB
        ret
dbp_n:
        or      a
        ret

; db_run: emit one DB line (up to 8 bytes) starting
; at the current w_off, breaking at label addresses
; and at the end of the DB region.
db_run:
        call    lb_reset
        call    p2_label
        ld      hl, msg_db
        call    pc_str
        ld      b, 8                    ; bytes left on this line
db_lp:
        ld      hl, (d_addr)
        call    getb
        call    pr_hex8
        call    w_adv1
        dec     b
        jr      z, db_dn
        call    w_done
        jr      nc, db_dn               ; end of range
        call    w_addr
        ld      hl, (d_addr)
        call    label_exists
        jr      c, db_dn                ; label starts a new line
        ld      hl, (d_addr)
        call    dbpos
        jr      nc, db_dn               ; canonical code resumes
        call    pc_comma
        jr      db_lp
db_dn:
        call    emit_line
        ret

; ---------------------------------------------------------------
; Number and operand formatting (all via pc_*).
; Hex style: digits + 'H', with a leading '0' when
; the top nibble exceeds 9, so the text re-assembles
; as a number (04000H, 0FFH, 0AH). Labels print as
; 'L' + 4 raw hex digits.
; ---------------------------------------------------------------

; pr_nib: print A (low nibble) as one hex digit.
pr_nib:
        and     00fh
        cp      10
        jr      c, pn_n
        add     a, 'A' - 10
        jp      pc_ch
pn_n:
        add     a, '0'
        jp      pc_ch

; pr_b2: print A as two hex digits.
pr_b2:
        push    af
        rrca
        rrca
        rrca
        rrca
        call    pr_nib
        pop     af
        jp      pr_nib

; pr_hex4raw: print HL as 4 hex digits (no suffix).
pr_hex4raw:
        ld      a, h
        call    pr_b2
        ld      a, l
        jp      pr_b2

; pr_hex8: print A as a hex byte (0XXH style).
pr_hex8:
        push    af
        and     0f0h
        rrca
        rrca
        rrca
        rrca
        cp      10
        jr      c, ph8_ok
        push    af
        ld      a, '0'
        call    pc_ch
        pop     af
ph8_ok:
        pop     af
        call    pr_b2
        ld      a, 'H'
        jp      pc_ch

; pr_hex16: print HL as a hex word (0XXXXH style).
pr_hex16:
        push    hl
        ld      a, h
        and     0f0h
        rrca
        rrca
        rrca
        rrca
        cp      10
        jr      c, ph16_ok
        ld      a, '0'
        call    pc_ch
ph16_ok:
        pop     hl
        call    pr_hex4raw
        ld      a, 'H'
        jp      pc_ch

; pr_addr: print the address in HL as a label when
; one exists for it, else as a hex word.
pr_addr:
        push    hl
        call    label_exists
        pop     hl
        jr      nc, pr_hex16
        push    hl
        ld      a, 'L'
        call    pc_ch
        pop     hl
        jp      pr_hex4raw

; pr_udec: print A as unsigned decimal (0..255),
; leading zeros suppressed.
pr_udec:
        ld      (ud_v), a
        xor     a
        ld      (ud_lead), a
        ld      a, (ud_v)
        ld      b, 100
        call    ud_one
        ld      b, 10
        call    ud_one
        ld      a, (ud_v)
        add     a, '0'
        jp      pc_ch
ud_one:
        ld      a, (ud_v)
        ld      e, 0                    ; digit value
uo_lp:
        cp      b
        jr      c, uo_dn
        sub     b
        ld      (ud_v), a
        inc     e
        jr      uo_lp
uo_dn:
        ld      a, e
        or      a
        jr      nz, uo_pr
        ld      a, (ud_lead)
        or      a
        ret     z                       ; still leading: skip
        xor     a                       ; middle zero: print '0'
uo_pr:
        push    af
        ld      a, 1
        ld      (ud_lead), a
        pop     af
        add     a, '0'
        jp      pc_ch

; pr_disp: print A as a signed displacement suffix
; ("+5", "-3", "+0").
pr_disp:
        or      a
        jp      p, pdp_pos
        push    af
        ld      a, '-'
        call    pc_ch
        pop     af
        neg
        jp      pr_udec
pdp_pos:
        push    af
        ld      a, '+'
        call    pc_ch
        pop     af
        jp      pr_udec

; pr_ixy: print "IX" or "IY" per d_pfx.
pr_ixy:
        ld      a, 'I'
        call    pc_ch
        ld      a, (d_pfx)
        cp      0fdh
        jr      z, pr_y
        ld      a, 'X'
        jp      pc_ch
pr_y:
        ld      a, 'Y'
        jp      pc_ch

; pr_reg: print register r = A (0..7). With a DD/FD
; prefix, r=6 prints (IX+d)/(IY+d) and r=4/5 print
; IXH/IXL forms (callers reject those as data before
; printing, except when d_plain marks the plain H/L
; of an (IX+d) LD).
pr_reg:
        cp      6
        jr      z, pr_r6
        ld      (pr_r), a
        ld      a, (d_plain)
        or      a
        jr      nz, pr_rn
        ld      a, (d_pfx)
        or      a
        jr      z, pr_rn
        ld      a, (pr_r)
        cp      4
        jr      z, pr_ixh
        cp      5
        jr      z, pr_ixl
pr_rn:
        ld      a, (pr_r)
        ld      hl, regtab
        add     a, l
        ld      l, a
        jr      nc, pr_rn2
        inc     h
pr_rn2:
        ld      a, (hl)
        jp      pc_ch
pr_ixh:
        call    pr_ixy
        ld      a, 'H'
        jp      pc_ch
pr_ixl:
        call    pr_ixy
        ld      a, 'L'
        jp      pc_ch
pr_r6:
        ld      a, (d_pfx)
        or      a
        jr      nz, pr_r6p
        ld      hl, msg_phl
        jp      pc_str
pr_r6p:
        ld      a, '('
        call    pc_ch
        call    pr_ixy
        ld      a, (d_disp)
        call    pr_disp
        ld      a, ')'
        jp      pc_ch
regtab:
        db      'B', 'C', 'D', 'E', 'H', 'L', 0, 'A'

; pr_rp: print register pair p = A (0..3):
; BC / DE / HL-or-IX / SP.
pr_rp:
        cp      2
        jr      z, pr_rp2
pr_rp_tab:                              ; print pair A from rp_tab
        add     a, a
        ld      hl, rp_tab
        add     a, l
        ld      l, a
        jr      nc, pr_rp3
        inc     h
pr_rp3:
        ld      a, (hl)
        call    pc_ch
        inc     hl
        ld      a, (hl)
        jp      pc_ch
pr_rp2:
        ld      a, (d_pfx)
        or      a
        jp      nz, pr_ixy
        ld      a, 2
        jr      pr_rp_tab
rp_tab:
        db      'B', 'C', 'D', 'E', 'H', 'L', 'S', 'P'

; pr_rp2f: print pair p = A for PUSH/POP (p=3 is AF).
pr_rp2f:
        cp      3
        jr      z, pr_af
        jp      pr_rp
pr_af:
        ld      hl, msg_af
        jp      pc_str

; pr_cc: print condition y = A (0..7).
pr_cc:
        add     a, a
        ld      hl, cc_tab
        add     a, l
        ld      l, a
        jr      nc, pr_cc2
        inc     h
pr_cc2:
        ld      a, (hl)
        call    pc_ch
        inc     hl
        ld      a, (hl)
        cp      ' '
        ret     z
        jp      pc_ch
cc_tab:
        db      'N', 'Z', 'Z', ' ', 'N', 'C', 'C', ' '
        db      'P', 'O', 'P', 'E', 'P', ' ', 'M', ' '

; pr_tab: print string number A from the pointer
; table at HL.
pr_tab:
        add     a, a
        ld      e, a
        ld      d, 0
        add     hl, de
        ld      e, (hl)
        inc     hl
        ld      d, (hl)
        ex      de, hl
        jp      pc_str

; ---------------------------------------------------------------
; The decoder. decode examines the instruction at
; d_addr and fills d_len (total length including any
; prefix), d_kind (0 = canonical instruction,
; 1 = data byte) and d_tgt / d_tflg (control-flow
; target). With emit = 1 it also formats the
; instruction text via pc_*. Pass 2 calls decode
; with emit = 1 only for instructions a measure pass
; already found canonical, so a set_data path never
; fires mid-line.
; ---------------------------------------------------------------

; set_data: the byte at d_addr is data (length 1).
; Used for every encoding whose mnemonic form would
; not re-assemble to the same bytes.
set_data:
        ld      a, 1
        ld      (d_kind), a
        ld      (d_len), a
        ret

; fin: finish a canonical instruction; A = length
; relative to d_opaddr.
fin:
        ld      (d_rlen), a
        ld      hl, (d_opaddr)
        ld      de, (d_addr)
        or      a
        sbc     hl, de
        ld      a, (d_rlen)
        add     a, l
        ld      (d_len), a
        xor     a
        ld      (d_kind), a
        ret

; pfx_chk: instructions that take no DD/FD prefix
; are data when one is present.
pfx_chk:
        ld      a, (d_pfx)
        or      a
        ret     z
        pop     hl                      ; discard return address
        jp      set_data

; dob1/dob2/dob3: operand bytes after the opcode
; byte at d_opaddr.
dob1:
        ld      hl, (d_opaddr)
        inc     hl
        jp      getb
dob2:
        ld      hl, (d_opaddr)
        inc     hl
        inc     hl
        jp      getb
dob3:
        ld      hl, (d_opaddr)
        inc     hl
        inc     hl
        inc     hl
        jp      getb

; dw_op: HL = 16-bit operand at d_opaddr+1.
dw_op:
        call    dob1
        ld      (dw_lo), a
        call    dob2
        ld      h, a
        ld      a, (dw_lo)
        ld      l, a
        ret

; fields: split d_op into d_x / d_y / d_z / d_p / d_q.
fields:
        ld      a, (d_op)
        and     007h
        ld      (d_z), a
        ld      a, (d_op)
        rrca
        rrca
        rrca
        and     007h
        ld      (d_y), a
        and     001h
        ld      (d_q), a
        ld      a, (d_y)
        rrca
        and     003h
        ld      (d_p), a
        ld      a, (d_op)
        rlca
        rlca
        and     003h
        ld      (d_x), a
        ret

; rel_set: d_tgt = address after the instruction +
; signed dob1; sets d_tflg.
rel_set:
        call    dob1
        ld      e, a
        ld      d, 0
        bit     7, e
        jr      z, rg_pos
        dec     d
rg_pos:
        ld      hl, (d_opaddr)
        inc     hl
        inc     hl
        add     hl, de
        ld      (d_tgt), hl
        ld      a, 1
        ld      (d_tflg), a
        ret

; abs_set: d_tgt = dw_op; sets d_tflg.
abs_set:
        call    dw_op
        ld      (d_tgt), hl
        ld      a, 1
        ld      (d_tflg), a
        ret

; pr_tgt: print d_tgt (label when one exists).
pr_tgt:
        ld      hl, (d_tgt)
        jp      pr_addr

; decode: entry point.
decode:
        xor     a
        ld      (d_pfx), a
        ld      (d_tflg), a
        ld      (d_plain), a
        ld      hl, (d_addr)
        ld      (d_opaddr), hl
        call    getb
        cp      0ddh
        jr      z, dc_pfx
        cp      0fdh
        jr      z, dc_pfx
        ld      (d_op), a
        jr      dec_base
dc_pfx:
        ld      (d_pfx), a
        ld      hl, (d_addr)
        inc     hl
        ld      (d_opaddr), hl
        call    getb
        cp      0cbh
        jr      z, dc_ddcb
        cp      0ddh
        jp      z, set_data             ; doubled prefix
        cp      0fdh
        jp      z, set_data
        cp      0edh
        jp      z, set_data             ; prefix before ED
        ld      (d_op), a
        jr      dec_base
dc_ddcb:
        ld      hl, (d_opaddr)
        inc     hl
        call    getb
        ld      (d_disp), a
        inc     hl
        call    getb
        ld      (d_op), a
        jp      dec_ddcb

; dec_base: decode d_op as a base-map opcode
; (d_pfx may hold a DD/FD prefix).
dec_base:
        ld      a, (d_op)
        cp      0cbh
        jr      z, dec_cb_entry
        cp      0edh
        jr      z, dec_ed_entry
        ld      a, (d_pfx)
        or      a
        jr      z, db_f
        call    dob1                    ; displacement byte
        ld      (d_disp), a
db_f:
        call    fields
        ld      a, (d_x)
        or      a
        jp      z, dx0
        cp      1
        jp      z, dx1
        cp      2
        jp      z, dx2
        jp      dx3
dec_cb_entry:
        ld      hl, (d_opaddr)
        inc     hl
        call    getb
        ld      (d_op), a
        jp      dec_cb
dec_ed_entry:
        ld      hl, (d_opaddr)
        inc     hl
        call    getb
        ld      (d_op2), a
        jp      dec_ed

; ---- x = 0 ----
dx0:
        ld      a, (d_z)
        or      a
        jr      z, dx0z0
        cp      1
        jp      z, dx0z1
        cp      2
        jp      z, dx0z2
        cp      3
        jp      z, dx0z3
        cp      4
        jp      z, dx0z45
        cp      5
        jp      z, dx0z45
        cp      6
        jp      z, dx0z6
        jp      dx0z7
dx0z0:
        call    pfx_chk
        ld      a, (d_y)
        or      a
        jr      z, dx0_nop
        cp      1
        jr      z, dx0_exaf
        cp      2
        jr      z, dx0_djnz
        cp      3
        jr      z, dx0_jr
        ld      hl, msg_jr              ; JR cc,e
        call    pc_str
        ld      a, (d_y)
        sub     4
        call    pr_cc
        call    pc_comma
        call    rel_set
        call    pr_tgt
        ld      a, 2
        jp      fin
dx0_nop:
        ld      hl, msg_nop
        call    pc_str
        ld      a, 1
        jp      fin
dx0_exaf:
        ld      hl, msg_exaf
        call    pc_str
        ld      a, 1
        jp      fin
dx0_djnz:
        ld      hl, msg_djnz
        call    pc_str
        call    rel_set
        call    pr_tgt
        ld      a, 2
        jp      fin
dx0_jr:
        ld      hl, msg_jr
        call    pc_str
        call    rel_set
        call    pr_tgt
        ld      a, 2
        jp      fin
dx0z1:
        ld      a, (d_q)
        or      a
        jr      z, dx0z1_ld
        ld      hl, msg_add             ; ADD HL,rp
        call    pc_str
        ld      a, 2
        call    pr_rp
        call    pc_comma
        ld      a, (d_p)
        call    pr_rp
        ld      a, 1
        jp      fin
dx0z1_ld:                               ; LD rp,nn
        ld      a, (d_p)
        cp      2
        jr      z, d1l_ok               ; only HL/IX consumes a prefix
        call    pfx_chk
d1l_ok:
        ld      hl, msg_ld
        call    pc_str
        ld      a, (d_p)
        call    pr_rp
        call    pc_comma
        call    dw_op
        call    pr_hex16
        ld      a, 3
        jp      fin
dx0z2:
        ld      a, (d_q)
        or      a
        jr      z, dx0z2_st
        ld      a, (d_p)
        cp      2
        jr      z, dx0z2_ldrr
        cp      3
        jr      z, dx0z2_lda
        call    pfx_chk                 ; LD A,(BC)/(DE) takes no prefix
        ld      hl, msg_ld              ; LD A,(BC)/(DE)
        call    pc_str
        ld      a, 'A'
        call    pc_ch
        call    pc_comma
        ld      a, '('
        call    pc_ch
        ld      a, (d_p)
        call    pr_rp
        ld      a, ')'
        call    pc_ch
        ld      a, 1
        jp      fin
dx0z2_ldrr:                             ; LD rp,(nn)
        ld      hl, msg_ld
        call    pc_str
        ld      a, 2
        call    pr_rp
        call    pc_comma
        ld      a, '('
        call    pc_ch
        call    dw_op
        call    pr_addr
        ld      a, ')'
        call    pc_ch
        ld      a, 3
        jp      fin
dx0z2_lda:                              ; LD A,(nn)
        call    pfx_chk
        ld      hl, msg_ld
        call    pc_str
        ld      a, 'A'
        call    pc_ch
        call    pc_comma
        ld      a, '('
        call    pc_ch
        call    dw_op
        call    pr_addr
        ld      a, ')'
        call    pc_ch
        ld      a, 3
        jp      fin
dx0z2_st:
        ld      a, (d_p)
        cp      2
        jr      z, dx0z2_strr
        cp      3
        jr      z, dx0z2_sta
        call    pfx_chk                 ; LD (BC)/(DE),A takes no prefix
        ld      hl, msg_ld              ; LD (BC)/(DE),A
        call    pc_str
        ld      a, '('
        call    pc_ch
        ld      a, (d_p)
        call    pr_rp
        ld      a, ')'
        call    pc_ch
        call    pc_comma
        ld      a, 'A'
        call    pc_ch
        ld      a, 1
        jp      fin
dx0z2_strr:                             ; LD (nn),rp
        ld      hl, msg_ld
        call    pc_str
        ld      a, '('
        call    pc_ch
        call    dw_op
        call    pr_addr
        ld      a, ')'
        call    pc_ch
        call    pc_comma
        ld      a, 2
        call    pr_rp
        ld      a, 3
        jp      fin
dx0z2_sta:                              ; LD (nn),A
        call    pfx_chk
        ld      hl, msg_ld
        call    pc_str
        ld      a, '('
        call    pc_ch
        call    dw_op
        call    pr_addr
        ld      a, ')'
        call    pc_ch
        call    pc_comma
        ld      a, 'A'
        call    pc_ch
        ld      a, 3
        jp      fin
dx0z3:
        ld      a, (d_p)
        cp      2
        jr      z, dz3_ok               ; only HL/IX consumes a prefix
        call    pfx_chk
dz3_ok:
        ld      a, (d_q)
        or      a
        jr      z, dx0z3_inc
        ld      hl, msg_dec
        jr      dx0z3_c
dx0z3_inc:
        ld      hl, msg_inc
dx0z3_c:
        call    pc_str
        ld      a, (d_p)
        call    pr_rp
        ld      a, 1
        jp      fin
dx0z45:                                 ; INC r / DEC r
        ld      a, (d_pfx)
        or      a
        jr      z, d45_go
        ld      a, (d_y)
        cp      6
        jr      z, d45_go               ; only (IX+d) consumes a prefix
        jp      set_data                ; plain r / IXH / IXL: DB
d45_go:
        ld      a, (d_z)
        cp      4
        jr      z, d45_inc
        ld      hl, msg_dec
        jr      d45_s
d45_inc:
        ld      hl, msg_inc
d45_s:
        call    pc_str
        ld      a, (d_y)
        call    pr_reg
        ld      a, (d_pfx)
        or      a
        jr      z, d45_l1
        ld      a, (d_y)
        cp      6
        jr      z, d45_l2
d45_l1:
        ld      a, 1
        jp      fin
d45_l2:
        ld      a, 2
        jp      fin
dx0z6:                                  ; LD r,n
        ld      a, (d_pfx)
        or      a
        jr      z, d6_go
        ld      a, (d_y)
        cp      6
        jr      z, d6_ix                ; only (IX+d) consumes a prefix
        jp      set_data                ; plain r / IXH / IXL: DB
d6_go:
        ld      hl, msg_ld
        call    pc_str
        ld      a, (d_y)
        call    pr_reg
        call    pc_comma
        call    dob1
        call    pr_hex8
        ld      a, 2
        jp      fin
d6_ix:                                  ; LD (IX+d),n
        ld      hl, msg_ld
        call    pc_str
        ld      a, 6
        call    pr_reg
        call    pc_comma
        call    dob2
        call    pr_hex8
        ld      a, 3
        jp      fin
dx0z7:
        call    pfx_chk
        ld      a, (d_y)
        ld      hl, z7_ptrs
        call    pr_tab
        ld      a, 1
        jp      fin

; ---- x = 1: LD r,r' / HALT ----
dx1:
        ld      a, (d_y)
        cp      6
        jr      z, dx1_y6
        ld      a, (d_z)
        cp      6
        jr      z, dx1_z6
        ld      a, (d_pfx)
        or      a
        jr      z, dx1_go
        ld      a, (d_y)                ; IXH/IXL involved?
        cp      4
        jp      z, set_data
        cp      5
        jp      z, set_data
        ld      a, (d_z)
        cp      4
        jp      z, set_data
        cp      5
        jp      z, set_data
        call    pfx_chk                 ; plain r,r' takes no prefix
dx1_go:
        ld      hl, msg_ld
        call    pc_str
        ld      a, (d_y)
        call    pr_reg
        call    pc_comma
        ld      a, (d_z)
        call    pr_reg
        ld      a, 1
        jp      fin
dx1_y6:
        ld      a, (d_z)
        cp      6
        jr      z, dx1_halt
        jr      dx1_mem
dx1_z6:
dx1_mem:                                ; one operand is (HL)/(IX+d)
        ld      a, (d_pfx)
        or      a
        jr      z, dx1_go               ; plain (HL), length 1
        ld      a, 1
        ld      (d_plain), a            ; other reg prints plain H/L
        ld      hl, msg_ld
        call    pc_str
        ld      a, (d_y)
        call    pr_reg
        call    pc_comma
        ld      a, (d_z)
        call    pr_reg
        ld      a, 2
        jp      fin
dx1_halt:
        call    pfx_chk                 ; DD 76 is data
        ld      hl, msg_halt
        call    pc_str
        ld      a, 1
        jp      fin

; ---- x = 2: ALU A,r ----
dx2:
        ld      a, (d_pfx)
        or      a
        jr      z, dx2_go
        ld      a, (d_z)
        cp      4
        jp      z, set_data
        cp      5
        jp      z, set_data
dx2_go:
        ld      a, (d_pfx)
        or      a
        jr      z, dx2_pr
        ld      a, (d_z)
        cp      6
        jp      nz, set_data            ; only (IX+d) consumes a prefix
dx2_pr:
        ld      a, (d_y)
        ld      hl, alu_ptrs
        call    pr_tab
        ld      a, (d_z)
        call    pr_reg
        ld      a, (d_pfx)
        or      a
        jr      z, dx2_l1
        ld      a, (d_z)
        cp      6
        jr      z, dx2_l2
dx2_l1:
        ld      a, 1
        jp      fin
dx2_l2:
        ld      a, 2
        jp      fin

; ---- x = 3 ----
dx3:
        ld      a, (d_z)
        or      a
        jp      z, dx3z0
        cp      1
        jp      z, dx3z1
        cp      2
        jp      z, dx3z2
        cp      3
        jp      z, dx3z3
        cp      4
        jp      z, dx3z4
        cp      5
        jp      z, dx3z5
        cp      6
        jp      z, dx3z6
        jp      dx3z7
dx3z0:                                  ; RET cc
        call    pfx_chk
        ld      hl, msg_ret
        call    pc_str
        call    pc_sp
        ld      a, (d_y)
        call    pr_cc
        ld      a, 1
        jp      fin
dx3z1:
        ld      a, (d_q)
        or      a
        jr      z, dx3z1_pop
        ld      a, (d_p)
        or      a
        jr      z, dx3z1_ret
        cp      1
        jr      z, dx3z1_exx
        cp      2
        jr      z, dx3z1_jphl
        ld      hl, msg_ld              ; LD SP,HL/IX
        call    pc_str
        ld      hl, msg_sp
        call    pc_str
        call    pc_comma
        ld      a, 2
        call    pr_rp
        ld      a, 1
        jp      fin
dx3z1_ret:
        call    pfx_chk
        ld      hl, msg_ret
        call    pc_str
        ld      a, 1
        jp      fin
dx3z1_exx:
        call    pfx_chk
        ld      hl, msg_exx
        call    pc_str
        ld      a, 1
        jp      fin
dx3z1_jphl:                             ; JP (HL) / JP (IX)
        ld      hl, msg_jp
        call    pc_str
        ld      a, '('
        call    pc_ch
        ld      a, (d_pfx)
        or      a
        jr      z, dx3z1_hl
        call    pr_ixy
        jr      dx3z1_c
dx3z1_hl:
        ld      hl, msg_hl2
        call    pc_str
dx3z1_c:
        ld      a, ')'
        call    pc_ch
        ld      a, 1
        jp      fin
dx3z1_pop:
        ld      a, (d_p)
        cp      2
        jr      z, dz1p_ok              ; only HL/IX consumes a prefix
        call    pfx_chk
dz1p_ok:
        ld      hl, msg_pop
        call    pc_str
        ld      a, (d_p)
        call    pr_rp2f
        ld      a, 1
        jp      fin
dx3z2:                                  ; JP cc,nn
        call    pfx_chk
        ld      hl, msg_jp
        call    pc_str
        ld      a, (d_y)
        call    pr_cc
        call    pc_comma
        call    abs_set
        call    pr_tgt
        ld      a, 3
        jp      fin
dx3z3:
        ld      a, (d_y)
        or      a
        jp      z, dx3z3_jp
        cp      2
        jp      z, dx3z3_out
        cp      3
        jp      z, dx3z3_in
        cp      4
        jp      z, dx3z3_exsp
        cp      5
        jp      z, dx3z3_exde
        cp      6
        jp      z, dx3z3_di
        cp      7
        jp      z, dx3z3_ei
        jp      set_data                ; y=1 is CB (intercepted)
dx3z3_jp:
        call    pfx_chk
        ld      hl, msg_jp
        call    pc_str
        call    abs_set
        call    pr_tgt
        ld      a, 3
        jp      fin
dx3z3_out:                              ; OUT (n),A
        call    pfx_chk
        ld      hl, msg_out
        call    pc_str
        ld      a, '('
        call    pc_ch
        call    dob1
        call    pr_hex8
        ld      a, ')'
        call    pc_ch
        call    pc_comma
        ld      a, 'A'
        call    pc_ch
        ld      a, 2
        jp      fin
dx3z3_in:                               ; IN A,(n)
        call    pfx_chk
        ld      hl, msg_in
        call    pc_str
        ld      a, 'A'
        call    pc_ch
        call    pc_comma
        ld      a, '('
        call    pc_ch
        call    dob1
        call    pr_hex8
        ld      a, ')'
        call    pc_ch
        ld      a, 2
        jp      fin
dx3z3_exsp:                             ; EX (SP),HL/IX
        ld      hl, msg_exsp
        call    pc_str
        ld      a, 2
        call    pr_rp
        ld      a, 1
        jp      fin
dx3z3_exde:
        call    pfx_chk
        ld      hl, msg_exde
        call    pc_str
        ld      a, 1
        jp      fin
dx3z3_di:
        call    pfx_chk
        ld      hl, msg_di
        call    pc_str
        ld      a, 1
        jp      fin
dx3z3_ei:
        call    pfx_chk
        ld      hl, msg_ei
        call    pc_str
        ld      a, 1
        jp      fin
dx3z4:                                  ; CALL cc,nn
        call    pfx_chk
        ld      hl, msg_call
        call    pc_str
        ld      a, (d_y)
        call    pr_cc
        call    pc_comma
        call    abs_set
        call    pr_tgt
        ld      a, 3
        jp      fin
dx3z5:
        ld      a, (d_q)
        or      a
        jr      z, dx3z5_push
        call    pfx_chk                 ; CALL nn
        ld      hl, msg_call
        call    pc_str
        call    abs_set
        call    pr_tgt
        ld      a, 3
        jp      fin
dx3z5_push:
        ld      a, (d_p)
        cp      2
        jr      z, dz5p_ok              ; only HL/IX consumes a prefix
        call    pfx_chk
dz5p_ok:
        ld      hl, msg_push
        call    pc_str
        ld      a, (d_p)
        call    pr_rp2f
        ld      a, 1
        jp      fin
dx3z6:                                  ; ALU A,n
        call    pfx_chk
        ld      a, (d_y)
        ld      hl, alu_ptrs
        call    pr_tab
        call    dob1
        call    pr_hex8
        ld      a, 2
        jp      fin
dx3z7:                                  ; RST y*8
        call    pfx_chk
        ld      hl, msg_rst
        call    pc_str
        ld      a, (d_y)
        add     a, a
        add     a, a
        add     a, a
        call    pr_hex8
        ld      a, 1
        jp      fin

; ---- CB map ----
dec_cb:
        call    fields
        ld      a, (d_x)
        or      a
        jr      z, dcb_rot
        cp      1
        jr      z, dcb_bit
        cp      2
        jr      z, dcb_res
        ld      hl, msg_set
        jr      dcb_r23
dcb_res:
        ld      hl, msg_res
dcb_r23:
        call    pc_str
        ld      a, (d_y)
        call    pr_udec
        call    pc_comma
        ld      a, (d_z)
        call    pr_reg
        ld      a, 2
        jp      fin
dcb_bit:
        ld      hl, msg_bit
        call    pc_str
        ld      a, (d_y)
        call    pr_udec
        call    pc_comma
        ld      a, (d_z)
        call    pr_reg
        ld      a, 2
        jp      fin
dcb_rot:
        ld      a, (d_y)
        cp      6
        jp      z, set_data             ; SLL
        ld      hl, rot_ptrs
        call    pr_tab
        call    pc_sp
        ld      a, (d_z)
        call    pr_reg
        ld      a, 2
        jp      fin

; ---- DDCB / FDCB map ----
dec_ddcb:
        call    fields
        ld      a, (d_x)
        or      a
        jr      z, ddc_rot
        cp      1
        jr      z, ddc_bit
        cp      2
        jr      z, ddc_res
        ld      hl, msg_set
        jr      ddc_r23
ddc_res:
        ld      hl, msg_res
ddc_r23:
        ld      a, (d_z)
        cp      6
        jp      nz, set_data            ; copy form
        call    pc_str
        ld      a, (d_y)
        call    pr_udec
        call    pc_comma
        ld      a, 6
        call    pr_reg
        ld      a, 3
        jp      fin
ddc_bit:
        ld      a, (d_z)
        cp      6
        jp      nz, set_data
        ld      hl, msg_bit
        call    pc_str
        ld      a, (d_y)
        call    pr_udec
        call    pc_comma
        ld      a, 6
        call    pr_reg
        ld      a, 3
        jp      fin
ddc_rot:
        ld      a, (d_y)
        cp      6
        jp      z, set_data             ; SLL
        ld      a, (d_z)
        cp      6
        jp      nz, set_data            ; copy form
        ld      a, (d_y)
        ld      hl, rot_ptrs
        call    pr_tab
        call    pc_sp
        ld      a, 6
        call    pr_reg
        ld      a, 3
        jp      fin

; ---- ED map ----
dec_ed:
        ld      a, (d_op2)
        ld      (d_op), a
        call    fields
        ld      a, (d_op2)
        cp      040h
        jp      c, set_data
        ld      a, (d_x)
        cp      1
        jr      z, ded1
        cp      2
        jr      z, ded2
        jp      set_data                ; x = 0 or 3
ded2:                                   ; block instructions
        ld      a, (d_z)
        cp      4
        jp      nc, set_data
        ld      a, (d_y)
        cp      4
        jp      c, set_data
        sub     4
        add     a, a
        add     a, a
        ld      b, a
        ld      a, (d_z)
        add     a, b
        ld      hl, blk_ptrs
        call    pr_tab
        ld      a, 2
        jp      fin
ded1:
        ld      a, (d_z)
        or      a
        jp      z, ded_z0
        cp      1
        jp      z, ded_z1
        cp      2
        jp      z, ded_z2
        cp      3
        jp      z, ded_z3
        cp      4
        jp      z, ded_z4
        cp      5
        jp      z, ded_z5
        cp      6
        jp      z, ded_z6
        jp      ded_z7
ded_z0:                                 ; IN r,(C)
        ld      a, (d_y)
        cp      6
        jp      z, set_data             ; IN (C): flags only
        ld      hl, msg_in
        call    pc_str
        ld      a, (d_y)
        call    pr_reg
        call    pc_comma
        ld      hl, msg_pc
        call    pc_str
        ld      a, 2
        jp      fin
ded_z1:                                 ; OUT (C),r
        ld      a, (d_y)
        cp      6
        jp      z, set_data             ; OUT (C),0
        ld      hl, msg_out
        call    pc_str
        ld      hl, msg_pc
        call    pc_str
        call    pc_comma
        ld      a, (d_y)
        call    pr_reg
        ld      a, 2
        jp      fin
ded_z2:                                 ; SBC / ADC HL,rp
        ld      a, (d_q)
        or      a
        jr      z, ded_z2s
        ld      hl, msg_adc
        jr      ded_z2c
ded_z2s:
        ld      hl, msg_sbc
ded_z2c:
        call    pc_str
        ld      a, (d_p)
        call    pr_rp
        ld      a, 2
        jp      fin
ded_z3:                                 ; LD (nn),rp / LD rp,(nn)
        ld      a, (d_op2)
        cp      063h
        jp      z, set_data             ; alternate encoding
        cp      06bh
        jp      z, set_data             ; alternate encoding
        ld      a, (d_q)
        or      a
        jr      z, ded_z3st
        ld      hl, msg_ld
        call    pc_str
        ld      a, (d_p)
        call    pr_rp
        call    pc_comma
        ld      a, '('
        call    pc_ch
        call    ded_nn
        call    pr_addr
        ld      a, ')'
        call    pc_ch
        ld      a, 4
        jp      fin
ded_z3st:
        ld      hl, msg_ld
        call    pc_str
        ld      a, '('
        call    pc_ch
        call    ded_nn
        call    pr_addr
        ld      a, ')'
        call    pc_ch
        call    pc_comma
        ld      a, (d_p)
        call    pr_rp
        ld      a, 4
        jp      fin
ded_nn:                                 ; HL = word at d_opaddr+2
        call    dob2
        ld      (dw_lo), a
        call    dob3
        ld      h, a
        ld      a, (dw_lo)
        ld      l, a
        ret
ded_z4:                                 ; NEG (canonical ED 44 only)
        ld      a, (d_op2)
        cp      044h
        jp      nz, set_data
        ld      hl, msg_neg
        call    pc_str
        ld      a, 2
        jp      fin
ded_z5:                                 ; RETN / RETI
        ld      a, (d_op2)
        cp      045h
        jr      z, ded_retn
        cp      04dh
        jr      z, ded_reti
        jp      set_data
ded_retn:
        ld      hl, msg_retn
        call    pc_str
        ld      a, 2
        jp      fin
ded_reti:
        ld      hl, msg_reti
        call    pc_str
        ld      a, 2
        jp      fin
ded_z6:                                 ; IM 0 / 1 / 2
        ld      a, (d_y)
        or      a
        jr      z, ded_im0
        cp      2
        jr      z, ded_im1
        cp      3
        jr      z, ded_im2
        jp      set_data
ded_im0:
        ld      hl, msg_im0
        jr      ded_imc
ded_im1:
        ld      hl, msg_im1
        jr      ded_imc
ded_im2:
        ld      hl, msg_im2
ded_imc:
        call    pc_str
        ld      a, 2
        jp      fin
ded_z7:
        ld      a, (d_y)
        or      a
        jr      z, ded_ldia
        cp      1
        jr      z, ded_ldra
        cp      2
        jr      z, ded_ldai
        cp      3
        jr      z, ded_ldar
        cp      4
        jr      z, ded_rrd
        cp      5
        jr      z, ded_rld
        jp      set_data
ded_ldia:
        ld      hl, msg_ldia
        jr      ded_z7c
ded_ldra:
        ld      hl, msg_ldra
        jr      ded_z7c
ded_ldai:
        ld      hl, msg_ldai
        jr      ded_z7c
ded_ldar:
        ld      hl, msg_ldar
        jr      ded_z7c
ded_rrd:
        ld      hl, msg_rrd
        jr      ded_z7c
ded_rld:
        ld      hl, msg_rld
ded_z7c:
        call    pc_str
        ld      a, 2
        jp      fin

; ---------------------------------------------------------------
; Strings and tables
; ---------------------------------------------------------------
msg_banner:     db      "DISASM - Z80 DISASSEMBLER", 13, 10, 0
msg_srcp:       db      "SRC: ", 0
msg_basep:      db      "BASE: ", 0
msg_fromp:      db      "FROM: ", 0
msg_top:        db      "TO: ", 0
msg_datap:      db      "DATA: ", 0
msg_outp:       db      "OUT: ", 0
msg_err:        db      "ERR ", 0
msg_ok:         db      "OK ", 0
msg_bytes:      db      " BYTES", 13, 10, 0
msg_hdr:        db      "; DISASM OUTPUT", 0
msg_org:        db      "ORG ", 0
msg_end:        db      "END", 0
msg_db:         db      "DB ", 0

msg_nop:        db      "NOP", 0
msg_exaf:       db      "EX AF,AF'", 0
msg_djnz:       db      "DJNZ ", 0
msg_jr:         db      "JR ", 0
msg_ld:         db      "LD ", 0
msg_add:        db      "ADD ", 0
msg_inc:        db      "INC ", 0
msg_dec:        db      "DEC ", 0
msg_ret:        db      "RET", 0
msg_exx:        db      "EXX", 0
msg_jp:         db      "JP ", 0
msg_halt:       db      "HALT", 0
msg_pop:        db      "POP ", 0
msg_push:       db      "PUSH ", 0
msg_call:       db      "CALL ", 0
msg_rst:        db      "RST ", 0
msg_in:         db      "IN ", 0
msg_out:        db      "OUT ", 0
msg_exsp:       db      "EX (SP),", 0
msg_exde:       db      "EX DE,HL", 0
msg_di:         db      "DI", 0
msg_ei:         db      "EI", 0
msg_sp:         db      "SP", 0
msg_hl2:        db      "HL", 0
msg_phl:        db      "(HL)", 0
msg_pc:         db      "(C)", 0
msg_af:         db      "AF", 0
msg_adc:        db      "ADC HL,", 0
msg_sbc:        db      "SBC HL,", 0
msg_neg:        db      "NEG", 0
msg_retn:       db      "RETN", 0
msg_reti:       db      "RETI", 0
msg_im0:        db      "IM 0", 0
msg_im1:        db      "IM 1", 0
msg_im2:        db      "IM 2", 0
msg_ldia:       db      "LD I,A", 0
msg_ldra:       db      "LD R,A", 0
msg_ldai:       db      "LD A,I", 0
msg_ldar:       db      "LD A,R", 0
msg_rrd:        db      "RRD", 0
msg_rld:        db      "RLD", 0
msg_bit:        db      "BIT ", 0
msg_res:        db      "RES ", 0
msg_set:        db      "SET ", 0

z7_ptrs:        dw      s_rlca, s_rrca, s_rla, s_rra
                dw      s_daa, s_cpl, s_scf, s_ccf
s_rlca:         db      "RLCA", 0
s_rrca:         db      "RRCA", 0
s_rla:          db      "RLA", 0
s_rra:          db      "RRA", 0
s_daa:          db      "DAA", 0
s_cpl:          db      "CPL", 0
s_scf:          db      "SCF", 0
s_ccf:          db      "CCF", 0

rot_ptrs:       dw      s_rlc, s_rrc, s_rl, s_rr
                dw      s_sla, s_sra, s_sll, s_srl
s_rlc:          db      "RLC", 0
s_rrc:          db      "RRC", 0
s_rl:           db      "RL", 0
s_rr:           db      "RR", 0
s_sla:          db      "SLA", 0
s_sra:          db      "SRA", 0
s_sll:          db      "SLL", 0
s_srl:          db      "SRL", 0

alu_ptrs:       dw      s_add, s_adc, s_sub, s_sbc
                dw      s_and, s_xor, s_or, s_cp
s_add:          db      "ADD A,", 0
s_adc:          db      "ADC A,", 0
s_sub:          db      "SUB ", 0
s_sbc:          db      "SBC A,", 0
s_and:          db      "AND ", 0
s_xor:          db      "XOR ", 0
s_or:           db      "OR ", 0
s_cp:           db      "CP ", 0

blk_ptrs:       dw      s_ldi, s_cpi, s_ini, s_outi
                dw      s_ldd, s_cpd, s_ind, s_outd
                dw      s_ldir, s_cpir, s_inir, s_otir
                dw      s_lddr, s_cpdr, s_indr, s_otdr
s_ldi:          db      "LDI", 0
s_cpi:          db      "CPI", 0
s_ini:          db      "INI", 0
s_outi:         db      "OUTI", 0
s_ldd:          db      "LDD", 0
s_cpd:          db      "CPD", 0
s_ind:          db      "IND", 0
s_outd:         db      "OUTD", 0
s_ldir:         db      "LDIR", 0
s_cpir:         db      "CPIR", 0
s_inir:         db      "INIR", 0
s_otir:         db      "OTIR", 0
s_lddr:         db      "LDDR", 0
s_cpdr:         db      "CPDR", 0
s_indr:         db      "INDR", 0
s_otdr:         db      "OTDR", 0

; ---------------------------------------------------------------
; Variables
; ---------------------------------------------------------------
saved_sp:       dw      0
err_code:       db      0
txtptr:         dw      0
pd_val:         dw      0
pd_ptr:         dw      0
pd_buf:         ds      7
find_name:      dw      0
name_src:       ds      11
name_out:       ds      11
src_mode:       db      0               ; 0 = file, 1 = memory
src_rec0:       dw      0
src_size:       dw      0
src_base:       dw      0
out_mode:       db      0               ; 0 = screen, 1 = file
out_entry:      dw      0
out_rec:        dw      0
out_rec0:       dw      0
out_pos:        dw      0
out_len:        ds      4
cache_rec:      dw      0
gb_off:         dw      0
ec_c:           db      0
lb_ptr:         dw      0
emit:           db      0
dry:            db      0
changed:        db      0
dr_cnt:         db      0
dr_lo:          dw      0
dr_hi:          dw      0
lab_cnt:        dw      0
le_lo:          dw      0
le_hi:          dw      0
le_mid:         dw      0
li_key:         dw      0
id_addr:        dw      0
fd_bit:         db      0
w_off:          dw      0
d_addr:         dw      0
d_opaddr:       dw      0
d_op:           db      0
d_op2:          db      0
d_x:            db      0
d_y:            db      0
d_z:            db      0
d_p:            db      0
d_q:            db      0
d_pfx:          db      0
d_disp:         db      0
d_len:          db      0
d_rlen:         db      0
d_kind:         db      0
d_tgt:          dw      0
d_tflg:         db      0
d_plain:        db      0
dw_lo:          db      0
ud_v:           db      0
ud_lead:        db      0
pr_r:           db      0

