AM_CFLAGS = $(EOS_UPDATER_CFLAGS)
eos_updater_LDADD = $(EOS_UPDATER_LIBS)

libexec_PROGRAMS = eos-updater

eos_updater_SOURCES = eos-updater.c

nodist_eos_updater_SOURCES = $(BUILT_SOURCES)

BUILT_SOURCES =

ostree-daemon-generated.c: ostree-daemon-generated.h
ostree-daemon-generated.h: $(srcdir)/ostree-daemon.xml
	gdbus-codegen $<				\
	--interface-prefix org.gnome.OSTree		\
	--generate-c-code ostree-daemon-generated	\
	--c-namespace OTD
BUILT_SOURCES += ostree-daemon-generated.c ostree-daemon-generated.h

eos-updater-generated.c: eos-updater-generated.h
eos-updater-generated.h: $(srcdir)/com.endlessm.SystemUpdater.xml
	gdbus-codegen $<				\
	--interface-prefix com.endlessm.SystemUpdater	\
	--generate-c-code eos-updater-generated		\
	--c-namespace SystemUpdater
BUILT_SOURCES += eos-updater-generated.c eos-updater-generated.h

EXTRA_DIST =				\
	ostree-daemon.xml		\
	com.endlessm.EOSUpdater.xml	\
	$(NULL)
