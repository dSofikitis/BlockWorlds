#pragma once

#include "vec3.h"

typedef struct { float m[16]; } mat4;

mat4 mat4_identity(void);
mat4 mat4_multiply(mat4 a, mat4 b);
mat4 mat4_translate(vec3 t);
mat4 mat4_scale(vec3 s);
mat4 mat4_rotate_x(float radians);
mat4 mat4_rotate_y(float radians);
mat4 mat4_perspective(float fovy_rad, float aspect, float near, float far);
mat4 mat4_ortho(float left, float right, float bottom, float top, float near, float far);
mat4 mat4_look_at(vec3 eye, vec3 target, vec3 up);
