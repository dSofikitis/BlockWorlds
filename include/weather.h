#pragma once

#include <stdint.h>

#include "mat4.h"
#include "vec3.h"

typedef struct weather_s weather_t;

#define WEATHER_CLEAR 0
#define WEATHER_RAIN  1
#define WEATHER_SNOW  2

weather_t *weather_create(void);
void       weather_destroy(weather_t *w);

void weather_set(weather_t *w, int mode);
int  weather_mode(const weather_t *w);

void weather_update(weather_t *w, vec3 cam, float dt, float intensity, uint32_t seed);

void weather_render(const weather_t *w, mat4 pv, float day);
