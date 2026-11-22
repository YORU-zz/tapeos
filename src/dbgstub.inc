; ===============================================================
; dbgstub.inc - resident debug stub for the TapeOS monitor.
;
; Deliberate deviation from the novel (see ../spec-notes.md,
; the debug-stub verification memo of 2026-11-21, and
; ../dbg-stub.md): the novel's early TapeOS had NO debug stub
; - debugging was local, serial-terminal debugging was
; considered and rejected in ep. 27, and the first
; kernel-embedded remote debugger in the story is the
; 386/ROS2 one of ep. 214. At the user's direction this
; reproduction embeds a small resident stub in the common
; monitor NOW, ahead of the novel's schedule, because a stub
; that lives outside the target program survives target
; crashes and makes post-mortem inspection possible.
;
; First target: X1 only. This file is included only when
; DBGSTUB is defined (currently only by the X1 master), so
; the MZ-80B / MZ-80K / PC-8801 monitor binaries stay
; byte-identical.
;
; Entry: shell command X. Commands (one letter):
;   D [addr]   dump 128 bytes (default: continue from last)
;   M addr b.. deposit bytes
;   R          show captured registers (main + shadow + I)
;   B [addr]   set breakpoint / no arg: clear
;   G [addr]   run at addr (fresh SP; a RET lands back in
;              the stub); no arg: continue
;   C          continue from the captured PC
;   Q          quit to the shell
;   ?          help
;
; Breakpoints: software patch with RST 30H (0F7h). Page 0 is
; RAM once the IPL has disconnected, and the boot code plants
; JP dbg_bp at 0030h, JP dbg_int at 0038h (IM 1) and at
; 0066h (NMI, defensive). On a hit the original byte is
; restored and the breakpoint disarms itself (one-shot): the
; Z80 has no single-step facility and an instruction-length
; decoder would cost hundreds of bytes, so "continue" simply
; re-executes the restored instruction. A breakpoint still
; armed when its target byte is overwritten with M is the
; caller's responsibility, as in any minimal stub.
;
; Attention / post-mortem: while a target runs under G/C the
; stub arms the sub-CPU key interrupt (command 0E4h, vector
; 38h; the CPU runs the target in IM 1 with interrupts
; enabled). A key press then interrupts the target: ESC
; captures all registers and enters the stub (post-mortem
; inspection of a runaway loop), any other key is forwarded
; into the keyboard ring so the target still receives it.
; The vector is disarmed whenever the stub itself has
; control, so the shell's cooperative polling - and the
; tape deck's ei window in tp_ctrl - is never disturbed
; outside a target run. Limits, honestly: a target that
; executes DI gives up ESC-break until it EIs or hits a
; breakpoint; DI+HALT is recoverable only by reset; a target
; that reprograms the sub-CPU vector or page 0 defeats the
; stub; a runaway that overwrites the monitor itself is
; beyond any resident stub.
;
; Remote operation: the novel's ep. 46 channel (a
; bidirectional 6-bit parallel link over the printer port
; and joystick pins, built for file transfer to an MBC-55)
; has no counterpart in this emulator core: the plain X1
; model here has no printer peripheral (the 8255 at 1A00h
; is the sub-CPU / tape PPI; its port A is a write-only
; latch), the SIO exists in the core only as turbo-model
; hardware and has no host endpoint, and the CTC is turbo
; hardware as well. The feasible channel in this
; environment is the host harness's shared SYSTEM_RAM: a
; mailbox at 0EA00h. dbg-stub/tools/dbghost.c drives it
; from the host - it plays the role of the second machine
; at the other end of the ep. 46 link, with shared RAM
; standing in for the cable, and injects ESC through the
; keyboard matrix for a remote break, exactly as a local
; user would press it. Local console operation is always
; available and needs no host support.
;
; Stub RAM (X1; all inside the free 0E900h-0FFFFh area):
;   0EA00h MB_FLAG   0 idle / 1 command pending / 2 reply
;   0EA01h MB_STATE  0 shell / 1 stub prompt / 2 target runs
;   0EA02h MB_OLEN   valid bytes in MB_OUT
;   0EA04h "DBST"    signature, written at boot
;   0EA10h MB_LINE   remote command line (48 bytes)
;   0EA40h MB_OUT    captured output (256 bytes)
;   0EB40h-0EBFFh    stub stack (top 0EC00h)
;   0EC10h save area captured registers (26 bytes)
;   0EC30h work variables
; ===============================================================

