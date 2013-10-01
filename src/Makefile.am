AM_CFLAGS = $(EOS_UPDATER_CFLAGS)
eos_updater_LDADD = $(EOS_UPDATER_LIBS)

libexec_PROGRAMS = eos-updater

ostree_daemon_gen_sources = \
	ostree-daemon-generated.h \
	ostree-daemon-generated.c

eos_updater_SOURCES =	\
	$(ostree_daemon_gen_sources) \
	ostree-daemon.xml \
	eos-updater.c

CLEANFILES = $(ostree_daemon_gen_sources)

$(ostree_daemon_OBJECTS): $(ostree_daemon_gen_sources)

$(ostree_daemon_gen_sources): ostree-daemon.xml
	@cd $(<D) && gdbus-codegen              \
	   --interface-prefix org.gnome.OSTree \
	   --generate-c-code $(basename $(@F))  \
	   --c-namespace OTD                    \
	   --c-generate-object-manager          \
	   $(<F)