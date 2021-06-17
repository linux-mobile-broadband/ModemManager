#!/usr/bin/env python3
# -*- Mode: python; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-

from __future__ import print_function

import gi
gi.require_version('ModemManager', '1.0')
from gi.repository import GLib, ModemManager
import argparse
import sys
import dbus
import dbus.service
import dbus.mainloop.glib
import random
import collections

mainloop = GLib.MainLoop()

#########################################################
IFACE_DBUS = 'org.freedesktop.DBus'

class UnknownInterfaceException(dbus.DBusException):
    def __init__(self, *args, **kwargs):
        self._dbus_error_name = '{}.UnknownInterface'.format(IFACE_DBUS)
        super().__init__(*args, **kwargs)

class UnknownPropertyException(dbus.DBusException):
    def __init__(self, *args, **kwargs):
        self._dbus_error_name = '{}.UnknownProperty'.format(IFACE_DBUS)
        super().__init__(*args, **kwargs)

class MobileEquipmentException(dbus.DBusException):
    _dbus_error_name = '{}.Error.MobileEquipment'.format(IFACE_DBUS)
    def __init__(self, *args, **kwargs):
        equipment_error_num = kwargs.pop('equipment_error', None)
        if equipment_error_num is not None:
            equipment_error_except = ModemManager.MobileEquipmentError(equipment_error_num)
            self._dbus_error_name = '{}.Error.MobileEquipment.{}'.format(IFACE_DBUS, equipment_error_except.value_nick)
        super().__init__(*args, **kwargs)

def log(msg):
    if log_file:
        try:
            log_file.write(msg + "\n")
            log_file.flush()
        except Exception:
            pass
    else:
        print(msg)

def to_path_array(src):
    array = dbus.Array([], signature=dbus.Signature('o'))
    for o in src:
        array.append(to_path(o))
    return array

def to_path(src):
    if src:
        return dbus.ObjectPath(src.path)
    return dbus.ObjectPath("/")

class ExportedObj(dbus.service.Object):

    DBusInterface = collections.namedtuple('DBusInterface', ['dbus_iface', 'get_props_func', 'set_props_func', 'prop_changed_func'])

    def __init__(self, bus, object_path):
        super(ExportedObj, self).__init__(bus, object_path)
        self._bus = bus
        self.path = object_path
        self.__ensure_dbus_ifaces()
        log("Will add object with path '%s' to object manager" % object_path)
        object_manager.add_object(self)

    def __ensure_dbus_ifaces(self):
        if not hasattr(self, '_ExportedObj__dbus_ifaces'):
            self.__dbus_ifaces = {}

    def add_dbus_interface(self, dbus_iface, get_props_func, set_props_func, prop_changed_func):
        self.__ensure_dbus_ifaces()
        self.__dbus_ifaces[dbus_iface] = ExportedObj.DBusInterface(dbus_iface, get_props_func, set_props_func, prop_changed_func)

    def __dbus_interface_get(self, dbus_iface):
        if dbus_iface not in self.__dbus_ifaces:
            raise UnknownInterfaceException()
        return self.__dbus_ifaces[dbus_iface]

    def _dbus_property_get(self, dbus_iface, propname=None):
        props = self.__dbus_interface_get(dbus_iface).get_props_func()
        if propname is None:
            return props
        if propname not in props:
            raise UnknownPropertyException()
        return props[propname]

    def _dbus_property_set(self, dbus_iface, propname, value):
        props = self.__dbus_interface_get(dbus_iface).get_props_func()

        try:
            if props[propname] == value:
                return
        except KeyError:
            raise UnknownPropertyException()

        if self.__dbus_interface_get(dbus_iface).set_props_func is not None:
            self.__dbus_interface_get(dbus_iface).set_props_func(propname, value)
            self._dbus_property_notify(dbus_iface, propname)

    def _dbus_property_notify(self, dbus_iface, propname):
        prop = self._dbus_property_get(dbus_iface, propname)
        self.__dbus_interface_get(dbus_iface).prop_changed_func(self, {propname: prop})
        ExportedObj.PropertiesChanged(self, dbus_iface, {propname: prop}, [])

    @dbus.service.signal(dbus.PROPERTIES_IFACE, signature='sa{sv}as')
    def PropertiesChanged(self, iface, changed, invalidated):
        pass

    @dbus.service.method(dbus_interface=dbus.PROPERTIES_IFACE, in_signature='s', out_signature='a{sv}')
    def GetAll(self, dbus_iface):
        return self._dbus_property_get(dbus_iface)

    @dbus.service.method(dbus_interface=dbus.PROPERTIES_IFACE, in_signature='ss', out_signature='v')
    def Get(self, dbus_iface, name):
        return self._dbus_property_get(dbus_iface, name)

    @dbus.service.method(dbus_interface=dbus.PROPERTIES_IFACE, in_signature='ssv', out_signature='')
    def Set(self, dbus_iface, name, value):
        return self._dbus_property_set(dbus_iface, name, value)

    def get_managed_ifaces(self):
        my_ifaces = {}
        for iface in self.__dbus_ifaces:
            my_ifaces[iface] = self.__dbus_ifaces[iface].get_props_func()
        return self.path, my_ifaces


