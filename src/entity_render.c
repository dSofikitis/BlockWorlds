#include "glcompat.h"

#include "entity.h"
#include "registry.h"
#include "texture.h"
#include "mat4.h"
#include "vec3.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

typedef enum {
    ROLE_STATIC,
    ROLE_HEAD,
    ROLE_LEG_FL, ROLE_LEG_FR, ROLE_LEG_BL, ROLE_LEG_BR,
    ROLE_ARM_L, ROLE_ARM_R,
    ROLE_BEAK
} part_role_t;

typedef struct {
    float ox, oy, oz;
    float sx, sy, sz;
    float r, g, b;
    part_role_t role;
} mob_part_t;

#define MAX_PARTS 14

typedef struct {
    const mob_part_t *parts;
    int               count;
} mob_model_t;

static const mob_part_t ZOMBIE_PARTS[] = {
    { 0.0f, 1.55f, 0.0f,  0.50f, 0.50f, 0.50f, 0.32f, 0.55f, 0.40f, ROLE_HEAD   },
    { 0.0f, 1.05f, 0.0f,  0.50f, 0.70f, 0.28f, 0.25f, 0.45f, 0.30f, ROLE_STATIC },
    {-0.36f, 1.05f, 0.0f, 0.22f, 0.70f, 0.24f, 0.30f, 0.52f, 0.38f, ROLE_ARM_L  },
    { 0.36f, 1.05f, 0.0f, 0.22f, 0.70f, 0.24f, 0.30f, 0.52f, 0.38f, ROLE_ARM_R  },
    {-0.15f, 0.35f, 0.0f, 0.22f, 0.70f, 0.24f, 0.20f, 0.30f, 0.55f, ROLE_LEG_FL },
    { 0.15f, 0.35f, 0.0f, 0.22f, 0.70f, 0.24f, 0.20f, 0.30f, 0.55f, ROLE_LEG_FR },
};

static const mob_part_t SKELETON_PARTS[] = {
    { 0.0f, 1.55f, 0.0f,  0.48f, 0.48f, 0.48f, 0.92f, 0.92f, 0.88f, ROLE_HEAD   },
    { 0.0f, 1.05f, 0.0f,  0.42f, 0.70f, 0.22f, 0.80f, 0.80f, 0.78f, ROLE_STATIC },
    {-0.30f, 1.05f, 0.0f, 0.14f, 0.70f, 0.14f, 0.72f, 0.72f, 0.70f, ROLE_ARM_L  },
    { 0.30f, 1.05f, 0.0f, 0.14f, 0.70f, 0.14f, 0.72f, 0.72f, 0.70f, ROLE_ARM_R  },
    {-0.13f, 0.35f, 0.0f, 0.14f, 0.70f, 0.14f, 0.70f, 0.70f, 0.68f, ROLE_LEG_FL },
    { 0.13f, 0.35f, 0.0f, 0.14f, 0.70f, 0.14f, 0.70f, 0.70f, 0.68f, ROLE_LEG_FR },
};

static const mob_part_t CREEPER_PARTS[] = {
    { 0.0f, 1.45f, 0.0f,  0.50f, 0.50f, 0.50f, 0.36f, 0.62f, 0.30f, ROLE_HEAD   },
    { 0.0f, 0.85f, 0.0f,  0.50f, 0.80f, 0.34f, 0.32f, 0.58f, 0.27f, ROLE_STATIC },
    {-0.16f, 0.20f, 0.18f, 0.22f, 0.40f, 0.22f, 0.30f, 0.55f, 0.25f, ROLE_LEG_FL },
    { 0.16f, 0.20f, 0.18f, 0.22f, 0.40f, 0.22f, 0.30f, 0.55f, 0.25f, ROLE_LEG_FR },
    {-0.16f, 0.20f,-0.18f, 0.22f, 0.40f, 0.22f, 0.30f, 0.55f, 0.25f, ROLE_LEG_BL },
    { 0.16f, 0.20f,-0.18f, 0.22f, 0.40f, 0.22f, 0.30f, 0.55f, 0.25f, ROLE_LEG_BR },
};

