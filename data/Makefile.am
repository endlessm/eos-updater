eos_updater_in = eos-updater.service.in eos-updater.timer.in

systemdsystemunit_DATA = $(eos_updater_in:%.in=%)

sysconf_DATA = eos-updater.conf

SUFFIXES = .in
.in:
	$(edit) $< >$@

edit = sed \
	-e 's|@bindir[@]|$(bindir)|g' \
	-e 's|@sbindir[@]|$(sbindir)|g' \
	-e 's|@sysconfdir[@]|$(sysconfdir)|g' \
	-e 's|@localstatedir[@]|$(localstatedir)|g' \
	-e 's|@libexecdir[@]|$(libexecdir)|g'

EXTRA_DIST = $(eos_updater_in) \
	$(sysconf_DATA) \
	$(NULL)

CLEANFILES = $(systemdsystemunit_DATA)
