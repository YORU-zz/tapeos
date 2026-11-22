; extiocs_x1.inc - extended IOCS: FAT-style main-memory
; management (ep.31, "kakuchou IOCS no dokuji system-ka").
; X1 first; included only when EXTIOCS is defined (the X1
; classic target). See ../extended-iocs.md and the ep.31
; verification memo in ../spec-notes.md.
; ---------------------------------------------------------------
; The novel's ep.31 programme, cut to this item's scope:
; the CALL table stays compatible (existing entries 0-27
; keep their numbers and conventions), and the memory
; management that ep.31 unified with the RAMDISK is brought
; to main memory: a pool of 256-byte blocks is managed by a
; FAT chain discipline so that executable images can live
; in memory under names and be listed / loaded / freed from
; the shell - the ep.31 picture of DIR for memory.
;
; Pool layout (main memory, X1):
;   MEMBASE = 09000h, up to MEMMAXB = 64 blocks (16 KB).
;   block 0: directory block - "TMEM" magic, version 1,
;     total blocks at +6 (LE16), then 8 entries x 16 bytes
;     at +16 in the TOSD entry layout (name 10 / start
;     block / size / load address). At most 8 resident
;     images in this first version.
;   block 1: FAT block - 64 entries x LE16 at +0.
;     0000h = free; a link is 8000h | next block, where the
;     top bit is the ep.31 class mark (1 = main memory,
;     the executable class); 0ffffh = end of chain.
;     Blocks 0 and 1 are permanently 0ffffh (system blocks).
;   blocks 2..: image data, chained in allocation order.
;
; New IOCS entries (X1 only; the slots were stubs on X1):
;   17 MINIT   hl = requested blocks (1..64); hl = 0 is a
;              query -> hl = granted blocks. carry = error.
;              Configuring formats the pool (directory and
;              FAT are rebuilt; resident images are lost),
;              like the I command reformats the RAMDISK.
;              hl is the return value: a documented
;              exception to the register convention, the
;              same pattern as RINIT (26).
;   18 MDIR    -> hl = directory block address (MEMBASE).
;              carry = error if the pool is not
;              initialised. hl is the return value
;              (documented exception); bc/de preserved.
;   19 MSTORE  hl = name (10 bytes, space padded, the TOSD
;              convention), de = source, bc = length.
;              Allocates a FAT chain, copies the image in
;              and records de as its load address. A name
;              that is already in use is an error (free it
;              first) - unlike TOSD's W, MSTORE never
;              destroys the old image on a failed store.
;              carry = error (uninitialised, bad length,
;              duplicate name, directory full, pool full).
;              bc/de/hl preserved.
;   27 MFREE   hl = name. Frees the image's chain and
;              clears its entry. carry = error
;              (uninitialised / not found). bc/de/hl kept.
; Loading an image back to its recorded address and
; running it is a shell-layer function (the N R command),
; built on the directory interface above - the same split
; as TOSD, whose named-file layer is not part of IOCS.
;
; What ep.31 lists but this item deliberately does NOT do
; (see the verification memo for the full cut):
;   - the relocation loader's move into the system
;     (reloc-plan stage 3; TOSR images keep self-relocating,
;     which already defers rewriting to execution time)
;   - the RST relative-call executable format with IX/IY
;     conventions (relocatable line, later stages)
;   - the multitask experiment (ep.32 / roadmap item 5)
;   - TinyBASIC (roadmap item 6)
; The GRAM side of the novel's "all RAM under FAT" is not
; reproduced: the X1 GRAM is cell-addressed and cannot be a
; linear block store (established in the SCED work), so the
; managed space here is the main-memory pool only.
; ---------------------------------------------------------------

MEMBASE         equ     09000h          ; pool base (main RAM)
MEMMAXB         equ     64              ; pool blocks (16 KB)
MEMDIR          equ     MEMBASE         ; block 0: directory
MEMFAT          equ     MEMBASE + 256   ; block 1: FAT

ext_start:
; ---------------------------------------------------------------
; helpers
; ---------------------------------------------------------------

; mm_fatptr: a = block -> hl = &FAT[block].
; (MEMFAT is page-aligned by design, and a < 128 always.)
mm_fatptr:
        add     a, a
        ld      l, a
        ld      h, MEMFAT / 256
        ret

; mm_fatget: a = block -> hl = FAT value. destroys de.
mm_fatget:
        call    mm_fatptr
        ld      e, (hl)
        inc     hl
        ld      d, (hl)
        ex      de, hl
        ret