static const mob_part_t SPIDER_PARTS[] = {
    { 0.0f, 0.45f, -0.18f, 0.80f, 0.45f, 0.80f, 0.12f, 0.10f, 0.12f, ROLE_STATIC },
    { 0.0f, 0.42f,  0.45f, 0.42f, 0.40f, 0.40f, 0.14f, 0.12f, 0.14f, ROLE_HEAD   },
    {-0.55f, 0.30f, 0.18f, 0.70f, 0.12f, 0.12f, 0.08f, 0.07f, 0.08f, ROLE_LEG_FL },
    { 0.55f, 0.30f, 0.18f, 0.70f, 0.12f, 0.12f, 0.08f, 0.07f, 0.08f, ROLE_LEG_FR },
    {-0.55f, 0.30f,-0.10f, 0.70f, 0.12f, 0.12f, 0.08f, 0.07f, 0.08f, ROLE_LEG_BL },
    { 0.55f, 0.30f,-0.10f, 0.70f, 0.12f, 0.12f, 0.08f, 0.07f, 0.08f, ROLE_LEG_BR },
    {-0.55f, 0.30f, 0.46f, 0.70f, 0.12f, 0.12f, 0.08f, 0.07f, 0.08f, ROLE_LEG_FL },
    { 0.55f, 0.30f, 0.46f, 0.70f, 0.12f, 0.12f, 0.08f, 0.07f, 0.08f, ROLE_LEG_FR },
};

static const mob_part_t PIG_PARTS[] = {
    { 0.0f, 0.62f, -0.10f, 0.55f, 0.50f, 0.85f, 0.92f, 0.62f, 0.66f, ROLE_STATIC },
    { 0.0f, 0.62f,  0.48f, 0.45f, 0.45f, 0.40f, 0.95f, 0.66f, 0.70f, ROLE_HEAD   },
    {-0.17f, 0.18f, 0.30f, 0.18f, 0.36f, 0.18f, 0.85f, 0.55f, 0.60f, ROLE_LEG_FL },
    { 0.17f, 0.18f, 0.30f, 0.18f, 0.36f, 0.18f, 0.85f, 0.55f, 0.60f, ROLE_LEG_FR },
    {-0.17f, 0.18f,-0.40f, 0.18f, 0.36f, 0.18f, 0.85f, 0.55f, 0.60f, ROLE_LEG_BL },
    { 0.17f, 0.18f,-0.40f, 0.18f, 0.36f, 0.18f, 0.85f, 0.55f, 0.60f, ROLE_LEG_BR },
};

static const mob_part_t COW_PARTS[] = {
    { 0.0f, 0.78f, -0.10f, 0.60f, 0.55f, 0.95f, 0.36f, 0.26f, 0.18f, ROLE_STATIC },
    { 0.0f, 0.82f,  0.55f, 0.45f, 0.48f, 0.42f, 0.90f, 0.88f, 0.84f, ROLE_HEAD   },
    {-0.20f, 0.24f, 0.34f, 0.20f, 0.48f, 0.20f, 0.30f, 0.22f, 0.15f, ROLE_LEG_FL },
    { 0.20f, 0.24f, 0.34f, 0.20f, 0.48f, 0.20f, 0.30f, 0.22f, 0.15f, ROLE_LEG_FR },
    {-0.20f, 0.24f,-0.44f, 0.20f, 0.48f, 0.20f, 0.30f, 0.22f, 0.15f, ROLE_LEG_BL },
    { 0.20f, 0.24f,-0.44f, 0.20f, 0.48f, 0.20f, 0.30f, 0.22f, 0.15f, ROLE_LEG_BR },
};

static const mob_part_t SHEEP_PARTS[] = {
    { 0.0f, 0.70f, -0.08f, 0.62f, 0.58f, 0.90f, 0.92f, 0.92f, 0.90f, ROLE_STATIC },
    { 0.0f, 0.70f,  0.52f, 0.40f, 0.42f, 0.38f, 0.78f, 0.72f, 0.66f, ROLE_HEAD   },
    {-0.18f, 0.20f, 0.30f, 0.18f, 0.40f, 0.18f, 0.55f, 0.52f, 0.50f, ROLE_LEG_FL },
    { 0.18f, 0.20f, 0.30f, 0.18f, 0.40f, 0.18f, 0.55f, 0.52f, 0.50f, ROLE_LEG_FR },
    {-0.18f, 0.20f,-0.42f, 0.18f, 0.40f, 0.18f, 0.55f, 0.52f, 0.50f, ROLE_LEG_BL },
    { 0.18f, 0.20f,-0.42f, 0.18f, 0.40f, 0.18f, 0.55f, 0.52f, 0.50f, ROLE_LEG_BR },
};

