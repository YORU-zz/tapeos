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
; The kf_* kanji work area and font data used to live here;
; they moved into the KANJI TOSD program (roadmap 26 -
; the shell's K command was demoted to a disk program).

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
        db      "H D E M G T S A F W R U I C X V N", 13, 10
        db      "FULL HELP IS THE 'HELP' PROGRAM ON DISK", 13, 10
        db      "KANJI IS THE 'KANJI' PROGRAM ON DISK", 0
msg_tload:
        db      " LOADED", 13, 10, 0
msg_tsave:
        db      "SAVED TO TAPE", 13, 10, 0
msg_fdsave:
        db      " SAVED", 13, 10, 0
msg_nofile:
        db      "NO FILES", 13, 10, 0
msg_demo:
        db      "DEMO IS ON DISK: R DEMO1, THEN G", 13, 10, 0
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

