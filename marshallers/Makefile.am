noinst_LTLIBRARIES = libmarshallers.la

BUILT_SOURCES = \
	mm-marshal.h \
	mm-marshal.c

libmarshallers_la_SOURCES = mm-marshal-main.c

EXTRA_DIST = mm-marshal.list
CLEANFILES = $(BUILT_SOURCES)

libmarshallers_la_CPPFLAGS = $(MM_CFLAGS)
libmarshallers_la_LIBADD = $(MM_LIBS)

mm-marshal.h: mm-marshal.list
	$(GLIB_GENMARSHAL) $< --prefix=mm_marshal --header > $@

mm-marshal.c: mm-marshal.list
	$(GLIB_GENMARSHAL) $< --prefix=mm_marshal --body > $@

mm-marshal-main.c: mm-marshal.c mm-marshal.h
