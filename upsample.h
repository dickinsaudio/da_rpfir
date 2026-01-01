// Create a set of samples at twice the input rate using a fixed filter
// This filter also includes a pre-emphasis to compensate for amp
// The filter has 20 taps, so this requires that there are 19 previous samples before *in
// The input should already be scaled down by 8 bits for the math and headroom
// The filter uses effective 6 bit coefficients
// Allow the output to be written in interleaved format
// 
// W = 2*fir1(63, 0.4751, 'low', kbdwin(64, 5), 'noscale');
// W = filter(1.3,[1 0 .3],W);
//
// A = reshape(round(W(13:end-10)*32),2,[])     % Fortunate pick is already balanced (rows sum to 32)
//    -1     1    -1     2    -3     4    -5     6    -8    16    31   -15     7    -3     1     0     0     1    -1     1    -1
//     1    -1     1    -1     0     0    -1     3    -7    38     2    -7     7    -5     4    -3     3    -2     1    -1     0
//
// Have checked and tweaked this to be ARM compiler friendly and optimal.  It is using about 2.5 cycles per MAC.
//

#define FILTER2X_TAPS 21
#define TAP(a, b, n)  { z1 += a * *(p+20-n); z2 += b * *(p+20-n); }

inline void filter2x(int32_t *in, int32_t *out, int n, int out_stride = 1)
{
    int32_t *p = in - 20;                       // Index from oldest sample, since M0+ only has positive load offset
    for (int i = 0; i < n; i++)                 // This gives us a 20% speedup saving one instruction per TAP
    {
        int32_t z1 = 0, z2 = 0;

        TAP(  -1,   1,  0);
        TAP(   1,  -1,  1);
        TAP(  -1,   1,  2);
        TAP(   2,  -1,  3);
        TAP(  -3,   0,  4);
        TAP(   4,   0,  5);
        TAP(  -5,  -1,  6);
        TAP(   6,   3,  7);
        TAP(  -8,  -7,  8);
        TAP(  16,  38,  9);
        TAP(  31,   2, 10);
        TAP( -15,  -7, 11);
        TAP(   7,   7, 12);
        TAP(  -3,  -5, 13);
        TAP(   1,   4, 14);
        TAP(   0,  -3, 15);
        TAP(   0,   3, 16);
        TAP(   1,  -2, 17);
        TAP(  -1,   1, 18);
        TAP(   1,  -1, 19);
        TAP(  -1,   0, 20);

        if (z1 >  0x0FFFFFFF) z1 =  0x0FFFFFFF;
        if (z1 < -0x10000000) z1 = -0x10000000; 
        *out = z1<<3;
        out += out_stride;

        if (z2 >  0x0FFFFFFF) z2 =  0x0FFFFFFF;
        if (z2 < -0x10000000) z2 = -0x10000000;
        *out = z2<<3;
        out += out_stride;
        p++;
    }
}

