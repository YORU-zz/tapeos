/* kr.c -- Small-C sample: K&R-style definitions and
   implicit int, the historical Small-C surface.
   Words under "SCRS": [42, 7, 100]. */

add(a, b)
int a, b;
{
    return a + b;
}

mul(a, b)
int a;
int b;
{
    return a * b;
}

int main()
{
    int *sig;

    sig = (int *)0xE680;
    sig[0] = 0x4353;
    sig[1] = 0x5352;
    sig[2] = 1;
    sig[4] = add(40, 2);        /* 42 */
    sig[5] = mul(1, 7);         /* 7 */
    sig[6] = add(mul(10, 10), 0);   /* 100 */
    return 0;
}
