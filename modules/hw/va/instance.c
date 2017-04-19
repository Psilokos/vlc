/*****************************************************************************
 * instance.c: VAAPI instance management for VLC
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

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <assert.h>
#include <pthread.h>
#include <stdlib.h>
#include <vlc_common.h>
#include "vlc_va.h"

static struct
{
    VADisplay           dpy;
    unsigned int        refcount;
} *va_instance = NULL;

static pthread_mutex_t  va_instance_lock = PTHREAD_MUTEX_INITIALIZER;

#pragma GCC visibility push(default)

/* Allocates the VA instance and sets the reference counter to 1. */
int
vlc_va_CreateInstance(VADisplay dpy)
{
    assert(!va_instance);
    if (!(va_instance = malloc(sizeof(*va_instance))))
        return VLC_ENOMEM;
    va_instance->refcount = 1;
    va_instance->dpy = dpy;
    return VLC_SUCCESS;
}

/* Retrieve the VA instance and increases the reference counter by 1. */
void
vlc_va_GetInstance(VADisplay *dpy)
{
    pthread_mutex_lock(&va_instance_lock);
    if (!va_instance)
        *dpy = NULL;
    else
    {
        *dpy = va_instance->dpy;
        ++va_instance->refcount;
    }
    pthread_mutex_unlock(&va_instance_lock);
}

/* Decreases the reference counter by 1 and frees the instance if that counter
   reaches 0. */
int
vlc_va_ReleaseInstance(void)
{
    int ret;

    pthread_mutex_lock(&va_instance_lock);
    if (!va_instance)
    {
        ret = VLC_EGENERIC;
        goto ret;
    }
    if (--va_instance->refcount)
    {
        ret = VLC_SUCCESS;
        goto ret;
    }
    if ((ret = vaTerminate(va_instance->dpy)) != VA_STATUS_SUCCESS)
        goto ret;
    free(va_instance);
    va_instance = NULL;
    ret = VLC_SUCCESS;
ret:
    pthread_mutex_unlock(&va_instance_lock);
    return ret;
}

#pragma GCC visibility pop
