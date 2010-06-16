noinst_LTLIBRARIES = libsystray.la

AM_CFLAGS = \
	@MUTTER_PLUGIN_CFLAGS@ 					\
	-DMX_CACHE=\"$(MUTTER_MEEGO_THEME_DIR)/mx.cache\"	\
	-DTHEMEDIR=\"$(MUTTER_MEEGO_THEME_DIR)\"		\
	-I$(srcdir)/tray

tray_built_sources =	\
	na-marshal.h 	\
	na-marshal.c

BUILT_SOURCES = $(tray_built_sources)

TRAY_STAMP_FILES = stamp-na-marshal.h

# please, keep this sorted alphabetically
tray_source =			\
	tray/na-tray-child.c	\
	tray/na-tray-child.h	\
	tray/na-tray-manager.c	\
	tray/na-tray-manager.h	\
	$(NULL)

na-marshal.h: stamp-na-marshal.h
	@true
stamp-na-marshal.h: Makefile tray/na-marshal.list
	$(AM_V_GEN) $(GLIB_GENMARSHAL) \
		--prefix=_na_marshal \
		--header \
	$(srcdir)/tray/na-marshal.list > xgen-tmh && \
	(cmp -s xgen-tmh na-marshal.h || cp -f xgen-tmh na-marshal.h) && \
	rm -f xgen-tmh && \
	echo timestamp > $(@F)

na-marshal.c: Makefile tray/na-marshal.list
	$(AM_V_GEN) (echo "#include \"na-marshal.h\"" ; \
	 $(GLIB_GENMARSHAL) \
		--prefix=_na_marshal \
		--body \
	 $(srcdir)/tray/na-marshal.list ) > xgen-tmc && \
	cp -f xgen-tmc na-marshal.c && \
	rm -f xgen-tmc

libsystray_la_LIBADD = $(MUTTER_PLUGIN_LIBS)
libsystray_la_SOURCES =	$(BUILT_SOURCES)		\
			shell-embedded-window.h		\
			shell-embedded-window.c		\
			shell-embedded-window-private.h	\
			shell-gtk-embed.h		\
			shell-gtk-embed.c		\
			shell-tray-manager.h		\
			shell-tray-manager.c		\
			tray/na-tray-child.h		\
			tray/na-tray-child.c		\
			tray/na-tray-manager.h		\
			tray/na-tray-manager.c

libsystray_la_CPPFLAGS = $(systray_cflags)
libsystray_la_LDFLAGS = $(LDADD)

CLEANFILES = $(TRAY_STAMP_FILES) $(BUILT_SOURCES)
EXTRA_DIST = tray/na-marshal.list
