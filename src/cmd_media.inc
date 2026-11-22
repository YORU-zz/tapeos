; cmd_media.inc - TOSD file layer + F/W/R/U/I commands
; (common layer since M7 stage 4; the K command lives in
; cmd_kanji.inc, its pixel writer in kanji_<machine>.inc).
; ---------------------------------------------------------------
; File layer (M4; drive selection added in M5). Two drives
; share the TOSD directory format and the code below; the
; U command picks the current drive (cur_drv):
;   drive 0 = FD. Addressed in IPL-compatible records
;     (256 bytes): record = ((track*2 + side) << 4) |
;     (sector - 1). Records 0..31 are the boot/system area
;     (record 0 = IPL header sector, 1.. = the shell program
;     itself). Record 32 holds the directory sector,
;     records 48..1279 are the file data area.
;   drive 1 = RAMDISK on EMM (IOCS RREAD/RWRITE). Block 0
;     holds the directory sector, blocks 1.. are the file
;     data area. Capacity is the RINIT block count.
; In both cases allocation is contiguous and append-only,
; and directory entries store record/block numbers in the
; drive's own numbering.
;
; Directory sector (256 bytes):
;   +0..3   magic "TOSD"
;   +4      format version (1)
;   +6..7   next free record (LE16)
;   +16..   15 entries x 16 bytes:
;             +0..9   name (space padded; byte 0 = 00h -> free)
;             +10..11 start record (LE16)
;             +12..13 size in bytes (LE16)
;             +14..15 load address (LE16)
; ---------------------------------------------------------------

; dir_load: DIRBUF <- directory block of the current drive
; (magic not checked). FD: record DIRREC. RAMDISK: block 0.
dir_load:
        ld      a, (cur_drv)
        or      a
        jr      nz, dl_rd
dir_load_fd:                            ; FD only (kanji font)
        ld      de, DIRREC
        ld      hl, DIRBUF
        jp      C_DREAD
dl_rd:
        ld      de, 0
        ld      hl, DIRBUF
        jp      C_RREAD

; dir_save: DIRBUF -> directory block of the current drive
dir_save:
        ld      a, (cur_drv)
        or      a
        jr      nz, ds_rd
        ld      de, DIRREC
        ld      hl, DIRBUF
        jp      C_DWRITE
ds_rd:
        ld      de, 0
        ld      hl, DIRBUF
        jp      C_RWRITE

; fs_read / fs_write: one 256-byte block of the current drive.
;   de = record (FD) / block (RAMDISK), hl = buffer.
;   Register behaviour matches DREAD / DWRITE.
fs_read:
        ld      a, (cur_drv)
        or      a
        jp      z, C_DREAD
        jp      C_RREAD

fs_write:
        ld      a, (cur_drv)
        or      a
        jp      z, C_DWRITE
        jp      C_RWRITE

; fs_setcap: refresh (fs_cap) = last usable record/block
; index of the current drive (FD: MAXREC; RAMDISK: the
; configured block count).
fs_setcap:
        ld      hl, MAXREC
        ld      a, (cur_drv)
        or      a
        jr      z, fsc_st
        ld      hl, (rd_blocks)
fsc_st:
        ld      (fs_cap), hl
        ret

; dir_magic_ok: carry set if DIRBUF lacks the "TOSD" magic
dir_magic_ok:
        ld      hl, dir_magic
        ld      de, DIRBUF
        ld      b, 4
dm_lp:
        ld      a, (de)
        cp      (hl)
        jr      nz, dm_bad
        inc     hl
        inc     de
        djnz    dm_lp
        or      a
        ret
dm_bad:
        scf
        ret

; dir_init: format DIRBUF in memory (the caller saves it)
dir_init:
        ld      hl, DIRBUF
        xor     a
        ld      b, 0                    ; 256 iterations
di_lp:
        ld      (hl), a
        inc     hl
        djnz    di_lp
        ld      hl, dir_magic
        ld      de, DIRBUF
        ld      bc, 4
        ldir
        ld      a, 1
        ld      (DIRBUF + 4), a         ; format version
        ld      hl, DATAREC0            ; FD: first data record
        ld      a, (cur_drv)
        or      a
        jr      z, di_st
        ld      hl, 1                   ; RAMDISK: data from block 1
di_st:
        ld      (DIRBUF + 6), hl        ; next free record/block
        ret

