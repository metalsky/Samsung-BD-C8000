camlib_LTLIBRARIES = enigma13.la

EXTRA_DIST = STATUS protocol.txt README.enigma13

enigma13_la_SOURCES	= enigma13.c
enigma13_la_LDFLAGS = = -module -no-undefined -avoid-version -export-dynamic \
	-export-symbols $(top_srcdir)/camlibs/camlib.sym
enigma13_la_DEPENDENCIES = $(top_srcdir)/camlibs/camlib.sym
enigma13_la_LIBADD = $(top_builddir)/libgphoto2/libgphoto2.la

camlibdoc_DATA = README.enigma13