###################################################################
IFACE_SIM = 'org.freedesktop.ModemManager1.Sim'

PS_IMSI = "Imsi"
PS_OPERATOR_IDENTIFIER = "OperatorIdentifier"
PS_OPERATOR_NAME = "OperatorName"
PS_SIM_IDENTIFIER = "SimIdentifier"

class Sim(ExportedObj):
    def __init__(self, bus, counter, iccid, modem):
        object_path = "/org/freedesktop/ModemManager1/SIM/%d" % counter
        self.iccid = iccid
        self.modem = modem

        self.add_dbus_interface(IFACE_SIM, self.__get_props, None, Sim.PropertiesChanged)
        super(Sim, self).__init__(bus, object_path)

    # Properties interface
    def __get_props(self):
        props = {}
        props[PS_IMSI] = "Imsi_1"
        props[PS_OPERATOR_IDENTIFIER] = "OperatorIdentifier_1"
        props[PS_OPERATOR_NAME] = "OperatorName_1"
        props[PS_SIM_IDENTIFIER] = self.iccid
        return props

    # methods
    @dbus.service.method(dbus_interface=IFACE_SIM, in_signature='ss', out_signature='')
    def ChangePin(self, old_pin, new_pin):
        pass

    @dbus.service.method(dbus_interface=IFACE_SIM, in_signature='sb', out_signature='ao')
    def EnablePin(self, pin, enabled):
        pass

    @dbus.service.method(dbus_interface=IFACE_SIM, in_signature='s', out_signature='')
    def SendPin(self, pin):
        if self.modem.equipmentError is not None:
            raise MobileEquipmentException(equipment_error=self.modem.equipmentError)
        self.modem.unlock()

    @dbus.service.method(dbus_interface=IFACE_SIM, in_signature='ss', out_signature='')
    def SendPuk(self, puk, pin):
        self.modem.unlock()

    # signals
    @dbus.service.signal(IFACE_SIM, signature='a{sv}')
    def PropertiesChanged(self, changed):
        pass


###################################################################
IFACE_MODEM = 'org.freedesktop.ModemManager1.Modem'

PM_SIM = "Sim"
PM_BEARERS = "Bearers"
PM_SUPPORTED_CAPABILITIES = "SupportedCapabilities"
PM_CURRENT_CAPABILITIES = "CurrentCapabilities"
PM_MAX_BEARERS = "MaxBearers"
PM_MAX_ACTIVE_BEARERS = "MaxActiveBearers"
PM_MANUFACTURER = "Manufacturer"
PM_MODEL = "Model"
PM_REVISION = "Revision"
PM_DEVICE_IDENTIFIER = "DeviceIdentifier"
PM_DEVICE = "Device"
PM_DRIVERS = "Drivers"
PM_PLUGIN = "Plugin"
PM_PRIMARY_PORT = "PrimaryPort"
PM_PORTS = "Ports"
PM_EQUIPMENT_IDENTIFIER = "EquipmentIdentifier"
PM_UNLOCK_REQUIRED = "UnlockRequired"
PM_UNLOCK_RETRIES = "UnlockRetries"
PM_STATE = "State"
PM_STATE_FAILED_REASON = "StateFailedReason"
PM_ACCESS_TECHNOLOGIES = "AccessTechnologies"
PM_SIGNAL_QUALITY = "SignalQuality"
PM_OWN_NUMBERS = "OwnNumbers"
PM_POWER_STATE = "PowerState"
PM_SUPPORTED_MODES = "SupportedModes"
PM_CURRENT_MODES = "CurrentModes"
PM_SUPPORTED_BANDS = "SupportedBands"
PM_CURRENT_BANDS = "CurrentBands"
PM_SUPPORTED_IP_FAMILIES = "SupportedIpFamilies"

