; machine_x1.inc - X1 machine constants (see tapeos.z80).
SPACE_CODE      equ     020h            ; raw VRAM code of blank
LOADADDR        equ     02000h
IOCS_BASE       equ     0e400h
SIGADDR         equ     0e100h          ; harness signature
LASTCMD         equ     0e110h          ; 32 bytes: last command line
KEYLOGN         equ     0e13fh          ; 1 byte: logged key count
KEYLOG          equ     0e140h          ; 64 bytes: accepted key codes
LINEBUF         equ     0e300h          ; shell line input buffer
SUBBUF          equ     0e380h          ; E-command input buffer
STACKTOP        equ     0e000h
SUBCPU          equ     01900h          ; 80C49 data port
PPIB            equ     01a01h          ; 8255 port B
PPIC            equ     01a02h          ; 8255 port C (bit0 = tape out)
TAPEBUF         equ     0e380h          ; tape info block (shares SUBBUF)
KBBUF           equ     0e340h          ; 16-byte key ring buffer (M4)
KBHEAD          equ     0e350h          ; ring write index (0..15)
KBTAIL          equ     0e351h          ; ring read index (0..15)
KBPREV          equ     0e352h          ; last code seen by any poller
NAMEBUF         equ     0e358h          ; FD file name (10 chars, padded)
TNAMEBUF        equ     0e368h          ; tape program name (13, padded)
DIRBUF          equ     0e500h          ; FD directory sector (256 bytes)
FNTBUF          equ     0e800h          ; Misaki font sector cache (256 bytes)
KFSIG           equ     0e6d0h          ; K writes "MSK8-OK" here on success
EMM_L           equ     00d00h          ; EMM address bits 0..7 (M5)
EMM_M           equ     00d01h          ; EMM address bits 8..15
EMM_H           equ     00d02h          ; EMM address bits 16..23
EMM_D           equ     00d03h          ; EMM data (auto-increment)
RDSIG           equ     0e6e0h          ; "RD-OK" + blocks (harness)
RDMAX           equ     256             ; RAMDISK max blocks (64 KB)
FDCR            equ     00ff8h          ; FDC command/status register
FDTR            equ     00ff9h          ; FDC track register
FDSR            equ     00ffah          ; FDC sector register
FDDR            equ     00ffbh          ; FDC data register
FDNO            equ     00ffch          ; drive / side / motor select
DIRREC          equ     32              ; directory record number
DATAREC0        equ     48              ; first file-data record
MAXREC          equ     1280            ; 80 units x 16 records (320 KB)
SCRW            equ     40
SCRH            equ     25

