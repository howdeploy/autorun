/*
 * OpenGL for the Switch display driver
 *
 * Copyright 2026 Wine-NX contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#if 0
#pragma makedep unix
#endif

#include "config.h"

#ifdef __SWITCH__

#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "win32u_private.h"
#include "wine/opengl_driver.h"
#include "wine/debug.h"
#include "../../wine-nx-probe/source/osk.h"

WINE_DEFAULT_DEBUG_CHANNEL(wgl);

/* The runtime hands the screen's NWindow to one OpenGL surface at a time
 * (wine-nx-probe/source/runtime.c). */
extern void *wine_nx_gl_acquire_window( void );
extern void wine_nx_gl_release_window( void );
extern void wine_nx_runtime_trace( const char *msg ) __attribute__((weak));

static const struct egl_platform *egl;
static const struct opengl_funcs *funcs;
static const struct opengl_drawable_funcs nx_drawable_funcs;
static const struct opengl_drawable_funcs nx_offscreen_funcs;

struct nx_gl_drawable
{
    struct opengl_drawable base;
    BOOL screen;  /* this drawable shares the screen's EGL surface */
    BOOL deferred;  /* no surface yet: the screen is taken on the first swap */
};

/* The one EGL surface on the screen's NWindow, and the drawables sharing it.
 * win32u gives a window a second drawable whenever another context is made
 * current on it while the first is still in use - wined3d does exactly that
 * when it replaces its caps context with a versioned one - so the drawables
 * of a window share the surface rather than each asking for the screen. */
static pthread_mutex_t nx_screen_mutex = PTHREAD_MUTEX_INITIALIZER;
static EGLSurface nx_screen_surface;
static unsigned int nx_screen_refs;
static int nx_screen_format;

static struct nx_gl_drawable *impl_from_opengl_drawable( struct opengl_drawable *base )
{
    return CONTAINING_RECORD( base, struct nx_gl_drawable, base );
}

static void nx_log( const char *format, ... )
{
    char buffer[256];
    va_list args;

    if (!&wine_nx_runtime_trace) return;
    va_start( args, format );
    vsnprintf( buffer, sizeof(buffer), format, args );
    va_end( args );
    wine_nx_runtime_trace( buffer );
}

/* The EGL config of a pixel format, as win32u's egldrv chooses it for pbuffers. */
static EGLConfig nx_config_for_format( int format )
{
    return egl->configs[(format - 1) % egl->config_count];
}

static void nx_drawable_destroy( struct opengl_drawable *base )
{
    struct nx_gl_drawable *gl = impl_from_opengl_drawable( base );
    EGLSurface surface = NULL;

    if (!gl->screen)
    {
        /* win32u destroys the EGL surface after this callback. */
        return;
    }

    /* The screen's surface outlives this drawable while another one shares it;
     * take it away from win32u either way, so the framebuffer only gets the
     * NWindow back once the surface is really gone. */
    base->surface = NULL;

    pthread_mutex_lock( &nx_screen_mutex );
    if (nx_screen_refs && !--nx_screen_refs)
    {
        surface = nx_screen_surface;
        nx_screen_surface = NULL;
    }
    pthread_mutex_unlock( &nx_screen_mutex );

    if (!surface) return;
    funcs->p_eglDestroySurface( egl->display, surface );
    wine_nx_gl_release_window();
}

static void nx_drawable_flush( struct opengl_drawable *base, UINT flags )
{
    TRACE( "drawable %s, flags %#x\n", debugstr_opengl_drawable( base ), flags );

    /* A deferred drawable takes its interval when it gets the screen. */
    if ((flags & GL_FLUSH_INTERVAL) && base->surface)
        funcs->p_eglSwapInterval( egl->display, abs( base->interval ) );
}

/* Frames presented and the time inside eglSwapBuffers, for [PROGRESS]. */
extern unsigned long long horizon_interrupt_time(void);
unsigned int wine_nx_gl_swaps;
unsigned int wine_nx_gl_swap_attempts, wine_nx_gl_swap_failures;
unsigned long long wine_nx_gl_swap_time;  /* 100 ns */

