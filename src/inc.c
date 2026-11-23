/* inc.c -- Small-C sample: #include and #define.
   Sums the even numbers 2..100 = 2550 with LIMIT and
   STEP from cfg.h, and checks a local macro.
   Words under "SCRS": [2550, 9]. */

#include "cfg.h"
#define BASE 40

int main()
{
    int i, s;
    int *sig;

    s = 0;
    for (i = STEP; i <= LIMIT; i += STEP)
        s += i;

    sig = (int *)0xE680;
    sig[0] = 0x4353;
    sig[1] = 0x5352;
    sig[2] = 1;
    sig[4] = s;                 /* 2550 */
    sig[5] = BASE - 31;         /* 9 */
    return 0;
}
