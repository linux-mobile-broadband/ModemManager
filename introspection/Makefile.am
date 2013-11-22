
SUBDIRS = . tests

# DBus Introspection files
XMLS = $(wildcard *.xml)
xmldir = $(datadir)/dbus-1/interfaces
xml_DATA = $(filter-out all.xml wip-*, $(XMLS))

EXTRA_DIST = $(XMLS)
