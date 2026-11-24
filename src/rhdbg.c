/* rhdbg.c - RHDBG: the host-side upper layer of the
   two-layer remote debugger (roadmap 26), in SmallC.
   It speaks RDP/1 (see remote-debug-protocol.md) to
   RSTUB on the target machine, over IOCS SIN/SOUT.

   Language choice (see debugger-ep56.md for the full
   account): the fiction's upper layer is TinyBASIC,
   and TinyBASIC was tried first. It cannot carry
   this program: FMAX caps a program at 16 functions,
   and a program using the Scheme gate must fit a
   4 KB compiled body (SCMCORE lands at 0x9000) -
   the TinyBASIC draft measured 8,157 bytes, and
   5,114 even after the frame pump was moved into
   ML. The brief's sanctioned fallback, SmallC/TOS,
   compiles the same logic densely enough to fit.
   The byte pumping goes through RHIO (src/rhio.z80),
   a small ML block routine at 0x7F00 called via the
   Scheme gate's (call) primitive (roadmap 61);
   per-byte Scheme calls would exhaust Tiny Scheme's
   no-GC arena. RHIO's buffer is the shell's resident
   Scheme seat (scratch use; address substituted at
   build time as @BUF@). The demo binary sits in
   host RAM at 0x8F00 (shell-loaded, RHDEMO file).

   Keys (single letters): L load demo, B set BP +
   run, R registers, D dump at PC, S stack dump,
   W patch maxpass=9, C continue (BP re-armed),
   G go (BP cleared), Q quit.
   Evidence at 0xE680: "RH", stops, last reason,
   ping ok, load ok, maxpass readback, demo count,
   link errors at 0xE68A, quit-done (81) at 0xE68C. */

char pb[110];
char rb[40];
int stops, lastrsn, pcval, spval;
int linkerr, mpval, dcount;

void prhex2(int v)
{
    int d;
    d = (v >> 4) & 15;
    putchar(d < 10 ? '0' + d : 'A' + d - 10);
    d = v & 15;
    putchar(d < 10 ? '0' + d : 'A' + d - 10);
}

void prhex4(int v)
{
    prhex2((v >> 8) & 255);
    prhex2(v & 255);
}

int recvn(int n)
{
    poke(0x7FE0, n);
    return scmeval("(call @RECV@)");
}

/* send the frame whose payload is pb[0..len) */
void send_frame(int cmd, int len)
{
    int i, chk;
    poke(@BUF@, 0xA5);
    poke(@BUF@ + 1, cmd);
    poke(@BUF@ + 2, len);
    chk = cmd ^ len;
    for (i = 0; i < len; i++) {
        poke(@BUF@ + 3 + i, pb[i]);
        chk = chk ^ (pb[i] & 255);
    }
    poke(@BUF@ + 3 + len, chk & 255);
    poke(0x7FE0, len + 4);
    scmeval("(call @SEND@)");
}

/* receive one frame; payload -> rb[0..flen).
   Returns the status byte, -1 on timeout, -2 on a
   checksum error. */
int recv_frame()
{
    int b, fst, flen, i, chk, tries;
    tries = 0;
    b = 0;
    while (b != 0x5A) {
        if (recvn(1) == 0) {
            if (++tries > 40)
                return -1;
            b = 0;
        } else {
            b = peek(@BUF@);
        }
    }
    if (recvn(1) == 0)
        return -1;
    fst = peek(@BUF@);
    if (recvn(1) == 0)
        return -1;
    flen = peek(@BUF@);
    if (recvn(flen + 1) != flen + 1)
        return -1;
    chk = fst ^ flen;
    for (i = 0; i < flen; i++) {
        rb[i] = peek(@BUF@ + i);
        chk = chk ^ (rb[i] & 255);
    }
    if ((chk & 255) != peek(@BUF@ + flen))
        return -2;
    return fst;
}

