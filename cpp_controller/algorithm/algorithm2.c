#include "algorithm2.h"
#include <math.h>

static double rt_roundd(double u);

static double rt_roundd(double u)
{
  double y;
  if (fabs(u) < 4.503599627370496E+15) {
    if (u >= 0.5) {
      y = floor(u + 0.5);
    } else if (u > -0.5) {
      y = 0.0;
    } else {
      y = ceil(u - 0.5);
    }
  } else {
    y = u;
  }
  return y;
}

unsigned short algorithm2(double u)
{
  unsigned short y;
  y = 0U;
  if (u >= 0.0) {
    y = (unsigned short)rt_roundd(10.0 * u);
  }
  return y;
}

void algorithm2_initialize(void)
{
}

void algorithm2_terminate(void)
{
}
