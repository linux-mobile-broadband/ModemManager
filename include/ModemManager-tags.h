/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * ModemManager Interface Specification
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 * Copyright (C) 2018 Aleksander Morgado <aleksander@aleksander.es>
 */

/*
 * NOTE! this file is NOT part of the installed ModemManager API.
 *
 * We keep this file under include/ because we want to build and
 * expose the associated documentation.
 */

#ifndef MM_TAGS_H
#define MM_TAGS_H

/**
 * SECTION:mm-tags
 * @short_description: generic udev tags supported
 *
 * This section defines generic udev tags that are used by ModemManager,
 * associated to full devices or to specific ports in a given device.
 */

/**
 * ID_MM_CANDIDATE:
 *
 * This is a port-specific tag added automatically when all other
 * ModemManager related tags have already been set.
 *
 * Since: 1.10
 */
#define ID_MM_CANDIDATE "ID_MM_CANDIDATE"

/**
 * ID_MM_PHYSDEV_UID:
 *
 * This is a device-specific tag that allows users to 'name' modem
 * devices with a predefined 'unique ID' string.
 *
 * When this tag is given per-port, the daemon will consider that all
 * ports with the same UID value are associated to the same device.
 * This is useful for e.g. modems that expose multiple RS232 ports
 * connected to the system via different platform ports (or USB to
 * RS232 adapters).
 *
 * This UID is exposed in
 * the '<link linkend="gdbus-property-org-freedesktop-ModemManager1-Modem.Device">Device</link>'
 * property and can then be used in mmcli calls to refer unequivocally
 * to a specific device, regardless of its modem index, e.g.:
 *  $ mmcli --modem=UID ...
 *
 * Since: 1.10
 */
#define ID_MM_PHYSDEV_UID "ID_MM_PHYSDEV_UID"

/**
 * ID_MM_DEVICE_PROCESS:
 *
 * This is a device-specific tag that allows explicitly requesting the
 * processing of all ports exposed by the device. This tag is usually
 * used by users when the daemon runs with ALLOWLIST-ONLY filter policy
 * type, and is associated to the MM_FILTER_RULE_EXPLICIT_ALLOWLIST rule.
 *
 * This tag may also be specified in specific ports, e.g. when the modem
 * exposes a single platform port without any parent device.
 *
 * Since: 1.10
 */
#define ID_MM_DEVICE_PROCESS "ID_MM_DEVICE_PROCESS"

/**
 * ID_MM_DEVICE_IGNORE:
 *
 * This is a device-specific tag that allows explicitly requesting to
 * ignore all ports exposed by the device.
 *
 * This tag was originally applicable to TTY ports and only when running
 * in certain filter policy types. Since 1.12, this tag applies to all
 * filter types and to all port types (not only TTYs), and is associated
 * to the MM_FILTER_RULE_EXPLICIT_BLOCKLIST rule.
 *
 * Since: 1.10
 */
#define ID_MM_DEVICE_IGNORE "ID_MM_DEVICE_IGNORE"

/**
 * ID_MM_PORT_IGNORE:
 *
 * This is a port-specific tag that allows explicitly ignoring a given port
 * in a device.
 *
 * This tag applies to all types of ports.
 *
 * Since: 1.10
 */
#define ID_MM_PORT_IGNORE "ID_MM_PORT_IGNORE"

/**
 * ID_MM_PORT_TYPE_AT_PRIMARY:
 *
 * This is a port-specific tag applied to TTYs that we know in advance
 * are AT ports to be used as primary control ports.
 *
 * This tag will also prevent QCDM probing on the port.
 *
 * Since: 1.10
 */
#define ID_MM_PORT_TYPE_AT_PRIMARY "ID_MM_PORT_TYPE_AT_PRIMARY"

/**
 * ID_MM_PORT_TYPE_AT_SECONDARY:
 *
 * This is a port-specific tag applied to TTYs that we know in advance
 * are AT ports to be used as secondary control ports.
 *
 * This tag will also prevent QCDM probing on the port.
 *
 * Since: 1.10
 */