DBG_MB_FLAG:    equ     0EA00h
DBG_MB_STATE:   equ     0EA01h
DBG_MB_OLEN:    equ     0EA02h
DBG_MB_LINE:    equ     0EA10h
DBG_MB_OUT:     equ     0EA40h
DBG_STACK:      equ     0EC00h
DBG_SV:         equ     0EC10h          ; +0 IY +2 IX +4 HL +6 DE
                                        ; +8 BC +10 AF +12 PC +14 SP
                                        ; +16 AF' +18 BC' +20 DE'
                                        ; +22 HL' +24 I +25 IFF flags
DBG_SPRAW:      equ     0EC30h          ; target SP at capture entry
DBG_KEY:        equ     0EC32h          ; key from interrupt txn
DBG_PREV:       equ     0EC33h          ; line-input edge filter
DBG_RUNF:       equ     0EC34h          ; 1 while a target runs
DBG_SRC:        equ     0EC35h          ; 0 local / 1 mailbox command
DBG_RSN:        equ     0EC36h          ; stop reason: B / I / R
DBG_DPTR:       equ     0EC38h          ; default dump address
DBG_BPA:        equ     0EC3Ah          ; breakpoint address
DBG_BPB:        equ     0EC3Ch          ; byte hidden by the patch
DBG_BPS:        equ     0EC3Dh          ; 1 while a breakpoint is armed
DBG_CAPF:       equ     0EC3Eh          ; 1 while output is captured
DBG_VEC:        equ     0EC3Fh          ; scratch: vector byte to send
DBG_TMPW:       equ     0EC40h          ; scratch: M write pointer

; ---------------------------------------------------------------
; dbg_main: shell X entry. Never returns (Q jumps to
; shell_loop). The initial context is fresh: all registers
; zero, SP = STACKTOP, PC = 0.
; ---------------------------------------------------------------
dbg_main:
        ld      sp, DBG_STACK
        xor     a
        ld      (DBG_BPS), a
        ld      (DBG_RUNF), a
        ld      (DBG_RSN), a
        ld      (DBG_SRC), a
        ld      (DBG_CAPF), a
        ld      (DBG_PREV), a
        ld      hl, DBG_SV
        ld      b, 26
dbg_iclr:
        ld      (hl), a
        inc     hl
        djnz    dbg_iclr
        ld      hl, STACKTOP
        ld      (DBG_SV+14), hl         ; sv SP
        ld      hl, 0
        ld      (DBG_DPTR), hl
        call    dbg_disarm
        call    dbg_drain
        call    dbg_cap_on
        ld      hl, dbg_msg_banner
        call    dbg_puts
        call    dbg_cap_off
        ld      a, 1
        ld      (DBG_MB_STATE), a       ; state 1 = banner ready
        jp      dbg_prompt

; ---------------------------------------------------------------
; dbg_setvec: program the sub-CPU key-interrupt vector
; (command 0E4h + one data byte; 0 disarms). Any staged
; sub-CPU transaction is drained first (kb_raw), because the
; core drops command bytes while a transaction is staged -
; the race the M4 keyboard design note in kb_x1.inc records.
; Destroys a, b.
; ---------------------------------------------------------------
dbg_setvec:
        ld      (DBG_VEC), a
        call    kb_raw                  ; drain (result discarded)
        ld      bc, PPIB
        ld      e, 0
dsv_w1: in      a, (c)
        and     040h
        jr      z, dsv_s1
        dec     e
        jr      nz, dsv_w1
dsv_s1: ld      bc, SUBCPU
        ld      a, 0E4h
        out     (c), a
        ld      bc, PPIB
        ld      e, 0
dsv_w2: in      a, (c)
        and     040h
        jr      z, dsv_s2
        dec     e
        jr      nz, dsv_w2
dsv_s2: ld      bc, SUBCPU
        ld      a, (DBG_VEC)
        out     (c), a
        ret
dbg_disarm:
        xor     a
        jr      dbg_setvec

