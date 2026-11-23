/* cvarr.c -- converter corpus B: arrays and chars.
   Words at 0xE688 + 2*i: [385, 67, 100, 55]. */

int sq[16];

void pw(int i, int v)
{
    poke(0xE688 + i + i, v & 255);
    poke(0xE689 + i + i, (v >> 8) & 255);
}

int main()
{
    int i, s;
    char c;
    /* fill the global array with squares */
    for (i = 1; i <= 10; i = i + 1)
        sq[i] = i * i;
    /* 0: sum of squares 1..10 = 385 */
    s = 0;
    for (i = 1; i <= 10; i = i + 1)
        s = s + sq[i];
    pw(0, s);
    /* 1: char arithmetic: 'A' + 2 = 67 */
    c = 65;
    c = c + 2;
    pw(1, c);
    /* 2: the largest square = 100 */
    pw(2, sq[10]);
    /* 3: sum 1..10 = 55 */
    s = 0;
    i = 1;
    while (i <= 10) {
        s = s + i;
        i = i + 1;
    }
    pw(3, s);
    poke(0xE680, 0x53);
    poke(0xE681, 0x43);
    poke(0xE682, 0x52);
    poke(0xE683, 0x53);
    poke(0xE684, 1);
    puts("CVARR DONE");
    return 0;
}
