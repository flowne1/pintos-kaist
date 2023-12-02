#include <stdio.h>
#include <stdint.h>

#include "lib/fixed.h"

const int fx_scale = 1 << 14;	// F value for 17.14 format fixed point calculating

// Calculating functions for fixed points
// Private note : these functions assumes that arguments given are 'fixed point', except convert functions

// Convert fixed point to integer
int fxtoi (int x) {
	return x / fx_scale;
}

// Multiply fixed point by fixed point
int mult_fixeds (int x, int y) {
	return ((int64_t) x) * y / fx_scale;
}

// Divide fixed point by fixed point
int div_fixeds (int x, int y) {
	return ((int64_t) x) * fx_scale / y;
}