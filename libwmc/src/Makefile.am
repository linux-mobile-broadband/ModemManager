AM_CFLAGS = $(CODE_COVERAGE_CFLAGS)
AM_LDFLAGS = $(CODE_COVERAGE_LDFLAGS)

noinst_LTLIBRARIES = libwmc.la

libwmc_la_CPPFLAGS = \
	$(MM_CFLAGS)

libwmc_la_SOURCES = \
	protocol.h \
	result-private.h \
	errors.c \
	errors.h \
	utils.c \
	utils.h \
	result.c \
	result.h \
	com.c \
	com.h \
	commands.c \
	commands.h

libwmc_la_LIBADD = \
	$(MM_LIBS)

