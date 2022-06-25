
#pragma once

#include "common.h"

typedef struct vec2f {
	float x;
	float y;
} vec2f;

typedef struct vec2d {
	double x;
	double y;
} vec2d;

extern void vec2f_print( const vec2f* const v );
extern void vec2d_print( const vec2d* const v );