/* The floating keyboard (wine-nx-probe/source/osk.c), put into the back
 * buffer just before it is shown: its picture in a texture of each context's
 * own, blitted from a framebuffer of its own onto the window's. Everything
 * the blit and the upload depend on is put back as the program left it. */
struct nx_osk_gl
{
    EGLContext context;
    GLuint texture, framebuffer;
    int width, height;
    unsigned int generation;
    void *pixels;
};
static struct nx_osk_gl nx_osk_gl[8];
static unsigned int nx_osk_gl_next;
static PFN_glGenFramebuffers p_glGenFramebuffers;
static PFN_glBindFramebuffer p_glBindFramebuffer;
static PFN_glFramebufferTexture2D p_glFramebufferTexture2D;
static PFN_glBlitFramebuffer p_glBlitFramebuffer;
static PFN_glIsFramebuffer p_glIsFramebuffer;
static PFN_glBindBuffer p_glBindBuffer;

static void nx_osk_draw( struct opengl_drawable *base )
{
    struct wine_nx_osk_frame frame;
    struct nx_osk_gl *osk = NULL;
    EGLint width = 0, height = 0;
    EGLContext context;
    GLint read_fb, draw_fb, texture, unpack_buffer, row_length, skip_pixels, skip_rows, alignment;
    GLboolean scissor, srgb;
    unsigned int i;

    if (!wine_nx_osk_visible()) return;
    if (!funcs->p_eglQuerySurface( egl->display, base->surface, EGL_WIDTH, &width ) ||
        !funcs->p_eglQuerySurface( egl->display, base->surface, EGL_HEIGHT, &height ) ||
        !wine_nx_osk_frame( width, height, &frame ) || !(context = funcs->p_eglGetCurrentContext()))
        return;
    if (!p_glBlitFramebuffer)
    {
        p_glGenFramebuffers = (void *)funcs->p_eglGetProcAddress( "glGenFramebuffers" );
        p_glBindFramebuffer = (void *)funcs->p_eglGetProcAddress( "glBindFramebuffer" );
        p_glFramebufferTexture2D = (void *)funcs->p_eglGetProcAddress( "glFramebufferTexture2D" );
        p_glIsFramebuffer = (void *)funcs->p_eglGetProcAddress( "glIsFramebuffer" );
        p_glBindBuffer = (void *)funcs->p_eglGetProcAddress( "glBindBuffer" );
        if (!p_glGenFramebuffers || !p_glBindFramebuffer || !p_glFramebufferTexture2D || !p_glIsFramebuffer ||
            !p_glBindBuffer)
            return;
        p_glBlitFramebuffer = (void *)funcs->p_eglGetProcAddress( "glBlitFramebuffer" );
        if (!p_glBlitFramebuffer) return;
    }
    for (i = 0; i < ARRAY_SIZE(nx_osk_gl) && !osk; i++)
        if (nx_osk_gl[i].context == context) osk = &nx_osk_gl[i];
    if (!osk)
    {
        /* A context not seen before: its names are its own, so the slot's
         * old ones are only forgotten, never deleted from here. */
        osk = &nx_osk_gl[nx_osk_gl_next++ % ARRAY_SIZE(nx_osk_gl)];
        free( osk->pixels );
        memset( osk, 0, sizeof(*osk) );
        osk->context = context;
    }

    funcs->p_glGetIntegerv( GL_READ_FRAMEBUFFER_BINDING, &read_fb );
    funcs->p_glGetIntegerv( GL_DRAW_FRAMEBUFFER_BINDING, &draw_fb );
    funcs->p_glGetIntegerv( GL_TEXTURE_BINDING_2D, &texture );
    funcs->p_glGetIntegerv( GL_PIXEL_UNPACK_BUFFER_BINDING, &unpack_buffer );
    funcs->p_glGetIntegerv( GL_UNPACK_ROW_LENGTH, &row_length );
    funcs->p_glGetIntegerv( GL_UNPACK_SKIP_PIXELS, &skip_pixels );
    funcs->p_glGetIntegerv( GL_UNPACK_SKIP_ROWS, &skip_rows );
    funcs->p_glGetIntegerv( GL_UNPACK_ALIGNMENT, &alignment );
    scissor = funcs->p_glIsEnabled( GL_SCISSOR_TEST );
    srgb = funcs->p_glIsEnabled( GL_FRAMEBUFFER_SRGB );

    /* Names the program deleted, or a context that took an old one's place. */
    if (!osk->texture || !funcs->p_glIsTexture( osk->texture ) || !p_glIsFramebuffer( osk->framebuffer ))
    {
        funcs->p_glGenTextures( 1, &osk->texture );
        p_glGenFramebuffers( 1, &osk->framebuffer );
        osk->width = osk->height = 0;
    }
    funcs->p_glBindTexture( GL_TEXTURE_2D, osk->texture );
    p_glBindBuffer( GL_PIXEL_UNPACK_BUFFER, 0 );
    funcs->p_glPixelStorei( GL_UNPACK_ROW_LENGTH, 0 );
    funcs->p_glPixelStorei( GL_UNPACK_SKIP_PIXELS, 0 );
    funcs->p_glPixelStorei( GL_UNPACK_SKIP_ROWS, 0 );
    funcs->p_glPixelStorei( GL_UNPACK_ALIGNMENT, 4 );
    if (osk->width != frame.width || osk->height != frame.height)
    {
        free( osk->pixels );
        osk->pixels = malloc( (size_t)frame.width * frame.height * 4 );
        funcs->p_glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, frame.width, frame.height, 0, GL_BGRA,
                               GL_UNSIGNED_BYTE, NULL );
        funcs->p_glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
        funcs->p_glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
        osk->width = frame.width;
        osk->height = frame.height;
        osk->generation = 0;
    }
    if (osk->pixels && osk->generation != frame.generation &&
        (osk->generation = wine_nx_osk_copy( width, height, osk->pixels, frame.width * 4, 0 )))
        funcs->p_glTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, frame.width, frame.height, GL_BGRA, GL_UNSIGNED_BYTE,
                                  osk->pixels );
    if (osk->generation)
    {
        p_glBindFramebuffer( GL_READ_FRAMEBUFFER, osk->framebuffer );
        p_glFramebufferTexture2D( GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, osk->texture, 0 );
        p_glBindFramebuffer( GL_DRAW_FRAMEBUFFER, 0 );
        if (scissor) funcs->p_glDisable( GL_SCISSOR_TEST );
        if (srgb) funcs->p_glDisable( GL_FRAMEBUFFER_SRGB );
        /* The picture's first row is its top; the window's first is its bottom. */
        p_glBlitFramebuffer( 0, 0, frame.width, frame.height, frame.x, height - frame.y,
                             frame.x + frame.width, height - frame.y - frame.height, GL_COLOR_BUFFER_BIT, GL_NEAREST );
        if (scissor) funcs->p_glEnable( GL_SCISSOR_TEST );
        if (srgb) funcs->p_glEnable( GL_FRAMEBUFFER_SRGB );
    }

    p_glBindFramebuffer( GL_READ_FRAMEBUFFER, read_fb );
    p_glBindFramebuffer( GL_DRAW_FRAMEBUFFER, draw_fb );
    funcs->p_glBindTexture( GL_TEXTURE_2D, texture );
    p_glBindBuffer( GL_PIXEL_UNPACK_BUFFER, unpack_buffer );
    funcs->p_glPixelStorei( GL_UNPACK_ROW_LENGTH, row_length );
    funcs->p_glPixelStorei( GL_UNPACK_SKIP_PIXELS, skip_pixels );
    funcs->p_glPixelStorei( GL_UNPACK_SKIP_ROWS, skip_rows );
    funcs->p_glPixelStorei( GL_UNPACK_ALIGNMENT, alignment );
}