/* send a command and wait for its OK reply; a NAK
   (or a garbled reply) resends, 3 tries in total.
   Returns 0 on OK, -1 on failure. */
int transact(int cmd, int len)
{
    int t, fst;
    for (t = 0; t < 3; t++) {
        send_frame(cmd, len);
        fst = recv_frame();
        if (fst == 0)
            return 0;
        if (fst == -1) {
            linkerr++;
            poke(0xE68A, linkerr);
            return -1;
        }
    }
    linkerr++;
    poke(0xE68A, linkerr);
    return -1;
}

/* wait for the async STOP frame; fills pcval/spval */
int waitstop()
{
    int fst;
    fst = recv_frame();
    while (fst != 0x80) {
        if (fst == -1) {
            linkerr++;
            poke(0xE68A, linkerr);
            return -1;
        }
        fst = recv_frame();
    }
    lastrsn = rb[0] & 255;
    pcval = (rb[13] & 255) + (rb[14] & 255) * 256;
    spval = (rb[15] & 255) + (rb[16] & 255) * 256;
    stops++;
    poke(0xE682, stops);
    poke(0xE683, lastrsn);
    return 0;
}

/* register dump; the block sits in rb at off
   (1 in a STOP payload, 0 in a REGS reply):
   IY IX HL DE BC AF PC SP AF' BC' DE' HL' I flags */
void showregs(int off)
{
    printf("PC=");
    prhex4((rb[off + 13] & 255) * 256 + (rb[off + 12] & 255));
    printf(" SP=");
    prhex4((rb[off + 15] & 255) * 256 + (rb[off + 14] & 255));
    printf(" AF=");
    prhex4((rb[off + 11] & 255) * 256 + (rb[off + 10] & 255));
    printf(" BC=");
    prhex4((rb[off + 9] & 255) * 256 + (rb[off + 8] & 255));
    printf(" DE=");
    prhex4((rb[off + 7] & 255) * 256 + (rb[off + 6] & 255));
    printf(" HL=");
    prhex4((rb[off + 5] & 255) * 256 + (rb[off + 4] & 255));
    putchar('\n');
    printf("IX=");
    prhex4((rb[off + 3] & 255) * 256 + (rb[off + 2] & 255));
    printf(" IY=");
    prhex4((rb[off + 1] & 255) * 256 + (rb[off + 0] & 255));
    printf(" AF'=");
    prhex4((rb[off + 17] & 255) * 256 + (rb[off + 16] & 255));
    printf(" BC'=");
    prhex4((rb[off + 19] & 255) * 256 + (rb[off + 18] & 255));
    printf(" DE'=");
    prhex4((rb[off + 21] & 255) * 256 + (rb[off + 20] & 255));
    printf(" HL'=");
    prhex4((rb[off + 23] & 255) * 256 + (rb[off + 22] & 255));
    putchar('\n');
}

void cmd_load()
{
    int i, r;
    r = transact(1, 0);
    if (r != 0) {
        puts("PING FAILED");
        return;
    }
    if (rb[0] == 'R' && rb[1] == 'D' && rb[2] == 1) {
        poke(0xE684, 1);
        puts("LINK OK");
    } else {
        puts("PING BAD REPLY");
        return;
    }
    pb[0] = 0;
    pb[1] = 128;
    for (i = 0; i < @DEMOLEN@; i++)
        pb[2 + i] = peek(0x8F00 + i);
    r = transact(3, @DEMOLEN@ + 2);
    if (r != 0) {
        puts("LOAD WRITE FAILED");
        return;
    }
    poke(0xE685, 1);
    puts("DEMO LOADED");
}

/* cont=0: fresh RUN at 8000 with BP armed.
   cont=1: continue (RUN FFFF) with BP re-armed.
   cont=2: continue with BP cleared (runs to the end). */