static const mob_part_t CHICKEN_PARTS[] = {
    { 0.0f, 0.40f, -0.04f, 0.32f, 0.36f, 0.42f, 0.95f, 0.95f, 0.93f, ROLE_STATIC },
    { 0.0f, 0.62f,  0.20f, 0.24f, 0.26f, 0.24f, 0.96f, 0.96f, 0.94f, ROLE_HEAD   },
    { 0.0f, 0.60f,  0.40f, 0.10f, 0.10f, 0.14f, 0.95f, 0.72f, 0.18f, ROLE_BEAK   },
    {-0.22f, 0.40f,-0.02f, 0.08f, 0.30f, 0.34f, 0.88f, 0.88f, 0.86f, ROLE_ARM_L  },
    { 0.22f, 0.40f,-0.02f, 0.08f, 0.30f, 0.34f, 0.88f, 0.88f, 0.86f, ROLE_ARM_R  },
    {-0.09f, 0.12f, 0.04f, 0.07f, 0.24f, 0.07f, 0.92f, 0.68f, 0.16f, ROLE_LEG_FL },
    { 0.09f, 0.12f, 0.04f, 0.07f, 0.24f, 0.07f, 0.92f, 0.68f, 0.16f, ROLE_LEG_FR },
};

static const mob_part_t PLAYER_PARTS[] = {
    { 0.0f, 1.52f, 0.0f,  0.50f, 0.50f, 0.50f, 0.88f, 0.68f, 0.55f, ROLE_HEAD   },
    { 0.0f, 1.02f, 0.0f,  0.50f, 0.72f, 0.26f, 0.25f, 0.38f, 0.72f, ROLE_STATIC },
    {-0.34f, 1.02f, 0.0f, 0.18f, 0.70f, 0.22f, 0.88f, 0.68f, 0.55f, ROLE_ARM_L  },
    { 0.34f, 1.02f, 0.0f, 0.18f, 0.70f, 0.22f, 0.88f, 0.68f, 0.55f, ROLE_ARM_R  },
    {-0.13f, 0.34f, 0.0f, 0.20f, 0.70f, 0.22f, 0.24f, 0.26f, 0.48f, ROLE_LEG_FL },
    { 0.13f, 0.34f, 0.0f, 0.20f, 0.70f, 0.22f, 0.24f, 0.26f, 0.48f, ROLE_LEG_FR },
};

static const mob_model_t MOB_MODELS[SP_COUNT] = {
    [SP_PIG]      = { PIG_PARTS,      (int)(sizeof PIG_PARTS      / sizeof PIG_PARTS[0])      },
    [SP_COW]      = { COW_PARTS,      (int)(sizeof COW_PARTS      / sizeof COW_PARTS[0])      },
    [SP_CHICKEN]  = { CHICKEN_PARTS,  (int)(sizeof CHICKEN_PARTS  / sizeof CHICKEN_PARTS[0])  },
    [SP_SHEEP]    = { SHEEP_PARTS,    (int)(sizeof SHEEP_PARTS    / sizeof SHEEP_PARTS[0])    },
    [SP_ZOMBIE]   = { ZOMBIE_PARTS,   (int)(sizeof ZOMBIE_PARTS   / sizeof ZOMBIE_PARTS[0])   },
    [SP_SKELETON] = { SKELETON_PARTS, (int)(sizeof SKELETON_PARTS / sizeof SKELETON_PARTS[0]) },
    [SP_CREEPER]  = { CREEPER_PARTS,  (int)(sizeof CREEPER_PARTS  / sizeof CREEPER_PARTS[0])  },
    [SP_SPIDER]   = { SPIDER_PARTS,   (int)(sizeof SPIDER_PARTS   / sizeof SPIDER_PARTS[0])   },
};