static BOOL nx_drawable_take_screen( struct nx_gl_drawable *gl );

static BOOL nx_drawable_swap( struct opengl_drawable *base )
{
    struct nx_gl_drawable *gl = impl_from_opengl_drawable( base );
    unsigned long long start;
    unsigned int attempt;
    BOOL ret;

    attempt = __atomic_add_fetch( &wine_nx_gl_swap_attempts, 1, __ATOMIC_RELAXED );
    if (attempt <= 4)
        nx_log( "[NXGL] swap begin attempt=%u hwnd=%p format=%d doublebuffer=%u",
                attempt, base->client ? base->client->hwnd : NULL, base->format, base->doublebuffer );
    if (gl->deferred)
    {
        EGLContext context = funcs->p_eglGetCurrentContext();

        /* The window presents for the first time: only now does it get the
         * screen. Its context was current without a surface until here, so
         * this frame's drawing is lost; the next ones reach the screen. */
        if (!nx_drawable_take_screen( gl ))
        {
            unsigned int failures = __atomic_add_fetch( &wine_nx_gl_swap_failures, 1, __ATOMIC_RELAXED );
            if (!(failures & (failures - 1)))
                nx_log( "[NXGL] swap failed attempt=%u failures=%u: no screen surface", attempt, failures );
            return FALSE;
        }
        if (context && funcs->p_eglGetCurrentSurface( EGL_DRAW ) == EGL_NO_SURFACE)
            funcs->p_eglMakeCurrent( egl->display, base->surface, base->surface, context );
        if (base->interval != INT_MIN) funcs->p_eglSwapInterval( egl->display, abs( base->interval ) );
    }
    nx_osk_draw( base );
    start = horizon_interrupt_time();
    ret = funcs->p_eglSwapBuffers( egl->display, base->surface );
    __atomic_add_fetch( &wine_nx_gl_swap_time, horizon_interrupt_time() - start, __ATOMIC_RELAXED );
    if (ret) __atomic_add_fetch( &wine_nx_gl_swaps, 1, __ATOMIC_RELAXED );
    else
    {
        unsigned int failures = __atomic_add_fetch( &wine_nx_gl_swap_failures, 1, __ATOMIC_RELAXED );
        EGLint error = funcs->p_eglGetError();
        if (!(failures & (failures - 1)))
            nx_log( "[NXGL] swap failed attempt=%u failures=%u error=%#x", attempt, failures, error );
    }
    if (attempt <= 4) nx_log( "[NXGL] swap return attempt=%u success=%u", attempt, ret );
    return ret;
}

