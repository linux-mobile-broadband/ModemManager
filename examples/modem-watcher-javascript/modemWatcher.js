// -*- mode: js; js-indent-level: 4; indent-tabs-mode: nil -*-

//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU Lesser General Public License as published by the Free
// Software Foundation; either version 2 of the License, or (at your option) any
// later version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
// FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more
// details.
//
// You should have received a copy of the GNU Lesser General Public License along
// with this program; if not, write to the Free Software Foundation, Inc., 51
// Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
//
// Copyright (C) 2014 Aleksander Morgado <aleksander@aleksander.es>
//

const Lang         = imports.lang;
const Mainloop     = imports.mainloop;
const Gio          = imports.gi.Gio;
const ModemManager = imports.gi.ModemManager;

// The ModemWatcher class
const ModemWatcher = new Lang.Class({
    Name: 'ModemWatcher',

    _init: function() {
        // The manager
        this._manager = ModemManager.Manager.new_sync(Gio.DBus.system,
                                                      Gio.DBusObjectManagerClientFlags.DO_NOT_AUTO_START,
                                                      null)

        // Follow changes in the ModemManager1 interface name owner
        this._nameOwnerId = this._manager.connect('notify::name-owner', Lang.bind(this, this._ModemManagerNameOwnerChanged));
        this._ModemManagerNameOwnerChanged()

        // Follow added/removed objects
        this._objectAddedId   = this._manager.connect('object-added',   Lang.bind(this, this._ModemAdded));
        this._objectRemovedId = this._manager.connect('object-removed', Lang.bind(this, this._ModemRemoved));

        // Add initial objects
        let modem_object_list = this._manager.get_objects();
        for (let i = 0; i < modem_object_list.length; i++)
            this._ModemAdded(this._manager, modem_object_list[i]);
    },

    _ModemManagerNameOwnerChanged: function() {
        if (this._manager.name_owner)
            print('[ModemWatcher] ModemManager ' + this._manager.get_version() + ' service is available in bus');
        else
            print('[ModemWatcher] ModemManager service not available in bus');
    },

    _ModemAdded: function(manager, object) {
        let modem = object.get_modem();
        print('[ModemWatcher] ' +
              modem.get_manufacturer() +
              ' (' +
              modem.get_model() +
              ') modem managed by ModemManager [' +
              modem.get_equipment_identifier() +
              ']: ' +
              object.get_object_path());
    },

    _ModemRemoved: function(manager, object) {
        print('[ModemWatcher] modem unmanaged by ModemManager: ' + object.get_object_path());
    },

    destroy: function() {
        if (this._nameOwnerId) {
            this._manager.disconnect(this._nameOwnerId);
            this._nameOwnerId = 0;
        }

        if (this._objectAddedId) {
            this._manager.disconnect(this._objectAddedId);
            this._objectAddedId = 0;
        }

        if (this._objectRemovedId) {
            this._manager.disconnect(this._objectRemovedId);
            this._objectRemovedId = 0;
        }
    }
});
