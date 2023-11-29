#include <fixed.h>

/* Fraction */
static const int32_t f = (1 << 14);

/* Integer to fixed point. */
fixed itofx (int32_t n) {
    return n * f;
}

/* Fixed point to integer. */
int32_t fxtoi (fixed x) {
    return x / f;
}

/* Fixed point to integer which rounds to nearest integer. */
int32_t fxtoin (fixed x) {
    if (x >= 0) {
        return (x + f / 2) / f;
    }

    return (x - f / 2) / f;
}

/* Add two fixed point. */
fixed addff (fixed x, fixed y) {
    return x + y;
}

/* Add fixed point X and integer N */
fixed addfi (fixed x, int32_t n) {
    return x + n * f;
}

/* Subtract X from Y. */
fixed subff (fixed x, fixed y) {
    return x - y;
}

/* Subtract X from N. */
fixed subfi (fixed x, int32_t n) {
    return x - n * f;
}

/* Multiply two fixed point. */
fixed multff (fixed x, fixed y) {
    return ((fixed_ext) x) * y / f;
}

/* Multiply X by integer N. */
fixed multfi (fixed x, int32_t n) {
    return x * n;
}

/* Divide X by Y. */
fixed divff (fixed x, fixed y) {
    return ((fixed_ext) x) * f / y;
}

/* Divide X by integer N. */
fixed divfi (fixed x, int32_t n) {
    return x / n;
}