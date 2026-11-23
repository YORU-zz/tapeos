/* ext.c -- Small-C sample: the external (IOCS)
   library. The session types 123 for input() and a
   line "B" for getchar(). Words under "SCRS":
   [123, 165, 65, 66, 7]. */

int *sig;

void pw(int i, int v)
{
    sig[4 + i] = v;
}

int main()
{
    int v;

    sig = (int *)0xE680;
    sig[0] = 0x4353;
    sig[1] = 0x5352;
    sig[2] = 1;

    /* 0: input() reads the typed number = 123 */
    v = input();
    pw(0, v);

    /* 1: poke then peek back = 0xA5 = 165 */
    poke(0xE700, 0xA5);
    pw(1, peek(0xE700));

    /* 2: putchar returns its argument = 'A' = 65 */
    pw(2, putchar('A'));
    putchar('\n');

    /* 3: getchar reads the typed line = 'B' = 66 */
    pw(3, getchar());

    /* 4: printf returns 0 (documented), so 0 + 7 = 7;
       also exercises %d and %% */
    pw(4, printf("V=%d 100%%\n", v) + 7);

    puts("EXT DONE");
    return 0;
}