static const struct opengl_drawable_funcs nx_drawable_funcs =
{
    .destroy = nx_drawable_destroy,
    .flush = nx_drawable_flush,
    .swap = nx_drawable_swap,
};

/* WineD3D's caps/backup windows and ddraw's hidden device window are never
 * display targets. Keep their FBOs offscreen so GDI can still be composed. */
static BOOL nx_auxiliary_window( HWND hwnd, LONG style, char *class_name, size_t size )
{
    static const WCHAR wined3d[] = {'W','i','n','e','D','3','D','_','O','p','e','n','G','L'};
    static const WCHAR ddraw[] = {'D','i','r','e','c','t','D','r','a','w','D','e','v','i','c','e','W','n','d'};
    WCHAR buffer[64];
    UNICODE_STRING name = {0, sizeof(buffer), buffer};
    int count = NtUserGetClassName( hwnd, FALSE, &name );
    size_t i;

    if (count < 0) count = 0;
    for (i = 0; i < (size_t)count && i + 1 < size; i++)
        class_name[i] = buffer[i] < 128 ? buffer[i] : '?';
    class_name[i] = 0;
    if (style & WS_VISIBLE) return FALSE;
    return ((size_t)count * sizeof(WCHAR) == sizeof(wined3d) && !memcmp( buffer, wined3d, sizeof(wined3d) )) ||
           ((style & WS_DISABLED) && (size_t)count * sizeof(WCHAR) == sizeof(ddraw) &&
            !memcmp( buffer, ddraw, sizeof(ddraw) ));
}

/* An auxiliary window's drawable: a pbuffer of its client size, never shown.
 * win32u makes the drawable itself current (raw 0 has no framebuffer wrapper),
 * so it needs an EGL surface of its own; without one the context is made
 * current surfaceless, which switch-mesa's EGL_KHR_surfaceless_context allows. */
