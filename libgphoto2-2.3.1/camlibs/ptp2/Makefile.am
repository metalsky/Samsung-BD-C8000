EXTRA_DIST = ptp-pack.c PTPIP.TXT README.ptp2 TODO ChangeLog ptpip.html

camlib_LTLIBRARIES = ptp2.la

ptp2_la_SOURCES = ptp.c ptp.h library.c ptp-bugs.h ptp-private.h ptpip.c config.c
ptp2_la_LDFLAGS = -module -no-undefined -avoid-version -export-dynamic \
	-export-symbols $(top_srcdir)/camlibs/camlib.sym
ptp2_la_DEPENDENCIES = $(top_srcdir)/camlibs/camlib.sym
ptp2_la_LIBADD = $(top_builddir)/libgphoto2/libgphoto2.la $(LTLIBICONV)

camlibdoc_DATA = README.ptp2
