/* fib.c -- Small-C sample: recursive Fibonacci.
   Writes the result signature ("SCRS", tag 1) at
   0xE680: words at +8 are fib(15) = 610, fib(10) = 55.
   The same source runs under SCC (compiled) and SCI
   (JIT) with identical results. */

int fib(int n)
{
    if (n < 2)
        return n;
    return fib(n - 1) + fib(n - 2);
}

int main()
{
    int *sig;
    int a, b;

    a = fib(15);
    b = fib(10);
    printf("FIB(15)=%d\n", a);
    printf("FIB(10)=%d\n", b);

    sig = (int *)0xE680;
    sig[0] = 0x4353;            /* 'S','C' */
    sig[1] = 0x5352;            /* 'R','S' */
    sig[2] = 1;
    sig[4] = a;
    sig[5] = b;
    return 0;
}
