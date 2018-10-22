/*****************************************************************************
 * engine.c : Run the playlist and handle its control
 *****************************************************************************
 * Copyright (C) 1999-2008 VLC authors and VideoLAN
 *
 * Authors: Samuel Hocevar <sam@zoy.org>
 *          Clément Stenac <zorglub@videolan.org>
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

#include <stddef.h>
#include <assert.h>

#include <vlc_common.h>
#include <vlc_arrays.h>
#include <vlc_sout.h>
#include <vlc_playlist.h>
#include <vlc_interface.h>
#include <vlc_http.h>
#include <vlc_renderer_discovery.h>
#include "playlist_internal.h"
#include "input/resource.h"

/**
 * Create playlist
 *
 * Create a playlist structure.
 * \param p_parent the vlc object that is to be the parent of this playlist
 * \return a pointer to the created playlist, or NULL on error
 */
playlist_t *playlist_Create( vlc_object_t *p_parent )
{
    playlist_t *p_playlist;
    playlist_private_t *p;

    /* Allocate structure */
    p = vlc_custom_create( p_parent, sizeof( *p ), "playlist" );
    if( !p )
        return NULL;

    p_playlist = &p->public_data;

    p->input_tree = NULL;
    p->id_tree = NULL;

    vlc_list_init(&p->sds);

    vlc_mutex_init( &p->lock );
    vlc_cond_init( &p->signal );
    p->killed = false;

    /* Initialise data structures */
    pl_priv(p_playlist)->i_last_playlist_id = 0;
    pl_priv(p_playlist)->p_input = NULL;

    ARRAY_INIT( p_playlist->items );
    ARRAY_INIT( p_playlist->current );

    p_playlist->i_current_index = 0;
    pl_priv(p_playlist)->b_reset_currently_playing = true;

    pl_priv(p_playlist)->b_tree = var_InheritBool( p_parent, "playlist-tree" );
    pl_priv(p_playlist)->b_preparse = var_InheritBool( p_parent, "auto-preparse" );

    p_playlist->root.p_input = NULL;
    p_playlist->root.pp_children = NULL;
    p_playlist->root.i_children = 0;
    p_playlist->root.i_nb_played = 0;
    p_playlist->root.i_id = 0;
    p_playlist->root.i_flags = 0;

    /* Create the root, playing items nodes */
    playlist_item_t *playing;

    PL_LOCK;
    playing = playlist_NodeCreate( p_playlist, _( "Playlist" ),
                                   &p_playlist->root, PLAYLIST_END,
                                   PLAYLIST_RO_FLAG|PLAYLIST_NO_INHERIT_FLAG );
    PL_UNLOCK;

    if( unlikely(playing == NULL) )
        abort();

    p_playlist->p_playing = playing;

    /* Initial status */
    pl_priv(p_playlist)->status.p_item = NULL;
    pl_priv(p_playlist)->status.p_node = p_playlist->p_playing;
    pl_priv(p_playlist)->request.b_request = false;
    p->request.input_dead = false;

    /* Input resources */
    p->p_input_resource = input_resource_New( VLC_OBJECT( p_playlist ) );
    if( unlikely(p->p_input_resource == NULL) )
        abort();

    /* Audio output (needed for volume and device controls). */
    audio_output_t *aout = input_resource_GetAout( p->p_input_resource );
    if( aout != NULL )
        input_resource_PutAout( p->p_input_resource, aout );

    /* Initialize the shared HTTP cookie jar */
    vlc_value_t cookies;
    cookies.p_address = vlc_http_cookies_new();
    if ( likely(cookies.p_address) )
    {
        var_Create( p_playlist, "http-cookies", VLC_VAR_ADDRESS );
        var_SetChecked( p_playlist, "http-cookies", VLC_VAR_ADDRESS, cookies );
    }

    /* Thread */
    playlist_Activate (p_playlist);

    /* Add service discovery modules */
    char *mods = var_InheritString( p_playlist, "services-discovery" );
    if( mods != NULL )
    {
        char *s = mods, *m;
        while( (m = strsep( &s, " :," )) != NULL )
            playlist_ServicesDiscoveryAdd( p_playlist, m );
        free( mods );
    }

    return p_playlist;
}

