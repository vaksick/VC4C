__kernel void test_shuffle(const __global char16* in, __global char16* out)
{
    char16 tmp = in[0];
    char16 tmp1 = in[1];

    //(7, 6, 4, 8, 1, c, d, 1, 0, 9, e, f, 4, 3, 8, 6)
    out[0] = shuffle(tmp, (uchar16)(7, 6, 4, 8, 1, 12, 13, 1, 0, 9, 14, 15, 4, 3, 8, 6));
    //(1, 7, b, 12, 15, f, 8, 9, 0, 13, 2, 1, 11, d, 7, 8)
    out[1] = shuffle2(tmp, tmp1, (uchar16)(1, 7, 11, 18, 21, 15, 8, 9, 0, 19, 2, 1, 17, 13, 7, 8));

    char16 tmp2 = in[0];
    //(1a, 1b, 2, 10, 4, 19, 6, 17, 8, 9, 1c, 13, 1a, d, e, f)
    tmp2.s5170abc3 = tmp1.s9b7ac3a0;
    out[2] = tmp2;

    char4 tmp3 = in[0].xyzw;
    //(11, 1, 2, 10)
    tmp3.wx = tmp1.xy;
    //(11, 1, 2, 10, 11, 1, 2, 10, 11, 1, 2, 10, 11, 1, 2, 10)
    out[3] = (char16)(tmp3, tmp3, tmp3, tmp3);

    char16 tmp4 = 0;
    //(0, 0, 0, 1, 11, 0, 0, 10, 0, 11, 1, 2, 2, 10, 0, 0)
    tmp4.s43b79acd = (char8)(tmp3, tmp3);
    out[4] = tmp4;

    // (0, 0, 0, 0, 4, 4, 4, 4, 8, 8, 8, 8, c, c, c, c)
    out[5] = shuffle(tmp, (uchar16)(0, 0, 0, 0, 4, 4, 4, 4, 8, 8, 8, 8, 12, 12, 12, 12));
    // (0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)
    out[6] = shuffle2(tmp, tmp1, (uchar16)(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0));

    // (0, 1, 2, 3, 4, 5, 6, 7, 8, 9, a, b, c, d, e, f)
    out[7] = shuffle(tmp, convert_uchar16(tmp));
    // (a, b, c, d, e, f, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9) - simple rotate all at once!
    out[8] = shuffle(tmp, (uchar16)(10, 11, 12, 13, 14, 15, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9));
    // (f, e, d, c, b, a, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0)
    out[9] = shuffle(tmp, (uchar16)(15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0));
}
