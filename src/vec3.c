#include "vec3.h"
#include <math.h>

vec3 vec3_add(vec3 a, vec3 b) {
    return (vec3){ a.x + b.x, a.y + b.y, a.z + b.z };
}

vec3 vec3_sub(vec3 a, vec3 b) {
    return (vec3){ a.x - b.x, a.y - b.y, a.z - b.z };
}

vec3 vec3_scale(vec3 v, float s) {
    return (vec3){ v.x * s, v.y * s, v.z * s };
}

float vec3_dot(vec3 a, vec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

vec3 vec3_cross(vec3 a, vec3 b) {
    return (vec3){
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}

vec3 vec3_normalize(vec3 v) {
    float len = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
    if (len > 0.0f) {
        return (vec3){ v.x / len, v.y / len, v.z / len };
    }
    return v;
}