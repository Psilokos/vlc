vadir = $(pluginsdir)/va

libva_chroma_plugin_la_SOURCES = hw/va/chroma.c hw/va/vlc_va.c hw/va/vlc_va.h
libva_chroma_plugin_la_CFLAGS = $(AM_CFLAGS) $(LIBVA_CFLAGS)
libva_chroma_plugin_la_LIBADD = $(AM_LIBADD) $(LIBVA_LIBS) $(LIBVA_X11_LIBS)

libva_xcb_display_plugin_la_SOURCES = hw/va/xcb_display.c hw/va/vlc_va.c hw/va/vlc_va.h
libva_xcb_display_plugin_la_CPPFLAGS = $(AM_CPPFLAGS) -I$(srcdir)/video_output/xcb
libva_xcb_display_plugin_la_CFLAGS = $(AM_CFLAGS) $(XCB_CFLAGS) $(LIBVA_CFLAGS)
libva_xcb_display_plugin_la_LIBADD = libvlc_xcb_events.la \
	$(AM_LIBADD) $(X_LIBS) $(X_PRE_LIBS) -lX11 $(XCB_LIBS) \
	$(LIBVA_LIBS) $(LIBVA_X11_LIBS)

if HAVE_VAAPI_X11
va_LTLIBRARIES = libva_chroma_plugin.la
endif
