/*****************************************************************************
 * converter_va.c: OpenGL VA opaque converter
 *****************************************************************************
 * Copyright (C) 2017 VLC authors and VideoLAN
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "internal.h"
#include "../../hw/vaapi/vlc_vaapi.h"
#include <vlc_vout_window.h>

#include <assert.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <va/va_drmcommon.h>

#ifdef HAVE_VA_WL
# include <va/va_wayland.h>
#endif

#ifdef HAVE_VA_X11
# include <va/va_x11.h>
/* TODO ugly way to get the X11 Display via EGL. */
struct vlc_gl_sys_t
{
    EGLDisplay display;
    EGLSurface surface;
    EGLContext context;
    Display *x11;
};
#endif

struct priv
{
    VADisplay vadpy;
    VASurfaceID *va_surface_ids;
    unsigned     va_surface_count;
    PFNEGLCREATEIMAGEKHRPROC            eglCreateImageKHR;
    PFNEGLDESTROYIMAGEKHRPROC           eglDestroyImageKHR;
    PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES;
    EGLDisplay egldpy;

    video_color_space_t yuv_space;
    unsigned fourcc;
    EGLint drm_fourccs[3];

    struct {
        picture_t *  pic;
        VAImage      va_image;
        VABufferInfo va_buffer_info;
        void *       egl_images[3];
    } last;
};

static void
vaegl_release_last_pic(struct priv *priv)
{
    for (unsigned i = 0; i < priv->last.va_image.num_planes; ++i)
        priv->eglDestroyImageKHR(priv->egldpy, priv->last.egl_images[i]);

    vaReleaseBufferHandle(priv->vadpy, priv->last.va_image.buf);

    vaDestroyImage(priv->vadpy, priv->last.va_image.image_id);

    picture_Release(priv->last.pic);
}

static int
vaegl_init_fourcc(const opengl_tex_converter_t *tc, struct priv *priv,
                  unsigned va_fourcc)
{
    (void) tc;
    switch (va_fourcc)
    {
        case VA_FOURCC_NV12:
            priv->drm_fourccs[0] = VLC_FOURCC('R', '8', ' ', ' ');
            priv->drm_fourccs[1] = VLC_FOURCC('G', 'R', '8', '8');
            break;
        case VA_FOURCC_RGBA:
            priv->drm_fourccs[0] = VLC_FOURCC('G', 'R', '3', '2');
            break;
        case VA_FOURCC_BGRA:
            priv->drm_fourccs[0] = VLC_FOURCC('G', 'R', '3', '2');
            break;
        case VA_FOURCC_YV12:
            priv->drm_fourccs[0] = VLC_FOURCC('R', '8', ' ', ' ');
            priv->drm_fourccs[1] = VLC_FOURCC('R', '8', ' ', ' ');
            priv->drm_fourccs[2] = VLC_FOURCC('R', '8', ' ', ' ');
            break;
        case VA_FOURCC_422H:
            priv->drm_fourccs[0] = VLC_FOURCC('R', '8', ' ', ' ');
            priv->drm_fourccs[1] = VLC_FOURCC('R', '8', ' ', ' ');
            priv->drm_fourccs[2] = VLC_FOURCC('R', '8', ' ', ' ');
            break;
        case VA_FOURCC_UYVY:
            priv->drm_fourccs[0] = VLC_FOURCC('R', '1', '6', ' ');
            break;
        case VA_FOURCC_444P:
            priv->drm_fourccs[0] = VLC_FOURCC('R', '1', '6', ' ');
            priv->drm_fourccs[1] = VLC_FOURCC('R', '1', '6', ' ');
            priv->drm_fourccs[2] = VLC_FOURCC('R', '1', '6', ' ');
            break;
        default: return VLC_EGENERIC;
    }
    return VLC_SUCCESS;
}

