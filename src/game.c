/* game.c -- Small-C sample: a bounded game loop, the
   C counterpart of TinyBASIC's game.tb. The '@' moves
   with stick() (keyboard 8/2/4/6, IKJL, WSAD), one
   step per vsync() frame, for 180 frames. The session
   holds 'l' (right), so px clamps at 39. Words under
   "SCRS" at 0xE688: [39, 12, 180]. */

int main()
{
    int px, py, f, s;
    int *sig;

    px = 20;
    py = 12;
    cls();
    for (f = 0; f < 180; f++) {
        vsync();
        s = stick();
        if (s == 3 && px > 0)
            px--;
        if (s == 4 && px < 39)
            px++;
        if (s == 1 && py > 0)
            py--;
        if (s == 2 && py < 24)
            py++;
        locate(px, py);
        putchar('@');
    }

    sig = (int *)0xE680;
    sig[0] = 0x4353;
    sig[1] = 0x5352;
    sig[2] = 1;
    sig[4] = px;
    sig[5] = py;
    sig[6] = 180;
    return 0;
}
