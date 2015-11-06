eos_autoupdater_in = eos-autoupdater.service.in eos-autoupdater.timer.in eos-update-apply.service.in

systemdsystemunit_DATA = $(eos_autoupdater_in:%.in=%)

sysconf_DATA = eos-updater.conf

eos-autoupdater.service: eos-autoupdater.service.in
	$(edit) $< >$@

eos-autoupdater.timer: eos-autoupdater.timer.in
	$(edit) $< >$@

eos-update-apply.service: eos-update-apply.service.in
	$(edit) $< >$@

edit = sed \
	-e 's|@bindir[@]|$(bindir)|g' \
	-e 's|@sbindir[@]|$(sbindir)|g' \
	-e 's|@sysconfdir[@]|$(sysconfdir)|g' \
	-e 's|@localstatedir[@]|$(localstatedir)|g' \
	-e 's|@libexecdir[@]|$(libexecdir)|g'

EXTRA_DIST = $(eos_autoupdater_in) \
	$(sysconf_DATA) \
	$(NULL)

CLEANFILES = $(systemdsystemunit_DATA)