static int
tc_vaegl_update(const opengl_tex_converter_t *tc, GLuint *textures,
                const GLsizei *tex_width, const GLsizei *tex_height,
                picture_t *pic, const size_t *plane_offset)
{
    (void) plane_offset;
    struct priv *priv = tc->priv;
    VAStatus status;
    VAImage va_image;
    VABufferInfo va_buffer_info;
    EGLImageKHR egl_images[3] = { };
    bool release_image = false, release_buffer_info = false;

    if (pic == priv->last.pic)
    {
        va_image = priv->last.va_image;
        va_buffer_info = priv->last.va_buffer_info;
        for (unsigned i = 0; i < priv->last.va_image.num_planes; ++i)
            egl_images[i] = priv->last.egl_images[i];
    }
    else
    {
        status = vaDeriveImage(priv->vadpy, vlc_vaapi_PicGetSurface(pic),
                               &va_image);

assert(status == VA_STATUS_SUCCESS); /*XXX */
        if (status != VA_STATUS_SUCCESS)
        {
            /* TODO: if derive fail, do extra memcpy via vaCreateImage/vaPutImage */
            goto error;
        }
        release_image = true;

        if (va_image.format.fourcc != priv->fourcc)
        {
            int ret = vaegl_init_fourcc(tc, priv, va_image.format.fourcc);
            if (ret != VLC_SUCCESS)
                goto error;
            priv->fourcc = va_image.format.fourcc;
        }

        va_buffer_info = (VABufferInfo) {
            .mem_type = VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME
        };
        status = vaAcquireBufferHandle(priv->vadpy, va_image.buf,
                                       &va_buffer_info);
assert(status == VA_STATUS_SUCCESS); /* XXX */
        if (status != VA_STATUS_SUCCESS)
            goto error;
        release_buffer_info = true;
    }

    for (unsigned i = 0; i < va_image.num_planes; ++i)
    {
        EGLint attribs[] = {
            EGL_WIDTH, tex_width[i],
            EGL_HEIGHT, tex_height[i],
            EGL_LINUX_DRM_FOURCC_EXT, priv->drm_fourccs[i],
            EGL_DMA_BUF_PLANE0_FD_EXT, va_buffer_info.handle,
            EGL_DMA_BUF_PLANE0_OFFSET_EXT, va_image.offsets[i],
            EGL_DMA_BUF_PLANE0_PITCH_EXT, va_image.pitches[i],
            EGL_NONE
        };

        egl_images[i] = priv->eglCreateImageKHR(priv->egldpy, EGL_NO_CONTEXT,
                                                EGL_LINUX_DMA_BUF_EXT, NULL,
                                                attribs);
        if (egl_images[i] == NULL)
            goto error;

        glBindTexture(tc->tex_target, textures[i]);

        priv->glEGLImageTargetTexture2DOES(tc->tex_target, egl_images[i]);
    }
    fprintf(stderr, "update: %lX\n", va_buffer_info.handle);

    if (pic != priv->last.pic)
    {
        if (priv->last.pic != NULL)
            vaegl_release_last_pic(priv);
        priv->last.pic = picture_Hold(pic);
        priv->last.va_image = va_image;
        priv->last.va_buffer_info = va_buffer_info;
        for (unsigned i = 0; i < va_image.num_planes; ++i)
            priv->last.egl_images[i] = egl_images[i];
    }

    return VLC_SUCCESS;

error:
    if (release_image)
    {
        if (release_buffer_info)
            vaReleaseBufferHandle(priv->vadpy, va_image.buf);

        for (unsigned i = 0; i < 3 && egl_images[i] != NULL; ++i)
            priv->eglDestroyImageKHR(priv->egldpy, egl_images[i]);

        vaDestroyImage(priv->vadpy, va_image.image_id);
    }
    return VLC_EGENERIC;
}

static void
tc_vaegl_release(const opengl_tex_converter_t *tc)
{
    struct priv *priv = tc->priv;

    if (priv->last.pic != NULL)
        vaegl_release_last_pic(priv);

    vlc_vaapi_ReleaseInstance(priv->vadpy);

    free(tc->priv);
}

