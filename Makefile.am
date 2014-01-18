AM_CFLAGS = $(PTHREAD_CFLAGS)
AM_LDFLAGS = $(PTHREAD_LIBS)

bin_PROGRAMS = fcgi

fcgi_SOURCES = \
debug.h \
dispatch.c \
dispatch.h \
main.c \
uri.c \
uri.h

if ENABLE_DEBUG
fcgi_SOURCES += debug.c
endif

if ENABLE_CGROUPS
fcgi_SOURCES += cgroups.c cgroups.h
endif

if HAVE_XXD
fcgi_SOURCES += license.c

license.c: LICENSE
	$(AM_V_GEN)$(XXD) -i $< > $@

CLEANFILES = license.c
endif

doc_DATA = LICENSE README

EXTRA_DIST = $(doc_DATA) TODO

