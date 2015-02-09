AM_CFLAGS = $(EOS_UPDATER_CFLAGS)
eos_updater_LDADD = $(EOS_UPDATER_LIBS)

libexec_PROGRAMS = eos-updater
dist_libexec_SCRIPTS = eos-release-upgrade

BUILT_SOURCES = \
	ostree-daemon-generated.h \
	ostree-daemon-generated.c \
	$(NULL)

eos_updater_SOURCES = eos-updater.c

nodist_eos_updater_SOURCES = $(BUILT_SOURCES)

EXTRA_DIST = ostree-daemon.xml

CLEANFILES = $(BUILT_SOURCES)

$(BUILT_SOURCES): ostree-daemon.xml
	@cd $(<D) && gdbus-codegen              \
	   --interface-prefix org.gnome.OSTree \
	   --generate-c-code $(basename $(@F))  \
	   --c-namespace OTD                    \
	   --c-generate-object-manager          \
	   $(<F)
