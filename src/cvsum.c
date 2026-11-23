/* cvsum.c -- converter corpus A: loops and a helper.
   Words at 0xE688 + 2*i: [1275, 650, 720, 42]. */

void pw(int i, int v)
{
    poke(0xE688 + i + i, v & 255);
    poke(0xE689 + i + i, (v >> 8) & 255);
}

int dbl(int a)
{
    return a + a;
}

int main()
{
    int i, s;
    /* 0: for loop, sum 1..50 = 1275 */
    s = 0;
    for (i = 1; i <= 50; i = i + 1)
        s = s + i;
    pw(0, s);
    /* 1: while loop, sum of evens 2..50 = 650 */
    s = 0;
    i = 2;
    while (i <= 50) {
        s = s + i;
        i = i + 2;
    }
    pw(1, s);
    /* 2: do-while, 6! = 720 */
    s = 1;
    i = 1;
    do {
        s = s * i;
        i = i + 1;
    } while (!(i > 6));
    pw(2, s);
    /* 3: if/else and a call: dbl(21) = 42 */
    if (dbl(20) == 40)
        s = dbl(21);
    else
        s = 0;
    pw(3, s);
    /* signature header */
    poke(0xE680, 0x53);
    poke(0xE681, 0x43);
    poke(0xE682, 0x52);
    poke(0xE683, 0x53);
    poke(0xE684, 1);
    puts("CVSUM DONE");
    return 0;
}
