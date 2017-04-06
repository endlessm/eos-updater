# Copyright © 2017 Endless Mobile, Inc.
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA

eos_autoupdater_in = \
	eos-autoupdater.service.in \
	eos-autoupdater.timer.in \
	eos-update-server.socket.in \
	eos-update-server.service.in \
	eos-updater.service.in \
	$(NULL)

systemdsystemunit_DATA = $(eos_autoupdater_in:%.in=%)

# Example configuration files.
sysconfexampledir = $(pkgdatadir)
dist_sysconfexample_DATA = \
	eos-autoupdater.conf \
	eos-updater.conf \
	eos-update-server.conf \
	$(NULL)

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

EXTRA_DIST = $(eos_autoupdater_in)
CLEANFILES = $(systemdsystemunit_DATA)

# Documentation
dist_man5_MANS = \
	docs/eos-autoupdater.conf.5 \
	docs/eos-updater.conf.5 \
	docs/eos-update-server.conf.5 \
	$(NULL)

-include $(top_srcdir)/git.mk
