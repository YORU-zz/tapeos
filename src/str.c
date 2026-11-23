/* str.c -- Small-C sample: strings, char pointers,
   printf. Words under "SCRS" at 0xE688 + 2*i:
   [5, 15, 300, 42, 7, 104]. */

char buf[24];
int *sig;

void pw(int i, int v)
{
    sig[4 + i] = v;
}

int mylen(char *s)
{
    int n;
    n = 0;
    while (*s) {
        n++;
        s++;
    }
    return n;
}

int atoi2(char *s)
{
    int n;
    n = 0;
    while (*s >= '0' && *s <= '9') {
        n = n * 10 + (*s - '0');
        s++;
    }
    return n;
}

int main()
{
    char *p;
    int i;

    sig = (int *)0xE680;
    sig[0] = 0x4353;
    sig[1] = 0x5352;
    sig[2] = 1;

    /* 0: length of "HELLO" = 5 */
    pw(0, mylen("HELLO"));

    /* 1: value of "15" = 15 */
    pw(1, atoi2("15"));

    /* 2: value of "300" = 300 */
    pw(2, atoi2("300"));

    /* copy "TAPEOS SMALL-C!" into the global buffer
       through array indexing */
    p = "TAPEOS SMALL-C!";
    i = 0;
    while (p[i]) {
        buf[i] = p[i];
        i++;
    }
    buf[i] = 0;

    /* 3: char literal '*' = 42 */
    pw(3, '*');

    /* 4: length of "SMALL-C" = 7 */
    pw(4, mylen("SMALL-C"));

    /* 5: char literal 'h' = 104 */
    pw(5, 'h');

    printf("%s %d\n", buf, mylen(buf));
    puts("STR DONE");
    return 0;
}
