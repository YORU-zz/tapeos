; common.inc - IOCS entry numbers and call addresses,
; shared by all targets. IOCS_BASE comes from the machine inc.
; IOCS entry numbers
I_INKEY         equ     0
I_KEYIN         equ     1
I_CONOUT        equ     2
I_PRTMSG        equ     3
I_CRLF          equ     4
I_GETLIN        equ     5
I_CLRSCR        equ     6
I_LOCATE        equ     7
I_HEXOUT        equ     8
I_HEX16         equ     9
I_TINF          equ     10
I_TDAT          equ     11
I_TWINF         equ     12
I_TWDAT         equ     13
I_TCTRL         equ     14
I_DREAD         equ     15
I_DWRITE        equ     16
I_MINIT         equ     17              ; extended IOCS (ep.31),
I_MDIR          equ     18              ; X1 only: main-memory
I_MSTORE        equ     19              ; pool (see extiocs_x1.inc)
I_BEEP          equ     21              ; PSG beep (body: X1, roadmap 26)
I_RTGET         equ     22              ; RTC read (body: X1, roadmap 26)
I_BREAK         equ     23              ; break flag (body: X1, roadmap 26)
I_RREAD         equ     24
I_RWRITE        equ     25
I_RINIT         equ     26
I_MFREE         equ     27              ; extended IOCS (ep.31), X1
        IF      DEFINED TARGET_X1
I_SIN           equ     28              ; serial in  (X1 only, roadmap 26)
I_SOUT          equ     29              ; serial out (X1 only, roadmap 26)
I_SPOLL         equ     30              ; serial poll (X1 only, roadmap 26)
        ENDIF

; absolute addresses of the table entries (call targets)
C_INKEY         equ     IOCS_BASE + 3 * I_INKEY
C_KEYIN         equ     IOCS_BASE + 3 * I_KEYIN
C_CONOUT        equ     IOCS_BASE + 3 * I_CONOUT
C_PRTMSG        equ     IOCS_BASE + 3 * I_PRTMSG
C_CRLF          equ     IOCS_BASE + 3 * I_CRLF
C_GETLIN        equ     IOCS_BASE + 3 * I_GETLIN
C_CLRSCR        equ     IOCS_BASE + 3 * I_CLRSCR
C_LOCATE        equ     IOCS_BASE + 3 * I_LOCATE
C_HEXOUT        equ     IOCS_BASE + 3 * I_HEXOUT
C_HEX16         equ     IOCS_BASE + 3 * I_HEX16
C_TINF          equ     IOCS_BASE + 3 * I_TINF
C_TDAT          equ     IOCS_BASE + 3 * I_TDAT
C_TWINF         equ     IOCS_BASE + 3 * I_TWINF
C_TWDAT         equ     IOCS_BASE + 3 * I_TWDAT
C_TCTRL         equ     IOCS_BASE + 3 * I_TCTRL
C_DREAD         equ     IOCS_BASE + 3 * I_DREAD
C_DWRITE        equ     IOCS_BASE + 3 * I_DWRITE
C_MINIT         equ     IOCS_BASE + 3 * I_MINIT
C_MDIR          equ     IOCS_BASE + 3 * I_MDIR
C_MSTORE        equ     IOCS_BASE + 3 * I_MSTORE
C_RREAD         equ     IOCS_BASE + 3 * I_RREAD
C_RWRITE        equ     IOCS_BASE + 3 * I_RWRITE
C_RINIT         equ     IOCS_BASE + 3 * I_RINIT
C_MFREE         equ     IOCS_BASE + 3 * I_MFREE
C_BEEP          equ     IOCS_BASE + 3 * I_BEEP
C_RTGET         equ     IOCS_BASE + 3 * I_RTGET
C_BREAK         equ     IOCS_BASE + 3 * I_BREAK
        IF      DEFINED TARGET_X1
C_SIN           equ     IOCS_BASE + 3 * I_SIN
C_SOUT          equ     IOCS_BASE + 3 * I_SOUT
C_SPOLL         equ     IOCS_BASE + 3 * I_SPOLL
        ENDIF