class Modem(ExportedObj):
    counter = 0

    def __init__(self, bus, add_sim, iccid):
        object_path = "/org/freedesktop/ModemManager1/Modem/%d" % Modem.counter
        self.sim_object = None
        if add_sim:
            self.sim_object = Sim(bus, Modem.counter, iccid, self)
        self.sim_path = to_path(self.sim_object)
        self.equipmentError = None
        self.reset_status = True
        self.reset_status_clear = False

        self.__props = self.__init_default_props()

        Modem.counter = Modem.counter + 1

        self.add_dbus_interface(IFACE_MODEM, self.__get_props, self.__set_prop, Modem.PropertiesChanged)
        super(Modem, self).__init__(bus, object_path)

    # Properties interface
    def __init_default_props(self):
        props = {}
        props[PM_SIM] = dbus.ObjectPath(self.sim_path)
        props[PM_DEVICE] = dbus.String("/fake/path")
        props[PM_UNLOCK_REQUIRED] = dbus.UInt32(ModemManager.ModemLock.NONE)
        props[PM_STATE] = dbus.Int32(ModemManager.ModemState.UNKNOWN)
        props[PM_STATE_FAILED_REASON] = dbus.UInt32(ModemManager.ModemStateFailedReason.UNKNOWN)
        # Not already used properties
        #props[PM_BEARERS] = None
        #props[PM_SUPPORTED_CAPABILITIES] = None
        #props[PM_CURRENT_CAPABILITIES] = None
        #props[PM_MAX_BEARERS] = None
        #props[PM_MAX_ACTIVE_BEARERS] = None
        #props[PM_MANUFACTURER] = None
        #props[PM_MODEL] = None
        #props[PM_REVISION] = None
        #props[PM_DEVICE_IDENTIFIER] = None
        #props[PM_DRIVERS] = None
        #props[PM_PLUGIN] = None
        #props[PM_PRIMARY_PORT] = None
        #props[PM_PORTS] = None
        #props[PM_EQUIPMENT_IDENTIFIER] = None
        #props[PM_UNLOCK_RETRIES] = dbus.UInt32(0)
        #props[PM_ACCESS_TECHNOLOGIES] = None
        #props[PM_SIGNAL_QUALITY] = None
        #props[PM_OWN_NUMBERS] = None
        #props[PM_POWER_STATE] = None
        #props[PM_SUPPORTED_MODES] = None
        #props[PM_CURRENT_MODES] = None
        #props[PM_SUPPORTED_BANDS] = None
        #props[PM_CURRENT_BANDS] = None
        #props[PM_SUPPORTED_IP_FAMILIES] = None
        return props

    def __get_props(self):
        return self.__props

    def __set_prop(self, name, value):
        try:
            self.__props[name] = value
        except KeyError:
            pass

    def unlock(self):
        self._dbus_property_set(IFACE_MODEM, PM_UNLOCK_REQUIRED , dbus.UInt32(ModemManager.ModemLock.NONE))

    # methods
    @dbus.service.method(dbus_interface=IFACE_MODEM, in_signature='b', out_signature='')
    def Enable(self, enable):
        pass

    @dbus.service.method(dbus_interface=IFACE_MODEM, in_signature='', out_signature='ao')
    def ListBearers(self):
        return None

    @dbus.service.method(dbus_interface=IFACE_MODEM, in_signature='a{sv}', out_signature='o')
    def CreateBearer(self, properties):
        return None

    @dbus.service.method(dbus_interface=IFACE_MODEM, in_signature='o', out_signature='')
    def DeleteBearer(self, bearer):
        pass

    @dbus.service.method(dbus_interface=IFACE_MODEM, in_signature='', out_signature='')
    def Reset(self):
        if not self.reset_status:
            if self.reset_status_clear:
                self.reset_status = True
                self.reset_status_clear = False

            raise Exception("Fake reset exception")

    @dbus.service.method(dbus_interface=IFACE_MODEM, in_signature='s', out_signature='')
    def FactoryReset(self, code):
        pass

    @dbus.service.method(dbus_interface=IFACE_MODEM, in_signature='u', out_signature='')
    def SetPowerState(self, state):
        pass

    @dbus.service.method(dbus_interface=IFACE_MODEM, in_signature='u', out_signature='')
    def SetCurrentCapabilities(self, capabilites):
        pass

    @dbus.service.method(dbus_interface=IFACE_MODEM, in_signature='(uu)', out_signature='')
    def SetCurrentModes(self, modes):
        pass

    @dbus.service.method(dbus_interface=IFACE_MODEM, in_signature='au', out_signature='')
    def SetCurrentBands(self, bands):
        pass

    @dbus.service.method(dbus_interface=IFACE_MODEM, in_signature='su', out_signature='s')
    def Command(self, cmd, timeout):
        return None

    # signals
    @dbus.service.signal(IFACE_MODEM, signature='a{sv}')
    def PropertiesChanged(self, changed):
        pass

    @dbus.service.signal(IFACE_MODEM, signature='iiu')
    def StateChanged(self, old_state, new_state, reason):
        pass