static void nx_offscreen_destroy( struct opengl_drawable *base )
{
    /* win32u destroys the pbuffer after this callback. */
}

static void nx_offscreen_flush( struct opengl_drawable *base, UINT flags )
{
}

static BOOL nx_offscreen_swap( struct opengl_drawable *base )
{
    return TRUE;
}

static const struct opengl_drawable_funcs nx_offscreen_funcs =
{
    .destroy = nx_offscreen_destroy,
    .flush = nx_offscreen_flush,
    .swap = nx_offscreen_swap,
};

static BOOL nx_offscreen_surface_create( HWND hwnd, int format, struct opengl_drawable **drawable )
{
    struct client_surface *client;
    struct opengl_drawable *gl;
    EGLint attribs[5];
    RECT rect;
    int width = 1, height = 1;

    if (get_client_rect( hwnd, &rect, 0 ))
    {
        width = min( max( rect.right - rect.left, 1 ), 4096 );
        height = min( max( rect.bottom - rect.top, 1 ), 4096 );
    }
    attribs[0] = EGL_WIDTH;
    attribs[1] = width;
    attribs[2] = EGL_HEIGHT;
    attribs[3] = height;
    attribs[4] = EGL_NONE;

    if (!(client = nulldrv_client_surface_create( hwnd ))) return FALSE;
    gl = opengl_drawable_create( sizeof(*gl), &nx_offscreen_funcs, format, client );
    client_surface_release( client );
    if (!gl) return FALSE;
    gl->buffer_map[0] = GL_BACK_LEFT;
    gl->buffer_map[1] = GL_BACK_RIGHT;
    gl->buffer_map[GL_FRONT - GL_FRONT_LEFT] = GL_BACK;
    gl->buffer_map[GL_FRONT_AND_BACK - GL_FRONT_LEFT] = GL_BACK;

    gl->surface = funcs->p_eglCreatePbufferSurface( egl->display, nx_config_for_format( format ), attribs );
    nx_log( "[NXGL] offscreen hwnd=%p %dx%d %s", hwnd, width, height,
            gl->surface ? "pbuffer" : "surfaceless (pbuffer failed)" );
    *drawable = gl;
    return TRUE;
}

/* The screen for a window drawable: the surface another drawable of the window
 * already put on it, or a new one. The Switch has one NWindow, and window
 * surfaces and the compositor cannot share it, so the compositor gives it up
 * for as long as the surface exists. */
static BOOL nx_drawable_take_screen( struct nx_gl_drawable *gl )
{
    HWND hwnd = gl->base.client ? gl->base.client->hwnd : NULL;
    int format = gl->base.format;
    void *window;

    pthread_mutex_lock( &nx_screen_mutex );
    if (nx_screen_surface && nx_screen_format == format)
    {
        /* Another drawable of this window still has the screen; share it. */
        gl->base.surface = nx_screen_surface;
        gl->screen = TRUE;
        gl->deferred = FALSE;
        nx_screen_refs++;
        pthread_mutex_unlock( &nx_screen_mutex );
        TRACE( "hwnd %p: sharing the screen surface %p\n", hwnd, nx_screen_surface );
        return TRUE;
    }
    if (nx_screen_surface)
    {
        pthread_mutex_unlock( &nx_screen_mutex );
        ERR( "hwnd %p: the screen has a format %d surface, cannot serve format %d\n",
             hwnd, nx_screen_format, format );
        return FALSE;
    }
    pthread_mutex_unlock( &nx_screen_mutex );

    if (!(window = wine_nx_gl_acquire_window()))
    {
        ERR( "hwnd %p: the screen already has an OpenGL surface\n", hwnd );
        return FALSE;
    }
    if (!(gl->base.surface = funcs->p_eglCreateWindowSurface( egl->display, nx_config_for_format( format ),
                                                              (EGLNativeWindowType)window, NULL )))
    {
        ERR( "hwnd %p: eglCreateWindowSurface failed, error %#x\n", hwnd, funcs->p_eglGetError() );
        /* nothing is sharing the screen yet, so give it back here */
        wine_nx_gl_release_window();
        return FALSE;
    }
    gl->screen = TRUE;
    gl->deferred = FALSE;

    pthread_mutex_lock( &nx_screen_mutex );
    nx_screen_surface = gl->base.surface;
    nx_screen_format = format;
    nx_screen_refs = 1;
    pthread_mutex_unlock( &nx_screen_mutex );

    TRACE( "created drawable %s with EGL surface %p\n", debugstr_opengl_drawable( &gl->base ), gl->base.surface );
    {
        static BOOL logged;
        const char *vendor = funcs->p_eglQueryString( egl->display, EGL_VENDOR );
        const char *version = funcs->p_eglQueryString( egl->display, EGL_VERSION );

        if (!logged) nx_log( "[NXGL] EGL %s %s, %u configs; window surface for format %d, hwnd=%p doublebuffer=%u",
                             vendor ? vendor : "?", version ? version : "?", egl->config_count, format,
                             hwnd, gl->base.doublebuffer );
        logged = TRUE;
    }
    return TRUE;
}