; ---------------------------------------------------------------
; dbg_drain: discard keyboard input that arrived while a
; target ran. Keys the interrupt handler forwarded into the
; ring belong to the run, not to the next stub command -
; the G command's own Return, still held when the target
; started, can be forwarded the same way and would
; otherwise execute as a phantom empty command at the
; prompt (and wipe the stop report out of MB_OUT). The
; edge filter is re-synced to the key currently down.
; ---------------------------------------------------------------
dbg_drain:
        ld      a, (KBTAIL)
        ld      (KBHEAD), a             ; ring empty
        call    kb_raw
        ld      (KBPREV), a             ; sync BOTH edge filters:
        ld      (DBG_PREV), a           ; a key still held (the
        ret                             ; X line's own CR) must not
                                        ; re-fire as a phantom CR

; ---------------------------------------------------------------
; dbg_rd_txn: read the interrupt-staged sub-CPU transaction.
; The core stages both bytes before raising the interrupt,
; so no waits are needed (ppib bit 5 only sets once the
; transaction has been fully read). First byte = function /
; shift flags (discarded), second = ASCII code -> a.
; ---------------------------------------------------------------
dbg_rd_txn:
        push    bc
        ld      bc, SUBCPU
        in      a, (c)
        in      a, (c)
        pop     bc
        ret

; ---------------------------------------------------------------
; dbg_int: key interrupt (IM 1 vector 38h, also NMI 66h).
; Entered with the target's PC on its own stack.
;
; Core note (xmil z80c): accepting an interrupt sets the
; core's DI latch, and RETI alone does NOT clear it - only
; EI does (ievent_progress() stays blocked while the latch
; is set, so a bare RETI would deaden every later key
; interrupt). Every exit from this handler therefore runs
; EI immediately before RETI, which is also the canonical
; handler exit on real Z80 hardware. Interrupts are only
; delivered between the core's execute chunks, never
; mid-instruction.
; ---------------------------------------------------------------
dbg_int:
        ld      (DBG_SPRAW), sp
        ld      sp, DBG_STACK
        push    af
        push    bc
        push    de
        push    hl
        call    dbg_rd_txn
        ld      (DBG_KEY), a
        ld      a, (DBG_RUNF)
        or      a
        jr      z, di_fwd
        ld      a, (DBG_KEY)
        cp      01bh                    ; ESC?
        jr      z, di_stop
di_fwd:
        ; Not a break: forward the key into the ring buffer
        ; (the push logic of kb_poll in console.inc), so a
        ; target polling KEYIN/INKEY still receives it.
        ld      a, (DBG_KEY)
        or      a
        jr      z, di_ret
        ld      (KBPREV), a
        ld      a, (KBHEAD)
        inc     a
        and     00fh
        ld      c, a                    ; c = next head candidate
        ld      a, (KBTAIL)
        cp      c
        jr      z, di_ret               ; ring full: drop
        ld      a, (KBHEAD)
        ld      hl, KBBUF
        ld      e, a
        ld      d, 0
        add     hl, de
        ld      a, (DBG_KEY)
        ld      (hl), a
        ld      a, c
        ld      (KBHEAD), a
di_ret:
        pop     hl
        pop     de
        pop     bc
        pop     af
        ld      sp, (DBG_SPRAW)
        ei                              ; see the note at dbg_int:
        reti                            ; this core re-enables via EI
di_stop:
        ld      a, 'I'
        ld      (DBG_RSN), a
        pop     hl
        pop     de
        pop     bc
        pop     af
        jr      dbg_cap_body

; ---------------------------------------------------------------
; dbg_bp: breakpoint hit (RST 30H vector). The stack holds
; the address AFTER the patched byte; dbg_cap_body adjusts.
; dbg_retcatch: a fresh G target RETurned here.
; ---------------------------------------------------------------
dbg_bp:
        ld      (DBG_SPRAW), sp
        ld      sp, DBG_STACK
        push    af                      ; reason store must not
        ld      a, 'B'                  ; clobber the target's A
        ld      (DBG_RSN), a
        pop     af
        jr      dbg_cap_body
dbg_retcatch:
        ld      (DBG_SPRAW), sp
        ld      sp, DBG_STACK
        push    af
        ld      a, 'R'
        ld      (DBG_RSN), a
        pop     af
        jr      dbg_cap_body

; ---------------------------------------------------------------
; dbg_cap_body: capture every register into the save area.
; Entered with SP = stub stack and (DBG_SPRAW) = the target
; SP, pointing at the pushed PC (breakpoint / interrupt) or
; just past the target's balanced stack (return catcher).
; ---------------------------------------------------------------
dbg_cap_body:
        push    af
        push    bc
        push    de
        push    hl
        push    ix
        push    iy
        ex      af, af'
        push    af                      ; shadow AF
        exx
        push    bc                      ; shadow BC
        push    de                      ; shadow DE
        push    hl                      ; shadow HL
        exx
        ex      af, af'
        call    dbg_disarm              ; vector off while stub runs
        ; stub stack, top down: SHL SDE SBC SAF IY IX HL DE BC AF
        pop     hl
        ld      (DBG_SV+22), hl         ; HL'
        pop     de
        ld      (DBG_SV+20), de         ; DE'
        pop     bc
        ld      (DBG_SV+18), bc         ; BC'
        pop     af
        push    af
        pop     hl
        ld      (DBG_SV+16), hl         ; AF'
        pop     iy
        ld      (DBG_SV+0), iy
        pop     ix
        ld      (DBG_SV+2), ix
        pop     hl
        ld      (DBG_SV+4), hl
        pop     de
        ld      (DBG_SV+6), de
        pop     bc
        ld      (DBG_SV+8), bc
        pop     af
        push    af
        pop     hl
        ld      (DBG_SV+10), hl         ; AF
        ld      hl, (DBG_SPRAW)
        ld      e, (hl)
        inc     hl
        ld      d, (hl)
        ld      (DBG_SV+12), de         ; PC
        ld      hl, (DBG_SPRAW)
        inc     hl
        inc     hl
        ld      (DBG_SV+14), hl         ; SP before the push
        ld      a, i
        ld      (DBG_SV+24), a          ; I
        push    af
        pop     hl
        ld      a, l
        ld      (DBG_SV+25), a          ; flags of LD A,I: bit2=IFF2
        ; reason-specific fixups
        ld      a, (DBG_RSN)
        cp      'I'
        jr      z, dcap_int
        cp      'B'
        jr      z, dcap_bp
        cp      'R'
        jr      z, dcap_ret
        jr      dcap_done
dcap_int:
        ; The core sets its DI latch when it accepts the
        ; interrupt, so the LD A,I above always read
        ; "disabled"; but delivery itself proves the target
        ; ran with interrupts enabled. Record IFF2 = 1 so a
        ; later C resumes interruptible. (On real hardware
        ; LD A,I returns the true IFF2, and this fixup
        ; agrees with it.)
        ld      a, (DBG_SV+25)
        or      004h
        ld      (DBG_SV+25), a
        jr      dcap_done
dcap_bp:
        ld      hl, (DBG_SV+12)
        dec     hl                      ; PC = the patched byte
        ld      (DBG_SV+12), hl
        call    db_restore              ; unpatch + disarm (one-shot)
        jr      dcap_done
dcap_ret:
        ld      hl, (DBG_SPRAW)
        ld      (DBG_SV+14), hl         ; SP was already balanced
        ld      hl, dbg_retcatch
        ld      (DBG_SV+12), hl         ; PC = the catcher itself
dcap_done:
        xor     a
        ld      (DBG_RUNF), a
        jp      dbg_stopped

; ---------------------------------------------------------------
; dbg_stopped: report a stop (reason + registers) and fall
; into the prompt. The report is captured into MB_OUT; after
; a remotely started run it is also published as an
; unsolicited mailbox reply.
; ---------------------------------------------------------------
dbg_stopped:
        call    dbg_drain
        call    dbg_cap_on
        ld      hl, dbg_msg_stop
        call    dbg_puts
        ld      a, (DBG_RSN)
        call    dbg_putc
        ld      a, (DBG_RSN)
        cp      'B'
        jr      nz, ds_nobp
        ld      a, ' '
        call    dbg_putc
        ld      hl, (DBG_SV+12)
        call    dbg_hex16
ds_nobp:
        call    dbg_crlf
        call    dbg_regs
        call    dbg_cap_off
        ld      a, 1
        ld      (DBG_MB_STATE), a       ; state 1 = report
        ld      a, (DBG_SRC)            ; COMPLETE in MB_OUT
        or      a
        jr      z, ds_loc
        ld      a, (DBG_MB_FLAG)
        or      a
        jr      nz, ds_loc              ; never overwrite a
        ld      a, 2                    ; pending command flag
        ld      (DBG_MB_FLAG), a
ds_loc: xor     a
        ld      (DBG_RSN), a
        jp      dbg_prompt

; ---------------------------------------------------------------
; dbg_prompt / line input. One shared executor serves the
; local keyboard and the mailbox. Local input has two
; observers, like KEYIN: codes kb_poll pushed into the ring
; during printing are exact press events and are accepted
; as-is; between prints this loop polls the raw level
; itself. Every acceptance is recorded in BOTH edge
; filters - the stub's DBG_PREV and kb_poll's KBPREV.
; Recording only one of them breaks type-ahead in one of
; two ways (both seen in testing): a raw-accepted press
; that kb_poll had not seen is pushed into the ring while
; it echoes and delivered twice ("9000" arrived as
; "90000"); a ring-accepted press whose same-key successor
; is already down when the raw poll resumes is filtered
; as "held" and lost ("9000" arrived as "900").
; ---------------------------------------------------------------
dbg_prompt:
        ld      hl, dbg_msg_prompt
        call    dbg_puts
        ld      b, 0                    ; b = line length
dp_poll:
        ld      a, (DBG_MB_FLAG)
        cp      1
        jp      z, dp_remote
        call    kb_pop
        jr      nc, dp_ring
        call    kb_raw
        ld      c, a                    ; c = code now down
        ld      a, (DBG_PREV)
        cp      c
        jr      z, dp_poll              ; held: not an edge
        ld      a, c
        ld      (DBG_PREV), a
        ld      (KBPREV), a             ; kb_poll must not push
        or      a                       ; this press again while
        jr      z, dp_poll              ; it is being echoed
        jr      dp_have                 ; release: re-arm only
dp_ring:
        ld      (DBG_PREV), a           ; sync both edge filters
        ld      (KBPREV), a
dp_have:
        cp      0dh
        jr      z, dp_enter
        cp      08h
        jr      z, dp_bs
        cp      020h
        jr      c, dp_poll              ; other control codes ignored
        ld      c, a
        ld      a, b
        cp      60
        jr      nc, dp_poll             ; line full
        push    de
        ld      hl, LINEBUF
        ld      e, b
        ld      d, 0
        add     hl, de
        ld      (hl), c
        pop     de
        inc     b
        ld      a, c
        call    dbg_putc                ; echo
        jr      dp_poll
dp_bs:
        ld      a, b
        or      a
        jr      z, dp_poll
        dec     b
        ld      a, 08h
        call    dbg_putc
        ld      a, ' '
        call    dbg_putc
        ld      a, 08h
        call    dbg_putc
        jr      dp_poll
dp_enter:
        push    de
        ld      hl, LINEBUF
        ld      e, b
        ld      d, 0
        add     hl, de
        ld      (hl), 0
        pop     de
        call    dbg_crlf
        xor     a
        ld      (DBG_SRC), a
        call    dbg_exec
        jp      dbg_prompt
dp_remote:
        ld      hl, DBG_MB_LINE
        ld      de, LINEBUF
        ld      b, 48
dp_rcp: ld      a, (hl)
        ld      (de), a
        or      a
        jr      z, dp_rdone
        inc     hl
        inc     de
        djnz    dp_rcp
        ld      (de), a                 ; a = 0 here only if the
                                        ; last byte copied was 0;
                                        ; force termination:
        xor     a
        ld      (de), a
dp_rdone:
        xor     a
        ld      (DBG_MB_FLAG), a        ; accepted: the host's
        ld      a, 1                    ; flag protocol is
        ld      (DBG_SRC), a            ; 1 -> 0 -> 2 (reply)
        call    dbg_exec
        ; G / C / Q never return; the rest land here.
        ld      a, 2
        ld      (DBG_MB_FLAG), a
        jp      dbg_prompt

; ---------------------------------------------------------------
; dbg_exec: run one command line from LINEBUF. Output is
; captured into MB_OUT either way (local users see it on
; screen; the mailbox reader and the test harness read the
; same text back from RAM).
; ---------------------------------------------------------------
dbg_exec:
        ld      a, (LINEBUF)
        or      a
        ret     z                       ; empty line: NO capture
        call    dbg_cap_on              ; session - a phantom CR
        ld      a, (LINEBUF)            ; must not wipe MB_OUT
        call    upcase
        cp      'D'
        jp      z, dbg_c_d
        cp      'M'
        jp      z, dbg_c_m
        cp      'R'
        jp      z, dbg_c_r
        cp      'B'
        jp      z, dbg_c_b
        cp      'G'
        jp      z, dbg_c_g
        cp      'C'
        jp      z, dbg_c_c
        cp      'Q'
        jp      z, dbg_c_q
        cp      '?'
        jp      z, dbg_c_help
        or      a
        jr      z, dbg_exec_ret         ; empty line
        ld      a, '?'
        call    dbg_putc
        call    dbg_crlf
dbg_exec_ret:
        call    dbg_cap_off
        ret

; D [addr]: dump 128 bytes.
dbg_c_d:
        ld      hl, LINEBUF+1
        call    skip_sp
        call    parse_hex
        jr      c, dd_def
        ld      (DBG_DPTR), de
dd_def: ld      hl, (DBG_DPTR)
        ld      b, 8
dd_line:
        push    bc
        push    hl
        call    dbg_hex16
        ld      a, ':'
        call    dbg_putc
        pop     hl
        push    hl
        ld      b, 16
dd_b:   ld      a, ' '
        call    dbg_putc
        ld      a, (hl)
        call    dbg_hex8
        inc     hl
        djnz    dd_b
        ld      a, ' '
        call    dbg_putc
        pop     hl
        ld      b, 16
dd_a:   ld      a, (hl)
        cp      020h
        jr      c, dd_dot
        cp      07fh
        jr      c, dd_ch
dd_dot: ld      a, '.'
dd_ch:  call    dbg_putc
        inc     hl
        djnz    dd_a
        call    dbg_crlf
        pop     bc
        djnz    dd_line
        ld      (DBG_DPTR), hl
        jp      dbg_exec_ret

; M addr b..: deposit bytes.
dbg_c_m:
        ld      hl, LINEBUF+1
        call    skip_sp
        call    parse_hex
        jr      c, dbg_bad
        ld      (DBG_TMPW), de
dgm_lp: call    skip_sp
        call    parse_hex
        jr      c, dgm_done
        ld      a, e
        push    hl
        ld      hl, (DBG_TMPW)
        ld      (hl), a
        inc     hl
        ld      (DBG_TMPW), hl
        pop     hl
        jr      dgm_lp
dgm_done:
        ld      hl, dbg_msg_ok
        call    dbg_puts
        jp      dbg_exec_ret
dbg_bad:
        ld      a, '?'
        call    dbg_putc
        call    dbg_crlf
        jp      dbg_exec_ret

; R: register display.
dbg_c_r:
        call    dbg_regs
        jp      dbg_exec_ret

; dbg_regs: print the save area (also used by dbg_stopped).
dbg_regs:
        ld      hl, dbg_s_pc
        ld      de, (DBG_SV+12)
        call    dbg_pf
        ld      hl, dbg_s_sp
        ld      de, (DBG_SV+14)
        call    dbg_pf
        ld      hl, dbg_s_af
        ld      de, (DBG_SV+10)
        call    dbg_pf
        ld      hl, dbg_s_bc
        ld      de, (DBG_SV+8)
        call    dbg_pf
        ld      hl, dbg_s_de
        ld      de, (DBG_SV+6)
        call    dbg_pf
        ld      hl, dbg_s_hl
        ld      de, (DBG_SV+4)
        call    dbg_pf
        call    dbg_crlf
        ld      hl, dbg_s_ix
        ld      de, (DBG_SV+2)
        call    dbg_pf
        ld      hl, dbg_s_iy
        ld      de, (DBG_SV+0)
        call    dbg_pf
        ld      hl, dbg_s_saf
        ld      de, (DBG_SV+16)
        call    dbg_pf
        ld      hl, dbg_s_sbc
        ld      de, (DBG_SV+18)
        call    dbg_pf
        ld      hl, dbg_s_sde
        ld      de, (DBG_SV+20)
        call    dbg_pf
        ld      hl, dbg_s_shl
        ld      de, (DBG_SV+22)
        call    dbg_pf
        ld      hl, dbg_s_i
        call    dbg_puts
        ld      a, (DBG_SV+24)
        call    dbg_hex8
        call    dbg_crlf
        ret
; dbg_pf: print "LABEL" + hex16(de) + ' '. hl = label.
dbg_pf:
        push    de
        call    dbg_puts
        pop     hl
        call    dbg_hex16
        ld      a, ' '
        call    dbg_putc
        ret

; B [addr]: set / clear the breakpoint.
dbg_c_b:
        ld      hl, LINEBUF+1
        call    skip_sp
        call    parse_hex
        jr      c, db_clear
        push    de
        call    db_restore              ; unpatch any old one
        pop     de
        ld      (DBG_BPA), de
        ld      h, d
        ld      l, e
        ld      a, (hl)
        ld      (DBG_BPB), a
        ld      a, 0F7h                 ; RST 30H
        ld      (hl), a
        ld      a, 1
        ld      (DBG_BPS), a
        ld      hl, dbg_msg_bp
        call    dbg_puts
        ld      hl, (DBG_BPA)
        call    dbg_hex16
        call    dbg_crlf
        jp      dbg_exec_ret
db_clear:
        call    db_restore
        ld      hl, dbg_msg_bpoff
        call    dbg_puts
        call    dbg_crlf
        jp      dbg_exec_ret
; db_restore: unpatch the breakpoint if one is armed.
db_restore:
        ld      a, (DBG_BPS)
        or      a
        ret     z
        ld      hl, (DBG_BPA)
        ld      a, (DBG_BPB)
        ld      (hl), a
        xor     a
        ld      (DBG_BPS), a
        ret

; G [addr]: run. With an address: fresh context - PC = addr,
; SP = STACKTOP with the return catcher planted under it,
; IFF2 forced on so a fresh run is always ESC-breakable.
; Without an address, G means C.
dbg_c_g:
        ld      hl, LINEBUF+1
        call    skip_sp
        call    parse_hex
        jr      c, dbg_c_c
        ld      (DBG_SV+12), de         ; PC = addr
        ld      hl, STACKTOP-2
        ld      (DBG_SV+14), hl         ; SP
        ld      de, dbg_retcatch
        ld      (hl), e
        inc     hl
        ld      (hl), d
        ld      a, 004h
        ld      (DBG_SV+25), a          ; IFF2 = 1
        ld      hl, dbg_msg_go
        call    dbg_puts
        ld      hl, (DBG_SV+12)
        call    dbg_hex16
        call    dbg_crlf
        jp      dbg_resume
; C: continue from the captured PC.
dbg_c_c:
        ld      hl, dbg_msg_cont
        call    dbg_puts
        ld      hl, (DBG_SV+12)
        call    dbg_hex16
        call    dbg_crlf
        jp      dbg_resume

; dbg_resume: restore everything and RETI into the target.
; The attention vector is armed first and the target always
; runs in IM 1; EI is the last step before RETI iff the
; captured IFF2 was set (the tail instruction is patched).
dbg_resume:
        ld      a, 038h
        call    dbg_setvec
        im      1
        ld      a, (DBG_SV+25)
        and     004h
        ld      a, 000h
        jr      z, dr_ei
        ld      a, 0FBh                 ; EI
dr_ei:  ld      (dbg_ei_slot), a
        ld      a, 1
        ld      (DBG_RUNF), a
        ld      a, 2
        ld      (DBG_MB_STATE), a
        call    dbg_cap_off
        ld      a, (DBG_SRC)
        or      a
        jr      z, dr_nack
        ld      a, 2
        ld      (DBG_MB_FLAG), a        ; GO / CONT ack
dr_nack:
        ld      a, (DBG_SV+24)
        ld      i, a                    ; I
        ld      hl, (DBG_SV+16)
        push    hl                      ; AF'
        ld      hl, (DBG_SV+22)
        push    hl                      ; HL'
        ld      hl, (DBG_SV+20)
        push    hl                      ; DE'
        ld      hl, (DBG_SV+18)
        push    hl                      ; BC' (top)
        exx
        ex      af, af'
        pop     bc
        pop     de
        pop     hl
        pop     af
        exx
        ex      af, af'
        ld      hl, (DBG_SV+14)
        dec     hl
        dec     hl
        ld      (DBG_SV+14), hl         ; SP slot for the PC
        ld      de, (DBG_SV+12)
        ld      (hl), e
        inc     hl
        ld      (hl), d                 ; PC planted on target stack
        ld      sp, DBG_SV              ; save-area image, low to high:
        pop     iy                      ; IY IX HL DE BC AF
        pop     ix
        pop     hl
        pop     de
        pop     bc
        pop     af
        ld      sp, (DBG_SV+14)
dbg_ei_slot:
        nop                             ; patched to EI when IFF2 set
        reti

; Q: quit to the shell (breakpoint unpatched, vector off).
dbg_c_q:
        call    db_restore
        call    dbg_disarm
        ld      hl, dbg_msg_bye
        call    dbg_puts
        call    dbg_crlf
        call    dbg_cap_off
        ld      a, (DBG_SRC)
        or      a
        jr      z, dq_loc
        ld      a, 2
        ld      (DBG_MB_FLAG), a
dq_loc: xor     a
        ld      (DBG_MB_STATE), a
        ld      (DBG_RUNF), a
        jp      shell_loop

; ?: help.
dbg_c_help:
        ld      hl, dbg_msg_help
        call    dbg_puts
        jp      dbg_exec_ret

; ---------------------------------------------------------------
; Output: dbg_putc writes to the console (IOCS CONOUT) and,
; while capture is on, appends to MB_OUT. Preserves all
; registers except a.
; ---------------------------------------------------------------
dbg_putc:
        push    bc
        push    de
        push    hl
        ld      c, a
        call    C_CONOUT
        ld      a, (DBG_CAPF)
        or      a
        jr      z, dpc_ret
        ld      a, (DBG_MB_OLEN)
        cp      0FEh
        jr      nc, dpc_ret
        ld      hl, DBG_MB_OUT
        ld      e, a
        ld      d, 0
        add     hl, de
        ld      (hl), c
        inc     a
        ld      (DBG_MB_OLEN), a
dpc_ret:
        pop     hl
        pop     de
        pop     bc
        ret
dbg_puts:
        ld      a, (hl)
        or      a
        ret     z
        call    dbg_putc
        inc     hl
        jr      dbg_puts
dbg_crlf:
        ld      a, 0dh
        call    dbg_putc
        ld      a, 0ah
        jp      dbg_putc
dbg_cap_on:
        xor     a
        ld      (DBG_MB_OLEN), a
        ld      a, 1
        ld      (DBG_CAPF), a
        ret
dbg_cap_off:
        xor     a
        ld      (DBG_CAPF), a
        ret
dbg_hex16:
        ld      a, h
        call    dbg_hex8
        ld      a, l
        jp      dbg_hex8
dbg_hex8:
        push    af
        rrca
        rrca
        rrca
        rrca
        call    dbg_hnib
        pop     af
dbg_hnib:
        and     00fh
        cp      00ah
        jr      c, dhn_n
        add     a, 007h
dhn_n:  add     a, 030h
        jp      dbg_putc

; ---------------------------------------------------------------
; Strings
; ---------------------------------------------------------------
dbg_msg_banner:
        db      "DEBUG STUB READY. ? = HELP", 13, 10, 0
dbg_msg_prompt:
        db      "DBG> ", 0
dbg_msg_stop:
        db      13, 10, "STOP ", 0
dbg_msg_bp:
        db      "BP ", 0
dbg_msg_bpoff:
        db      "BP OFF", 0
dbg_msg_ok:
        db      "OK", 13, 10, 0
dbg_msg_go:
        db      "GO ", 0
dbg_msg_cont:
        db      "CONT ", 0
dbg_msg_bye:
        db      "BYE", 0
dbg_msg_help:
        db      "D [A] / M A B.. / R / B [A] / G [A] / C / Q", 13, 10
        db      "ESC BREAKS A RUNNING TARGET. BP IS ONE-SHOT.", 13, 10, 0
dbg_s_pc:       db      "PC=", 0
dbg_s_sp:       db      "SP=", 0
dbg_s_af:       db      "AF=", 0
dbg_s_bc:       db      "BC=", 0
dbg_s_de:       db      "DE=", 0
dbg_s_hl:       db      "HL=", 0
dbg_s_ix:       db      "IX=", 0
dbg_s_iy:       db      "IY=", 0
dbg_s_saf:      db      "AF'=", 0
dbg_s_sbc:      db      "BC'=", 0
dbg_s_sde:      db      "DE'=", 0
dbg_s_shl:      db      "HL'=", 0
dbg_s_i:        db      "I=", 0