static GLuint
tc_vaegl_init(video_format_t *fmt, opengl_tex_converter_t *tc,
              struct priv *priv, VADisplay *vadpy)
{
#define GETPROC(x) do { \
    if ((priv->x = vlc_gl_GetProcAddress(tc->gl, #x)) == NULL) return -1; \
} while(0)

    if (vadpy == NULL)
        return 0;
    priv->vadpy = vadpy;
    priv->fourcc = 0;
    priv->yuv_space = fmt->space;

    if (!HasExtension(tc->glexts, "GL_OES_EGL_image"))
        return 0;

    void *(*func)() = vlc_gl_GetProcAddress(tc->gl, "eglGetCurrentDisplay");
    priv->egldpy = func ? func() : NULL;
    if (priv->egldpy == NULL)
        return 0;

    func = vlc_gl_GetProcAddress(tc->gl, "eglQueryString");
    const char *eglexts = func ? func(priv->egldpy, EGL_EXTENSIONS) : "";
    if (!HasExtension(eglexts, "EGL_EXT_image_dma_buf_import"))
        return 0;

    GETPROC(eglCreateImageKHR);
    GETPROC(eglDestroyImageKHR);
    GETPROC(glEGLImageTargetTexture2DOES);

    tc->pf_update  = tc_vaegl_update;
    tc->pf_release = tc_vaegl_release;

    int status = vlc_vaapi_Initialize(VLC_OBJECT(tc->gl), priv->vadpy);
    if (status != VA_STATUS_SUCCESS)
        return 0;

    if (vlc_vaapi_SetInstance(priv->vadpy))
    {
        msg_Err(tc->gl, "VAAPI instance already in use");
        return 0;
    }

    return opengl_fragment_shader_init(tc, GL_TEXTURE_2D, VLC_CODEC_NV12,
                                       fmt->space);
#undef GETPROC
}

static picture_pool_t *
tc_va_get_pool(const opengl_tex_converter_t *tc, const video_format_t *fmt,
               unsigned requested_count)
{
    struct priv *priv = tc->priv;

    priv->va_surface_ids =
        vlc_vaapi_CreateSurfaces(VLC_OBJECT(tc->gl), priv->vadpy,
                                 requested_count, fmt, VA_RT_FORMAT_YUV420);
    if (priv->va_surface_ids == NULL)
        return NULL;

    picture_pool_t *pool =
        vlc_vaapi_PoolNew(VLC_OBJECT(tc->gl), priv->va_surface_ids,
                          requested_count, fmt);
    if (!pool)
    {
        vaDestroySurfaces(priv->vadpy, priv->va_surface_ids, requested_count);
        return NULL;
    }
    priv->va_surface_count = requested_count;
    vlc_vaapi_SetRenderTargets(priv->vadpy, priv->va_surface_ids,
                               priv->va_surface_count);
    return pool;
}

GLuint
opengl_tex_converter_va_init(video_format_t *fmt, opengl_tex_converter_t *tc)
{
    fprintf(stderr, "opengl_tex_converter_va_init ? %4.4s\n", (const char *)&fmt->i_chroma);

    if (fmt->i_chroma != VLC_CODEC_VAAPI_420)
        return 0;

    struct priv *priv = tc->priv = calloc(1, sizeof(struct priv));
    if (unlikely(priv == NULL))
        return VLC_ENOMEM;

    GLuint fshader = 0;
    switch (tc->gl->surface->type)
    {
#ifdef HAVE_VA_X11
        case VOUT_WINDOW_TYPE_XID:
        {
            struct vlc_gl_sys_t *glsys = tc->gl->sys;
            fshader = tc_vaegl_init(fmt, tc, priv, vaGetDisplay(glsys->x11));
            break;
        }
#endif
#ifdef HAVE_VA_WL
        case VOUT_WINDOW_TYPE_WAYLAND:
            fshader = tc_vaegl_init(fmt, tc, priv,
                                    vaGetDisplayWl(tc->gl->surface->display.wl));
            break;
#endif
        default:
            goto error;
    }
    if (fshader == 0)
        goto error;
fprintf(stderr, "vadpy: %p\n", priv->vadpy); /* XXX */

    tc->priv              = priv;
    tc->pf_get_pool       = tc_va_get_pool;

fprintf(stderr, "opengl_tex_converter_va_init !\n"); /* XXX */
    return fshader;

error:
    free(priv);
    return 0;
}
