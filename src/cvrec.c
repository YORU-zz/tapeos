/* cvrec.c -- converter corpus C: recursion, nested
   loops, break. Words: [720, 55, 100, 8]. */

void pw(int i, int v)
{
    poke(0xE688 + i + i, v & 255);
    poke(0xE689 + i + i, (v >> 8) & 255);
}

int fact(int n)
{
    if (n <= 1)
        return 1;
    return n * fact(n - 1);
}

int fib(int n)
{
    if (n < 2)
        return n;
    return fib(n - 1) + fib(n - 2);
}

int main()
{
    int i, j, s;
    /* 0: fact(6) = 720 */
    pw(0, fact(6));
    /* 1: fib(10) = 55 */
    pw(1, fib(10));
    /* 2: nested loops: (1+2+3+4)^2 = 100 */
    s = 0;
    for (i = 1; i <= 4; i = i + 1)
        for (j = 1; j <= 4; j = j + 1)
            s = s + i * j;
    pw(2, s);
    /* 3: break: first i with i*i > 50 is 8 */
    s = 0;
    for (i = 0; i <= 100; i = i + 1) {
        if (i * i > 50) {
            s = i;
            break;
        }
    }
    pw(3, s);
    poke(0xE680, 0x53);
    poke(0xE681, 0x43);
    poke(0xE682, 0x52);
    poke(0xE683, 0x53);
    poke(0xE684, 1);
    puts("CVREC DONE");
    return 0;
}