#define ID_MM_PORT_TYPE_AT_SECONDARY "ID_MM_PORT_TYPE_AT_SECONDARY"

/**
 * ID_MM_PORT_TYPE_AT_GPS_CONTROL:
 *
 * This is a port-specific tag applied to TTYs that we know in advance
 * are AT ports to be used for GPS control. Depending on the device,
 * this may or may not mean the same port is also used fo GPS data.
 *
 * This tag will also prevent QCDM probing on the port.
 *
 * Since: 1.20
 */
#define ID_MM_PORT_TYPE_AT_GPS_CONTROL "ID_MM_PORT_TYPE_AT_GPS_CONTROL"

/**
 * ID_MM_PORT_TYPE_AT_PPP:
 *
 * This is a port-specific tag applied to TTYs that we know in advance
 * are AT ports to be used as data ports exclusively.
 *
 * This tag will also prevent QCDM probing on the port.
 *
 * Since: 1.10
 */
#define ID_MM_PORT_TYPE_AT_PPP "ID_MM_PORT_TYPE_AT_PPP"

/**
 * ID_MM_PORT_TYPE_QCDM:
 *
 * This is a port-specific tag applied to TTYs that we know in advance
 * are QCDM ports.
 *
 * The only purpose of this tag is to prevent AT probing in the port.
 *
 * Since: 1.10
 */
#define ID_MM_PORT_TYPE_QCDM "ID_MM_PORT_TYPE_QCDM"

/**
 * ID_MM_PORT_TYPE_GPS:
 *
 * This is a port-specific tag applied to TTYs that we know in advance
 * are GPS data ports where we expect to receive NMEA traces.
 *
 * This tag also prevents AT and QCDM probing in the port.
 *
 * Since: 1.10
 */
#define ID_MM_PORT_TYPE_GPS "ID_MM_PORT_TYPE_GPS"

/**
 * ID_MM_PORT_TYPE_AUDIO:
 *
 * This is a port-specific tag applied to TTYs that we know in advance
 * are audio ports.
 *
 * This tag also prevents AT and QCDM probing in the port.
 *
 * Since: 1.12
 */
#define ID_MM_PORT_TYPE_AUDIO "ID_MM_PORT_TYPE_AUDIO"

/**
 * ID_MM_PORT_TYPE_QMI:
 *
 * This is a port-specific tag applied to generic ports that we know in advance
 * are QMI ports.
 *
 * This tag will also prevent other types of probing (e.g. AT, MBIM) on the
 * port.
 *
 * This tag is not required for QMI ports exposed by the qmi_wwan driver.
 *
 * Since: 1.16
 */
#define ID_MM_PORT_TYPE_QMI "ID_MM_PORT_TYPE_QMI"

/**
 * ID_MM_PORT_TYPE_MBIM:
 *
 * This is a port-specific tag applied to generic ports that we know in advance
 * are MBIM ports.
 *
 * This tag will also prevent other types of probing (e.g. AT, QMI) on the
 * port.
 *
 * This tag is not required for MBIM ports exposed by the cdc_mbim driver.
 *
 * Since: 1.16
 */
#define ID_MM_PORT_TYPE_MBIM "ID_MM_PORT_TYPE_MBIM"

/**
 * ID_MM_PORT_TYPE_XMMRPC:
 *
 * This is a port-specific tag applied to generic ports that we know in advance
 * are RPC control ports for Intel XMM modems.
 *
 * This tag will also prevent other types of probing (e.g. AT, QMI) on the
 * port.
 *
 * Since: 1.24
 */
#define ID_MM_PORT_TYPE_XMMRPC "ID_MM_PORT_TYPE_XMMRPC"

/**
 * ID_MM_TTY_BAUDRATE:
 *
 * This is a port-specific tag applied to TTYs that require a specific
 * baudrate to work. USB modems will usually allow auto-bauding
 * configuration, so this tag is really only meaningful to true RS232
 * devices.
 *
 * The value of the tag should be the number of bauds per second to
 * use when talking to the port, e.g. "115200". If not given, the
 * default of 57600bps is assumed.
 *
 * Since: 1.10
 */
