/* Copyright (C) 2011 The Android Open Source Project
**
** This software is licensed under the terms of the GNU General Public
** License version 2, as published by the Free Software Foundation, and
** may be copied, distributed, and modified under those terms.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
*/
#include "opengles.h"

#include <SDL.h>
#include <SDL_syswm.h>

#include <assert.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gmodule.h>

#define RENDER_API_NO_PROTOTYPES 1
#include "render_api.h"

/* Name of the GLES rendering library we're going to use */
#ifdef _WIN32
#define RENDERER_MODULE_NAME  "libOpenglRender"
#else
#define RENDERER_MODULE_NAME  "OpenglRender"
#endif

#define DYNLINK_FUNCTIONS                   \
    DYNLINK_FUNC(initLibrary)               \
    DYNLINK_FUNC(getOpenGLRendererStatus)   \
    DYNLINK_FUNC(setStreamMode)             \
    DYNLINK_FUNC(initOpenGLRenderer)        \
    DYNLINK_FUNC(setPostCallback)           \
    DYNLINK_FUNC(getHardwareStrings)        \
    DYNLINK_FUNC(createOpenGLSubwindow)     \
    DYNLINK_FUNC(destroyOpenGLSubwindow)    \
    DYNLINK_FUNC(repaintOpenGLDisplay)      \
    DYNLINK_FUNC(stopOpenGLRenderer)

static GModule *g_rendererLib;

static int          g_rendererStarted;
static char         g_rendererAddress[256];

/* Define the function pointers */
#define DYNLINK_FUNC(name) \
    static name##Fn name = NULL;
DYNLINK_FUNCTIONS
#undef DYNLINK_FUNC