; find_entry: NAMEBUF vs the directory entries.
;   -> hl = entry address, carry clear; or carry set.
find_entry:
        ld      hl, DIRBUF + 16
        ld      b, 15
fe_lp:
        ld      a, (hl)
        or      a
        jr      z, fe_next              ; free slot
        push    hl
        push    bc
        ld      de, NAMEBUF
        ld      b, 10
fe_cmp:
        ld      a, (de)
        cp      (hl)
        jr      nz, fe_dif
        inc     hl
        inc     de
        djnz    fe_cmp
        pop     bc
        pop     hl
        or      a
        ret                             ; found
fe_dif:
        pop     bc
        pop     hl
fe_next:
        ld      de, 16
        add     hl, de
        djnz    fe_lp
        scf
        ret

; find_free: -> hl = first free entry, or carry set.
find_free:
        ld      hl, DIRBUF + 16
        ld      b, 15
ff_lp:
        ld      a, (hl)
        or      a
        jr      z, ff_ok
        ld      de, 16
        add     hl, de
        djnz    ff_lp
        scf
        ret
ff_ok:
        or      a
        ret

; F - list disk files
cmd_fdlist:
        call    dir_load
        jp      c, err_exit
        call    dir_magic_ok
        jr      nc, fl_ok
        ld      hl, msg_nofile
        call    C_PRTMSG
        jp      shell_loop
fl_ok:
        ld      a, (cur_drv)
        or      a
        jr      z, fl_list
        ld      hl, msg_rdtag
        call    C_PRTMSG
fl_list:
        ld      hl, DIRBUF + 16
        ld      b, 15
fl_lp:
        ld      a, (hl)
        or      a
        jr      z, fl_next
        push    hl
        push    bc
        ld      b, 10                   ; name (trim at space)
fl_nm:
        ld      a, (hl)
        cp      ' '
        jr      z, fl_nm_done
        inc     hl
        push    hl
        push    bc
        call    C_CONOUT
        pop     bc
        pop     hl
        djnz    fl_nm
fl_nm_done:
        ld      a, ' '
        call    C_CONOUT
        pop     bc
        pop     hl
        push    hl
        push    bc
        ld      de, 12                  ; size
        add     hl, de
        ld      e, (hl)
        inc     hl
        ld      d, (hl)
        ex      de, hl
        call    C_HEX16
        ld      a, ' '
        call    C_CONOUT
        pop     bc
        pop     hl
        push    hl
        push    bc
        ld      de, 14                  ; load address
        add     hl, de
        ld      e, (hl)
        inc     hl
        ld      d, (hl)
        ex      de, hl
        call    C_HEX16
        call    C_CRLF
        pop     bc
        pop     hl
fl_next:
        ld      de, 16
        add     hl, de
        djnz    fl_lp
        jp      shell_loop

; W name addr len - save a memory block as a disk file.
; The block's start address is recorded as the load address.
cmd_fdwrite:
        call    skip_sp
        call    parse_name
        jp      c, err_exit
        call    skip_sp
        call    parse_hex               ; de = addr
        jp      c, err_exit
        ld      (fw_addr), de
        call    skip_sp
        call    parse_hex               ; de = len
        jp      c, err_exit
        ld      (fw_len), de
        ld      a, d
        or      e
        jp      z, err_exit             ; len 0 is meaningless
        ; sectors needed = ((len - 1) >> 8) + 1 (no overflow)
        ld      hl, (fw_len)
        dec     hl
        ld      l, h
        ld      h, 0
        inc     hl
        ld      (fw_sec), hl
        call    fs_setcap               ; capacity of current drive
        ; directory: load it, or format a fresh one in memory
        call    dir_load
        jp      c, err_exit
        call    dir_magic_ok
        jr      c, fw_init
        jr      fw_have
fw_init:
        call    dir_init
fw_have:
        call    find_entry
        jr      c, fw_alloc
        ; existing file: reuse its space if the new data fits
        ld      (fw_entry), hl
        push    hl
        ld      de, 12
        add     hl, de
        ld      e, (hl)
        inc     hl
        ld      d, (hl)                 ; de = old size
        pop     hl
        ld      a, d
        or      e
        jr      z, fw_oldsec0
        dec     de
        ld      e, d
        ld      d, 0
        inc     de                      ; de = old sectors
        jr      fw_cmpsec
