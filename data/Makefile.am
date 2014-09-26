eos_updater_in = eos-updater.service.in eos-updater.timer.in

systemdsystemunit_DATA = $(eos_updater_in:%.in=%)

sysconf_DATA = eos-updater.conf

eos-updater.service: eos-updater.service.in
	$(edit) $< >$@

eos-updater.timer: eos-updater.timer.in
	$(edit) $< >$@

dbusservicedir = $(datadir)/dbus-1/system-services
dbusservice_in_files = com.endlessm.SystemUpdater.service.in
dbusservice_DATA = $(dbusservice_in_files:.service.in=.service)

$(dbusservice_DATA): $(dbusservice_in_files)
	$(edit) $< >$@

dbusconfdir = $(sysconfdir)/dbus-1/system.d
dbusconf_DATA = com.endlessm.SystemUpdater.conf

edit = sed \
	-e 's|@bindir[@]|$(bindir)|g' \
	-e 's|@sbindir[@]|$(sbindir)|g' \
	-e 's|@sysconfdir[@]|$(sysconfdir)|g' \
	-e 's|@localstatedir[@]|$(localstatedir)|g' \
	-e 's|@libexecdir[@]|$(libexecdir)|g'

EXTRA_DIST = \
	$(dbusservice_in_files) \
	$(dbusconf_DATA) \
	$(eos_updater_in) \
	$(sysconf_DATA) \
	$(NULL)

CLEANFILES = \
	$(systemdsystemunit_DATA) \
	$(dbusservice_DATA) \
	$(NULL)
