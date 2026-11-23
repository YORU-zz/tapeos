/* err.c -- Small-C sample: an undefined variable.
   Both SCC and SCI must stop with a nonzero error
   code (E_UNDEF) in the status block at 0xE640. */

int main()
{
    return nosuchvar + 1;
}
