/* bad.c -- converter corpus: unconvertible constructs.
   Every construct below must be REPORTED, never dropped:
   pointers, dereference, address-of, ?:, getchar, a
   (char) cast in an expression, ++ in an expression. */

int *gp;

int pick(int a, int b)
{
    return a > b ? a : b;
}

int main()
{
    int x, y;
    int *p;
    p = &x;
    *p = 41;
    y = *p + 1;
    x = pick(3, 9);
    y = getchar();
    y = (char)(x + 200);
    y = x++ + 1;
    gp = p;
    return y;
}
