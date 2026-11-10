/* GPL-2.0-or-later. Exact compact visplane column bounds.
 * Valid pixel rows are 0..65534. Decode the reserved empty-top marker back to
 * UINT32_MAX before span generation, preserving the renderer's sentinel math.
 */
#ifndef DOOM_R_PLANE_BOUNDS_H
#define DOOM_R_PLANE_BOUNDS_H
#include <stdint.h>
static inline uint32_t R_PackPlaneColumn(unsigned top, unsigned bottom)
{
  return (uint32_t)top | ((uint32_t)bottom<<16);
}
static inline unsigned R_PackedPlaneTop(uint32_t bounds)
{
  unsigned top=bounds&UINT16_MAX;
  return top==UINT16_MAX ? UINT32_MAX : top;
}
static inline unsigned R_PackedPlaneBottom(uint32_t bounds)
{
  return bounds>>16;
}
#endif