/* A window drawable shares a screen surface the window already has, and
 * otherwise waits for its first swap to take the screen: Direct3D creates
 * contexts on windows it never presents to (RGSS keeps drawing them with GDI
 * through ddraw), and those must leave the compositor showing them. */
static BOOL nx_surface_create( HWND hwnd, BOOL raw, int format, struct opengl_drawable **drawable )
{
    struct opengl_drawable *previous;
    struct client_surface *client;
    struct nx_gl_drawable *gl;
    char class_name[64];
    LONG style = NtUserGetWindowLongW( hwnd, GWL_STYLE );
    BOOL offscreen = nx_auxiliary_window( hwnd, style, class_name, sizeof(class_name) );

    TRACE( "hwnd %p, raw %u, format %d\n", hwnd, raw, format );
    nx_log( "[NXGL] surface hwnd=%p class=%s visible=%u style=%#lx format=%d raw=%u route=%s",
            hwnd, class_name, !!(style & WS_VISIBLE), (unsigned long)style, format, raw,
            offscreen ? "offscreen" : "screen on first swap" );

    if ((previous = *drawable) && previous->format == format) return TRUE;
    /* A previous surface may hold the screen: let it go first. */
    if (previous)
    {
        opengl_drawable_release( previous );
        *drawable = NULL;
    }

    if (offscreen) return nx_offscreen_surface_create( hwnd, format, drawable );

    if (!(client = nulldrv_client_surface_create( hwnd ))) return FALSE;
    gl = opengl_drawable_create( sizeof(*gl), &nx_drawable_funcs, format, client );
    client_surface_release( client );
    if (!gl) return FALSE;
    gl->base.buffer_map[0] = GL_BACK_LEFT;
    gl->base.buffer_map[1] = GL_BACK_RIGHT;
    gl->base.buffer_map[GL_FRONT - GL_FRONT_LEFT] = GL_BACK;
    gl->base.buffer_map[GL_FRONT_AND_BACK - GL_FRONT_LEFT] = GL_BACK;

    pthread_mutex_lock( &nx_screen_mutex );
    gl->deferred = !(nx_screen_surface && nx_screen_format == format);
    pthread_mutex_unlock( &nx_screen_mutex );
    if (!gl->deferred && !nx_drawable_take_screen( gl )) gl->deferred = TRUE;
    *drawable = &gl->base;
    return TRUE;
}

/* win32u's egldrv creates contexts without a config, but switch-mesa 20.1's EGL
 * driver lacks EGL_KHR_no_config_context (such a context fails with
 * EGL_BAD_CONFIG) and EGL_KHR_create_context_no_error. A context gets the config
 * of its pixel format, the one this format's window surfaces and pbuffers use,
 * so EGL lets them be made current together. */
