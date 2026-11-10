/* GPL-2.0-or-later. Exact 16.16 division using native 32-bit divides.
 * Base-65536 long division: normalize the divisor, estimate each quotient
 * digit from its high half, then correct using the low half. All intermediates
 * deliberately use unsigned modulo-2^32 arithmetic. No floating approximation.
 */
#ifndef DOOM_M_FIXED_FAST_H
#define DOOM_M_FIXED_FAST_H
#include <stdint.h>
#include <limits.h>

static inline uint32_t DoomDiv48By32(uint32_t a, uint32_t b)
{
  // Divide (a * 65536) by b. Caller guarantees a/b < 16384.
  if (a <= UINT16_MAX) return (a << 16) / b;
  if (b <= UINT16_MAX) {
    uint32_t whole=a/b, rem=a-whole*b;
    return (whole<<16) + (rem<<16)/b;
  }
  // A quotient below one fixed-point unit has only one base-65536 digit.
  // Normalization cannot overflow a because a<b. No high-digit division is
  // needed; retain the same low-digit estimate and exact correction.
  if(a<b) {
    unsigned shift=__builtin_clz(b);
    uint32_t v=b<<shift, vhi=v>>16, vlo=v&UINT16_MAX;
    uint32_t middle=a<<shift;
    uint32_t q=middle/vhi, rem=middle-q*vhi;
    while(q>=65536u || q*vlo>(rem<<16)) {
      --q;rem+=vhi;if(rem>=65536u)break;
    }
    return q;
  }
  unsigned shift=__builtin_clz(b);
  uint32_t v=b<<shift, vhi=v>>16, vlo=v&UINT16_MAX;
  uint32_t low=a<<16;
  uint32_t high=shift ? ((a>>16)<<shift)|(low>>(32-shift)) : a>>16;
  low<<=shift;
  uint32_t next=low>>16, tail=low&UINT16_MAX;
  uint32_t qhi=high/vhi, rem=high-qhi*vhi;
  while (qhi>=65536u || qhi*vlo>(rem<<16)+next) {
    --qhi;
    rem+=vhi;
    if (rem>=65536u) break;
  }
  uint32_t middle=(high<<16)+next-qhi*v;
  uint32_t qlo=middle/vhi;
  rem=middle-qlo*vhi;
  while (qlo>=65536u || qlo*vlo>(rem<<16)+tail) {
    --qlo;
    rem+=vhi;
    if (rem>=65536u) break;
  }
  return (qhi<<16)+qlo;
}

static inline int32_t DoomFixedDivFast(int32_t a, int32_t b)
{
  uint32_t aa=a<0 ? 0u-(uint32_t)a : (uint32_t)a;
  uint32_t bb=b<0 ? 0u-(uint32_t)b : (uint32_t)b;
  int negative=(a^b)<0;
  // Retain Doom's conservative saturation policy, including division by zero.
  if ((aa>>14)>=bb) return negative ? INT32_MIN : INT32_MAX;
  uint32_t q=DoomDiv48By32(aa,bb);
  return negative ? -(int32_t)q : (int32_t)q;
}
#endif
