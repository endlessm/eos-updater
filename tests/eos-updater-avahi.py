#!/usr/bin/python3
# -*- coding: utf-8 -*-

# Copyright © 2017 Endless Mobile, Inc.
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, write to the
# Free Software Foundation, Inc., 59 Temple Place - Suite 330,
# Boston, MA 02111-1307, USA.

"""Integration tests for Avahi .service file updates."""

import os
import subprocess
import time
import unittest

from gi.repository import GLib
import taptestrunner


class TestEosUpdaterAvahi(unittest.TestCase):
    """Integration test for Avahi .service file updates.

    It can only be run when installed, due to requiring all the systemd unit
    files to be in place.

    It must be run as root in order to be able to modify all the files it needs
    to touch.
    """

    def setUp(self):
        self.timeout_seconds = 10  # seconds per test
        self._main_context = GLib.main_context_default()
        self.__timed_out = False
        self.__service_file = '/etc/avahi/services/eos-updater.service'
        self.__config_file = '/etc/eos-updater/eos-update-server.conf'

    def tearDown(self):
        self.assertFalse(self._main_context.iteration(False))
        self._main_context = None

        try:
            os.unlink(self.__service_file)
        except OSError:
            pass
        try:
            os.unlink(self.__config_file)
        except OSError:
            pass

        # Explicitly stop the service so it isn’t running when the next test
        # starts, which could confuse things.
        subprocess.call(['systemctl', 'stop', '--quiet',
                         'eos-updater-avahi.service'])

        # And wait for a second to avoid triggering systemd’s rate limiting
        # for start requests.
        time.sleep(1)

    def __timeout_cb(self):
        """Callback when a timeout is reached on a long-running operation."""
        self.__timed_out = True
        self._main_context.wakeup()

        return GLib.SOURCE_REMOVE

    def _wait_for_condition(self, condition_function):
        """Block until condition_function returns True, or a timeout occurs."""
        timeout_id = GLib.timeout_add_seconds(self.timeout_seconds,
                                              self.__timeout_cb)

        while not condition_function(self) and not self.__timed_out:
            self._main_context.iteration(False)

        if self.__timed_out:
            self.fail('Timeout reached')
        else:
            GLib.Source.remove(timeout_id)

    def _is_service_enabled(self, unit_file, indirect_ok=False):
        out = subprocess.check_output(['systemctl', 'is-enabled', unit_file])
        out = out.decode('utf-8').strip()
        return (out in ['enabled', 'enabled-runtime',
                        'linked', 'linked-runtime'] or
                (indirect_ok and out == 'indirect'))

    def test_disabled_by_default(self):
        """Test Avahi adverts are disabled by default, on a clean install."""
        self.assertFalse(os.path.isfile(self.__service_file))

    @unittest.skipIf(os.geteuid() != 0, "Must be run as root")
    def test_enable_via_configuration_file(self):
        """Test enabling the configuration file creates the .service file."""
        os.makedirs('/etc/eos-updater/', mode=0o755, exist_ok=True)
        with open(self.__config_file, 'w') as conf_file:
            conf_file.write(
                '[Local Network Updates]\n'
                'AdvertiseUpdates=true\n'
            )

        self._wait_for_condition(
            lambda s: os.path.isfile(self.__service_file))

    @unittest.skipIf(os.geteuid() != 0, "Must be run as root")
    def test_disable_via_configuration_file(self):
        """Test disabling the configuration file deletes the .service file."""
        os.makedirs('/etc/eos-updater/', mode=0o755, exist_ok=True)
        with open(self.__config_file, 'w') as conf_file:
            conf_file.write(
                '[Local Network Updates]\n'
                'AdvertiseUpdates=false\n'
            )

        self._wait_for_condition(
            lambda s: not os.path.isfile(self.__service_file))

    @unittest.skipIf(os.geteuid() != 0, "Must be run as root")
    def test_update_on_ostree_update(self):
        """Test updating the OSTree deployment updates the .service file."""
        def get_mtime_if_exists(path):
            try:
                return os.stat(path).st_mtime
            except FileNotFoundError:
                return 0

        # Enable advertisements first and wait for the service file to appear
        # as a result.
        os.makedirs('/etc/eos-updater/', mode=0o755, exist_ok=True)
        with open(self.__config_file, 'w') as conf_file:
            conf_file.write(
                '[Local Network Updates]\n'
                'AdvertiseUpdates=true\n'
            )

        self._wait_for_condition(
            lambda s: os.path.isfile(self.__service_file))
        before = get_mtime_if_exists(self.__service_file)

        # Wait 2s to ensure the mtimes will change from after enabling
        # advertisements.
        time.sleep(2)

        # ‘Touch’ a deployment ref by rewriting it with the same contents
        done = False
        for boot_version in ['0/0', '0/1', '1/0', '1/1']:
            try:
                path = '/ostree/repo/refs/heads/ostree/' + boot_version + '/0'
                with open(path, 'r') as f:
                    contents = f.read()
                with open(path, 'w') as f:
                    f.write(contents)
                done = True
            except FileNotFoundError:
                pass

        self.assertTrue(done, 'no suitable deployment ref found')

        self._wait_for_condition(
            lambda s: get_mtime_if_exists(self.__service_file) > before)

    @unittest.skipIf(os.geteuid() != 0, "Must be run as root")
    def test_services_enabled_by_default(self):
        """Test the Avahi systemd units are enabled by default."""
        self.assertTrue(self._is_service_enabled('eos-update-server.service',
                                                 indirect_ok=True))
        self.assertTrue(self._is_service_enabled('eos-update-server.socket'))
        self.assertTrue(self._is_service_enabled('eos-updater-avahi.service',
                                                 indirect_ok=True))
        self.assertTrue(self._is_service_enabled('eos-updater-avahi.path'))
        self.assertTrue(self._is_service_enabled('avahi-daemon.service'))


if __name__ == '__main__':
    unittest.main(testRunner=taptestrunner.TAPTestRunner())