typedef struct {
    int    ready;
    GLuint prog;
    GLuint vao, vbo;
    GLuint atlas;
    int    vert_count;
    GLint  mvp_loc, model_loc, sun_dir_loc, sun_strength_loc, ambient_loc, color_loc, flash_loc;
    GLint  atlas_loc, textured_loc, body_tile_loc, front_tile_loc, tile_size_loc;
    GLint  eye_loc, sea_level_loc, eye_in_water_loc, eye_water_depth_loc;
    GLint  fog_color_loc, fog_density_loc;
    GLint  shadow_map_loc, shadow_enabled_loc, shadow_texel_loc, shadow_world_texel_loc, light_vp_loc;
    GLint  blocklight_tex_loc, light_origin_loc, light_dim_loc, blocklight_enabled_loc;
} render_state_t;

static render_state_t g_rs;

#define MOB_VERT_FLOATS 9

static const float CUBE_VERTS[] = {
     0.5f,-0.5f,-0.5f,  1,0,0,  0,1, 0,
     0.5f, 0.5f,-0.5f,  1,0,0,  0,0, 0,
     0.5f, 0.5f, 0.5f,  1,0,0,  1,0, 0,
     0.5f,-0.5f,-0.5f,  1,0,0,  0,1, 0,
     0.5f, 0.5f, 0.5f,  1,0,0,  1,0, 0,
     0.5f,-0.5f, 0.5f,  1,0,0,  1,1, 0,
    -0.5f,-0.5f,-0.5f, -1,0,0,  0,1, 0,
    -0.5f,-0.5f, 0.5f, -1,0,0,  1,1, 0,
    -0.5f, 0.5f, 0.5f, -1,0,0,  1,0, 0,
    -0.5f,-0.5f,-0.5f, -1,0,0,  0,1, 0,
    -0.5f, 0.5f, 0.5f, -1,0,0,  1,0, 0,
    -0.5f, 0.5f,-0.5f, -1,0,0,  0,0, 0,
    -0.5f, 0.5f,-0.5f,  0,1,0,  0,0, 0,
     0.5f, 0.5f,-0.5f,  0,1,0,  1,0, 0,
     0.5f, 0.5f, 0.5f,  0,1,0,  1,1, 0,
    -0.5f, 0.5f,-0.5f,  0,1,0,  0,0, 0,
     0.5f, 0.5f, 0.5f,  0,1,0,  1,1, 0,
    -0.5f, 0.5f, 0.5f,  0,1,0,  0,1, 0,
    -0.5f,-0.5f,-0.5f,  0,-1,0, 0,0, 0,
     0.5f,-0.5f,-0.5f,  0,-1,0, 1,0, 0,
     0.5f,-0.5f, 0.5f,  0,-1,0, 1,1, 0,
    -0.5f,-0.5f,-0.5f,  0,-1,0, 0,0, 0,
     0.5f,-0.5f, 0.5f,  0,-1,0, 1,1, 0,
    -0.5f,-0.5f, 0.5f,  0,-1,0, 0,1, 0,
    -0.5f,-0.5f, 0.5f,  0,0,1,  0,1, 1,
     0.5f,-0.5f, 0.5f,  0,0,1,  1,1, 1,
     0.5f, 0.5f, 0.5f,  0,0,1,  1,0, 1,
    -0.5f,-0.5f, 0.5f,  0,0,1,  0,1, 1,
     0.5f, 0.5f, 0.5f,  0,0,1,  1,0, 1,
    -0.5f, 0.5f, 0.5f,  0,0,1,  0,0, 1,
    -0.5f,-0.5f,-0.5f,  0,0,-1, 0,1, 0,
    -0.5f, 0.5f,-0.5f,  0,0,-1, 0,0, 0,
     0.5f, 0.5f,-0.5f,  0,0,-1, 1,0, 0,
    -0.5f,-0.5f,-0.5f,  0,0,-1, 0,1, 0,
     0.5f, 0.5f,-0.5f,  0,0,-1, 1,0, 0,
     0.5f,-0.5f,-0.5f,  0,0,-1, 1,1, 0,
};

static char *read_text(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "mob: failed to open %s\n", path); return NULL; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    if (sz < 0) { fclose(f); return NULL; }
    char *buf = malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t r = fread(buf, 1, (size_t)sz, f);
    buf[r] = '\0';
    fclose(f);
    return buf;
}

