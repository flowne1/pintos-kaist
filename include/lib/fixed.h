#ifndef __LIB_FIXED_H
#define __LIB_FIXED_H
#include <stdint.h>

typedef int32_t fixed;
typedef int64_t fixed_ext;

fixed itofx (int32_t n);
int32_t fxtoi (fixed x);
int32_t fxtoin (fixed x);

fixed addff (fixed x, fixed y);
fixed addfi (fixed x, int32_t n);

fixed subff (fixed x, fixed y);
fixed subfi (fixed x, int32_t n);

fixed multff (fixed x, fixed y);
fixed multfi (fixed x, int32_t n);

fixed divff (fixed x, fixed y);
fixed divfi (fixed x, int32_t n);
#endif 