; mm_fatset: a = block, de = value.
mm_fatset:
        call    mm_fatptr
        ld      (hl), e
        inc     hl
        ld      (hl), d
        ret

; mm_blkaddr: a = block -> hl = block address.
; (MEMBASE is page-aligned by design.)
mm_blkaddr:
        add     a, MEMBASE / 256
        ld      h, a
        ld      l, 0
        ret

; mm_probe: write a test byte at hl and read it back.
; carry set on mismatch. hl preserved. (The pool is plain
; main RAM; the probe's job is "RAM is really there".)
mm_probe:
        ld      (hl), 0a5h
        ld      a, (hl)
        cp      0a5h
        ret     z                       ; carry clear from cp
        scf
        ret

; mm_find: (mm_nptr) = name -> hl = entry, carry clear;
; or carry set. 8 entries at MEMDIR + 16.
mm_find:
        ld      hl, MEMDIR + 16
        ld      b, 8
mf_lp:
        ld      a, (hl)
        or      a
        jr      z, mf_next              ; free slot
        push    hl
        push    bc
        ld      de, (mm_nptr)
        ld      b, 10
mf_cmp:
        ld      a, (de)
        cp      (hl)
        jr      nz, mf_dif
        inc     hl
        inc     de
        djnz    mf_cmp
        pop     bc
        pop     hl
        or      a
        ret                             ; found
mf_dif:
        pop     bc
        pop     hl
mf_next:
        ld      de, 16
        add     hl, de
        djnz    mf_lp
        scf
        ret

; mm_findfree: -> hl = first free entry, or carry set.
mm_findfree:
        ld      hl, MEMDIR + 16
        ld      b, 8
mff_lp:
        ld      a, (hl)
        or      a
        jr      z, mff_ok
        ld      de, 16
        add     hl, de
        djnz    mff_lp
        scf
        ret
mff_ok:
        or      a
        ret

; mm_countfree: -> hl = free blocks among 2..mm_blocks-1.
; (the count accumulates in bc; mm_fatget preserves bc.)
mm_countfree:
        ld      bc, 0
        ld      a, 2
        ld      (mm_b), a
mcf_lp:
        ld      a, (mm_b)
        ld      hl, (mm_blocks)
        cp      l                       ; block vs total (< 256)
        jr      nc, mcf_done
        call    mm_fatget               ; hl = FAT value
        ld      a, h
        or      l
        jr      nz, mcf_next
        inc     bc
mcf_next:
        ld      a, (mm_b)
        inc     a
        ld      (mm_b), a
        jr      mcf_lp
mcf_done:
        ld      h, b
        ld      l, c
        ret

; mm_alloc1: -> a = first free block (>= 2), carry if none.
mm_alloc1:
        ld      a, 2
        ld      (mm_b), a
ma_lp:
        ld      a, (mm_b)
        ld      hl, (mm_blocks)
        cp      l
        jr      nc, ma_none
        call    mm_fatget
        ld      a, h
        or      l
        jr      z, ma_found
        ld      a, (mm_b)
        inc     a
        ld      (mm_b), a
        jr      ma_lp
ma_found:
        ld      de, 0ffffh              ; mark it allocated at
        ld      a, (mm_b)               ; once (MSTORE patches
        call    mm_fatset               ; in the real link, or
        ld      a, (mm_b)               ; the end mark, later)
        or      a
        ret
ma_none:
        scf
        ret

; mm_freechain: free the chain of the entry at (mm_ent).
mm_freechain:
        ld      hl, (mm_ent)
        ld      de, 10
        add     hl, de
        ld      a, (hl)                 ; start block
        ld      (mm_b), a
mfc_lp:
        ld      a, (mm_b)
        call    mm_fatget               ; hl = FAT value
        push    hl
        ld      de, 0
        ld      a, (mm_b)
        call    mm_fatset               ; FAT[b] = free
        pop     hl
        ld      a, h
        cp      080h
        ret     nz                      ; not a link: end mark
        ld      a, l
        ld      (mm_b), a
        jr      mfc_lp

; ---------------------------------------------------------------
; MINIT (17)
; ---------------------------------------------------------------
i_minit:
        push    bc
        push    de
        ld      a, h
        or      l
        jr      z, mi_query             ; hl = 0: report only
        ld      a, h
        or      a
        jr      nz, mi_clamp            ; requested > 255
        ld      a, l
        cp      MEMMAXB + 1
        jr      c, mi_probe             ; requested <= MEMMAXB
mi_clamp:
        ld      hl, MEMMAXB
mi_probe:
        ld      (mm_len), hl            ; scratch (MSTORE idle)
        ld      hl, MEMBASE             ; probe the pool base
        call    mm_probe                ; (the pool is one
        jr      c, mi_fail              ; contiguous RAM area)
        ; format: zero the directory + FAT blocks (512 bytes)
        ld      hl, MEMDIR
        ld      bc, 512
mi_z:
        ld      (hl), 0
        inc     hl
        dec     bc
        ld      a, b
        or      c
        jr      nz, mi_z
        ld      hl, mm_magic
        ld      de, MEMDIR
        ld      bc, 4
        ldir                            ; de = MEMDIR + 4 now
        ld      a, 1
        ld      (de), a                 ; format version
        ld      hl, (mm_len)
        ld      (MEMDIR + 6), hl        ; total blocks
        ld      hl, 0ffffh              ; system blocks: end
        ld      (MEMFAT), hl
        ld      (MEMFAT + 2), hl
        ld      hl, (mm_len)
        ld      (mm_blocks), hl
mi_query:
        ld      hl, (mm_blocks)
        pop     de
        pop     bc
        or      a                       ; carry clear
        ret
mi_fail:
        pop     de
        pop     bc
        scf
        ret

; ---------------------------------------------------------------
; MDIR (18)
; ---------------------------------------------------------------
i_mdir:
        ld      hl, (mm_blocks)
        ld      a, h
        or      l
        jr      z, md_fail
        ld      hl, MEMBASE
        or      a
        ret
md_fail:
        scf
        ret

; ---------------------------------------------------------------
; MSTORE (19)
; ---------------------------------------------------------------
i_mstore:
        push    bc
        push    de
        push    hl
        ld      (mm_nptr), hl
        ld      (mm_src), de
        ld      (mm_len), bc
        ld      hl, (mm_blocks)
        ld      a, h
        or      l
        jr      z, ms_fj                ; uninitialised
        ld      a, b
        or      c
        jr      z, ms_fj                ; length 0 (bc = len)
        ; blocks needed = ((len - 1) >> 8) + 1; the count
        ; doubles as the allocation countdown in ms_alloc.
        ld      hl, (mm_len)
        dec     hl
        ld      a, h
        inc     a
        jr      z, ms_fj                ; 256 blocks: too big
        ld      (mm_need), a
        ; a name already in use is an error (see header)
        call    mm_find
        jr      nc, ms_fj               ; duplicate name
        call    mm_findfree
        jr      c, ms_fj                ; directory full
        ld      (mm_ent), hl
ms_haveent:
        call    mm_countfree            ; hl = free blocks
        ld      a, (mm_need)
        ld      e, a
        ld      d, 0
        or      a
        sbc     hl, de                  ; free - needed
        jr      c, ms_fj                ; pool full
        jr      ms_body                 ; skip the trampoline
ms_fj:
        jp      ms_fail                 ; trampoline (jr range)
ms_body:
        ; allocate + copy, block by block
        ld      hl, (mm_src)
        ld      (mm_ptr), hl
        ld      hl, (mm_len)
        ld      (mm_rem), hl
        ld      a, 0ffh
        ld      (mm_prev), a
ms_alloc:
        call    mm_alloc1               ; a = block, already
        jp      c, ms_fail              ; marked end-of-chain
        ld      b, a                    ; b = current block
        ld      a, (mm_prev)
        inc     a                       ; prev == 0ffh (none)?
        jr      z, ms_first
        dec     a                       ; a = previous block
        ld      e, b
        ld      d, 080h                 ; class mark | next
        call    mm_fatset               ; FAT[prev] = link(b)
        jr      ms_copy
ms_first:
        ld      a, b
        ld      (mm_first), a
ms_copy:
        ld      a, b
        call    mm_blkaddr              ; hl = destination
        ex      de, hl                  ; de = destination
        push    bc                      ; keep b across ldir
        ; n = min(256, remaining)
        ld      hl, (mm_rem)
        ld      a, h
        or      a
        jr      nz, ms_full
        ld      b, h                    ; b = 0 (h is 0 here)
        ld      c, l
        jr      ms_cp
ms_full:
        ld      bc, 256
ms_cp:
        push    bc                      ; keep the chunk size
        ld      hl, (mm_ptr)
        ldir                            ; bc bytes: hl/de advance
        ld      (mm_ptr), hl
        pop     bc                      ; chunk size
        ld      hl, (mm_rem)
        or      a
        sbc     hl, bc
        ld      (mm_rem), hl
        pop     bc                      ; b = block again
        ld      a, b
        ld      (mm_prev), a
        ld      a, (mm_need)            ; blocks still to go
        dec     a
        ld      (mm_need), a
        jr      nz, ms_alloc
        ; no terminate step: mm_alloc1 births every block
        ; with the end mark; links replaced all but the last.
        ; fill in the directory entry
        ld      hl, (mm_nptr)
        ld      de, (mm_ent)
        ld      bc, 10
        ldir                            ; de = entry + 10 now
        ex      de, hl
        ld      a, (mm_first)
        ld      (hl), a
        inc     hl
        xor     a
        ld      (hl), a                 ; start block (LE16)
        inc     hl
        ld      de, (mm_len)
        ld      (hl), e
        inc     hl
        ld      (hl), d                 ; size
        inc     hl
        ld      de, (mm_src)
        ld      (hl), e
        inc     hl
        ld      (hl), d                 ; load address = source
ms_ok:                                  ; shared with MFREE:
        pop     hl                      ; both pushed bc/de/hl
        pop     de
        pop     bc
        or      a
        ret
ms_fail:
        pop     hl
        pop     de
        pop     bc
        scf
        ret

; ---------------------------------------------------------------
; MFREE (27)
; ---------------------------------------------------------------
i_mfree:
        push    bc
        push    de
        push    hl
        ld      (mm_nptr), hl
        ld      hl, (mm_blocks)
        ld      a, h
        or      l
        jr      z, mfr_fail
        call    mm_find
        jr      c, mfr_fail
        ld      (mm_ent), hl
        call    mm_freechain
        ld      hl, (mm_ent)
        ld      (hl), 0                 ; name byte 0 = free
        jp      ms_ok                   ; shared epilogues
mfr_fail:
        jp      ms_fail

; ---------------------------------------------------------------
; Shell command N - memory images (ep.31 shell side)
;   N            list resident images + free blocks
;   N I [n]      initialise the pool (n blocks, hex;
;                default = MEMMAXB). Reformats.
;   N S name a l store memory a..a+l-1 under name
;   N R name     load the image to its recorded address
;                and call it (RET returns, as with G)
;   N F name     free the image
; ---------------------------------------------------------------
cmd_mem:
        call    skip_sp
        ld      a, (hl)
        or      a
        jr      z, cmn_list
        call    upcase
        cp      'I'
        jp      z, cmn_init
        ld      (mm_sub), a             ; S / R / F all take
        inc     hl                      ; a name argument
        call    skip_sp
        call    parse_name
        jp      c, err_exit
        ld      a, (mm_sub)
        cp      'S'
        jp      z, cmn_store
        cp      'R'
        jp      z, cmn_run
        cp      'F'
        jp      z, cmn_free
        jp      err_exit

cmn_list:
        call    C_MDIR                  ; initialised?
        jp      c, cmn_none
        call    mm_countfree            ; hl = free blocks
        push    hl
        ld      hl, MEMDIR + 16         ; first entry
        ld      b, 8
cmnl_lp:
        ld      a, (hl)
        or      a
        jr      z, cmnl_next
        push    bc
        ld      de, NAMEBUF             ; print the name via
        ld      bc, 10                  ; prt_name (trims at
        ldir                            ; the first space)
        call    prt_name                ; hl = entry + 10 now
        ld      a, ' '
        call    C_CONOUT
        inc     hl                      ; +12: size
        inc     hl
        ld      e, (hl)
        inc     hl
        ld      d, (hl)                 ; de = entry + 13,
        ex      de, hl                  ; hl = size; HEX16
        call    C_HEX16                 ; preserves hl, so
        ld      a, ' '                  ; de keeps the entry
        call    C_CONOUT                ; pointer throughout
        ex      de, hl                  ; hl = entry + 13
        inc     hl                      ; +14: load address
        ld      e, (hl)
        inc     hl
        ld      d, (hl)                 ; de = entry + 15
        ex      de, hl                  ; hl = load address
        call    C_HEX16
        call    C_CRLF
        pop     bc
        ex      de, hl                  ; hl = entry + 15
        inc     hl                      ; next entry
        djnz    cmnl_lp
        jr      cmnl_tail
cmnl_next:
        ld      de, 16
        add     hl, de
        djnz    cmnl_lp
cmnl_tail:
        pop     hl                      ; free blocks
        jr      cmn_tail
cmn_none:
        ld      hl, msg_mmnone
        call    C_PRTMSG
        jp      shell_loop

; cmn_tail: print "MEMORY " + HEX16(hl) + the shared
; " BLOCKS (X256 BYTES)" suffix and return to the shell.
; Used by N (list) and N I.
cmn_tail:
        push    hl
        ld      hl, msg_mmpre
        call    C_PRTMSG
        pop     hl
        call    C_HEX16
        ld      hl, msg_rdsuf
        call    C_PRTMSG
        jp      shell_loop

cmn_init:
        inc     hl                      ; past the subcommand
        call    skip_sp
        call    parse_hex               ; de = blocks
        jr      c, cmni_def
        ex      de, hl                  ; hl = requested
        jr      cmni_go
cmni_def:
        ld      hl, MEMMAXB
cmni_go:
        call    C_MINIT                 ; hl = granted
        jp      c, err_exit
        jr      cmn_tail                ; "MEMORY 0040 BLOCKS..."

cmn_store:                              ; name in NAMEBUF
        call    skip_sp
        call    parse_hex               ; de = address
        jp      c, err_exit
        ld      (mm_src), de            ; scratch (IOCS idle)
        call    skip_sp
        call    parse_hex               ; de = length
        jp      c, err_exit
        ld      b, d
        ld      c, e                    ; bc = length
        ld      de, (mm_src)
        ld      hl, NAMEBUF
        call    C_MSTORE
        jp      c, err_exit
        call    prt_name
        ld      hl, msg_fdsave          ; " SAVED"
        call    C_PRTMSG
        jp      shell_loop

cmn_free:                               ; name in NAMEBUF
        ld      hl, NAMEBUF
        call    C_MFREE
        jp      c, err_exit
        jp      shell_loop

cmn_run:                                ; name in NAMEBUF
        call    C_MDIR                  ; initialised?
        jp      c, err_exit
        ld      hl, NAMEBUF
        ld      (mm_nptr), hl
        call    mm_find
        jp      c, err_exit
        ld      (mm_ent), hl
        ld      de, 10
        add     hl, de
        ld      a, (hl)                 ; start block
        ld      (mm_b), a
        inc     hl
        inc     hl                      ; +12: size
        ld      e, (hl)
        inc     hl
        ld      d, (hl)
        ld      (mm_rem), de            ; bytes to copy
        inc     hl                      ; +14: load address
        ld      e, (hl)
        inc     hl
        ld      d, (hl)
        ld      (mm_ptr), de            ; destination pointer
        ld      (mm_tgt), de            ; and the jump target
cmnr_lp:
        ld      hl, (mm_rem)
        ld      a, h
        or      a
        jr      nz, cmnr_full
        ld      b, h                    ; b = 0 (h is 0 here)
        ld      c, l
        jr      cmnr_cp
cmnr_full:
        ld      bc, 256
cmnr_cp:
        push    bc
        ld      a, (mm_b)
        call    mm_blkaddr              ; hl = source block
        ld      de, (mm_ptr)
        ldir
        ld      (mm_ptr), de
        pop     bc
        ld      hl, (mm_rem)
        or      a
        sbc     hl, bc
        ld      (mm_rem), hl
        ld      a, h
        or      l
        jr      z, cmnr_go
        ld      a, (mm_b)
        call    mm_fatget               ; hl = FAT value
        ld      a, h
        cp      080h
        jp      nz, err_exit            ; not a link: the
        ld      a, l                    ; chain is too short
        ld      (mm_b), a
        jr      cmnr_lp
cmnr_go:
        ld      hl, (mm_tgt)
        ld      bc, shell_loop
        push    bc
        jp      (hl)

; ---------------------------------------------------------------
; data
; ---------------------------------------------------------------
mm_magic:
        db      "TMEM"
mm_blocks:
        dw      0                       ; granted blocks (0 = none)
mm_nptr:
        dw      0
mm_src:
        dw      0
mm_len:
        dw      0
mm_ent:
        dw      0
mm_ptr:
        dw      0
mm_rem:
        dw      0
mm_tgt:
        dw      0
mm_b:
        db      0
mm_sub:
        db      0
mm_need:
        db      0
mm_prev:
        db      0
mm_first:
        db      0
msg_mmpre:
        db      "MEMORY ", 0
msg_mmnone:
        db      "NO MEMORY POOL", 13, 10, 0