static GLuint compile_shader(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, (GLsizei)sizeof log, NULL, log);
        fprintf(stderr, "mob shader compile: %s\n", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

static GLuint load_program(const char *vp, const char *fp) {
    char *vs = read_text(vp);
    char *fs = read_text(fp);
    if (!vs || !fs) { free(vs); free(fs); return 0; }
    GLuint v = compile_shader(GL_VERTEX_SHADER, vs);
    GLuint f = compile_shader(GL_FRAGMENT_SHADER, fs);
    free(vs); free(fs);
    if (!v || !f) {
        if (v) glDeleteShader(v);
        if (f) glDeleteShader(f);
        return 0;
    }
    GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    glDeleteShader(v);
    glDeleteShader(f);
    if (!ok) { glDeleteProgram(p); return 0; }
    return p;
}

static int rs_init(void) {
    if (g_rs.ready) return 1;

    g_rs.prog = load_program("shaders/mob.vert", "shaders/mob.frag");
    if (!g_rs.prog) return 0;

    g_rs.mvp_loc          = glGetUniformLocation(g_rs.prog, "u_mvp");
    g_rs.model_loc        = glGetUniformLocation(g_rs.prog, "u_model");
    g_rs.sun_dir_loc      = glGetUniformLocation(g_rs.prog, "u_sun_dir");
    g_rs.sun_strength_loc = glGetUniformLocation(g_rs.prog, "u_sun_strength");
    g_rs.ambient_loc      = glGetUniformLocation(g_rs.prog, "u_ambient");
    g_rs.color_loc        = glGetUniformLocation(g_rs.prog, "u_color");
    g_rs.flash_loc        = glGetUniformLocation(g_rs.prog, "u_flash");
    g_rs.atlas_loc        = glGetUniformLocation(g_rs.prog, "u_atlas");
    g_rs.textured_loc     = glGetUniformLocation(g_rs.prog, "u_textured");
    g_rs.body_tile_loc    = glGetUniformLocation(g_rs.prog, "u_body_tile");
    g_rs.front_tile_loc   = glGetUniformLocation(g_rs.prog, "u_front_tile");
    g_rs.tile_size_loc    = glGetUniformLocation(g_rs.prog, "u_tile_size");
    g_rs.eye_loc          = glGetUniformLocation(g_rs.prog, "u_eye");
    g_rs.sea_level_loc    = glGetUniformLocation(g_rs.prog, "u_sea_level");
    g_rs.eye_in_water_loc = glGetUniformLocation(g_rs.prog, "u_eye_in_water");
    g_rs.eye_water_depth_loc = glGetUniformLocation(g_rs.prog, "u_eye_water_depth");
    g_rs.fog_color_loc    = glGetUniformLocation(g_rs.prog, "u_fog_color");
    g_rs.fog_density_loc  = glGetUniformLocation(g_rs.prog, "u_fog_density");
    g_rs.shadow_map_loc   = glGetUniformLocation(g_rs.prog, "u_shadow_map");
    g_rs.shadow_enabled_loc = glGetUniformLocation(g_rs.prog, "u_shadow_enabled");
    g_rs.shadow_texel_loc = glGetUniformLocation(g_rs.prog, "u_shadow_texel");
    g_rs.shadow_world_texel_loc = glGetUniformLocation(g_rs.prog, "u_shadow_world_texel");
    g_rs.light_vp_loc     = glGetUniformLocation(g_rs.prog, "u_light_vp");
    g_rs.blocklight_tex_loc = glGetUniformLocation(g_rs.prog, "u_blocklight_tex");
    g_rs.light_origin_loc = glGetUniformLocation(g_rs.prog, "u_light_origin");
    g_rs.light_dim_loc    = glGetUniformLocation(g_rs.prog, "u_light_dim");
    g_rs.blocklight_enabled_loc = glGetUniformLocation(g_rs.prog, "u_blocklight_enabled");

    g_rs.atlas = texture_create_mob_atlas();

    glGenVertexArrays(1, &g_rs.vao);
    glGenBuffers(1, &g_rs.vbo);
    glBindVertexArray(g_rs.vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_rs.vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof CUBE_VERTS, CUBE_VERTS, GL_STATIC_DRAW);
    GLsizei stride = MOB_VERT_FLOATS * (GLsizei)sizeof(float);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void *)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void *)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (void *)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, stride, (void *)(8 * sizeof(float)));
    glEnableVertexAttribArray(3);
    glBindVertexArray(0);

    g_rs.vert_count = (int)(sizeof CUBE_VERTS / (sizeof(float) * MOB_VERT_FLOATS));
    g_rs.ready = 1;
    return 1;
}