/**
 * Destroy playlist.
 * This is not thread-safe. Any reference to the playlist is assumed gone.
 * (In particular, all interface and services threads must have been joined).
 *
 * \param p_playlist the playlist object
 */
void playlist_Destroy( playlist_t *p_playlist )
{
    playlist_private_t *p_sys = pl_priv(p_playlist);

    /* Remove all services discovery */
    playlist_ServicesDiscoveryKillAll( p_playlist );

    msg_Dbg( p_playlist, "destroying" );

    playlist_Deactivate( p_playlist );

    /* Release input resources */
    assert( p_sys->p_input == NULL );
    input_resource_Release( p_sys->p_input_resource );
    if( p_sys->p_renderer )
        vlc_renderer_item_release( p_sys->p_renderer );

    PL_LOCK;
    /* Release the current node */
    set_current_status_node( p_playlist, NULL );
    /* Release the current item */
    set_current_status_item( p_playlist, NULL );

    /* Destroy arrays completely - faster than one item at a time */
    ARRAY_RESET( p_playlist->items );
    ARRAY_RESET( p_playlist->current );

    /* Remove all remaining items */
    playlist_NodeDeleteExplicit( p_playlist, p_playlist->p_playing,
        PLAYLIST_DELETE_FORCE );

    assert( p_playlist->root.i_children <= 0 );
    PL_UNLOCK;

    vlc_cond_destroy( &p_sys->signal );
    vlc_mutex_destroy( &p_sys->lock );

    vlc_http_cookie_jar_t *cookies = var_GetAddress( p_playlist, "http-cookies" );
    if ( cookies )
    {
        var_Destroy( p_playlist, "http-cookies" );
        vlc_http_cookies_destroy( cookies );
    }

    vlc_object_release( p_playlist );
}

/** Get current playing input.
 */
input_thread_t *playlist_CurrentInputLocked( playlist_t *p_playlist )
{
    PL_ASSERT_LOCKED;

    input_thread_t *p_input = pl_priv(p_playlist)->p_input;
    if( p_input != NULL )
        vlc_object_hold( p_input );
    return p_input;
}


/** Get current playing input.
 */
input_thread_t * playlist_CurrentInput( playlist_t * p_playlist )
{
    input_thread_t * p_input;
    PL_LOCK;
    p_input = playlist_CurrentInputLocked( p_playlist );
    PL_UNLOCK;
    return p_input;
}

/**
 * @}
 */

/** Accessor for status item and status nodes.
 */
playlist_item_t * get_current_status_item( playlist_t * p_playlist )
{
    PL_ASSERT_LOCKED;

    return pl_priv(p_playlist)->status.p_item;
}

playlist_item_t * get_current_status_node( playlist_t * p_playlist )
{
    PL_ASSERT_LOCKED;

    return pl_priv(p_playlist)->status.p_node;
}

void set_current_status_item( playlist_t * p_playlist,
    playlist_item_t * p_item )
{
    PL_ASSERT_LOCKED;

    pl_priv(p_playlist)->status.p_item = p_item;
}

void set_current_status_node( playlist_t * p_playlist,
    playlist_item_t * p_node )
{
    PL_ASSERT_LOCKED;

    pl_priv(p_playlist)->status.p_node = p_node;
}

playlist_item_t * playlist_CurrentPlayingItem( playlist_t * p_playlist )
{
    PL_ASSERT_LOCKED;

    return pl_priv(p_playlist)->status.p_item;
}

int playlist_Status( playlist_t * p_playlist )
{
    input_thread_t *p_input = pl_priv(p_playlist)->p_input;

    PL_ASSERT_LOCKED;

    if( p_input == NULL )
        return PLAYLIST_STOPPED;
    if( var_GetInteger( p_input, "state" ) == PAUSE_S )
        return PLAYLIST_PAUSED;
    return PLAYLIST_RUNNING;
}

