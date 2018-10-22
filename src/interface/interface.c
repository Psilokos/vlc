/*****************************************************************************
 * interface.c: interface access for other threads
 * This library provides basic functions for threads to interact with user
 * interface, such as command line.
 *****************************************************************************
 * Copyright (C) 1998-2007 VLC authors and VideoLAN
 * $Id$
 *
 * Authors: Vincent Seguin <seguin@via.ecp.fr>
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

/**
 *   \file
 *   This file contains functions related to interface management
 */


/*****************************************************************************
 * Preamble
 *****************************************************************************/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <unistd.h>

#include <vlc_common.h>
#include <vlc_modules.h>
#include <vlc_interface.h>
#include <vlc_playlist_new.h>
#include "libvlc.h"
#include "../lib/libvlc_internal.h"

static int AddIntfCallback( vlc_object_t *, char const *,
                            vlc_value_t , vlc_value_t , void * );

/**
 * Create and start an interface.
 *
 * @param libvlc libvlc and parent object for the interface
 * @param chain configuration chain string
 * @return VLC_SUCCESS or an error code
 */
int intf_Create(libvlc_int_t *libvlc, char const *chain)
{
    /* Allocate structure */
    intf_thread_t *p_intf = vlc_custom_create( libvlc, sizeof( *p_intf ),
                                               "interface" );
    if( unlikely(p_intf == NULL) )
        return VLC_ENOMEM;

    /* Variable used for interface spawning */
    vlc_value_t val;
    var_Create( p_intf, "intf-add", VLC_VAR_STRING | VLC_VAR_ISCOMMAND );
    var_Change( p_intf, "intf-add", VLC_VAR_SETTEXT, _("Add Interface") );
#if !defined(_WIN32) && defined(HAVE_ISATTY)
    if( isatty( 0 ) )
#endif
    {
        val.psz_string = (char *)"rc,none";
        var_Change( p_intf, "intf-add", VLC_VAR_ADDCHOICE, val, _("Console") );
    }
    val.psz_string = (char *)"telnet,none";
    var_Change( p_intf, "intf-add", VLC_VAR_ADDCHOICE, val, _("Telnet") );
    val.psz_string = (char *)"http,none";
    var_Change( p_intf, "intf-add", VLC_VAR_ADDCHOICE, val, _("Web") );
    val.psz_string = (char *)"gestures,none";
    var_Change( p_intf, "intf-add", VLC_VAR_ADDCHOICE, val,
                _("Mouse Gestures") );

    var_AddCallback(p_intf, "intf-add", AddIntfCallback, NULL);

    /* Choose the best module */
    char *module;

    p_intf->p_cfg = NULL;
    free( config_ChainCreate( &module, &p_intf->p_cfg, chain ) );
    p_intf->p_module = module_need( p_intf, "interface", module, true );
    free(module);
    if( p_intf->p_module == NULL )
    {
        msg_Err( p_intf, "no suitable interface module" );
        goto error;
    }

    vlc_list_append(&p_intf->node, &libvlc_priv(libvlc)->interfaces);

    return VLC_SUCCESS;

error:
    if( p_intf->p_module )
        module_unneed( p_intf, p_intf->p_module );
    config_ChainDestroy( p_intf->p_cfg );
    vlc_object_release( p_intf );
    return VLC_EGENERIC;
}

vlc_playlist_t *
vlc_intf_GetMainPlaylist(intf_thread_t *intf)
{
    return libvlc_priv(intf->obj.libvlc)->main_playlist;
}

/**
 * Inserts an item in the playlist.
 *
 * This function is used during initialization. It inserts an item to the
 * beginning of the playlist. That is meant to compensate for the reverse
 * parsing order of the command line.
 *
 * @note This function may <b>not</b> be called at the same time as
 * intf_DestroyAll().
 */
int intf_InsertItem(libvlc_int_t *libvlc, const char *mrl, unsigned optc,
                    const char *const *optv, unsigned flags)
{
    int ret = -1;

    input_item_t *item = input_item_New(mrl, NULL);
    if (unlikely(item == NULL))
        goto end;

    if (input_item_AddOptions(item, optc, optv, flags) == VLC_SUCCESS)
    {
        vlc_playlist_t *playlist = libvlc_priv(libvlc)->main_playlist;
        vlc_playlist_Lock(playlist);
        if (vlc_playlist_InsertOne(playlist, 0, item) == VLC_SUCCESS)
            ret = 0;
        vlc_playlist_Unlock(playlist);
    }
    input_item_Release(item);

end:
    return ret;
}

void libvlc_InternalPlay(libvlc_int_t *libvlc)
{
    if (var_GetBool(libvlc, "playlist-autostart"))
    {
        vlc_playlist_t *playlist = libvlc_priv(libvlc)->main_playlist;
        vlc_playlist_Lock(playlist);
        vlc_playlist_Start(playlist);
        vlc_playlist_Unlock(playlist);
    }
}

/**
 * Starts an interface plugin.
 */
int libvlc_InternalAddIntf(libvlc_int_t *libvlc, const char *name)
{
    int ret;

    if (name != NULL)
        ret = intf_Create(libvlc, name);
    else
    {   /* Default interface */
        char *intf = var_InheritString(libvlc, "intf");
        if (intf == NULL) /* "intf" has not been set */
        {
#if !defined(_WIN32) && !defined(__OS2__)
            if (!var_InheritBool(libvlc, "daemon"))
#endif
                msg_Info(libvlc, _("Running vlc with the default interface. "
                         "Use 'cvlc' to use vlc without interface."));
        }
        ret = intf_Create(libvlc, intf);
        free(intf);
        name = "default";
    }

    if (ret != VLC_SUCCESS)
        msg_Err(libvlc, "interface \"%s\" initialization failed", name);
    return ret;
}

/**
 * Stops and destroys all interfaces
 * @param libvlc the LibVLC instance
 */
void intf_DestroyAll(libvlc_int_t *libvlc)
{
    libvlc_priv_t *libvlc_p = libvlc_priv(libvlc);
    intf_thread_t *intf;
    vlc_list_foreach(intf, &libvlc_p->interfaces, node)
    {
        vlc_list_remove(&intf->node);
        module_unneed(intf, intf->p_module);
        config_ChainDestroy(intf->p_cfg);
        var_DelCallback(intf, "intf-add", AddIntfCallback, NULL);
        vlc_object_release(intf);
    }
}

/* Following functions are local */

static int AddIntfCallback( vlc_object_t *obj, char const *var,
                            vlc_value_t old, vlc_value_t cur, void *data )
{
    int ret = intf_Create( obj->obj.libvlc, cur.psz_string );
    if( ret )
        msg_Err( obj, "interface \"%s\" initialization failed",
                 cur.psz_string );

    (void) var; (void) old; (void) data;
    return ret;
}