fw_oldsec0:
        ld      de, 0
fw_cmpsec:
        ld      hl, (fw_sec)
        or      a
        sbc     hl, de                  ; newsec - oldsec
        jr      c, fw_reuse
        jr      z, fw_reuse
        ; does not fit: free the old entry and append afresh
        ld      hl, (fw_entry)
        ld      (hl), 0
        jr      fw_alloc
fw_reuse:
        ld      hl, (fw_entry)
        ld      de, 10
        add     hl, de
        ld      e, (hl)
        inc     hl
        ld      d, (hl)
        ld      (fw_start), de
        jr      fw_write
fw_alloc:
        call    find_free
        jp      c, err_exit
        ld      (fw_entry), hl
        ld      hl, (DIRBUF + 6)        ; next free record
        ld      (fw_start), hl
        ld      de, (fw_sec)
        add     hl, de
        ld      (fw_newfree), hl
        ld      de, (fs_cap)
        inc     de                      ; one past the last block
        or      a
        sbc     hl, de
        jp      nc, err_exit            ; disk full
        ld      hl, (fw_newfree)
        ld      (DIRBUF + 6), hl
fw_write:
        ld      hl, (fw_start)
        ld      (fw_rec), hl
        ld      hl, (fw_addr)
        ld      (fw_ptr), hl
        ld      hl, (fw_sec)
        ld      (fw_cnt), hl
fw_wlp:
        ld      de, (fw_rec)
        ld      hl, (fw_ptr)
        call    fs_write
        jp      c, err_exit
        ld      hl, (fw_rec)
        inc     hl
        ld      (fw_rec), hl
        ld      hl, (fw_ptr)
        ld      de, 256
        add     hl, de
        ld      (fw_ptr), hl
        ld      hl, (fw_cnt)
        dec     hl
        ld      (fw_cnt), hl
        ld      a, h
        or      l
        jr      nz, fw_wlp
        ; fill in the directory entry
        ld      hl, NAMEBUF
        ld      de, (fw_entry)
        ld      bc, 10
        ldir
        ld      hl, (fw_entry)
        ld      de, 10
        add     hl, de
        ld      de, (fw_start)
        ld      (hl), e
        inc     hl
        ld      (hl), d
        inc     hl
        ld      de, (fw_len)
        ld      (hl), e
        inc     hl
        ld      (hl), d
        inc     hl
        ld      de, (fw_addr)
        ld      (hl), e
        inc     hl
        ld      (hl), d
        call    dir_save
        jp      c, err_exit
        ; flush assist (FD only): reading record 0 (another
        ; track) makes the core flush the dirty directory
        ; track to the file. The RAMDISK needs no flush.
        ld      a, (cur_drv)
        or      a
        jr      nz, fw_saved
        ld      de, 0
        ld      hl, DIRBUF
        call    C_DREAD                 ; result ignored
fw_saved:
        call    prt_name
        ld      hl, msg_fdsave
        call    C_PRTMSG
        jp      shell_loop

; R name - load a disk file to its recorded load address.
; The final partial sector goes through DIRBUF so exactly
; <size> bytes land in memory (same semantics as tape TDAT).
cmd_fdread:
        call    skip_sp
        call    parse_name
        jp      c, err_exit
        call    dir_load
        jp      c, err_exit
        call    dir_magic_ok
        jp      c, err_exit
        call    find_entry
        jp      c, err_exit
        ld      de, 10
        add     hl, de
        ld      e, (hl)
        inc     hl
        ld      d, (hl)
        inc     hl
        ld      (fr_start), de
        ld      e, (hl)
        inc     hl
        ld      d, (hl)
        inc     hl
        ld      (fr_size), de
        ld      e, (hl)
        inc     hl
        ld      d, (hl)
        ld      (fr_ptr), de            ; destination = load addr
        ld      hl, (fr_start)
        ld      (fr_rec), hl
        ld      hl, (fr_size)
        ld      a, h
        ld      (fr_full), a            ; whole sectors
        ld      a, l
        ld      (fr_rem), a             ; trailing bytes
