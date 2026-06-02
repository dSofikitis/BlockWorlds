#include "platform.h"

#if defined(_WIN32)

#include <direct.h>
int pf_mkdir(const char *path) { return _mkdir(path); }
int pf_rmdir(const char *path) { return _rmdir(path); }

#else

#include <sys/stat.h>
#include <unistd.h>
int pf_mkdir(const char *path) { return mkdir(path, 0755); }
int pf_rmdir(const char *path) { return rmdir(path); }

#endif