static void box_mvp(mat4 pv, mat4 base, vec3 offset, vec3 size) {
    mat4 m = mat4_multiply(base, mat4_translate(offset));
    m = mat4_multiply(m, mat4_scale(size));
    mat4 mvp = mat4_multiply(pv, m);
    glUniformMatrix4fv(g_rs.mvp_loc, 1, GL_FALSE, mvp.m);
    glUniformMatrix4fv(g_rs.model_loc, 1, GL_FALSE, m.m);
}

static void bind_env(const mob_render_env_t *env) {
    glUniform1i(g_rs.shadow_map_loc, 3);
    glUniform1i(g_rs.blocklight_tex_loc, 2);
    if (!env) {
        glUniform1i(g_rs.shadow_enabled_loc, 0);
        glUniform1i(g_rs.blocklight_enabled_loc, 0);
        return;
    }
    glUniform3f(g_rs.eye_loc, env->eye.x, env->eye.y, env->eye.z);
    glUniform1f(g_rs.sea_level_loc, env->sea_level);
    glUniform1i(g_rs.eye_in_water_loc, env->eye_in_water);
    glUniform1f(g_rs.eye_water_depth_loc, env->eye_water_depth);
    glUniform3f(g_rs.fog_color_loc, env->fog_color.x, env->fog_color.y, env->fog_color.z);
    glUniform1f(g_rs.fog_density_loc, env->fog_density);
    glUniformMatrix4fv(g_rs.light_vp_loc, 1, GL_FALSE, env->light_vp.m);
    glUniform1i(g_rs.shadow_enabled_loc, env->shadow_enabled);
    glUniform1f(g_rs.shadow_texel_loc, env->shadow_texel);
    glUniform1f(g_rs.shadow_world_texel_loc, env->shadow_world_texel);
    glUniform1i(g_rs.blocklight_enabled_loc, env->blocklight_enabled);
    glUniform3f(g_rs.light_origin_loc, env->light_origin.x, env->light_origin.y, env->light_origin.z);
    glUniform1f(g_rs.light_dim_loc, env->light_dim);

    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, (GLuint)env->shadow_map_tex);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_3D, (GLuint)env->blocklight_tex);
    glActiveTexture(GL_TEXTURE0);
}

static void draw_box(mat4 pv, mat4 base,
                     vec3 offset, vec3 size, float r, float g, float b, float flash) {
    box_mvp(pv, base, offset, size);
    glUniform1i(g_rs.textured_loc, 0);
    glUniform3f(g_rs.color_loc, r, g, b);
    glUniform1f(g_rs.flash_loc, flash);
    glDrawArrays(GL_TRIANGLES, 0, g_rs.vert_count);
}

static void draw_box_tex(mat4 pv, mat4 base, vec3 offset, vec3 size,
                         float btx, float bty, float ftx, float fty,
                         float fr, float fg, float fb, float flash) {
    box_mvp(pv, base, offset, size);
    if (g_rs.atlas) {
        glUniform1i(g_rs.textured_loc, 1);
        glUniform2f(g_rs.body_tile_loc, btx, bty);
        glUniform2f(g_rs.front_tile_loc, ftx, fty);
    } else {
        glUniform1i(g_rs.textured_loc, 0);
        glUniform3f(g_rs.color_loc, fr, fg, fb);
    }
    glUniform1f(g_rs.flash_loc, flash);
    glDrawArrays(GL_TRIANGLES, 0, g_rs.vert_count);
}

static float limb_swing(part_role_t role, float phase) {
    float s = sinf(phase);
    switch (role) {
        case ROLE_LEG_FL: case ROLE_LEG_BR: case ROLE_ARM_R: return  s;
        case ROLE_LEG_FR: case ROLE_LEG_BL: case ROLE_ARM_L: return -s;
        default: return 0.0f;
    }
}

