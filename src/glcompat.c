#include "glcompat.h"

#if !defined(__APPLE__)

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#define X(ret, name, params) PFN_##name name = NULL;
GLCOMPAT_FUNCS(X)
#undef X

int gl_load(void) {
    int ok = 1;
#define X(ret, name, params) \
    name = (PFN_##name)glfwGetProcAddress(#name); \
    if (!name) ok = 0;
    GLCOMPAT_FUNCS(X)
#undef X
    return ok;
}

#endif

typedef int glcompat_translation_unit;
