
AM_CFLAGS = \
	$(WARN_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS) \
	$(NULL)

AM_LDFLAGS = \
	$(WARN_LDFLAGS) \
	$(CODE_COVERAGE_LDFLAGS) \
	$(NULL)

noinst_LTLIBRARIES = libqcdm.la libqcdm-test.la

libqcdm_la_CPPFLAGS = \
	$(MM_CFLAGS)

libqcdm_la_SOURCES = \
	dm-commands.h \
	nv-items.h \
	log-items.h \
	com.c \
	com.h \
	commands.c \
	commands.h \
	errors.c \
	errors.h \
	logs.c \
	logs.h \
	result.c \
	result.h \
	result-private.h \
	utils.c \
	utils.h

libqcdm_la_LIBADD = \
	$(MM_LIBS)


###########################################
# Test library without symbol versioning
###########################################

libqcdm_test_la_CPPFLAGS = \
	$(MM_CFLAGS)

libqcdm_test_la_SOURCES = \
	utils.c \
	utils.h

libqcdm_test_la_LIBADD = \
	$(MM_LIBS)