#define ID_MM_TTY_BAUDRATE "ID_MM_TTY_BAUDRATE"

/**
 * ID_MM_TTY_FLOW_CONTROL:
 *
 * This is a port-specific tag applied to TTYs that require a specific
 * flow control mechanism to work not only in data mode but also in
 * control mode.
 *
 * The value of the tag should be either 'none', 'xon-xoff' or
 * 'rts-cts', and must be a flow control value supported by the device
 * where it's configured. If not given, it is assumed that the TTYs
 * don't require any specific flow control setting in command mode.
 *
 * Since: 1.10
 */
#define ID_MM_TTY_FLOW_CONTROL "ID_MM_TTY_FLOW_CONTROL"

/**
 * ID_MM_REQUIRED:
 *
 * This is a port-specific tag that allows users to specify that the modem
 * must be able to successfully probe and use the given control port.
 *
 * If this tag is set and the port probing procedure fails, the modem object
 * will not be created, which is the same as if the port didn't exist in the
 * first place.
 *
 * E.g. this tag may be set on a QMI control port if we want to make sure the
 * modem object exposed by ModemManager is QMI-capable and never an AT-based
 * modem created due to falling back on a failed QMI port probing procedure.
 *
 * Since: 1.22
 */
#define ID_MM_REQUIRED "ID_MM_REQUIRED"

/**
 * ID_MM_MAX_MULTIPLEXED_LINKS:
 *
 * This is a device-specific tag that allows users to specify the maximum amount
 * of multiplexed links the modem supports.
 *
 * An integer value greater or equal than 0 must be given. The value 0 in this
 * tag completely disables the multiplexing support in the device.
 *
 * This setting does nothing if the modem doesn't support multiplexing, or if the
 * value configured is greater than the one specified by the modem itself (e.g.
 * the control protocol in use also limits this value).
 *
 * Since: 1.22
 */
#define ID_MM_MAX_MULTIPLEXED_LINKS "ID_MM_MAX_MULTIPLEXED_LINKS"

/**
 * ID_MM_TTY_AT_PROBE_TRIES:
 *
 * For ports that require a longer time to become ready to respond to AT
 * commands, this tag specifies maximum number of AT probes to try as an
 * integer between 1 and 20 (inclusive). Each probe attempt has a three-second
 * timeout before the next probe is tried. Values outside the allowed range
 * will be clamped to the min/max.
 *
 * Plugins implementing custom initialization without opting into this tag
 * will ignore it.
 *
 * Since: 1.24
 */
#define ID_MM_TTY_AT_PROBE_TRIES "ID_MM_TTY_AT_PROBE_TRIES"

/*
 * The following symbols are deprecated. We don't add them to -compat
 * because this -tags file is not really part of the installed API.
 */

#ifndef MM_DISABLE_DEPRECATED

 /**
  * ID_MM_TTY_BLACKLIST:
  *
  * This was a device-specific tag that allowed explicitly blacklisting
  * devices that exposed TTY devices so that they were never probed.
  *
  * This tag was applicable only when running in certain filter policy types,
  * and is no longer used since 1.18.
  *
  * Since: 1.12
  * Deprecated: 1.18.0
  */
#define ID_MM_TTY_BLACKLIST "ID_MM_TTY_BLACKLIST"

/**
 * ID_MM_TTY_MANUAL_SCAN_ONLY:
 *
 * This was a device-specific tag that allowed explicitly allowlisting
 * devices that exposed TTY devices so that they were never probed
 * automatically. Instead, an explicit manual scan request could
 * be sent to the daemon so that the TTY ports exposed by the device
 * were probed.
 *
 * This tag was applicable only when running in certain filter policy types,
 * and is no longer used since 1.18.
 *
 * Since: 1.12
 * Deprecated: 1.18.0
 */
#define ID_MM_TTY_MANUAL_SCAN_ONLY "ID_MM_TTY_MANUAL_SCAN_ONLY"

#endif

#endif /* MM_TAGS_H */
