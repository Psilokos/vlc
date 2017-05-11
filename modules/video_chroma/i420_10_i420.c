/*****************************************************************************
 * i420_10_i420.c : Planar YUV 4:2:0 10bits to Planar YUV 4:2:0 8bits
 *****************************************************************************
 * Copyright (C) 2017 Videolabs
 *
 * Author: Victorien Le Couviour--Tuffet <victorien.lecouviour.tuffet@gmail.com>
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

/*****************************************************************************
 * Preamble
 *****************************************************************************/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <vlc_common.h>
#include <vlc_plugin.h>
#include <vlc_filter.h>
#include <vlc_picture.h>
#include "copy.h"

/*****************************************************************************
 * Filter sys struct.
 *****************************************************************************/

struct  filter_sys_t
{
    copy_cache_t        cache;
};

/*****************************************************************************
 * Local and extern prototypes.
 *****************************************************************************/

static int              Create(vlc_object_t *obj);
static void             Delete(vlc_object_t *obj);
static picture_t *      I420_10_I420_Filter(filter_t *, picture_t *);
static void             I420_10_I420(filter_t *filter,
                                     picture_t *src, picture_t *dest);

/*****************************************************************************
 * Module descriptor
 *****************************************************************************/
vlc_module_begin ()
    set_description( N_("YUV 420 10-bits planar to YUV 420 8-bits planar conversions") )
    set_capability( "video converter", 1 )
    set_category( CAT_VIDEO )
    set_subcategory( SUBCAT_VIDEO_VFILTER )
    set_callbacks( Create, Delete )
vlc_module_end ()

/*****************************************************************************
 * Create: allocate a chroma function
 *****************************************************************************
 * This function allocates and initializes a chroma function
 *****************************************************************************/

static int
Create(vlc_object_t *obj)
{
    filter_t            *const filter = (filter_t *)obj;
    video_format_t      *const fmt_in = &filter->fmt_in.video;
    video_format_t      *const fmt_out = &filter->fmt_out.video;

    if (fmt_out->i_chroma != VLC_CODEC_I420 ||
        fmt_in->i_width & 1 || fmt_in->i_height & 1 ||
        fmt_in->i_x_offset + fmt_in->i_visible_width !=
        fmt_out->i_x_offset + fmt_in->i_visible_width ||
        fmt_in->i_y_offset + fmt_in->i_visible_height !=
        fmt_out->i_y_offset + fmt_in->i_visible_height ||
        fmt_in->orientation != fmt_out->orientation)
        return VLC_EGENERIC;

    filter->p_sys = calloc(1, sizeof(filter_sys_t));
    if (!filter->p_sys)
    {
        msg_Err(obj, "unable to allocate memory");
        return VLC_ENOMEM;
    }

    filter->pf_video_filter = I420_10_I420_Filter;
    if (CopyInitCache(&filter->p_sys->cache,
                      fmt_in->i_x_offset + fmt_in->i_width))
        return VLC_EGENERIC;

    return VLC_SUCCESS;
}

/*****************************************************************************
 * Delete: release a chroma function
 *****************************************************************************
 * This function releases a chroma function
 *****************************************************************************/

static void
Delete(vlc_object_t *obj)
{
    filter_t    *const filter = (filter_t *)obj;

    CopyCleanCache(&filter->p_sys->cache);
    free(filter->p_sys);
}

/*****************************************************************************
 * planar YUV I420 10-bits to planar YUV I420 8-bits
 *****************************************************************************/

VIDEO_FILTER_WRAPPER(I420_10_I420)

static void
I420_10_I420(filter_t *filter, picture_t *src, picture_t *dest)
{
    size_t      pitches[3] =
    {
        src->p[Y_PLANE].i_pitch,
        src->p[U_PLANE].i_pitch,
        src->p[V_PLANE].i_pitch
    };
    uint16_t     *planes[3] =
    {
        (uint16_t *)src->p[Y_PLANE].p_pixels,
        (uint16_t *)src->p[U_PLANE].p_pixels,
        (uint16_t *)src->p[V_PLANE].p_pixels
    };

    dest->format.i_x_offset = src->format.i_x_offset;
    dest->format.i_y_offset = src->format.i_y_offset;

    CopyFromI420_10ToI420(dest, planes, pitches,
                          src->format.i_y_offset +
                          src->format.i_visible_height,
                          &filter->p_sys->cache,
                          src->format.i_chroma == VLC_CODEC_I420_10B);
}
