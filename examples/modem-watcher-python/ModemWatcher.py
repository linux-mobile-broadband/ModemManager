#!/usr/bin/env python3
# -*- Mode: python; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
#
# This program is free software; you can redistribute it and/or modify it under
# the terms of the GNU Lesser General Public License as published by the Free
# Software Foundation; either version 2 of the License, or (at your option) any
# later version.
#
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
# FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more
# details.
#
# You should have received a copy of the GNU Lesser General Public License along
# with this program; if not, write to the Free Software Foundation, Inc., 51
# Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
#
# Copyright (C) 2014 Aleksander Morgado <aleksander@aleksander.es>
#

import gi
gi.require_version('ModemManager', '1.0')
from gi.repository import Gio, GLib, GObject, ModemManager


class ModemWatcher:
    """
    The ModemWatcher class is responsible for monitoring ModemManager.
    """

    def __init__(self):
        # Flag for initial logs
        self.initializing = True
        # Setup DBus monitoring
        self.connection = Gio.bus_get_sync(Gio.BusType.SYSTEM, None)
        self.manager = ModemManager.Manager.new_sync(
            self.connection,
            Gio.DBusObjectManagerClientFlags.DO_NOT_AUTO_START,
            None)
        # IDs for added/removed signals
        self.object_added_id = 0
        self.object_removed_id = 0
        # Follow availability of the ModemManager process
        self.available = False
        self.manager.connect('notify::name-owner', self.on_name_owner)
        self.on_name_owner(self.manager, None)
        # Finish initialization
        self.initializing = False

    def set_available(self):
        """
        ModemManager is now available.
        """
        if not self.available or self.initializing:
            print('[ModemWatcher] ModemManager %s service is available in bus' % self.manager.get_version())
        self.object_added_id = self.manager.connect('object-added', self.on_object_added)
        self.object_removed_id = self.manager.connect('object-removed', self.on_object_removed)
        self.available = True
        # Initial scan
        if self.initializing:
            for obj in self.manager.get_objects():
                self.on_object_added(self.manager, obj)

    def set_unavailable(self):
        """
        ModemManager is now unavailable.
        """
        if self.available or self.initializing:
            print('[ModemWatcher] ModemManager service not available in bus')
        if self.object_added_id:
            self.manager.disconnect(self.object_added_id)
            self.object_added_id = 0
        if self.object_removed_id:
            self.manager.disconnect(self.object_removed_id)
            self.object_removed_id = 0
        self.available = False

    def on_name_owner(self, manager, prop):
        """
        Name owner updates.
        """
        if self.manager.get_name_owner():
            self.set_available()
        else:
            self.set_unavailable()

    def on_modem_state_updated(self, modem, old, new, reason):
        """
        Modem state updated
        """
        print('[ModemWatcher] %s: modem state updated: %s -> %s (%s) ' %
              (modem.get_object_path(),
               ModemManager.ModemState.get_string (old),
               ModemManager.ModemState.get_string (new),
               ModemManager.ModemStateChangeReason.get_string (reason)))

    def on_object_added(self, manager, obj):
        """
        Object added.
        """
        modem = obj.get_modem()
        print('[ModemWatcher] %s: modem managed by ModemManager [%s]: %s (%s)' %
              (obj.get_object_path(),
               modem.get_equipment_identifier(),
               modem.get_manufacturer(),
               modem.get_model()))
        if modem.get_state() == ModemManager.ModemState.FAILED:
            print('[ModemWatcher] %s: ignoring failed modem' %
                  obj.get_object_path())
        else:
            modem.connect('state-changed', self.on_modem_state_updated)

    def on_object_removed(self, manager, obj):
        """
        Object removed.
        """
        print('[ModemWatcher] %s: modem unmanaged by ModemManager' %
              obj.get_object_path())
