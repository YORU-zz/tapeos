; template_x1.inc - X1 IOCS jump table template
; (copied to IOCS_BASE at startup).
; ---------------------------------------------------------------
; IOCS jump table template (copied to IOCS_BASE at startup)
; ---------------------------------------------------------------
iocs_template:
        jp      i_inkey                 ;  0 INKEY
        jp      i_keyin                 ;  1 KEYIN
        jp      i_conout                ;  2 CONOUT
        jp      i_prtmsg                ;  3 PRTMSG
        jp      i_crlf                  ;  4 CRLF
        jp      i_getlin                ;  5 GETLIN
        jp      i_clrscr                ;  6 CLRSCR
        jp      i_locate                ;  7 LOCATE
        jp      i_hexout                ;  8 HEXOUT
        jp      i_hex16                 ;  9 HEX16
        jp      i_tinf                  ; 10 TINF   (tape)
        jp      i_tdat                  ; 11 TDAT   (tape)
        jp      i_twinf                 ; 12 TWINF  (tape)
        jp      i_twdat                 ; 13 TWDAT  (tape)
        jp      i_tctrl                 ; 14 TCTRL  (tape)
        jp      i_dread                 ; 15 DREAD  (disk) (M4)
        jp      i_dwrite                ; 16 DWRITE (disk) (M4)
        IF      DEFINED EXTIOCS
        jp      i_minit                 ; 17 MINIT  (memory) (ep.31)
        jp      i_mdir                  ; 18 MDIR   (memory) (ep.31)
        jp      i_mstore                ; 19 MSTORE (memory) (ep.31)
        ELSE
        jp      i_stub                  ; 17 PRTOUT (printer)
        jp      i_stub                  ; 18 SIN    (serial)
        jp      i_stub                  ; 19 SOUT   (serial)
        ENDIF
        jp      i_delay                 ; 20 DELAY
        jp      i_stub                  ; 21 BEEP   (PSG)
        jp      i_stub                  ; 22 RTGET  (RTC)
        jp      i_stub                  ; 23 BREAK
        jp      i_rread                 ; 24 RREAD  (RAMDISK) (M5)
        jp      i_rwrite                ; 25 RWRITE (RAMDISK) (M5)
        jp      i_rinit                 ; 26 RINIT  (RAMDISK) (M5)
        IF      DEFINED EXTIOCS
        jp      i_mfree                 ; 27 MFREE  (memory) (ep.31)
        ELSE
        jp      i_stub                  ; 27 (reserved)
        ENDIF

