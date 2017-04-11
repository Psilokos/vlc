vaapidir = $(pluginsdir)/vaapi

libvlc_vaapi_instance_la_SOURCES = hw/vaapi/instance.c hw/vaapi/vlc_vaapi.h
libvlc_vaapi_instance_la_CFLAGS = $(LIBVA_CFLAGS)
libvlc_vaapi_instance_la_LIBADD = $(LIBVA_LIBS) $(LIBPTHREAD)
libvlc_vaapi_instance_la_LDFLAGS =			\
				   -no-undefined	\
				   -version-info 0:0:0

libvaapi_chroma_plugin_la_SOURCES = hw/vaapi/chroma.c hw/vaapi/vlc_vaapi.c hw/vaapi/vlc_vaapi.h	\
				    video_chroma/copy.c video_chroma/copy.h
libvaapi_chroma_plugin_la_CFLAGS = $(AM_CFLAGS) $(LIBVA_CFLAGS)
libvaapi_chroma_plugin_la_LIBADD = $(AM_LIBADD) $(LIBVA_LIBS) $(LIBVA_X11_LIBS) libvlc_vaapi_instance.la

libvaapi_deinterlace_plugin_la_SOURCES = hw/vaapi/deinterlace.c hw/vaapi/va_filter.h	\
					 hw/vaapi/vlc_vaapi.c hw/vaapi/vlc_vaapi.h
libvaapi_deinterlace_plugin_la_CPPFLAGS = $(AM_CPPFLAGS)
libvaapi_deinterlace_plugin_la_CFLAGS = $(AM_CFLAGS) $(LIBVA_CFLAGS)
libvaapi_deinterlace_plugin_la_LIBADD = $(AM_LIBADD) $(LIBVA_LIBS) libvlc_vaapi_instance.la

if HAVE_VAAPI_X11
vaapi_LTLIBRARIES = libvlc_vaapi_instance.la		\
		    libvaapi_chroma_plugin.la		\
		    libvaapi_deinterlace_plugin.la
endif
