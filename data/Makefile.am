eos_autoupdater_in = \
	eos-autoupdater.service.in \
	eos-autoupdater.timer.in \
	eos-update-server.socket.in \
	eos-update-server.service.in \
	$(NULL)

systemdsystemunit_DATA = $(eos_autoupdater_in:%.in=%)

sysconf_DATA = eos-updater.conf

%: %.in
	$(AM_V_GEN)$(edit) $< >$@

edit = sed \
	-e 's|@bindir[@]|$(bindir)|g' \
	-e 's|@sbindir[@]|$(sbindir)|g' \
	-e 's|@sysconfdir[@]|$(sysconfdir)|g' \
	-e 's|@localstatedir[@]|$(localstatedir)|g' \
	-e 's|@libexecdir[@]|$(libexecdir)|g' \
	-e 's|@server_port[@]|$(server_port)|g' \
	$(NULL)

EXTRA_DIST = $(eos_autoupdater_in) \
	$(sysconf_DATA) \
	$(NULL)

CLEANFILES = $(systemdsystemunit_DATA)