static BOOL nx_context_create( int format, void *share, const int *attribs, void **context )
{
    EGLint egl_attribs[16], *end = egl_attribs, error;

    TRACE( "format %d, share %p, attribs %p\n", format, share, attribs );

    for (; attribs && attribs[0]; attribs += 2)
    {
        EGLint name, *dst;

        switch (attribs[0])
        {
        case WGL_CONTEXT_MAJOR_VERSION_ARB:
            name = EGL_CONTEXT_MAJOR_VERSION_KHR;
            break;
        case WGL_CONTEXT_MINOR_VERSION_ARB:
            name = EGL_CONTEXT_MINOR_VERSION_KHR;
            break;
        case WGL_CONTEXT_FLAGS_ARB:
            name = EGL_CONTEXT_FLAGS_KHR;
            break;
        case WGL_CONTEXT_PROFILE_MASK_ARB:
            if (attribs[1] & WGL_CONTEXT_ES2_PROFILE_BIT_EXT)
            {
                ERR( "OpenGL ES contexts are not supported\n" );
                return FALSE;
            }
            name = EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR;
            break;
        case WGL_CONTEXT_OPENGL_NO_ERROR_ARB:
            FIXME( "no-error contexts are unavailable, ignoring %#x\n", attribs[1] );
            continue;
        default:
            FIXME( "unhandled attribute %#x %#x\n", attribs[0], attribs[1] );
            continue;
        }

        /* A repeated attribute replaces the earlier one. */
        for (dst = egl_attribs; dst != end && *dst != name; dst += 2) continue;
        if (dst == end)
        {
            if (end - egl_attribs >= (int)ARRAY_SIZE(egl_attribs) - 2) continue;
            end += 2;
        }
        dst[0] = name;
        dst[1] = attribs[1];
    }
    *end = EGL_NONE;

    funcs->p_eglBindAPI( EGL_OPENGL_API );
    *context = funcs->p_eglCreateContext( egl->display, nx_config_for_format( format ), share,
                                          end != egl_attribs ? egl_attribs : NULL );
    if ((error = funcs->p_eglGetError()) != EGL_SUCCESS || !*context)
    {
        ERR( "context creation failed for format %d, share %p, error %#x\n", format, share, error );
        return FALSE;
    }
    TRACE( "created context %p\n", *context );
    return TRUE;
}

/* Mesa's Switch platform is the default display; its windows are NWindows. */
static void nx_init_egl_platform( struct egl_platform *platform )
{
    platform->type = 0;
    platform->native_display = 0;
    egl = platform;
}

static struct opengl_driver_funcs nx_driver_funcs =
{
    .p_init_egl_platform = nx_init_egl_platform,
    .p_surface_create = nx_surface_create,
    .p_context_create = nx_context_create,
};

UINT wine_nx_drv_OpenGLInit( UINT version, const struct opengl_funcs *opengl_funcs,
                             const struct opengl_driver_funcs **driver_funcs )
{
    if (version != WINE_OPENGL_DRIVER_VERSION)
    {
        ERR( "version mismatch, opengl32 wants %u but the driver has %u\n", version, WINE_OPENGL_DRIVER_VERSION );
        return STATUS_INVALID_PARAMETER;
    }
    if (!opengl_funcs->egl_handle) return STATUS_NOT_SUPPORTED;
    funcs = opengl_funcs;

    nx_driver_funcs.p_get_proc_address = (*driver_funcs)->p_get_proc_address;
    nx_driver_funcs.p_init_pixel_formats = (*driver_funcs)->p_init_pixel_formats;
    nx_driver_funcs.p_describe_pixel_format = (*driver_funcs)->p_describe_pixel_format;
    nx_driver_funcs.p_init_wgl_extensions = (*driver_funcs)->p_init_wgl_extensions;
    nx_driver_funcs.p_context_destroy = (*driver_funcs)->p_context_destroy;
    nx_driver_funcs.p_make_current = (*driver_funcs)->p_make_current;
    nx_driver_funcs.p_pbuffer_create = (*driver_funcs)->p_pbuffer_create;
    nx_driver_funcs.p_pbuffer_updated = (*driver_funcs)->p_pbuffer_updated;
    nx_driver_funcs.p_pbuffer_bind = (*driver_funcs)->p_pbuffer_bind;

    *driver_funcs = &nx_driver_funcs;
    return STATUS_SUCCESS;
}

#endif /* __SWITCH__ */