###################################################################
IFACE_OBJECT_MANAGER = 'org.freedesktop.DBus.ObjectManager'

PATH_OBJECT_MANAGER = '/org/freedesktop/ModemManager1'

IFACE_TEST = 'org.freedesktop.ModemManager1.LibmmGlibTest'
IFACE_MM = 'org.freedesktop.ModemManager1'

class ObjectManager(dbus.service.Object):
    def __init__(self, bus, object_path):
        super(ObjectManager, self).__init__(bus, object_path)
        self.objs = []
        self.bus = bus
        self.modem = None

    @dbus.service.method(dbus_interface=IFACE_OBJECT_MANAGER,
                         in_signature='', out_signature='a{oa{sa{sv}}}',
                         sender_keyword='sender')
    def GetManagedObjects(self, sender=None):
        managed_objects = {}
        for obj in self.objs:
            name, ifaces = obj.get_managed_ifaces()
            managed_objects[name] = ifaces
        return managed_objects

    def add_object(self, obj):
        self.objs.append(obj)
        name, ifaces = obj.get_managed_ifaces()
        self.InterfacesAdded(name, ifaces)

    def remove_object(self, obj):
        self.objs.remove(obj)
        name, ifaces = obj.get_managed_ifaces()
        self.InterfacesRemoved(name, ifaces.keys())

    @dbus.service.signal(IFACE_OBJECT_MANAGER, signature='oa{sa{sv}}')
    def InterfacesAdded(self, name, ifaces):
        pass

    @dbus.service.signal(IFACE_OBJECT_MANAGER, signature='oas')
    def InterfacesRemoved(self, name, ifaces):
        pass

    # ModemManager methods
    @dbus.service.method(dbus_interface=IFACE_MM, in_signature='', out_signature='')
    def ScanDevices(self):
        pass

    @dbus.service.method(dbus_interface=IFACE_MM, in_signature='s', out_signature='')
    def SetLogging(self, logging):
        pass

    # Testing methods
    @dbus.service.method(IFACE_TEST, in_signature='', out_signature='')
    def Quit(self):
        mainloop.quit()

    @dbus.service.method(IFACE_TEST, in_signature='bs', out_signature='o')
    def AddModem(self, add_sim, iccid):
        self.modem = Modem(self.bus, add_sim, iccid)
        return dbus.ObjectPath(self.modem.path)

    @dbus.service.method(IFACE_TEST, in_signature='iiu', out_signature='')
    def EmitStateChanged(self, old_state, new_state, reason):
        if self.modem is not None:
            self.modem.StateChanged(old_state, new_state, reason)

    @dbus.service.method(IFACE_TEST, in_signature='ub', out_signature='')
    def SetMobileEquipmentError(self, error, clear):
        if self.modem is not None:
            if clear:
                self.modem.equipmentError = None
            else:
                self.modem.equipmentError = error

    @dbus.service.method(IFACE_TEST, in_signature='bb', out_signature='')
    def SetResetStatus(self, status, clear):
        if self.modem is not None:
            self.modem.reset_status = status
            self.modem.reset_status_clear = clear

    @dbus.service.method(dbus_interface=IFACE_TEST, in_signature='', out_signature='')
    def Restart(self):
        bus.release_name("org.freedesktop.ModemManager1")
        bus.request_name("org.freedesktop.ModemManager1")

###################################################################
def stdin_cb(io, condition):
    mainloop.quit()

def quit_cb(user_data):
    mainloop.quit()

def main():
    parser = argparse.ArgumentParser(description="ModemManager dbus interface stub utility")
    parser.add_argument("-f", "--log-file", help="Path of a file to log things into")

    cfg = parser.parse_args()

    global log_file

    if cfg.log_file:
        try:
            log_file = open(cfg.log_file, "w")
        except Exception:
            log_file = None
    else:
        log_file = None

    log("Starting mainloop")
    dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)

    random.seed()

    global object_manager, bus

    bus = dbus.SessionBus()
    log("Creating object manager for /org/freedesktop/ModemManager1")
    object_manager = ObjectManager(bus, "/org/freedesktop/ModemManager1")

    log("Requesting name org.freedesktop.ModemManager1")
    if not bus.request_name("org.freedesktop.ModemManager1"):
        log("Unable to acquire the DBus name")
        sys.exit(1)

    # Watch stdin; if it closes, assume our parent has crashed, and exit
    id1 = GLib.io_add_watch(0, GLib.IOCondition.HUP, stdin_cb)

    log("Starting the main loop")
    try:
        mainloop.run()
    except (Exception, KeyboardInterrupt):
        pass

    GLib.source_remove(id1)

    log("Ending the stub")
    if log_file:
        log_file.close()
    sys.exit(0)


if __name__ == '__main__':
    main()