void cmd_run(int cont)
{
    int r;
    if (cont == 2) {
        pb[0] = 0;
        pb[1] = 0;
    } else {
        pb[0] = @SMEXEC@ & 255;
        pb[1] = @SMEXEC@ >> 8;
    }
    r = transact(4, 2);
    if (r != 0) {
        puts("SETBP FAILED");
        return;
    }
    if (cont == 0) {
        pb[0] = 0;
        pb[1] = 128;
    } else {
        pb[0] = 255;
        pb[1] = 255;
    }
    send_frame(5, 2);
    puts("RUNNING...");
    if (waitstop() != 0) {
        puts("NO STOP");
        return;
    }
    printf("STOP %c PC=", lastrsn);
    prhex4(pcval);
    putchar('\n');
    showregs(1);
}

int dread(int addr, int n)
{
    pb[0] = addr & 255;
    pb[1] = (addr >> 8) & 255;
    pb[2] = n;
    return transact(2, 3);
}

void cmd_read(int addr, int n)
{
    int i;
    if (dread(addr, n) != 0) {
        puts("READ FAILED");
        return;
    }
    prhex4(addr);
    printf(": ");
    for (i = 0; i < n; i++) {
        prhex2(rb[i] & 255);
        putchar(' ');
    }
    putchar('\n');
}

void cmd_write()
{
    int r;
    pb[0] = @MAXPASS@ & 255;
    pb[1] = @MAXPASS@ >> 8;
    pb[2] = 9;
    r = transact(3, 3);
    if (r != 0) {
        puts("WRITE FAILED");
        return;
    }
    r = dread(@MAXPASS@, 1);
    if (r != 0) {
        puts("RB FAILED");
        return;
    }
    mpval = rb[0] & 255;
    poke(0xE686, mpval);
    printf("MAXPASS NOW %d\n", mpval);
}

void cmd_quit()
{
    int r, i;
    /* read the demo's signature block before QUIT */
    r = dread(0xE670, 6);
    if (r == 0) {
        printf("DEMO SIG: ");
        for (i = 0; i < 5; i++)
            putchar(rb[i]);
        dcount = rb[5] & 255;
        printf(" COUNT=%d\n", dcount);
    } else {
        linkerr++;
        poke(0xE68A, linkerr);
    }
    send_frame(7, 0);
    recv_frame();
    /* evidence pokes come LAST: the host harness exits
       on 0xE68C, and the QUIT frame must be fully sent
       and answered before that can happen */
    poke(0xE687, dcount);
    poke(0xE68C, 81);
    puts("BYE");
}

int waitkey()
{
    int k;
    k = inkey();
    while (k == 0)
        k = inkey();
    while (inkey() != 0)
        k = k;
    return k;
}

int main()
{
    int k, r;
    stops = 0;
    lastrsn = 0;
    pcval = 0;
    spval = 0;
    linkerr = 0;
    mpval = 0;
    dcount = 0;
    poke(0xE680, 'R');
    poke(0xE681, 'H');
    /* probe the gate once (a 1-byte RECV that times
       out): if SCMCORE is absent, scmerr() says so
       and nothing else in this program can work */
    poke(0x7FE0, 1);
    r = scmeval("(call @RECV@)");
    if (scmerr() != 0) {
        puts("NO SCMCORE");
        return 1;
    }
    puts("RHDBG - REMOTE DEBUGGER HOST");
    puts("KEYS: L B R D S W C G Q");
    k = 0;
    while (k != 'q') {
        printf("RH>");
        k = waitkey();
        if (k == 'l')
            cmd_load();
        if (k == 'b')
            cmd_run(0);
        if (k == 'r') {
            if (transact(6, 0) == 0)
                showregs(0);
            else
                puts("REGS FAILED");
        }
        if (k == 'd')
            cmd_read(pcval, 16);
        if (k == 's')
            cmd_read(spval, 16);
        if (k == 'w')
            cmd_write();
        if (k == 'c')
            cmd_run(1);
        if (k == 'g')
            cmd_run(2);
        if (k == 'q')
            cmd_quit();
    }
    return 0;
}