static void part_tiles(int sp, part_role_t role,
                       float *btx, float *bty, float *ftx, float *fty) {
    float row = (float)sp;
    if (role == ROLE_BEAK) {
        *btx = 3.0f; *bty = row; *ftx = 3.0f; *fty = row;
        return;
    }
    float col = (role == ROLE_LEG_FL || role == ROLE_LEG_FR ||
                 role == ROLE_LEG_BL || role == ROLE_LEG_BR ||
                 role == ROLE_ARM_L  || role == ROLE_ARM_R) ? 2.0f : 0.0f;
    *btx = col; *bty = row;
    if (role == ROLE_HEAD) { *ftx = 1.0f; *fty = row; }
    else                   { *ftx = col;  *fty = row; }
}

static void render_mob(const entity_t *e, mat4 pv) {
    int sp = (int)e->species;
    if (sp < 0 || sp >= SP_COUNT) return;
    const mob_model_t *model = &MOB_MODELS[sp];
    if (!model->parts || model->count <= 0) return;

    mat4 base = mat4_multiply(mat4_translate(e->pos), mat4_rotate_y(e->yaw));

    float flash = e->hurt_flash;
    if (flash < 0.0f) flash = 0.0f;

    const float SWING = 0.18f;

    int n = model->count;
    if (n > MAX_PARTS) n = MAX_PARTS;
    for (int i = 0; i < n; i++) {
        const mob_part_t *p = &model->parts[i];
        float dz = limb_swing(p->role, e->anim_phase) * SWING;
        vec3 offset = { p->ox, p->oy, p->oz + dz };
        vec3 size   = { p->sx, p->sy, p->sz };
        float btx, bty, ftx, fty;
        part_tiles(sp, p->role, &btx, &bty, &ftx, &fty);
        draw_box_tex(pv, base, offset, size, btx, bty, ftx, fty, p->r, p->g, p->b, flash);
    }
}

static void render_item(const entity_t *e, mat4 pv) {
    float bob = sinf(e->bob_phase) * 0.08f;
    vec3 pos = { e->pos.x, e->pos.y + bob, e->pos.z };
    mat4 base = mat4_multiply(mat4_translate(pos), mat4_rotate_y(e->bob_phase));

    float r, g, b;
    if (item_is_block(e->item_id_payload)) {
        block_avg_color((block_t)e->item_id_payload, &r, &g, &b);
    } else {
        r = 0.62f; g = 0.62f; b = 0.62f;
    }
    vec3 zero = { 0.0f, 0.0f, 0.0f };
    vec3 size = { 0.30f, 0.30f, 0.30f };
    draw_box(pv, base, zero, size, r, g, b, 0.0f);
}

static void render_xp_orb(const entity_t *e, mat4 pv) {
    float bob = sinf(e->bob_phase) * 0.06f;
    vec3 pos = { e->pos.x, e->pos.y + bob, e->pos.z };
    mat4 base = mat4_translate(pos);
    vec3 zero = { 0.0f, 0.0f, 0.0f };
    vec3 size = { 0.18f, 0.18f, 0.18f };
    draw_box(pv, base, zero, size, 0.40f, 0.95f, 0.30f, 0.35f);
}

static void render_thrown(const entity_t *e, mat4 pv) {
    mat4 base = mat4_multiply(mat4_translate(e->pos), mat4_rotate_y(e->bob_phase));
    float r, g, b;
    block_avg_color(0, &r, &g, &b);
    item_id base_id = potion_base(e->item_id_payload);
    switch (base_id) {
        case ITEM_POTION_HARMING: case ITEM_POTION_POISON: r=0.35f; g=0.6f; b=0.2f; break;
        case ITEM_POTION_HEALING: case ITEM_POTION_REGEN:  r=0.9f;  g=0.3f; b=0.45f; break;
        case ITEM_POTION_SWIFTNESS: case ITEM_POTION_WATER_BREATH: r=0.35f; g=0.7f; b=0.85f; break;
        default: r=0.6f; g=0.45f; b=0.75f; break;
    }
    vec3 zero = { 0.0f, 0.0f, 0.0f };
    vec3 sz   = { 0.20f, 0.26f, 0.20f };
    draw_box(pv, base, zero, sz, r, g, b, 0.0f);
}

