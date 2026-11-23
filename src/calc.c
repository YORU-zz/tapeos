/* calc.c -- Small-C sample: operators, control
   structures, arrays, pointers, char semantics. The C
   counterpart of TinyBASIC's calc.tb: every result is
   written as a word at 0xE688 + 2*i under the "SCRS"
   signature, and SCI must produce exactly the same
   words as SCC. See smallc-tos.md for the values. */

int sq[16];
int gsum;
int *sig;

void pw(int i, int v)
{
    sig[4 + i] = v;
}

int addg(int x)
{
    gsum = gsum + x;
    return gsum;
}

int main()
{
    int i, s, f, r;
    int *p;
    char c;

    sig = (int *)0xE680;
    sig[0] = 0x4353;
    sig[1] = 0x5352;
    sig[2] = 1;

    /* 0: for loop, sum 1..100 = 5050 */
    s = 0;
    for (i = 1; i <= 100; i++)
        s += i;
    pw(0, s);

    /* 1: while loop, sum of evens 2..50 = 650 */
    s = 0;
    i = 2;
    while (i <= 50) {
        s += i;
        i += 2;
    }
    pw(1, s);

    /* 2: do-while, 6! = 720 */
    f = 1;
    i = 1;
    do {
        f *= i;
        i++;
    } while (i <= 6);
    pw(2, f);

    /* 3: break: first i with i*i > 50 is 8 */
    r = 0;
    for (i = 0; i <= 100; i++) {
        if (i * i > 50) {
            r = i;
            break;
        }
    }
    pw(3, r);

    /* 4: global array filled through a pointer,
       sum of squares 0..15 = 1240 */
    s = 0;
    p = sq;
    for (i = 0; i < 16; i++) {
        *p = i * i;
        s += *p;
        p++;
    }
    pw(4, s);

    /* 5: bitwise: 28 + 62 + 34 + 40 = 164 */
    pw(5, (0x3C & 0x1E) + (0x3C | 0x1E) + (0x3C ^ 0x1E)
          + (5 << 3));

    /* 6: arithmetic shift right: -256 >> 4 = -16 */
    pw(6, (0 - 256) >> 4);

    /* 7: div/mod signs: -3 + -2*4 + -3*16 + 2*64 = 69 */
    pw(7, (0 - 17) / 5 + ((0 - 17) % 5) * 4
          + (17 / (0 - 5)) * 16 + (17 % (0 - 5)) * 64);

    /* 8: comparisons as values = 219 (the last one
       overflows a naive 16-bit subtraction) */
    pw(8, (3 < 5) * 1 + (5 <= 5) * 2 + (7 > 9) * 4
          + (7 >= 7) * 8 + (4 == 4) * 16 + (4 != 4) * 32
          + ((0 - 3) < (0 - 2)) * 64
          + (20000 > (0 - 20000)) * 128);

    /* 9: char truncation: 200 + 100 in a char = 44 */
    c = 200;
    c = c + 100;
    pw(9, c);

    /* 10: logical operators = 10 (C short-circuits;
       the values agree with TinyBASIC's eager ones) */
    pw(10, (1 && 0) * 1 + (1 || 0) * 2 + (!5) * 4
           + (!0) * 8);

    /* 11: nested loops: (1+2+3+4)^2 = 100 */
    s = 0;
    for (i = 1; i <= 4; i++) {
        int j;
        for (j = 1; j <= 4; j++)
            s += i * j;
    }
    pw(11, s);

    /* 12: for with negative step: 10+8+6+4+2 = 30 */
    s = 0;
    for (i = 10; i >= 2; i -= 2)
        s += i;
    pw(12, s);

    /* 13: global scalar shared across functions = 7 */
    gsum = 0;
    r = addg(3);
    r = addg(4);
    pw(13, r);

    /* 14: precedence mix: -6 + 2 + 1 - 1 = -4 */
    pw(14, (0 - 2) * 3 + 10 / 4 - (0 - 1) + 0 - 1);

    puts("CALC DONE");
    return 0;
}
