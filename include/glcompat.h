#pragma once

#if defined(__APPLE__)

#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl3.h>
static inline int gl_load(void) { return 1; }

#else

#define GLFW_INCLUDE_NONE
#include <stddef.h>

typedef unsigned int   GLenum;
typedef unsigned char  GLboolean;
typedef unsigned int   GLbitfield;
typedef signed char    GLbyte;
typedef short          GLshort;
typedef int            GLint;
typedef int            GLsizei;
typedef unsigned char  GLubyte;
typedef unsigned short GLushort;
typedef unsigned int   GLuint;
typedef float          GLfloat;
typedef float          GLclampf;
typedef double         GLdouble;
typedef void           GLvoid;
typedef char           GLchar;
typedef ptrdiff_t      GLsizeiptr;
typedef ptrdiff_t      GLintptr;

#define GL_FALSE 0
#define GL_TRUE  1
#define GL_NONE  0

#define GL_POINTS                      0x0000
#define GL_LINES                       0x0001
#define GL_TRIANGLES                   0x0004
#define GL_LEQUAL                      0x0203
#define GL_SRC_ALPHA                   0x0302
#define GL_ONE_MINUS_SRC_ALPHA         0x0303
#define GL_DEPTH_BUFFER_BIT            0x00000100
#define GL_COLOR_BUFFER_BIT            0x00004000
#define GL_TEXTURE_BORDER_COLOR        0x1004
#define GL_DEPTH_COMPONENT             0x1902
#define GL_RED                         0x1903
#define GL_RGB                         0x1907
#define GL_RGBA                        0x1908
#define GL_VERSION                     0x1F02
#define GL_NEAREST                     0x2600
#define GL_LINEAR                      0x2601
#define GL_TEXTURE_MAG_FILTER          0x2800
#define GL_TEXTURE_MIN_FILTER          0x2801
#define GL_TEXTURE_WRAP_S              0x2802
#define GL_TEXTURE_WRAP_T              0x2803
#define GL_POLYGON_OFFSET_FILL         0x8037
#define GL_RGBA8                       0x8058
#define GL_R8                          0x8229
#define GL_TEXTURE_2D                  0x0DE1
#define GL_TEXTURE_3D                  0x806F
#define GL_TEXTURE_WRAP_R              0x8072
#define GL_PROGRAM_POINT_SIZE          0x8642
#define GL_DEPTH_TEST                  0x0B71
#define GL_BLEND                       0x0BE2
#define GL_UNSIGNED_BYTE               0x1401
#define GL_UNSIGNED_INT                0x1405
#define GL_FLOAT                       0x1406
#define GL_MULTISAMPLE                 0x809D
#define GL_CLAMP_TO_BORDER             0x812D
#define GL_CLAMP_TO_EDGE               0x812F
#define GL_DEPTH_COMPONENT24           0x81A6
#define GL_DEPTH_CLAMP                 0x864F
#define GL_TEXTURE0                    0x84C0
#define GL_TEXTURE1                    0x84C1
#define GL_TEXTURE2                    0x84C2
#define GL_TEXTURE3                    0x84C3
#define GL_TEXTURE4                    0x84C4
#define GL_TEXTURE5                    0x84C5
#define GL_TEXTURE6                    0x84C6
#define GL_TEXTURE_COMPARE_MODE        0x884C
#define GL_TEXTURE_COMPARE_FUNC        0x884D
#define GL_COMPARE_REF_TO_TEXTURE      0x884E
#define GL_ARRAY_BUFFER                0x8892
#define GL_ELEMENT_ARRAY_BUFFER        0x8893
#define GL_STATIC_DRAW                 0x88E4
#define GL_DYNAMIC_DRAW                0x88E8
#define GL_FRAGMENT_SHADER             0x8B30
#define GL_VERTEX_SHADER               0x8B31
#define GL_COMPILE_STATUS              0x8B81
#define GL_LINK_STATUS                 0x8B82
#define GL_SHADING_LANGUAGE_VERSION    0x8B8C
#define GL_FRAMEBUFFER_COMPLETE        0x8CD5
#define GL_COLOR_ATTACHMENT0           0x8CE0
#define GL_DEPTH_ATTACHMENT            0x8D00
#define GL_FRAMEBUFFER                 0x8D40

#if defined(_WIN32)
#define GLCAPI __stdcall
#else
#define GLCAPI
#endif