static void render_arrow(const entity_t *e, mat4 pv) {
    mat4 base = mat4_multiply(mat4_translate(e->pos), mat4_rotate_y(e->yaw));
    base = mat4_multiply(base, mat4_rotate_x(-e->head_pitch));
    vec3 zero   = { 0.0f, 0.0f, 0.0f };
    vec3 shaft  = { 0.05f, 0.05f, 0.7f };
    draw_box(pv, base, zero, shaft, 0.52f, 0.40f, 0.26f, 0.0f);
    vec3 hoff   = { 0.0f, 0.0f, 0.40f };
    vec3 hsz    = { 0.10f, 0.10f, 0.14f };
    draw_box(pv, base, hoff, hsz, 0.72f, 0.74f, 0.78f, 0.0f);
}

void entity_render_avatars(mat4 pv, vec3 sun_dir, float sun_strength, float ambient,
                           const vec3 *pos, const float *yaw, const float *anim, int count,
                           const mob_render_env_t *env) {
    if (!rs_init() || count <= 0) return;
    glEnable(GL_DEPTH_TEST);
    glUseProgram(g_rs.prog);
    glUniform3f(g_rs.sun_dir_loc, sun_dir.x, sun_dir.y, sun_dir.z);
    glUniform1f(g_rs.sun_strength_loc, sun_strength);
    glUniform1f(g_rs.ambient_loc, ambient);
    bind_env(env);
    if (g_rs.atlas) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, g_rs.atlas);
        glUniform1i(g_rs.atlas_loc, 0);
    }
    glUniform1f(g_rs.tile_size_loc, 1.0f / (float)MOB_ATLAS_TILES);
    glBindVertexArray(g_rs.vao);
    int nparts = (int)(sizeof PLAYER_PARTS / sizeof PLAYER_PARTS[0]);
    for (int k = 0; k < count; k++) {
        mat4 base = mat4_multiply(mat4_translate(pos[k]), mat4_rotate_y(yaw[k]));
        for (int i = 0; i < nparts; i++) {
            const mob_part_t *p = &PLAYER_PARTS[i];
            float dz = limb_swing(p->role, anim[k]) * 0.22f;
            vec3 off = { p->ox, p->oy, p->oz + dz };
            vec3 sz  = { p->sx, p->sy, p->sz };
            float btx, bty, ftx, fty;
            part_tiles(8, p->role, &btx, &bty, &ftx, &fty);
            draw_box_tex(pv, base, off, sz, btx, bty, ftx, fty, p->r, p->g, p->b, 0.0f);
        }
    }
    glBindVertexArray(0);
}

void entity_render(const entity_system_t *es, mat4 pv,
                   vec3 sun_dir, float sun_strength, float ambient,
                   const mob_render_env_t *env) {
    if (!es) return;
    if (!rs_init()) return;

    glEnable(GL_DEPTH_TEST);
    glUseProgram(g_rs.prog);
    glUniform3f(g_rs.sun_dir_loc, sun_dir.x, sun_dir.y, sun_dir.z);
    glUniform1f(g_rs.sun_strength_loc, sun_strength);
    glUniform1f(g_rs.ambient_loc, ambient);
    bind_env(env);
    if (g_rs.atlas) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, g_rs.atlas);
        glUniform1i(g_rs.atlas_loc, 0);
    }
    glUniform1f(g_rs.tile_size_loc, 1.0f / (float)MOB_ATLAS_TILES);
    glBindVertexArray(g_rs.vao);

    int n = es->count;
    if (n > MAX_ENTITIES) n = MAX_ENTITIES;
    for (int i = 0; i < n; i++) {
        const entity_t *e = &es->e[i];
        if (!e->active) continue;
        switch (e->kind) {
            case EK_MOB:    render_mob(e, pv);     break;
            case EK_ITEM:   render_item(e, pv);    break;
            case EK_XP_ORB: render_xp_orb(e, pv);  break;
            case EK_ARROW:  render_arrow(e, pv);   break;
            case EK_THROWN: render_thrown(e, pv);  break;
            default:        break;
        }
    }

    glBindVertexArray(0);
}
