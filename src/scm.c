/* scm.c -- Small-C sample: calling into the embedded
   Tiny Scheme core (roadmap 61).  The session disk also
   carries SCMCORE; the runtime entry loads it before
   main runs (the scmcall/scmeval code generation pulls
   the loader routine in).  Definitions persist across
   calls for the whole run.
   Writes the result signature ("SCRS", tag 1) at
   0xE680: words at +8 are:
     scmeval("(+ 40 2)")     = 42
     scmcall("fact", 7)      = 5040  (fact defined below)
     scmcall("sq", 12)       = 144   (sq defined below)
     scmerr() after (/ 1 0)  = 9     (E_DIV0)
     scmcall("nosuch", 1)    = 0, scmerr() = 1 (E_UNBOUND)
     scmerr() after scmcall("fact", 20000) = 252 (E_RANGE)
     scmerr() after scmeval("(cons 1 2)")  = 253 (E_RESULT)
   Run on a disk WITHOUT SCMCORE, every call returns 0
   and scmerr() reports 251 (0xFB, core not loaded); the
   scmnc session checks that case.
   The same source runs under SCC (compiled) and SCI
   (JIT) with identical results. */

int main()
{
    int *sig;
    int r1, r2, r3, e1, r4, e2, r5, e3, r6, e4;

    scmeval("(begin (define (fact n) (if (= n 0) 1 (* n (fact (- n 1))))) 0)");
    scmeval("(begin (define (sq x) (* x x)) 0)");
    r1 = scmeval("(+ 40 2)");
    r2 = scmcall("fact", 7);
    r3 = scmcall("sq", 12);
    scmeval("(quotient 1 0)");
    e1 = scmerr();
    r4 = scmcall("nosuch", 1);
    e2 = scmerr();
    r5 = scmcall("fact", 20000);
    e3 = scmerr();
    r6 = scmeval("(cons 1 2)");
    e4 = scmerr();

    printf("EVAL=%d FACT=%d SQ=%d\n", r1, r2, r3);
    printf("ERR=%d %d\n", e1, e2);

    sig = (int *)0xE680;
    sig[0] = 0x4353;            /* 'S','C' */
    sig[1] = 0x5352;            /* 'R','S' */
    sig[2] = 1;
    sig[4] = r1;
    sig[5] = r2;
    sig[6] = r3;
    sig[7] = e1;
    sig[8] = r4;
    sig[9] = e2;
    sig[10] = e3;
    sig[11] = e4;
    return 0;
}
