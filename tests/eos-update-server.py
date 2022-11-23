#!/usr/bin/python3
# -*- coding: utf-8 -*-
#
# Copyright © 2017 Endless Mobile, Inc.
#
# SPDX-License-Identifier: LGPL-2.1-or-later
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

"""Integration tests for eos-update-server."""

import os
import subprocess
import tempfile
import time
import unittest

import taptestrunner


class TestEosUpdateServer(unittest.TestCase):
    """Integration test for eos-update-server.

    It can only be run when installed, due to requiring all the systemd unit
    files to be in place.

    It must be run as root in order to be able to modify all the files it needs
    to touch.
    """

    def setUp(self):
        self.timeout_seconds = 10  # seconds per test
        self.__config_file = '/etc/eos-updater/eos-update-server.conf'
        self.__eos_update_server = \
            os.path.join('/', 'usr', 'libexec', 'eos-update-server')

    def tearDown(self):
        try:
            os.unlink(self.__config_file)
        except OSError:
            pass

        # Explicitly stop the service so it isn’t running when the next test
        # starts, which could confuse things.
        subprocess.check_call(['systemctl', 'stop', '--quiet',
                               'eos-update-server.service'])

        # And wait for a second to avoid triggering systemd’s rate limiting
        # for start requests.
        time.sleep(1)

    @unittest.skipIf(os.geteuid() != 0, "Must be run as root")
    def test_enable_via_configuration_file(self):
        """Test enabling the configuration file allows the server to run."""
        os.makedirs('/etc/eos-updater/', mode=0o755, exist_ok=True)
        with open(self.__config_file, 'w') as conf_file:
            conf_file.write(
                '[Local Network Updates]\n'
                'AdvertiseUpdates=true\n'
            )

        # Pass --port-file to force the server to open a local port rather
        # than expecting a socket from systemd.
        port_file = tempfile.NamedTemporaryFile(
            prefix='eos-update-server-test')
        status = subprocess.call([self.__eos_update_server,
                                  '--port-file=' + port_file.name,
                                  '--timeout=1'])
        self.assertEqual(status, 0)

    @unittest.skipIf(os.geteuid() != 0, "Must be run as root")
    def test_disable_via_configuration_file(self):
        """
        Test disabling the configuration file causes the server to not start.
        """
        os.makedirs('/etc/eos-updater/', mode=0o755, exist_ok=True)
        with open(self.__config_file, 'w') as conf_file:
            conf_file.write(
                '[Local Network Updates]\n'
                'AdvertiseUpdates=false\n'
            )

        status = subprocess.call([self.__eos_update_server,
                                  '--timeout=1'])
        self.assertEqual(status, 4)  # EXIT_DISABLED

    @unittest.skipIf(os.geteuid() != 0, "Must be run as root")
    @unittest.expectedFailure
    def test_disable_via_configuration_file_at_runtime(self):
        """
        Test disabling the configuration file at runtime causes the server to
        quit.

        FIXME: This is expected to fail until the server is modified to monitor
        its config file.
        """
        os.makedirs('/etc/eos-updater/', mode=0o755, exist_ok=True)
        with open(self.__config_file, 'w') as conf_file:
            conf_file.write(
                '[Local Network Updates]\n'
                'AdvertiseUpdates=true\n'
            )

        # Pass --port-file to force the server to open a local port rather
        # than expecting a socket from systemd.
        port_file = tempfile.NamedTemporaryFile(
            prefix='eos-update-server-test')
        proc = subprocess.Popen([self.__eos_update_server,
                                 '--port-file=' + port_file.name,
                                 '--timeout=' + str(self.timeout_seconds)])

        # Give it a bit of time to start up (this is technically racy)
        time.sleep(2)

        with open(self.__config_file, 'w') as conf_file:
            conf_file.write(
                '[Local Network Updates]\n'
                'AdvertiseUpdates=false\n'
            )

        proc.wait(timeout=self.timeout_seconds)
        self.assertEqual(proc.returncode, 4)  # EXIT_DISABLED


if __name__ == '__main__':
    unittest.main(testRunner=taptestrunner.TAPTestRunner())