#define GLCOMPAT_FUNCS(X) \
    X(void,   glActiveTexture,           (GLenum)) \
    X(void,   glAttachShader,            (GLuint, GLuint)) \
    X(void,   glBindBuffer,              (GLenum, GLuint)) \
    X(void,   glBindFramebuffer,         (GLenum, GLuint)) \
    X(void,   glBindTexture,             (GLenum, GLuint)) \
    X(void,   glBindVertexArray,         (GLuint)) \
    X(void,   glBlendFunc,               (GLenum, GLenum)) \
    X(void,   glBufferData,              (GLenum, GLsizeiptr, const void *, GLenum)) \
    X(void,   glBufferSubData,           (GLenum, GLintptr, GLsizeiptr, const void *)) \
    X(GLenum, glCheckFramebufferStatus,  (GLenum)) \
    X(void,   glClear,                   (GLbitfield)) \
    X(void,   glClearColor,              (GLfloat, GLfloat, GLfloat, GLfloat)) \
    X(void,   glCompileShader,           (GLuint)) \
    X(void,   glCopyTexImage2D,          (GLenum, GLint, GLenum, GLint, GLint, GLsizei, GLsizei, GLint)) \
    X(GLuint, glCreateProgram,           (void)) \
    X(GLuint, glCreateShader,            (GLenum)) \
    X(void,   glDeleteBuffers,           (GLsizei, const GLuint *)) \
    X(void,   glDeleteFramebuffers,      (GLsizei, const GLuint *)) \
    X(void,   glDeleteProgram,           (GLuint)) \
    X(void,   glDeleteShader,            (GLuint)) \
    X(void,   glDeleteTextures,          (GLsizei, const GLuint *)) \
    X(void,   glDeleteVertexArrays,      (GLsizei, const GLuint *)) \
    X(void,   glDepthMask,               (GLboolean)) \
    X(void,   glDisable,                 (GLenum)) \
    X(void,   glDrawArrays,              (GLenum, GLint, GLsizei)) \
    X(void,   glDrawBuffer,              (GLenum)) \
    X(void,   glDrawElements,            (GLenum, GLsizei, GLenum, const void *)) \
    X(void,   glEnable,                  (GLenum)) \
    X(void,   glEnableVertexAttribArray, (GLuint)) \
    X(void,   glFramebufferTexture2D,    (GLenum, GLenum, GLenum, GLuint, GLint)) \
    X(void,   glGenBuffers,              (GLsizei, GLuint *)) \
    X(void,   glGenFramebuffers,         (GLsizei, GLuint *)) \
    X(void,   glGenTextures,             (GLsizei, GLuint *)) \
    X(void,   glGenVertexArrays,         (GLsizei, GLuint *)) \
    X(void,   glGetProgramInfoLog,       (GLuint, GLsizei, GLsizei *, GLchar *)) \
    X(void,   glGetProgramiv,            (GLuint, GLenum, GLint *)) \
    X(void,   glGetShaderInfoLog,        (GLuint, GLsizei, GLsizei *, GLchar *)) \
    X(void,   glGetShaderiv,             (GLuint, GLenum, GLint *)) \
    X(const GLubyte *, glGetString,      (GLenum)) \
    X(GLint,  glGetUniformLocation,      (GLuint, const GLchar *)) \
    X(void,   glLineWidth,               (GLfloat)) \
    X(void,   glLinkProgram,             (GLuint)) \
    X(void,   glPolygonOffset,           (GLfloat, GLfloat)) \
    X(void,   glReadBuffer,              (GLenum)) \
    X(void,   glShaderSource,            (GLuint, GLsizei, const GLchar *const *, const GLint *)) \
    X(void,   glTexImage2D,              (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *)) \
    X(void,   glTexImage3D,              (GLenum, GLint, GLint, GLsizei, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *)) \
    X(void,   glTexSubImage3D,           (GLenum, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei, GLenum, GLenum, const void *)) \
    X(void,   glTexParameterfv,          (GLenum, GLenum, const GLfloat *)) \
    X(void,   glTexParameteri,           (GLenum, GLenum, GLint)) \
    X(void,   glUniform1f,               (GLint, GLfloat)) \
    X(void,   glUniform1i,               (GLint, GLint)) \
    X(void,   glUniform2f,               (GLint, GLfloat, GLfloat)) \
    X(void,   glUniform3f,               (GLint, GLfloat, GLfloat, GLfloat)) \
    X(void,   glUniform4f,               (GLint, GLfloat, GLfloat, GLfloat, GLfloat)) \
    X(void,   glUniformMatrix4fv,        (GLint, GLsizei, GLboolean, const GLfloat *)) \
    X(void,   glUseProgram,              (GLuint)) \
    X(void,   glVertexAttribPointer,     (GLuint, GLint, GLenum, GLboolean, GLsizei, const void *)) \
    X(void,   glViewport,                (GLint, GLint, GLsizei, GLsizei))

#define X(ret, name, params) typedef ret (GLCAPI *PFN_##name) params; extern PFN_##name name;
GLCOMPAT_FUNCS(X)
#undef X

int gl_load(void);

#endif