fr_flp:
        ld      a, (fr_full)
        or      a
        jr      z, fr_remain
        ld      de, (fr_rec)
        ld      hl, (fr_ptr)
        call    fs_read
        jp      c, err_exit
        ld      hl, (fr_rec)
        inc     hl
        ld      (fr_rec), hl
        ld      hl, (fr_ptr)
        ld      de, 256
        add     hl, de
        ld      (fr_ptr), hl
        ld      a, (fr_full)
        dec     a
        ld      (fr_full), a
        jr      fr_flp
fr_remain:
        ld      a, (fr_rem)
        or      a
        jr      z, fr_done
        ld      de, (fr_rec)
        ld      hl, DIRBUF
        call    fs_read
        jp      c, err_exit
        ld      hl, DIRBUF
        ld      de, (fr_ptr)
        ld      a, (fr_rem)
        ld      c, a
        ld      b, 0
        ldir
fr_done:
        call    prt_name
        ld      hl, msg_tload           ; " LOADED"
        call    C_PRTMSG
        jp      shell_loop

; ---------------------------------------------------------------
; U [drv] - select the current drive for F / W / R (M5).
;   0 = FD (the default at boot), 1 = RAMDISK. Without an
;   argument, prints the current selection. Selecting the
;   RAMDISK before it is configured (I command) is an error.
; ---------------------------------------------------------------
cmd_usedrv:
        call    skip_sp
        ld      a, (hl)
        or      a
        jr      z, ud_show              ; no argument: report
        call    parse_hex               ; de = drive number
        jp      c, err_exit
        ld      a, d
        or      a
        jp      nz, err_exit
        ld      a, e
        or      a
        jr      z, ud_set               ; drive 0 (a = 0)
        cp      1
        jp      nz, err_exit
        ld      hl, (rd_blocks)         ; RAMDISK configured?
        ld      a, h
        or      l
        jp      z, err_exit
        ld      a, 1
ud_set:
        ld      (cur_drv), a
ud_show:
        ld      a, (cur_drv)
        or      a
        jr      nz, ud_rd
        ld      hl, msg_drv0
        call    C_PRTMSG
        jp      shell_loop
ud_rd:
        ld      hl, msg_drv1
        call    C_PRTMSG
        jp      shell_loop

; ---------------------------------------------------------------
; I [blocks] - initialise / reconfigure the RAMDISK (M5).
;   The argument is a hex block count (1..100h; one block is
;   256 bytes, so 100h blocks = 64 KB - the in-fiction IOCS
;   spec maximum). Initialising (re)formats the RAMDISK: a
;   fresh TOSD directory is written to block 0 and previous
;   content is lost (dynamic reconfiguration, ep.47, is a
;   resize + format here). Without an argument, prints the
;   current configuration. On success "RD-OK" + the granted
;   block count (LE16) is written to RDSIG for the harness.
; ---------------------------------------------------------------
cmd_raminit:
        call    skip_sp
        ld      a, (hl)
        or      a
        jr      z, ri_show              ; no argument: report
        call    parse_hex               ; de = blocks
        jp      c, err_exit
        ld      a, d
        or      e
        jp      z, err_exit             ; 0 is the RINIT query
        ld      h, d
        ld      l, e
        call    C_RINIT                 ; hl = granted blocks
        jp      c, err_exit             ; no EMM / bad size
        ; format: dir_init / dir_save follow (cur_drv), so
        ; select the RAMDISK temporarily, then restore.
        ld      a, (cur_drv)
        ld      (ri_savdrv), a
        ld      a, 1
        ld      (cur_drv), a
        call    dir_init
        call    dir_save                ; carry = error
        ld      a, (ri_savdrv)          ; (ld does not move flags)
        ld      (cur_drv), a
        jp      c, err_exit
        ; harness signature: "RD-OK" + granted blocks (LE16)
        ld      hl, rd_sigtext
        ld      de, RDSIG
        ld      bc, 5
        ldir
        ld      hl, (rd_blocks)
        ld      (RDSIG + 5), hl
        ; fall into the report
ri_show:
        ld      hl, (rd_blocks)
        ld      a, h
        or      l
        jr      nz, ri_have
        ld      hl, msg_rdnone
        call    C_PRTMSG
        jp      shell_loop
ri_have:
        ld      hl, msg_rdpre
        call    C_PRTMSG
        ld      hl, (rd_blocks)
        call    C_HEX16
        ld      hl, msg_rdsuf
        call    C_PRTMSG
        jp      shell_loop
