#include <stdlib.h>
#include "sysemu/sysemu.h"

/* Function missing in mingw */

int setenv(const char *name, const char *value, int overwrite)
{
    int result = 0;
    if (overwrite || !getenv(name)) {
        size_t length = strlen(name) + strlen(value) + 2;
        char *string = g_malloc(length);
        snprintf(string, length, "%s=%s", name, value);
        result = putenv(string);

        /* Windows takes a copy and does not continue to use our string.
         * Therefore it can be safely freed on this platform.  POSIX code
         * typically has to leak the string because according to the spec it
         * becomes part of the environment.
         */
        g_free(string);
    }
    return result;
}

int unsetenv(const char *name)
{
    int result = 0;
    if (getenv(name)) {
        size_t length = strlen(name) + 2;
        char *string = g_malloc(length);
        snprintf(string, length, "%s=", name);
        result = putenv(string);

        /* Windows takes a copy and does not continue to use our string.
         * Therefore it can be safely freed on this platform.  POSIX code
         * typically has to leak the string because according to the spec it
         * becomes part of the environment.
         */
        g_free(string);
    }

    return result;
}
