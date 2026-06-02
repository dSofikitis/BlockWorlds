#include "mat4.h"
#include <math.h>

mat4 mat4_identity(void) {
    mat4 r = {0};
    r.m[0]  = 1.0f;
    r.m[5]  = 1.0f;
    r.m[10] = 1.0f;
    r.m[15] = 1.0f;
    return r;
}

mat4 mat4_translate(vec3 t) {
    mat4 r = mat4_identity();
    r.m[12] = t.x;
    r.m[13] = t.y;
    r.m[14] = t.z;
    return r;
}

mat4 mat4_scale(vec3 s) {
    mat4 r = {0};
    r.m[0]  = s.x;
    r.m[5]  = s.y;
    r.m[10] = s.z;
    r.m[15] = 1.0f;
    return r;
}

mat4 mat4_rotate_x(float radians) {
    float c = cosf(radians);
    float s = sinf(radians);
    mat4 r = {0};
    r.m[0]  =  1.0f;
    r.m[5]  =  c;   r.m[6]  =  s;
    r.m[9]  = -s;   r.m[10] =  c;
    r.m[15] =  1.0f;
    return r;
}

mat4 mat4_rotate_y(float radians) {
    float c = cosf(radians);
    float s = sinf(radians);
    mat4 r = {0};
    r.m[0]  =  c;   r.m[2]  = -s;
    r.m[5]  =  1.0f;
    r.m[8]  =  s;   r.m[10] =  c;
    r.m[15] =  1.0f;
    return r;
}

mat4 mat4_multiply(mat4 a, mat4 b) {
    mat4 r = {0};
    for (int col = 0; col < 4; col++) {
        for (int row = 0; row < 4; row++) {
            float sum = 0.0f;
            for (int k = 0; k < 4; k++) {
                sum += a.m[k * 4 + row] * b.m[col * 4 + k];
            }
            r.m[col * 4 + row] = sum;
        }
    }
    return r;
}

mat4 mat4_perspective(float fovy_rad, float aspect, float near, float far) {
    float f = 1.0f / tanf(fovy_rad * 0.5f);
    mat4 r = {0};
    r.m[0]  = f / aspect;
    r.m[5]  = f;
    r.m[10] = (near + far) / (near - far);
    r.m[11] = -1.0f;
    r.m[14] = (2.0f * near * far) / (near - far);
    return r;
}

mat4 mat4_ortho(float left, float right, float bottom, float top, float near, float far) {
    mat4 r = {0};
    r.m[0]  =  2.0f / (right - left);
    r.m[5]  =  2.0f / (top - bottom);
    r.m[10] = -2.0f / (far - near);
    r.m[12] = -(right + left) / (right - left);
    r.m[13] = -(top + bottom) / (top - bottom);
    r.m[14] = -(far + near) / (far - near);
    r.m[15] =  1.0f;
    return r;
}

mat4 mat4_look_at(vec3 eye, vec3 target, vec3 up) {
    vec3 f = vec3_normalize(vec3_sub(target, eye));
    vec3 s = vec3_normalize(vec3_cross(f, up));
    vec3 u = vec3_cross(s, f);

    mat4 r = {0};
    r.m[0]  =  s.x;
    r.m[1]  =  u.x;
    r.m[2]  = -f.x;
    r.m[4]  =  s.y;
    r.m[5]  =  u.y;
    r.m[6]  = -f.y;
    r.m[8]  =  s.z;
    r.m[9]  =  u.z;
    r.m[10] = -f.z;
    r.m[12] = -vec3_dot(s, eye);
    r.m[13] = -vec3_dot(u, eye);
    r.m[14] =  vec3_dot(f, eye);
    r.m[15] =  1.0f;
    return r;
}