static int
initOpenglesEmulationFuncs(GModule* g_rendererLib)
{
    void*  symbol;

#define DYNLINK_FUNC(name)                                          \
    if(g_module_symbol(g_rendererLib, #name, &symbol)) {            \
        name = symbol;                                              \
    } else {                                                        \
        OPENGL_EMU_ERR("QEMU: GLES emulation: "                     \
                       "Could not find required symbol (%s): %s\n", \
                       #name, g_module_error());                    \
        return -1;                                                  \
    }
DYNLINK_FUNCTIONS
#undef DYNLINK_FUNC

    return 0;
}

static GModule *
find_and_open_module(const char *module_name, GModuleFlags flags)
{
    GModule *module = NULL;
    const char *path;
    const char *path_var;
#ifdef _WIN32
    path_var = "PATH";
#else
    path_var = "LD_LIBRARY_PATH";
#endif
    path = getenv(path_var);
    if (path) {
        gchar **dirs = g_strsplit(path, G_SEARCHPATH_SEPARATOR_S, 0);
        if (dirs) {
            gchar **pdir;
            for (pdir = dirs; pdir && !module; ++pdir) {
                if (*pdir) {
                    char *filename = g_module_build_path(*pdir, module_name);
                    module = g_module_open(filename, flags);
                    g_free(filename);
                }
            }
            g_strfreev(dirs);
        }
    }
    if (!module) {
        module = g_module_open(NULL, flags);
    }
    return module;
}

int
android_initOpenglesEmulation(void)
{
    if (g_rendererLib != NULL)
        return 0;

    OPENGL_EMU_DBG("QEMU: Initializing hardware OpenGLES emulation support\n");

    g_rendererLib = find_and_open_module(RENDERER_MODULE_NAME, G_MODULE_BIND_LAZY);
    if (g_rendererLib == NULL) {
        OPENGL_EMU_ERR("QEMU: Could not load OpenGLES emulation library: %s\n",
                       RENDERER_MODULE_NAME);
        return -1;
    }

    /* Resolve the functions */
    if (initOpenglesEmulationFuncs(g_rendererLib) < 0) {
        OPENGL_EMU_ERR("QEMU: OpenGLES emulation library mismatch. Be sure to use the correct version!\n");
        goto BAD_EXIT;
    }

    if (!initLibrary()) {
        OPENGL_EMU_ERR("QEMU: OpenGLES initialization failed!\n");
        goto BAD_EXIT;
    }

#ifdef _WIN32
    setStreamMode(STREAM_MODE_PIPE);
#else
    setStreamMode(STREAM_MODE_UNIX);
#endif

    return 0;

BAD_EXIT:
    OPENGL_EMU_ERR("QEMU: OpenGLES emulation library could not be initialized!\n");
    g_module_close(g_rendererLib);
    g_rendererLib = NULL;
    return -1;
}

int
android_startOpenglesRenderer(void)
{
    const SDL_VideoInfo  *screenInfo;
    int width = 320, height = 240;

    if (!g_rendererLib) {
        OPENGL_EMU_ERR("QEMU: Can't start OpenGLES renderer without support libraries\n");
        return -1;
    }

    if (g_rendererStarted) {
        return 0;
    }

    // Get info of the current window
    screenInfo = SDL_GetVideoInfo();
    if (screenInfo != NULL) {
        width = screenInfo->current_w;
        height = screenInfo->current_h;
    }
    else {
        OPENGL_EMU_DBG("QEMU: Using default resolution\n");
    }

    if (!initOpenGLRenderer(width, height, g_rendererAddress, sizeof(g_rendererAddress))) {
        OPENGL_EMU_ERR("QEMU: Can't start OpenGLES renderer?\n");
        return -1;
    }

    g_rendererStarted = 1;
    return 0;
}

void
android_setPostCallback(OnPostFunc onPost, void* onPostContext)
{
    if (g_rendererLib) {
        setPostCallback(onPost, onPostContext);
    }
}

static void strncpy_safe(char* dst, const char* src, size_t n)
{
    strncpy(dst, src, n);
    dst[n-1] = '\0';
}

static void extractBaseString(char* dst, const char* src, size_t dstSize)
{
    const char* begin = strchr(src, '(');
    const char* end = strrchr(src, ')');

    if (!begin || !end) {
        strncpy_safe(dst, src, dstSize);
        return;
    }
    begin += 1;

    // "foo (bar)"
    //       ^  ^
    //       b  e
    //     = 5  8
    // substring with NUL-terminator is end-begin+1 bytes
    if (end - begin + 1 > dstSize) {
        end = begin + dstSize - 1;
    }

    strncpy_safe(dst, begin, end - begin + 1);
}

void
android_getOpenglesHardwareStrings(char* vendor, size_t vendorBufSize,
                                   char* renderer, size_t rendererBufSize,
                                   char* version, size_t versionBufSize)
{
    const char *vendorSrc, *rendererSrc, *versionSrc;

    assert(vendorBufSize > 0 && rendererBufSize > 0 && versionBufSize > 0);
    assert(vendor != NULL && renderer != NULL && version != NULL);

    if (!g_rendererStarted) {
        OPENGL_EMU_ERR("QEMU: Can't get OpenGL ES hardware strings when renderer not started\n");
        vendor[0] = renderer[0] = version[0] = '\0';
        return;
    }

    getHardwareStrings(&vendorSrc, &rendererSrc, &versionSrc);
    if (!vendorSrc) vendorSrc = "";
    if (!rendererSrc) rendererSrc = "";
    if (!versionSrc) versionSrc = "";

    /* Special case for the default ES to GL translators: extract the strings
     * of the underlying OpenGL implementation. */
    if (strncmp(vendorSrc, "Google", 6) == 0 &&
            strncmp(rendererSrc, "Android Emulator OpenGL ES Translator", 37) == 0) {
        extractBaseString(vendor, vendorSrc, vendorBufSize);
        extractBaseString(renderer, rendererSrc, rendererBufSize);
        extractBaseString(version, versionSrc, versionBufSize);
    } else {
        strncpy_safe(vendor, vendorSrc, vendorBufSize);
        strncpy_safe(renderer, rendererSrc, rendererBufSize);
        strncpy_safe(version, versionSrc, versionBufSize);
    }
}

void
android_stopOpenglesRenderer(void)
{
    if (g_rendererStarted) {
        stopOpenGLRenderer();
        g_rendererStarted = 0;
    }
}

int
android_showOpenglesWindow(void)
{
    SDL_SysWMinfo   wminfo;
    void           *winhandle;
    const SDL_VideoInfo  *screenInfo;

    if (!g_rendererStarted) {
        return -1;
    }

    memset(&wminfo, 0, sizeof(wminfo));
    SDL_GetWMInfo(&wminfo);

#ifdef _WIN32
    winhandle = (void*)wminfo.window;
#elif defined(CONFIG_DARWIN)
    winhandle = (void*)wminfo.nsWindowPtr;
#else
    winhandle = (void*)wminfo.info.x11.window;
#endif

    // Get info of the current window
    screenInfo = SDL_GetVideoInfo();
    if (screenInfo != NULL) {
        return createOpenGLSubwindow((FBNativeWindowType)winhandle,
                                     0, 0,
                                     screenInfo->current_w,
                                     screenInfo->current_h,
                                     0);
    }
    else {
        OPENGL_EMU_ERR("QEMU: unable to get SDL window info (1)\n");
        return -1;
    }
}

int
android_hideOpenglesWindow(void)
{
    if (g_rendererStarted) {
        int success = destroyOpenGLSubwindow();
        return success ? 0 : -1;
    } else {
        return -1;
    }
}

int
android_redrawOpenglesWindow(int force)
{
    SDL_SysWMinfo   wminfo;
    void           *winhandle;
    const SDL_VideoInfo  *screenInfo;

    if (!g_rendererStarted) {
        return -1;
    }

    memset(&wminfo, 0, sizeof(wminfo));
    SDL_GetWMInfo(&wminfo);

#ifdef _WIN32
    winhandle = (void*)wminfo.window;
#elif defined(CONFIG_DARWIN)
    winhandle = (void*)wminfo.nsWindowPtr;
#else
    winhandle = (void*)wminfo.info.x11.window;
#endif

    // Get info of the current window
    screenInfo = SDL_GetVideoInfo();
    if (screenInfo != NULL) {
        return repaintOpenGLDisplay((FBNativeWindowType)winhandle,
                                     0, 0,
                                     screenInfo->current_w,
                                     screenInfo->current_h,
                                     0,
                                     force);
    }
    else {
        OPENGL_EMU_ERR("QEMU: unable to get SDL window info (2)\n");
        return -1;
    }
}

void
android_gles_server_path(char* buff, size_t buffsize)
{
    strncpy_safe(buff, g_rendererAddress, buffsize);
}

int
android_getOpenGLRendererStatus(void)
{
    return (getOpenGLRendererStatus());
}
