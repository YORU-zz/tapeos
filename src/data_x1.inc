; data_x1.inc - X1 variables and messages.
; ---------------------------------------------------------------
; Data
; ---------------------------------------------------------------
cur_x:
        db      0
cur_y:
        db      0
cur_attr:
        db      07h
dump_ptr:
        dw      LOADADDR
sc_src:
        dw      0
sc_dst:
        dw      0
sc_tc:
        db      0
sc_ta:
        db      0
tp_byte:
        db      0
tp_shadow:
        db      0
tp_zcnt:
        db      0
tp_cksum:
        dw      0
tp_named:
        db      0
sv_addr:
        dw      0
sv_len:
        dw      0
sv_append:
        db      0
sv_name:
        db      "TAPEOS"
sv_ext:
        db      "Sys"
kb_tmp:
        db      0
fd_rec:
        dw      0
fd_buf:
        dw      0
fd_cmd:
        db      0
fd_sec:
        db      0
fd_side:
        db      0
fd_trk:
        db      0
fd_bcnt:
        db      0
rd_blocks:
        dw      0                       ; configured RAMDISK blocks
rd_req:
        dw      0
rd_blk:
        dw      0
rd_buf:
        dw      0
rd_cnt:
        db      0
cur_drv:
        db      0                       ; 0 = FD, 1 = RAMDISK (M5)
fs_cap:
        dw      MAXREC
ri_savdrv:
        db      0
dir_magic:
        db      "TOSD"
fw_addr:
        dw      0
fw_len:
        dw      0
fw_sec:
        dw      0
fw_start:
        dw      0
fw_entry:
        dw      0
fw_rec:
        dw      0
fw_ptr:
        dw      0
fw_cnt:
        dw      0
fw_newfree:
        dw      0
fr_start:
        dw      0
fr_size:
        dw      0
fr_ptr:
        dw      0
fr_rec:
        dw      0
fr_full:
        db      0
fr_rem:
        db      0
kf_cell:
        dw      0
kf_cc:
        dw      0
kf_strp:
        dw      0
kf_start:
        dw      0
kf_rec:
        dw      0
kf_dx:
        db      0
kf_dy:
        db      0
kf_r:
        db      0
kf_j0:
        db      0
kf_byte:
        db      0
kf_ku:
        db      0
kf_ten:
        db      0
kf_off:
        db      0
kf_rem:
        db      0
kf_xhi:
        db      0
kf_xlo:
        db      0
kf_gbuf:
        db      0, 0, 0, 0, 0, 0, 0, 0

; Misaki font support data (see cmd_kanji). The hand-made
; 16x16 glyphs that M4 used here were removed: the Misaki
; font covers all of JIS X 0208, including OU and SHOU.
kf_name:
        db      "MISAKI8   "
kf_magic:
        db      "MSK8"
kf_sig:
        db      "MSK8-OK"
; 2x expansion table: nibble -> byte with every bit doubled
kf_xtab:
        db      000h, 003h, 00ch, 00fh
        db      030h, 033h, 03ch, 03fh
        db      0c0h, 0c3h, 0cch, 0cfh
        db      0f0h, 0f3h, 0fch, 0ffh
; Demo strings as JIS X 0208 (ku, ten) pairs, 0-terminated.
kf_str1:                                ; OU SHOU (the king)
        db      18, 6, 30, 13, 0
kf_str2:                                ; "konnichiha, oushou no
                                        ;  sekai e youkoso"
        db      4, 19, 4, 83, 4, 43, 4, 33, 4, 47, 1, 2
        db      18, 6, 30, 13, 4, 46, 32, 4, 19, 6, 4, 56
        db      4, 72, 4, 6, 4, 19, 4, 29, 0

sig_text:
        db      "TAPEOS-M2-SHELL"

msg_title:
        db      "T A P E O S", 13, 10, 0
msg_sub:
        db      "X1 SINGLETASK OS - MONITOR SHELL", 13, 10
        db      "IOCS TABLE AT 0E400H", 13, 10, 0
msg_prompt:
        db      13, 10, "> ", 0
msg_err:
        db      "?", 0
msg_ver:
        db      "TAPEOS M5 - OUROBO/ROS REPRO PROJECT", 0
msg_help:
        db      "H: HELP   D [A]: DUMP 128 BYTES", 13, 10
        db      "E A: EXAMINE/MODIFY BYTES", 13, 10
        db      "M A B..: WRITE BYTES", 13, 10
        db      "G [A]: CALL (NO ARG = DEMO)", 13, 10
        db      "T [NAME]: LOAD FROM TAPE", 13, 10
        db      "S A L [NAME]: SAVE TO TAPE (NEW)", 13, 10
        db      "A A L [NAME]: APPEND TO TAPE", 13, 10
        db      "F: LIST FILES (CURRENT DRIVE)", 13, 10
        db      "W NAME A L: SAVE TO DRIVE", 13, 10
        db      "R NAME: LOAD FROM DRIVE", 13, 10
        db      "U [D]: DRIVE SELECT (0=FD 1=RAMDISK)", 13, 10
        db      "I [N]: INIT RAMDISK (N BLOCKS, HEX)", 13, 10
        db      "K: KANJI DEMO   V: VERSION", 13, 10
        IF      DEFINED EXTIOCS
        db      "N: MEMORY (EP.31)", 13, 10
        ENDIF
        db      "X: RESIDENT DEBUG STUB", 0
msg_tload:
        db      " LOADED", 13, 10, 0
msg_tsave:
        db      "SAVED TO TAPE", 13, 10, 0
msg_fdsave:
        db      " SAVED", 13, 10, 0
msg_nofile:
        db      "NO FILES", 13, 10, 0
msg_kanji:
        db      "JAPANESE BY MISAKI FONT (ON FD)", 13, 10, 0
msg_kferr:
        db      "MISAKI8 FONT NOT FOUND ON DISK", 13, 10, 0
msg_demo:
        db      "HELLO FROM G", 13, 10, 0
msg_drv0:
        db      "DRIVE 0: FD", 13, 10, 0
msg_drv1:
        db      "DRIVE 1: RAMDISK", 13, 10, 0
msg_rdtag:
        db      "RAMDISK:", 13, 10, 0
msg_rdnone:
        db      "RAMDISK NOT INITIALIZED", 13, 10, 0
msg_rdpre:
        db      "RAMDISK ", 0
msg_rdsuf:
        db      " BLOCKS (X256 BYTES)", 13, 10, 0
rd_sigtext:
        db      "RD-OK"

