/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * mmcli -- Control modem status & access information from the command line
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Copyright (C) 2018 Aleksander Morgado <aleksander@aleksander.es>
 */

#include <stdio.h>
#include <string.h>

#include <libmm-glib.h>
#include "mm-common-helpers.h"
#include "mmcli-output.h"

/******************************************************************************/
/* List of sections (grouped fields) displayed in the human-friendly output */

typedef struct {
    const gchar *name;
} SectionInfo;

static SectionInfo section_infos[] = {
    [MMC_S_MODEM_GENERAL]           = { "General"            },
    [MMC_S_MODEM_HARDWARE]          = { "Hardware"           },
    [MMC_S_MODEM_SYSTEM]            = { "System"             },
    [MMC_S_MODEM_NUMBERS]           = { "Numbers"            },
    [MMC_S_MODEM_STATUS]            = { "Status"             },
    [MMC_S_MODEM_MODES]             = { "Modes"              },
    [MMC_S_MODEM_BANDS]             = { "Bands"              },
    [MMC_S_MODEM_IP]                = { "IP"                 },
    [MMC_S_MODEM_3GPP]              = { "3GPP"               },
    [MMC_S_MODEM_3GPP_EPS]          = { "3GPP EPS"           },
    [MMC_S_MODEM_3GPP_SCAN]         = { "3GPP scan"          },
    [MMC_S_MODEM_3GPP_USSD]         = { "3GPP USSD"          },
    [MMC_S_MODEM_CDMA]              = { "CDMA"               },
    [MMC_S_MODEM_SIM]               = { "SIM"                },
    [MMC_S_MODEM_BEARER]            = { "Bearer"             },
    [MMC_S_MODEM_TIME]              = { "Time"               },
    [MMC_S_MODEM_TIMEZONE]          = { "Timezone"           },
    [MMC_S_MODEM_MESSAGING]         = { "Messaging"          },
    [MMC_S_MODEM_SIGNAL]            = { "Signal"             },
    [MMC_S_MODEM_SIGNAL_CDMA1X]     = { "CDMA1x"             },
    [MMC_S_MODEM_SIGNAL_EVDO]       = { "EV-DO"              },
    [MMC_S_MODEM_SIGNAL_GSM]        = { "GSM"                },
    [MMC_S_MODEM_SIGNAL_UMTS]       = { "UMTS"               },
    [MMC_S_MODEM_SIGNAL_LTE]        = { "LTE"                },
    [MMC_S_MODEM_SIGNAL_5G]         = { "5G"                 },
    [MMC_S_MODEM_OMA]               = { "OMA"                },
    [MMC_S_MODEM_OMA_CURRENT]       = { "Current session"    },
    [MMC_S_MODEM_OMA_PENDING]       = { "Pending sessions"   },
    [MMC_S_MODEM_LOCATION]          = { "Location"           },
    [MMC_S_MODEM_LOCATION_3GPP]     = { "3GPP"               },
    [MMC_S_MODEM_LOCATION_GPS]      = { "GPS"                },
    [MMC_S_MODEM_LOCATION_CDMABS]   = { "CDMA BS"            },
    [MMC_S_MODEM_FIRMWARE]          = { "Firmware"           },
    [MMC_S_MODEM_FIRMWARE_FASTBOOT] = { "Fastboot settings"  },
    [MMC_S_MODEM_VOICE]             = { "Voice"              },
    [MMC_S_BEARER_GENERAL]          = { "General"            },
    [MMC_S_BEARER_STATUS]           = { "Status"             },
    [MMC_S_BEARER_PROPERTIES]       = { "Properties"         },
    [MMC_S_BEARER_IPV4_CONFIG]      = { "IPv4 configuration" },
    [MMC_S_BEARER_IPV6_CONFIG]      = { "IPv6 configuration" },
    [MMC_S_BEARER_STATS]            = { "Statistics"         },
    [MMC_S_CALL_GENERAL]            = { "General"            },
    [MMC_S_CALL_PROPERTIES]         = { "Properties"         },
    [MMC_S_CALL_AUDIO_FORMAT]       = { "Audio format"       },
    [MMC_S_SMS_GENERAL]             = { "General"            },
    [MMC_S_SMS_CONTENT]             = { "Content"            },
    [MMC_S_SMS_PROPERTIES]          = { "Properties"         },
    [MMC_S_SIM_GENERAL]             = { "General"            },
    [MMC_S_SIM_PROPERTIES]          = { "Properties"         },
};

/******************************************************************************/
/* List of fields */

typedef struct {
    const gchar  *key;
    const gchar  *name;
    MmcS          section;
} FieldInfo;

static FieldInfo field_infos[] = {
    [MMC_F_GENERAL_DBUS_PATH]                 = { "modem.dbus-path",                                 "path",                     MMC_S_MODEM_GENERAL,           },
    [MMC_F_GENERAL_DEVICE_ID]                 = { "modem.generic.device-identifier",                 "device id",                MMC_S_MODEM_GENERAL,           },
    [MMC_F_HARDWARE_MANUFACTURER]             = { "modem.generic.manufacturer",                      "manufacturer",             MMC_S_MODEM_HARDWARE,          },
    [MMC_F_HARDWARE_MODEL]                    = { "modem.generic.model",                             "model",                    MMC_S_MODEM_HARDWARE,          },
    [MMC_F_HARDWARE_REVISION]                 = { "modem.generic.revision",                          "firmware revision",        MMC_S_MODEM_HARDWARE,          },
    [MMC_F_HARDWARE_CARRIER_CONF]             = { "modem.generic.carrier-configuration",             "carrier config",           MMC_S_MODEM_HARDWARE,          },
    [MMC_F_HARDWARE_CARRIER_CONF_REV]         = { "modem.generic.carrier-configuration-revision",    "carrier config revision",  MMC_S_MODEM_HARDWARE,          },
    [MMC_F_HARDWARE_HW_REVISION]              = { "modem.generic.hardware-revision",                 "h/w revision",             MMC_S_MODEM_HARDWARE,          },
    [MMC_F_HARDWARE_SUPPORTED_CAPABILITIES]   = { "modem.generic.supported-capabilities",            "supported",                MMC_S_MODEM_HARDWARE,          },
    [MMC_F_HARDWARE_CURRENT_CAPABILITIES]     = { "modem.generic.current-capabilities",              "current",                  MMC_S_MODEM_HARDWARE,          },
    [MMC_F_HARDWARE_EQUIPMENT_ID]             = { "modem.generic.equipment-identifier",              "equipment id",             MMC_S_MODEM_HARDWARE,          },
    [MMC_F_SYSTEM_DEVICE]                     = { "modem.generic.device",                            "device",                   MMC_S_MODEM_SYSTEM,            },
    [MMC_F_SYSTEM_DRIVERS]                    = { "modem.generic.drivers",                           "drivers",                  MMC_S_MODEM_SYSTEM,            },
    [MMC_F_SYSTEM_PLUGIN]                     = { "modem.generic.plugin",                            "plugin",                   MMC_S_MODEM_SYSTEM,            },
    [MMC_F_SYSTEM_PRIMARY_PORT]               = { "modem.generic.primary-port",                      "primary port",             MMC_S_MODEM_SYSTEM,            },
    [MMC_F_SYSTEM_PORTS]                      = { "modem.generic.ports",                             "ports",                    MMC_S_MODEM_SYSTEM,            },
    [MMC_F_NUMBERS_OWN]                       = { "modem.generic.own-numbers",                       "own",                      MMC_S_MODEM_NUMBERS,           },
    [MMC_F_STATUS_LOCK]                       = { "modem.generic.unlock-required",                   "lock",                     MMC_S_MODEM_STATUS,            },
    [MMC_F_STATUS_UNLOCK_RETRIES]             = { "modem.generic.unlock-retries",                    "unlock retries",           MMC_S_MODEM_STATUS,            },
    [MMC_F_STATUS_STATE]                      = { "modem.generic.state",                             "state",                    MMC_S_MODEM_STATUS,            },
    [MMC_F_STATUS_FAILED_REASON]              = { "modem.generic.state-failed-reason",               "failed reason",            MMC_S_MODEM_STATUS,            },
    [MMC_F_STATUS_POWER_STATE]                = { "modem.generic.power-state",                       "power state",              MMC_S_MODEM_STATUS,            },
    [MMC_F_STATUS_ACCESS_TECH]                = { "modem.generic.access-technologies",               "access tech",              MMC_S_MODEM_STATUS,            },
    [MMC_F_STATUS_SIGNAL_QUALITY_VALUE]       = { "modem.generic.signal-quality.value",              "signal quality",           MMC_S_MODEM_STATUS,            },
    [MMC_F_STATUS_SIGNAL_QUALITY_RECENT]      = { "modem.generic.signal-quality.recent",             NULL,                       MMC_S_UNKNOWN,                 },
    [MMC_F_MODES_SUPPORTED]                   = { "modem.generic.supported-modes",                   "supported",                MMC_S_MODEM_MODES,             },
    [MMC_F_MODES_CURRENT]                     = { "modem.generic.current-modes",                     "current",                  MMC_S_MODEM_MODES,             },
    [MMC_F_BANDS_SUPPORTED]                   = { "modem.generic.supported-bands",                   "supported",                MMC_S_MODEM_BANDS,             },
    [MMC_F_BANDS_CURRENT]                     = { "modem.generic.current-bands",                     "current",                  MMC_S_MODEM_BANDS,             },
    [MMC_F_IP_SUPPORTED]                      = { "modem.generic.supported-ip-families",             "supported",                MMC_S_MODEM_IP,                },
    [MMC_F_3GPP_IMEI]                         = { "modem.3gpp.imei",                                 "imei",                     MMC_S_MODEM_3GPP,              },
    [MMC_F_3GPP_ENABLED_LOCKS]                = { "modem.3gpp.enabled-locks",                        "enabled locks",            MMC_S_MODEM_3GPP,              },
    [MMC_F_3GPP_OPERATOR_ID]                  = { "modem.3gpp.operator-code",                        "operator id",              MMC_S_MODEM_3GPP,              },
    [MMC_F_3GPP_OPERATOR_NAME]                = { "modem.3gpp.operator-name",                        "operator name",            MMC_S_MODEM_3GPP,              },
    [MMC_F_3GPP_REGISTRATION]                 = { "modem.3gpp.registration-state",                   "registration",             MMC_S_MODEM_3GPP,              },
    [MMC_F_3GPP_PCO]                          = { "modem.3gpp.pco",                                  "pco",                      MMC_S_MODEM_3GPP,              },
    [MMC_F_3GPP_EPS_UE_MODE]                  = { "modem.3gpp.eps.ue-mode-operation",                "ue mode of operation",     MMC_S_MODEM_3GPP_EPS,          },
    [MMC_F_3GPP_EPS_INITIAL_BEARER_PATH]      = { "modem.3gpp.eps.initial-bearer.dbus-path",         "initial bearer path",      MMC_S_MODEM_3GPP_EPS,          },
    [MMC_F_3GPP_EPS_BEARER_SETTINGS_APN]      = { "modem.3gpp.eps.initial-bearer.settings.apn",      "initial bearer apn",       MMC_S_MODEM_3GPP_EPS,          },
    [MMC_F_3GPP_EPS_BEARER_SETTINGS_IP_TYPE]  = { "modem.3gpp.eps.initial-bearer.settings.ip-type",  "initial bearer ip type",   MMC_S_MODEM_3GPP_EPS,          },
    [MMC_F_3GPP_EPS_BEARER_SETTINGS_USER]     = { "modem.3gpp.eps.initial-bearer.settings.user",     "initial bearer user",      MMC_S_MODEM_3GPP_EPS,          },
    [MMC_F_3GPP_EPS_BEARER_SETTINGS_PASSWORD] = { "modem.3gpp.eps.initial-bearer.settings.password", "initial bearer password",  MMC_S_MODEM_3GPP_EPS,          },
    [MMC_F_3GPP_SCAN_NETWORKS]                = { "modem.3gpp.scan-networks",                        "networks",                 MMC_S_MODEM_3GPP_SCAN,         },
    [MMC_F_3GPP_USSD_STATUS]                  = { "modem.3gpp.ussd.status",                          "status",                   MMC_S_MODEM_3GPP_USSD,         },
    [MMC_F_3GPP_USSD_NETWORK_REQUEST]         = { "modem.3gpp.ussd.network-request",                 "network request",          MMC_S_MODEM_3GPP_USSD,         },
    [MMC_F_3GPP_USSD_NETWORK_NOTIFICATION]    = { "modem.3gpp.ussd.network-notification",            "network notification",     MMC_S_MODEM_3GPP_USSD,         },
    [MMC_F_CDMA_MEID]                         = { "modem.cdma.meid",                                 "meid",                     MMC_S_MODEM_CDMA,              },
    [MMC_F_CDMA_ESN]                          = { "modem.cdma.esn",                                  "esn",                      MMC_S_MODEM_CDMA,              },
    [MMC_F_CDMA_SID]                          = { "modem.cdma.sid",                                  "sid",                      MMC_S_MODEM_CDMA,              },
    [MMC_F_CDMA_NID]                          = { "modem.cdma.nid",                                  "nid",                      MMC_S_MODEM_CDMA,              },
    [MMC_F_CDMA_REGISTRATION_CDMA1X]          = { "modem.cdma.cdma1x-registration-state",            "registration cdma1x",      MMC_S_MODEM_CDMA,              },
    [MMC_F_CDMA_REGISTRATION_EVDO]            = { "modem.cdma.evdo-registration-state",              "registration evdo",        MMC_S_MODEM_CDMA,              },
    [MMC_F_CDMA_ACTIVATION]                   = { "modem.cdma.activation-state",                     "activation",               MMC_S_MODEM_CDMA,              },
    [MMC_F_SIM_PATH]                          = { "modem.generic.sim",                               "primary sim path",         MMC_S_MODEM_SIM,               },
    [MMC_F_SIM_PRIMARY_SLOT]                  = { "modem.generic.primary-sim-slot",                  NULL,                       MMC_S_MODEM_SIM,               },
    [MMC_F_SIM_SLOT_PATHS]                    = { "modem.generic.sim-slots",                         "sim slot paths",           MMC_S_MODEM_SIM,               },
    [MMC_F_BEARER_PATHS]                      = { "modem.generic.bearers",                           "paths",                    MMC_S_MODEM_BEARER,            },
    [MMC_F_TIME_CURRENT]                      = { "modem.time.current",                              "current",                  MMC_S_MODEM_TIME,              },
    [MMC_F_TIMEZONE_CURRENT]                  = { "modem.timezone.current",                          "current",                  MMC_S_MODEM_TIMEZONE,          },
    [MMC_F_TIMEZONE_DST_OFFSET]               = { "modem.time.dst-offset",                           "dst offset",               MMC_S_MODEM_TIMEZONE,          },
    [MMC_F_TIMEZONE_LEAP_SECONDS]             = { "modem.time.leap-seconds",                         "leap seconds",             MMC_S_MODEM_TIMEZONE,          },
    [MMC_F_MESSAGING_SUPPORTED_STORAGES]      = { "modem.messaging.supported-storages",              "supported storages",       MMC_S_MODEM_MESSAGING,         },
    [MMC_F_MESSAGING_DEFAULT_STORAGES]        = { "modem.messaging.default-storages",                "default storages",         MMC_S_MODEM_MESSAGING,         },
    [MMC_F_SIGNAL_REFRESH_RATE]               = { "modem.signal.refresh.rate",                       "refresh rate",             MMC_S_MODEM_SIGNAL,            },
    [MMC_F_SIGNAL_CDMA1X_RSSI]                = { "modem.signal.cdma1x.rssi",                        "rssi",                     MMC_S_MODEM_SIGNAL_CDMA1X,     },
    [MMC_F_SIGNAL_CDMA1X_ECIO]                = { "modem.signal.cdma1x.ecio",                        "ecio",                     MMC_S_MODEM_SIGNAL_CDMA1X,     },
    [MMC_F_SIGNAL_EVDO_RSSI]                  = { "modem.signal.evdo.rssi",                          "rssi",                     MMC_S_MODEM_SIGNAL_EVDO,       },
    [MMC_F_SIGNAL_EVDO_ECIO]                  = { "modem.signal.evdo.ecio",                          "ecio",                     MMC_S_MODEM_SIGNAL_EVDO,       },
    [MMC_F_SIGNAL_EVDO_SINR]                  = { "modem.signal.evdo.sinr",                          "sinr",                     MMC_S_MODEM_SIGNAL_EVDO,       },
    [MMC_F_SIGNAL_EVDO_IO]                    = { "modem.signal.evdo.io",                            "io",                       MMC_S_MODEM_SIGNAL_EVDO,       },
    [MMC_F_SIGNAL_GSM_RSSI]                   = { "modem.signal.gsm.rssi",                           "rssi",                     MMC_S_MODEM_SIGNAL_GSM,        },
    [MMC_F_SIGNAL_UMTS_RSSI]                  = { "modem.signal.umts.rssi",                          "rssi",                     MMC_S_MODEM_SIGNAL_UMTS,       },
    [MMC_F_SIGNAL_UMTS_RSCP]                  = { "modem.signal.umts.rscp",                          "rscp",                     MMC_S_MODEM_SIGNAL_UMTS,       },
    [MMC_F_SIGNAL_UMTS_ECIO]                  = { "modem.signal.umts.ecio",                          "ecio",                     MMC_S_MODEM_SIGNAL_UMTS,       },
    [MMC_F_SIGNAL_LTE_RSSI]                   = { "modem.signal.lte.rssi",                           "rssi",                     MMC_S_MODEM_SIGNAL_LTE,        },
    [MMC_F_SIGNAL_LTE_RSRQ]                   = { "modem.signal.lte.rsrq",                           "rsrq",                     MMC_S_MODEM_SIGNAL_LTE,        },
    [MMC_F_SIGNAL_LTE_RSRP]                   = { "modem.signal.lte.rsrp",                           "rsrp",                     MMC_S_MODEM_SIGNAL_LTE,        },
    [MMC_F_SIGNAL_LTE_SNR]                    = { "modem.signal.lte.snr",                            "s/n",                      MMC_S_MODEM_SIGNAL_LTE,        },
    [MMC_F_SIGNAL_5G_RSRQ]                    = { "modem.signal.5g.rsrq",                            "rsrq",                     MMC_S_MODEM_SIGNAL_5G,         },
    [MMC_F_SIGNAL_5G_RSRP]                    = { "modem.signal.5g.rsrp",                            "rsrp",                     MMC_S_MODEM_SIGNAL_5G,         },
    [MMC_F_SIGNAL_5G_SNR]                     = { "modem.signal.5g.snr",                             "s/n",                      MMC_S_MODEM_SIGNAL_5G,         },
    [MMC_F_OMA_FEATURES]                      = { "modem.oma.features",                              "features",                 MMC_S_MODEM_OMA,               },
    [MMC_F_OMA_CURRENT_TYPE]                  = { "modem.oma.current.type",                          "type",                     MMC_S_MODEM_OMA_CURRENT,       },
    [MMC_F_OMA_CURRENT_STATE]                 = { "modem.oma.current.state",                         "state",                    MMC_S_MODEM_OMA_CURRENT,       },
    [MMC_F_OMA_PENDING_SESSIONS]              = { "modem.oma.pending-sessions",                      "sessions",                 MMC_S_MODEM_OMA_PENDING,       },
    [MMC_F_LOCATION_CAPABILITIES]             = { "modem.location.capabilities",                     "capabilities",             MMC_S_MODEM_LOCATION,          },
    [MMC_F_LOCATION_ENABLED]                  = { "modem.location.enabled",                          "enabled",                  MMC_S_MODEM_LOCATION,          },
    [MMC_F_LOCATION_SIGNALS]                  = { "modem.location.signals",                          "signals",                  MMC_S_MODEM_LOCATION,          },
    [MMC_F_LOCATION_GPS_REFRESH_RATE]         = { "modem.location.gps.refresh-rate",                 "refresh rate",             MMC_S_MODEM_LOCATION_GPS,      },
    [MMC_F_LOCATION_GPS_SUPL_SERVER]          = { "modem.location.gps.supl-server",                  "a-gps supl server",        MMC_S_MODEM_LOCATION_GPS,      },
    [MMC_F_LOCATION_GPS_ASSISTANCE]           = { "modem.location.gps.assistance",                   "supported assistance",     MMC_S_MODEM_LOCATION_GPS,      },
    [MMC_F_LOCATION_GPS_ASSISTANCE_SERVERS]   = { "modem.location.gps.assistance-servers",           "assistance servers",       MMC_S_MODEM_LOCATION_GPS,      },
    [MMC_F_LOCATION_3GPP_MCC]                 = { "modem.location.3gpp.mcc",                         "operator code",            MMC_S_MODEM_LOCATION_3GPP,     },
    [MMC_F_LOCATION_3GPP_MNC]                 = { "modem.location.3gpp.mnc",                         "operator name",            MMC_S_MODEM_LOCATION_3GPP,     },
    [MMC_F_LOCATION_3GPP_LAC]                 = { "modem.location.3gpp.lac",                         "location area code",       MMC_S_MODEM_LOCATION_3GPP,     },
    [MMC_F_LOCATION_3GPP_TAC]                 = { "modem.location.3gpp.tac",                         "tracking area code",       MMC_S_MODEM_LOCATION_3GPP,     },
    [MMC_F_LOCATION_3GPP_CID]                 = { "modem.location.3gpp.cid",                         "cell id",                  MMC_S_MODEM_LOCATION_3GPP,     },
    [MMC_F_LOCATION_GPS_NMEA]                 = { "modem.location.gps.nmea",                         "nmea",                     MMC_S_MODEM_LOCATION_GPS,      },
    [MMC_F_LOCATION_GPS_UTC]                  = { "modem.location.gps.utc",                          "utc",                      MMC_S_MODEM_LOCATION_GPS,      },
    [MMC_F_LOCATION_GPS_LONG]                 = { "modem.location.gps.longitude",                    "longitude",                MMC_S_MODEM_LOCATION_GPS,      },
    [MMC_F_LOCATION_GPS_LAT]                  = { "modem.location.gps.latitude",                     "latitude",                 MMC_S_MODEM_LOCATION_GPS,      },
    [MMC_F_LOCATION_GPS_ALT]                  = { "modem.location.gps.altitude",                     "altitude",                 MMC_S_MODEM_LOCATION_GPS,      },
    [MMC_F_LOCATION_CDMABS_LONG]              = { "modem.location.cdma-bs.longitude",                "longitude",                MMC_S_MODEM_LOCATION_CDMABS,   },
    [MMC_F_LOCATION_CDMABS_LAT]               = { "modem.location.cdma-bs.latitude",                 "latitude",                 MMC_S_MODEM_LOCATION_CDMABS,   },
    [MMC_F_FIRMWARE_LIST]                     = { "modem.firmware.list",                             "list",                     MMC_S_MODEM_FIRMWARE,          },
    [MMC_F_FIRMWARE_METHOD]                   = { "modem.firmware.method",                           "method",                   MMC_S_MODEM_FIRMWARE,          },
    [MMC_F_FIRMWARE_DEVICE_IDS]               = { "modem.firmware.device-ids",                       "device ids",               MMC_S_MODEM_FIRMWARE,          },
    [MMC_F_FIRMWARE_VERSION]                  = { "modem.firmware.version",                          "version",                  MMC_S_MODEM_FIRMWARE,          },
    [MMC_F_FIRMWARE_FASTBOOT_AT]              = { "modem.firmware.fastboot.at",                      "at command",               MMC_S_MODEM_FIRMWARE_FASTBOOT, },
    [MMC_F_VOICE_EMERGENCY_ONLY]              = { "modem.voice.emergency-only",                      "emergency only",           MMC_S_MODEM_VOICE,             },
    [MMC_F_BEARER_GENERAL_DBUS_PATH]          = { "bearer.dbus-path",                                "path",                     MMC_S_BEARER_GENERAL,          },
    [MMC_F_BEARER_GENERAL_TYPE]               = { "bearer.type",                                     "type",                     MMC_S_BEARER_GENERAL,          },
    [MMC_F_BEARER_STATUS_CONNECTED]           = { "bearer.status.connected",                         "connected",                MMC_S_BEARER_STATUS,           },
    [MMC_F_BEARER_STATUS_SUSPENDED]           = { "bearer.status.suspended",                         "suspended",                MMC_S_BEARER_STATUS,           },
    [MMC_F_BEARER_STATUS_INTERFACE]           = { "bearer.status.interface",                         "interface",                MMC_S_BEARER_STATUS,           },
    [MMC_F_BEARER_STATUS_IP_TIMEOUT]          = { "bearer.status.ip-timeout",                        "ip timeout",               MMC_S_BEARER_STATUS,           },
    [MMC_F_BEARER_PROPERTIES_APN]             = { "bearer.properties.apn",                           "apn",                      MMC_S_BEARER_PROPERTIES,       },
    [MMC_F_BEARER_PROPERTIES_ROAMING]         = { "bearer.properties.roaming",                       "roaming",                  MMC_S_BEARER_PROPERTIES,       },
    [MMC_F_BEARER_PROPERTIES_IP_TYPE]         = { "bearer.properties.ip-type",                       "ip type",                  MMC_S_BEARER_PROPERTIES,       },
    [MMC_F_BEARER_PROPERTIES_ALLOWED_AUTH]    = { "bearer.properties.allowed-auth",                  "allowed-auth",             MMC_S_BEARER_PROPERTIES,       },
    [MMC_F_BEARER_PROPERTIES_USER]            = { "bearer.properties.user",                          "user",                     MMC_S_BEARER_PROPERTIES,       },
    [MMC_F_BEARER_PROPERTIES_PASSWORD]        = { "bearer.properties.password",                      "password",                 MMC_S_BEARER_PROPERTIES,       },
    [MMC_F_BEARER_PROPERTIES_NUMBER]          = { "bearer.properties.number",                        "number",                   MMC_S_BEARER_PROPERTIES,       },
    [MMC_F_BEARER_PROPERTIES_RM_PROTOCOL]     = { "bearer.properties.rm-protocol",                   "rm protocol",              MMC_S_BEARER_PROPERTIES,       },
    [MMC_F_BEARER_IPV4_CONFIG_METHOD]         = { "bearer.ipv4-config.method",                       "method",                   MMC_S_BEARER_IPV4_CONFIG,      },
    [MMC_F_BEARER_IPV4_CONFIG_ADDRESS]        = { "bearer.ipv4-config.address",                      "address",                  MMC_S_BEARER_IPV4_CONFIG,      },
    [MMC_F_BEARER_IPV4_CONFIG_PREFIX]         = { "bearer.ipv4-config.prefix",                       "prefix",                   MMC_S_BEARER_IPV4_CONFIG,      },
    [MMC_F_BEARER_IPV4_CONFIG_GATEWAY]        = { "bearer.ipv4-config.gateway",                      "gateway",                  MMC_S_BEARER_IPV4_CONFIG,      },
    [MMC_F_BEARER_IPV4_CONFIG_DNS]            = { "bearer.ipv4-config.dns",                          "dns",                      MMC_S_BEARER_IPV4_CONFIG,      },
    [MMC_F_BEARER_IPV4_CONFIG_MTU]            = { "bearer.ipv4-config.mtu",                          "mtu",                      MMC_S_BEARER_IPV4_CONFIG,      },
    [MMC_F_BEARER_IPV6_CONFIG_METHOD]         = { "bearer.ipv6-config.method",                       "method",                   MMC_S_BEARER_IPV6_CONFIG,      },
    [MMC_F_BEARER_IPV6_CONFIG_ADDRESS]        = { "bearer.ipv6-config.address",                      "address",                  MMC_S_BEARER_IPV6_CONFIG,      },
    [MMC_F_BEARER_IPV6_CONFIG_PREFIX]         = { "bearer.ipv6-config.prefix",                       "prefix",                   MMC_S_BEARER_IPV6_CONFIG,      },
    [MMC_F_BEARER_IPV6_CONFIG_GATEWAY]        = { "bearer.ipv6-config.gateway",                      "gateway",                  MMC_S_BEARER_IPV6_CONFIG,      },
    [MMC_F_BEARER_IPV6_CONFIG_DNS]            = { "bearer.ipv6-config.dns",                          "dns",                      MMC_S_BEARER_IPV6_CONFIG,      },
    [MMC_F_BEARER_IPV6_CONFIG_MTU]            = { "bearer.ipv6-config.mtu",                          "mtu",                      MMC_S_BEARER_IPV6_CONFIG,      },
    [MMC_F_BEARER_STATS_DURATION]             = { "bearer.stats.duration",                           "duration",                 MMC_S_BEARER_STATS,            },
    [MMC_F_BEARER_STATS_BYTES_RX]             = { "bearer.stats.bytes-rx",                           "bytes rx",                 MMC_S_BEARER_STATS,            },
    [MMC_F_BEARER_STATS_BYTES_TX]             = { "bearer.stats.bytes-tx",                           "bytes tx",                 MMC_S_BEARER_STATS,            },
    [MMC_F_BEARER_STATS_ATTEMPTS]             = { "bearer.stats.attempts",                           "attempts",                 MMC_S_BEARER_STATS,            },
    [MMC_F_BEARER_STATS_FAILED_ATTEMPTS]      = { "bearer.stats.failed-attempts",                    "attempts",                 MMC_S_BEARER_STATS,            },
    [MMC_F_BEARER_STATS_TOTAL_DURATION]       = { "bearer.stats.total-duration",                     "total-duration",           MMC_S_BEARER_STATS,            },
    [MMC_F_BEARER_STATS_TOTAL_BYTES_RX]       = { "bearer.stats.total-bytes-rx",                     "total-bytes rx",           MMC_S_BEARER_STATS,            },
    [MMC_F_BEARER_STATS_TOTAL_BYTES_TX]       = { "bearer.stats.total-bytes-tx",                     "total-bytes tx",           MMC_S_BEARER_STATS,            },
    [MMC_F_CALL_GENERAL_DBUS_PATH]            = { "call.dbus-path",                                  "path",                     MMC_S_CALL_GENERAL,            },
    [MMC_F_CALL_PROPERTIES_NUMBER]            = { "call.properties.number",                          "number",                   MMC_S_CALL_PROPERTIES,         },
    [MMC_F_CALL_PROPERTIES_DIRECTION]         = { "call.properties.direction",                       "direction",                MMC_S_CALL_PROPERTIES,         },
    [MMC_F_CALL_PROPERTIES_MULTIPARTY]        = { "call.properties.multiparty",                      "multiparty",               MMC_S_CALL_PROPERTIES,         },
    [MMC_F_CALL_PROPERTIES_STATE]             = { "call.properties.state",                           "state",                    MMC_S_CALL_PROPERTIES,         },
    [MMC_F_CALL_PROPERTIES_STATE_REASON]      = { "call.properties.state-reason",                    "state reason",             MMC_S_CALL_PROPERTIES,         },
    [MMC_F_CALL_PROPERTIES_AUDIO_PORT]        = { "call.properties.audio-port",                      "audio port",               MMC_S_CALL_PROPERTIES,         },
    [MMC_F_CALL_AUDIO_FORMAT_ENCODING]        = { "call.audio-format.encoding",                      "encoding",                 MMC_S_CALL_AUDIO_FORMAT,       },
    [MMC_F_CALL_AUDIO_FORMAT_RESOLUTION]      = { "call.audio-format.resolution",                    "resolution",               MMC_S_CALL_AUDIO_FORMAT,       },
    [MMC_F_CALL_AUDIO_FORMAT_RATE]            = { "call.audio-format.rate",                          "rate",                     MMC_S_CALL_AUDIO_FORMAT,       },
    [MMC_F_SMS_GENERAL_DBUS_PATH]             = { "sms.dbus-path",                                   "path",                     MMC_S_SMS_GENERAL,             },
    [MMC_F_SMS_CONTENT_NUMBER]                = { "sms.content.number",                              "number",                   MMC_S_SMS_CONTENT,             },
    [MMC_F_SMS_CONTENT_TEXT]                  = { "sms.content.text",                                "text",                     MMC_S_SMS_CONTENT,             },
    [MMC_F_SMS_CONTENT_DATA]                  = { "sms.content.data",                                "data",                     MMC_S_SMS_CONTENT,             },
    [MMC_F_SMS_PROPERTIES_PDU_TYPE]           = { "sms.properties.pdu-type",                         "pdu type",                 MMC_S_SMS_PROPERTIES,          },
    [MMC_F_SMS_PROPERTIES_STATE]              = { "sms.properties.state",                            "state",                    MMC_S_SMS_PROPERTIES,          },
    [MMC_F_SMS_PROPERTIES_VALIDITY]           = { "sms.properties.validity",                         "validity",                 MMC_S_SMS_PROPERTIES,          },
    [MMC_F_SMS_PROPERTIES_STORAGE]            = { "sms.properties.storage",                          "storage",                  MMC_S_SMS_PROPERTIES,          },
    [MMC_F_SMS_PROPERTIES_SMSC]               = { "sms.properties.smsc",                             "smsc",                     MMC_S_SMS_PROPERTIES,          },
    [MMC_F_SMS_PROPERTIES_CLASS]              = { "sms.properties.class",                            "class",                    MMC_S_SMS_PROPERTIES,          },
    [MMC_F_SMS_PROPERTIES_TELESERVICE_ID]     = { "sms.properties.teleservice-id",                   "teleservice id",           MMC_S_SMS_PROPERTIES,          },
    [MMC_F_SMS_PROPERTIES_SERVICE_CATEGORY]   = { "sms.properties.service-category",                 "service category",         MMC_S_SMS_PROPERTIES,          },
    [MMC_F_SMS_PROPERTIES_DELIVERY_REPORT]    = { "sms.properties.delivery-report",                  "delivery report",          MMC_S_SMS_PROPERTIES,          },
    [MMC_F_SMS_PROPERTIES_MSG_REFERENCE]      = { "sms.properties.message-reference",                "message reference",        MMC_S_SMS_PROPERTIES,          },
    [MMC_F_SMS_PROPERTIES_TIMESTAMP]          = { "sms.properties.timestamp",                        "timestamp",                MMC_S_SMS_PROPERTIES,          },
    [MMC_F_SMS_PROPERTIES_DELIVERY_STATE]     = { "sms.properties.delivery-state",                   "delivery state",           MMC_S_SMS_PROPERTIES,          },
    [MMC_F_SMS_PROPERTIES_DISCH_TIMESTAMP]    = { "sms.properties.discharge-timestamp",              "discharge timestamp",      MMC_S_SMS_PROPERTIES,          },
    [MMC_F_SIM_GENERAL_DBUS_PATH]             = { "sim.dbus-path",                                   "path",                     MMC_S_SIM_GENERAL,             },
    [MMC_F_SIM_PROPERTIES_ACTIVE]             = { "sim.properties.active",                           "active",                   MMC_S_SIM_PROPERTIES,          },
    [MMC_F_SIM_PROPERTIES_IMSI]               = { "sim.properties.imsi",                             "imsi",                     MMC_S_SIM_PROPERTIES,          },
    [MMC_F_SIM_PROPERTIES_ICCID]              = { "sim.properties.iccid",                            "iccid",                    MMC_S_SIM_PROPERTIES,          },
    [MMC_F_SIM_PROPERTIES_EID]                = { "sim.properties.eid",                              "eid",                      MMC_S_SIM_PROPERTIES,          },
    [MMC_F_SIM_PROPERTIES_OPERATOR_ID]        = { "sim.properties.operator-code",                    "operator id",              MMC_S_SIM_PROPERTIES,          },
    [MMC_F_SIM_PROPERTIES_OPERATOR_NAME]      = { "sim.properties.operator-name",                    "operator name",            MMC_S_SIM_PROPERTIES,          },
    [MMC_F_SIM_PROPERTIES_EMERGENCY_NUMBERS]  = { "sim.properties.emergency-numbers",                "emergency numbers",        MMC_S_SIM_PROPERTIES,          },
    [MMC_F_MODEM_LIST_DBUS_PATH]              = { "modem-list",                                      "modems",                   MMC_S_UNKNOWN,                 },
    [MMC_F_SMS_LIST_DBUS_PATH]                = { "modem.messaging.sms",                             "sms messages",             MMC_S_UNKNOWN,                 },
    [MMC_F_CALL_LIST_DBUS_PATH]               = { "modem.voice.call",                                "calls",                    MMC_S_UNKNOWN,                 },
};

/******************************************************************************/
/* Output type selection */

static MmcOutputType selected_type = MMC_OUTPUT_TYPE_NONE;

void
mmcli_output_set (MmcOutputType type)
{
    selected_type = type;
}

MmcOutputType
mmcli_output_get (void)
{
    return selected_type;
}

/******************************************************************************/
/* Generic output management */

typedef enum {
    VALUE_TYPE_SINGLE,
    VALUE_TYPE_MULTIPLE,
    VALUE_TYPE_LISTITEM,
} ValueType;

typedef struct {
    MmcF      field;
    ValueType type;
} OutputItem;

typedef struct {
    OutputItem  base;
    gchar      *value;
} OutputItemSingle;

typedef struct {
    OutputItem   base;
    gchar      **values;
    gboolean     multiline;
} OutputItemMultiple;

typedef struct {
    OutputItem   base;
    gchar       *prefix;
    gchar       *value;
    gchar       *extra;
} OutputItemListitem;

static GList *output_items;

static void
output_item_free (OutputItem *item)
{
    switch (item->type) {
        case VALUE_TYPE_SINGLE:
            g_free (((OutputItemSingle *)item)->value);
            g_slice_free (OutputItemSingle, (OutputItemSingle *)item);
            break;
        case VALUE_TYPE_MULTIPLE:
            g_strfreev (((OutputItemMultiple *)item)->values);
            g_slice_free (OutputItemMultiple, (OutputItemMultiple *)item);
            break;
        case VALUE_TYPE_LISTITEM:
            g_free (((OutputItemListitem *)item)->prefix);
            g_free (((OutputItemListitem *)item)->value);
            g_free (((OutputItemListitem *)item)->extra);
            break;
        default:
            g_assert_not_reached ();
    }
}

static gboolean
filter_out_value (const gchar *value)
{
    return (!g_strcmp0 (value, "unknown") || !g_strcmp0 (value, "none"));
}

static void
output_item_new_take_single (MmcF   field,
                             gchar *value)
{
    OutputItemSingle *item;

    item = g_slice_new0 (OutputItemSingle);
    item->base.field = field;
    item->base.type = VALUE_TYPE_SINGLE;

    if (filter_out_value (value))
        g_free (value);
    else
        item->value = value;

    output_items = g_list_prepend (output_items, item);
}

static void
output_item_new_take_multiple (MmcF       field,
                               gchar    **values,
                               gboolean   multiline)
{
    OutputItemMultiple *item;

    item = g_slice_new0 (OutputItemMultiple);
    item->base.field = field;
    item->base.type = VALUE_TYPE_MULTIPLE;
    item->multiline = multiline;

    if (values && (g_strv_length (values) == 1) && filter_out_value (values[0]))
        g_strfreev (values);
    else
        item->values = values;

    output_items = g_list_prepend (output_items, item);
}

static void
output_item_new_take_listitem (MmcF   field,
                               gchar *prefix,
                               gchar *value,
                               gchar *extra)
{
    OutputItemListitem *item;

    item = g_slice_new0 (OutputItemListitem);
    item->base.field = field;
    item->base.type = VALUE_TYPE_LISTITEM;
    item->prefix = prefix;
    item->value = value;
    item->extra = extra;

    output_items = g_list_prepend (output_items, item);
}

void
mmcli_output_string_list (MmcF         field,
                          const gchar *str)
{
    gchar **split;

    split = str ? g_strsplit (str, ",", -1) : NULL;
    if (split) {
        guint i;
        for (i = 0; split[i]; i++)
            g_strstrip (split[i]);
    }

    output_item_new_take_multiple (field, split, FALSE);
}

void
mmcli_output_string_list_take (MmcF   field,
                               gchar *str)
{
    mmcli_output_string_list (field, str);
    g_free (str);
}

void
mmcli_output_string_multiline (MmcF         field,
                               const gchar *str)
{
    gchar **split;

    split = str ? g_strsplit (str, "\n", -1) : NULL;
    if (split) {
        guint i;
        for (i = 0; split[i]; i++)
            g_strstrip (split[i]);
    }

    output_item_new_take_multiple (field, split, TRUE);
}

void
mmcli_output_string_multiline_take (MmcF   field,
                                    gchar *str)
{
    mmcli_output_string_multiline (field, str);
    g_free (str);
}

void
mmcli_output_string_array (MmcF          field,
                           const gchar **strv,
                           gboolean      multiline)
{
    output_item_new_take_multiple (field, g_strdupv ((gchar **)strv), multiline);
}

void
mmcli_output_string_array_take (MmcF       field,
                                gchar    **strv,
                                gboolean   multiline)
{
    output_item_new_take_multiple (field, strv, multiline);
}

void
mmcli_output_string (MmcF         field,
                     const gchar *str)
{
    output_item_new_take_single (field, g_strdup (str));
}

void
mmcli_output_string_take (MmcF   field,
                          gchar *str)
{
    output_item_new_take_single (field, str);
}

void
mmcli_output_string_take_typed (MmcF         field,
                                gchar       *value,
                                const gchar *type)
{
    if (value && selected_type == MMC_OUTPUT_TYPE_HUMAN) {
        gchar *aux;

        aux = g_strdup_printf ("%s %s", value, type);
        g_free (value);
        output_item_new_take_single (field, aux);
        return;
    }

    output_item_new_take_single (field, value);
}

void
mmcli_output_listitem (MmcF         field,
                       const gchar *prefix,
                       const gchar *value,
                       const gchar *extra)
{
    output_item_new_take_listitem (field, g_strdup (prefix), g_strdup (value), g_strdup (extra));
}

/******************************************************************************/
/* (Custom) Signal quality output */

void
mmcli_output_signal_quality (guint    value,
                             gboolean recent)
{
    /* Merge value and recent flag in a single item in human output */
    if (selected_type == MMC_OUTPUT_TYPE_HUMAN) {
        output_item_new_take_single (MMC_F_STATUS_SIGNAL_QUALITY_VALUE,
                                     g_strdup_printf ("%u%% (%s)", value, recent ? "recent" : "cached"));
        return;
    }

    output_item_new_take_single (MMC_F_STATUS_SIGNAL_QUALITY_VALUE,
                                 g_strdup_printf ("%u", value));
    output_item_new_take_single (MMC_F_STATUS_SIGNAL_QUALITY_RECENT,
                                 g_strdup_printf ("%s", recent ? "yes" : "no"));
}

/******************************************************************************/
/* (Custom) State output */

void
mmcli_output_state (MMModemState             state,
                    MMModemStateFailedReason reason)
{
#define KNRM "\x1B[0m"
#define KRED "\x1B[31m"
#define KGRN "\x1B[32m"
#define KYEL "\x1B[33m"

    if (selected_type == MMC_OUTPUT_TYPE_HUMAN) {
        if (state == MM_MODEM_STATE_FAILED)
            output_item_new_take_single (MMC_F_STATUS_STATE, g_strdup_printf (KRED "%s" KNRM, mm_modem_state_get_string (state)));
        else if (state == MM_MODEM_STATE_CONNECTED)
            output_item_new_take_single (MMC_F_STATUS_STATE, g_strdup_printf (KGRN "%s" KNRM, mm_modem_state_get_string (state)));
        else if (state == MM_MODEM_STATE_CONNECTING)
            output_item_new_take_single (MMC_F_STATUS_STATE, g_strdup_printf (KYEL "%s" KNRM, mm_modem_state_get_string (state)));
        else
            output_item_new_take_single (MMC_F_STATUS_STATE, g_strdup (mm_modem_state_get_string (state))) ;

        if (state == MM_MODEM_STATE_FAILED)
            output_item_new_take_single (MMC_F_STATUS_FAILED_REASON,
                                         g_strdup_printf (KRED "%s" KNRM, mm_modem_state_failed_reason_get_string (reason)));
        return;
    }

    output_item_new_take_single (MMC_F_STATUS_STATE, g_strdup (mm_modem_state_get_string (state)));
    output_item_new_take_single (MMC_F_STATUS_FAILED_REASON,
                                 (state == MM_MODEM_STATE_FAILED) ?
                                 g_strdup (mm_modem_state_failed_reason_get_string (reason)) :
                                 NULL);
}

/******************************************************************************/
/* (Custom) SIM slots output */

void
mmcli_output_sim_slots (gchar **sim_slot_paths,
                        guint   primary_sim_slot)
{
    guint i;

    if (selected_type != MMC_OUTPUT_TYPE_HUMAN || !sim_slot_paths) {
        mmcli_output_string_take       (MMC_F_SIM_PRIMARY_SLOT, primary_sim_slot ? g_strdup_printf ("%u", primary_sim_slot) : NULL);
        mmcli_output_string_array_take (MMC_F_SIM_SLOT_PATHS, sim_slot_paths ? sim_slot_paths : NULL, TRUE);
        return;
    }

    /* Include SIM slot number in each item */
    for (i = 0; sim_slot_paths[i]; i++) {
        gchar *aux;
        guint  slot_number = i + 1;

        aux = g_strdup_printf ("slot %u: %s%s",
                               slot_number,
                               g_str_equal (sim_slot_paths[i], "/") ? "none" : sim_slot_paths[i],
                               (primary_sim_slot == slot_number) ? " (active)" : "");
        g_free (sim_slot_paths[i]);
        sim_slot_paths[i] = aux;
    }

    mmcli_output_string_array_take (MMC_F_SIM_SLOT_PATHS, sim_slot_paths, TRUE);
}

/******************************************************************************/
/* (Custom) Network scan output */

static gchar *
build_network_info (MMModem3gppNetwork *network)
{
    const gchar *operator_code;
    const gchar *operator_name;
    gchar       *access_technologies;
    const gchar *availability;
    gchar       *out;

    operator_code = mm_modem_3gpp_network_get_operator_code (network);
    operator_name = mm_modem_3gpp_network_get_operator_long (network);
    if (!operator_name)
        operator_name = mm_modem_3gpp_network_get_operator_short (network);
    access_technologies = mm_modem_access_technology_build_string_from_mask (mm_modem_3gpp_network_get_access_technology (network));
    availability = mm_modem_3gpp_network_availability_get_string (mm_modem_3gpp_network_get_availability (network));

    if (selected_type == MMC_OUTPUT_TYPE_HUMAN)
        out = g_strdup_printf ("%s - %s (%s, %s)",
                               operator_code ? operator_code : "code n/a",
                               operator_name ? operator_name : "name n/a",
                               access_technologies,
                               availability);
    else
        out = g_strdup_printf ("operator-code: %s, operator-name: %s, access-technologies: %s, availability: %s",
                               operator_code ? operator_code : "--",
                               operator_name ? operator_name : "--",
                               access_technologies,
                               availability);
    g_free (access_technologies);

    return out;
}

void
mmcli_output_scan_networks (GList *network_list)
{
    gchar **networks = NULL;

    if (network_list) {
        GPtrArray *aux;
        GList     *l;

        aux = g_ptr_array_new ();
        for (l = network_list; l; l = g_list_next (l))
            g_ptr_array_add (aux, build_network_info ((MMModem3gppNetwork *)(l->data)));
        g_ptr_array_add (aux, NULL);
        networks = (gchar **) g_ptr_array_free (aux, FALSE);
    }

    /* When printing human result, we want to show some result even if no networks
     * are found, so we force a explicit string result. */
    if (selected_type == MMC_OUTPUT_TYPE_HUMAN && !networks)
        output_item_new_take_single (MMC_F_3GPP_SCAN_NETWORKS, g_strdup ("n/a"));
    else
        output_item_new_take_multiple (MMC_F_3GPP_SCAN_NETWORKS, networks, TRUE);
}

/******************************************************************************/
/* (Custom) Firmware list output */

static void
build_firmware_info_human (GPtrArray            *array,
                           MMFirmwareProperties *props,
                           gboolean              selected)
{
    g_ptr_array_add (array, g_strdup (mm_firmware_properties_get_unique_id (props)));
    g_ptr_array_add (array, g_strdup_printf ("    current: %s", selected ? "yes" : "no"));

    if (mm_firmware_properties_get_image_type (props) == MM_FIRMWARE_IMAGE_TYPE_GOBI) {
        const gchar *aux;

        if ((aux = mm_firmware_properties_get_gobi_pri_version (props)) != NULL)
            g_ptr_array_add (array, g_strdup_printf ("    gobi pri version: %s", aux));
        if ((aux = mm_firmware_properties_get_gobi_pri_info (props)) != NULL)
            g_ptr_array_add (array, g_strdup_printf ("    gobi pri info: %s", aux));
        if ((aux = mm_firmware_properties_get_gobi_boot_version (props)) != NULL)
            g_ptr_array_add (array, g_strdup_printf ("    gobi boot version: %s", aux));
        if ((aux = mm_firmware_properties_get_gobi_pri_unique_id (props)) != NULL)
            g_ptr_array_add (array, g_strdup_printf ("    gobi pri unique id: %s", aux));
        if ((aux = mm_firmware_properties_get_gobi_modem_unique_id (props)) != NULL)
            g_ptr_array_add (array, g_strdup_printf ("    gobi modem unique id: %s", aux));
    }
}

static void
build_firmware_info_keyvalue (GPtrArray            *array,
                              MMFirmwareProperties *props,
                              gboolean              selected)
{
    GString *str;

    str = g_string_new ("");
    g_string_append_printf (str, "unique-id: %s", mm_firmware_properties_get_unique_id (props));
    g_string_append_printf (str, ", current: %s",   selected ? "yes" : "no");

    if (mm_firmware_properties_get_image_type (props) == MM_FIRMWARE_IMAGE_TYPE_GOBI) {
        const gchar *aux;

        if ((aux = mm_firmware_properties_get_gobi_pri_version (props)) != NULL)
            g_string_append_printf (str, ", gobi-pri-version: %s", aux);
        if ((aux = mm_firmware_properties_get_gobi_pri_info (props)) != NULL)
            g_string_append_printf (str, ", gobi-pri-info: %s", aux);
        if ((aux = mm_firmware_properties_get_gobi_boot_version (props)) != NULL)
            g_string_append_printf (str, ", gobi-boot-version: %s", aux);
        if ((aux = mm_firmware_properties_get_gobi_pri_unique_id (props)) != NULL)
            g_string_append_printf (str, ", gobi-pri-unique id: %s", aux);
        if ((aux = mm_firmware_properties_get_gobi_modem_unique_id (props)) != NULL)
            g_string_append_printf (str, ", gobi-modem-unique id: %s", aux);
    }

    g_ptr_array_add (array, g_string_free (str, FALSE));
}

void
mmcli_output_firmware_list (GList                *firmware_list,
                            MMFirmwareProperties *selected)
{
    gchar **firmwares = NULL;

    if (firmware_list) {
        GPtrArray *aux;
        GList     *l;

        aux = g_ptr_array_new ();
        for (l = firmware_list; l; l = g_list_next (l)) {
            MMFirmwareProperties *props = (MMFirmwareProperties *)(l->data);
            gboolean              current_selected;

            current_selected = (selected &&
                                g_str_equal (mm_firmware_properties_get_unique_id (props),
                                             mm_firmware_properties_get_unique_id (selected)));

            if (selected_type == MMC_OUTPUT_TYPE_HUMAN)
                build_firmware_info_human (aux, props, current_selected);
            else
                build_firmware_info_keyvalue (aux, props, current_selected);
        }
        g_ptr_array_add (aux, NULL);
        firmwares = (gchar **) g_ptr_array_free (aux, FALSE);
    }

    /* When printing human result, we want to show some result even if no firmwares
     * are found, so we force a explicit string result. */
    if (selected_type == MMC_OUTPUT_TYPE_HUMAN && !firmwares)
        output_item_new_take_single (MMC_F_FIRMWARE_LIST, g_strdup ("n/a"));
    else
        output_item_new_take_multiple (MMC_F_FIRMWARE_LIST, firmwares, TRUE);
}

/******************************************************************************/
/* (Custom) PCO list output */

void
mmcli_output_pco_list (GList *pco_list)
{
    GPtrArray *aux;
    GList     *l;

    if (!pco_list) {
        output_item_new_take_single (MMC_F_3GPP_PCO, NULL);
        return;
    }

    aux = g_ptr_array_new ();
    for (l = pco_list; l; l = g_list_next (l)) {
        MMPco        *pco;
        gchar        *pco_data_hex;
        const guint8 *pco_data;
        gsize         pco_data_size;

        pco = MM_PCO (l->data);
        pco_data = mm_pco_get_data (pco, &pco_data_size);
        pco_data_hex = (pco_data ? mm_utils_bin2hexstr (pco_data, pco_data_size) : NULL);

        if (selected_type == MMC_OUTPUT_TYPE_HUMAN)
            g_ptr_array_add (aux, g_strdup_printf ("%u: (%s) '%s'\n",
                                                   mm_pco_get_session_id (pco),
                                                   mm_pco_is_complete (pco) ? "complete" : "partial",
                                                   pco_data_hex ? pco_data_hex : ""));
        else
            g_ptr_array_add (aux, g_strdup_printf ("session-id: %u, complete: %s, data: %s\n",
                                                   mm_pco_get_session_id (pco),
                                                   mm_pco_is_complete (pco) ? "yes" : "no",
                                                   pco_data_hex ? pco_data_hex : ""));
        g_free (pco_data_hex);
    }
    g_ptr_array_add (aux, NULL);

    output_item_new_take_multiple (MMC_F_3GPP_PCO, (gchar **) g_ptr_array_free (aux, FALSE), TRUE);
}

/******************************************************************************/
/* Human-friendly output */

#define HUMAN_MAX_VALUE_LENGTH 60

static gint
list_sort_human (const OutputItem *item_a,
                 const OutputItem *item_b)
{
    if (field_infos[item_a->field].section < field_infos[item_b->field].section)
        return -1;
    if (field_infos[item_a->field].section > field_infos[item_b->field].section)
        return 1;
    return item_a->field - item_b->field;
}

static void
dump_output_human (void)
{
    GList *l;
    MmcS   current_section = MMC_S_UNKNOWN;
    guint  longest_section_name = 0;
    guint  longest_field_name = 0;

    output_items = g_list_sort (output_items, (GCompareFunc) list_sort_human);

    /* First pass to process */
    for (l = output_items; l; l = g_list_next (l)) {
        OutputItem *item_l;
        guint       aux;
        gboolean    ignore = FALSE;

        item_l = (OutputItem *)(l->data);

        /* Post-process values */
        if (item_l->type == VALUE_TYPE_SINGLE) {
            OutputItemSingle *single = (OutputItemSingle *)item_l;

            if (!single->value)
                ignore = TRUE;
        } else if (item_l->type == VALUE_TYPE_MULTIPLE) {
            OutputItemMultiple *multiple = (OutputItemMultiple *)item_l;

            if (!multiple->values)
                ignore = TRUE;
        }

        /* Compute max lengths */
        if (!ignore) {
            aux = strlen (section_infos[field_infos[item_l->field].section].name);
            if (aux > longest_section_name)
                longest_section_name = aux;
            aux = strlen (field_infos[item_l->field].name);
            if (aux > longest_field_name)
                longest_field_name = aux;
        }
    }

    /* Second pass to print */
    for (l = output_items; l; l = g_list_next (l)) {
        OutputItem         *item_l;
        OutputItemSingle   *single = NULL;
        OutputItemMultiple *multiple = NULL;

        item_l = (OutputItem *)(l->data);
        if (item_l->type == VALUE_TYPE_SINGLE)
            single = (OutputItemSingle *)item_l;
        else if (item_l->type == VALUE_TYPE_MULTIPLE)
            multiple = (OutputItemMultiple *)item_l;
        else
            g_assert_not_reached ();

        /* Ignore items without a value set */
        if ((single && (!single->value || !single->value[0])) ||
            (multiple && (!multiple->values || !g_strv_length (multiple->values))))
            continue;

        /* Section change? */
        if (field_infos[item_l->field].section != current_section) {
            current_section = field_infos[item_l->field].section;
            g_print ("  %.*s\n",
                     longest_section_name + longest_field_name + 4,
                     "------------------------------------------------------------");
            g_print ("  %-*.*s | ",
                     longest_section_name,
                     longest_section_name,
                     section_infos[field_infos[item_l->field].section].name);
        } else
            g_print ("  %*.*s | ",
                     longest_section_name,
                     longest_section_name,
                     "");

        g_print ("%*.*s: ",
                 longest_field_name, longest_field_name, field_infos[item_l->field].name);

        if (single) {
            gchar **split_lines;
            guint   i;

            split_lines = g_strsplit (single->value, "\n", -1);
            for (i = 0; split_lines[i]; i++) {
                if (i != 0) {
                    g_print ("  %*.*s | %*.*s  ",
                             longest_section_name, longest_section_name, "",
                             longest_field_name, longest_field_name, "");
                }
                g_print ("%s\n", split_lines[i]);
            }
            g_strfreev (split_lines);
        } else if (multiple) {
            guint line_length = 0;
            guint n, i;

            n = multiple->values ? g_strv_length (multiple->values) : 0;
            for (i = 0; i < n; i++) {
                const gchar *value;
                guint        value_length;

                value = multiple->values[i];
                value_length = strlen (value) + ((i < (n - 1)) ? 2 : 0);
                if ((multiple->multiline && i != 0) || ((line_length + value_length) > HUMAN_MAX_VALUE_LENGTH)) {
                    line_length = 0;
                    g_print ("\n"
                             "  %*.*s | %*.*s  ",
                             longest_section_name, longest_section_name, "",
                             longest_field_name, longest_field_name, "");
                } else
                    line_length += value_length;
                g_print ("%s%s", value, (!multiple->multiline && i < (n - 1)) ? ", " : "");
            }
            g_print ("\n");
        }
    }
}

static void
dump_output_list_human (MmcF field)
{
    GList *l;
    guint  n;

    g_assert (field != MMC_F_UNKNOWN);

    /* First pass to process */
    for (n = 0, l = output_items; l; l = g_list_next (l), n++) {
        OutputItem         *item_l;
        OutputItemListitem *listitem;

        item_l = (OutputItem *)(l->data);
        g_assert (item_l->type == VALUE_TYPE_LISTITEM);
        listitem = (OutputItemListitem *)item_l;
        g_assert (listitem->value);

        /* All items must be of same type */
        g_assert_cmpint (item_l->field, ==, field);
    }

    /* Second pass to print */
    if (n == 0) {
        g_print ("No %s were found\n", field_infos[field].name);
        return;
    }
    for (l = output_items; l; l = g_list_next (l)) {
        OutputItemListitem *listitem;

        listitem = (OutputItemListitem *)(l->data);
        g_print ("%s%s %s\n", listitem->prefix, listitem->value, listitem->extra);
    }
}

/******************************************************************************/
/* Key-value output */

#define KEY_ARRAY_LENGTH_SUFFIX ".length"
#define KEY_ARRAY_VALUE_SUFFIX  ".value"

static gint
list_sort_keyvalue (const OutputItem *item_a,
                    const OutputItem *item_b)
{
    return item_a->field - item_b->field;
}

static void
dump_output_keyvalue (void)
{
    GList *l;
    guint  longest_field_key = 0;

    output_items = g_list_sort (output_items, (GCompareFunc) list_sort_keyvalue);

    /* First pass to process */
    for (l = output_items; l; l = g_list_next (l)) {
        OutputItem         *item_l;
        OutputItemMultiple *multiple = NULL;
        guint               key_length;

        item_l = (OutputItem *)(l->data);
        if (item_l->type == VALUE_TYPE_MULTIPLE)
            multiple = (OutputItemMultiple *)item_l;

        key_length = strlen (field_infos[item_l->field].key);

        /* when printing array contents, each item is given with an index,
         *   e.g.: something.value[1]
         * The max length of the field will need to consider the array length
         * in order to accommodate the length of the index.
         */
        if (multiple) {
            guint n;

            n = multiple->values ? g_strv_length (multiple->values) : 0;
            if (n > 0) {
                guint aux = n;

                key_length += ((strlen (KEY_ARRAY_VALUE_SUFFIX)) + 3);
                while ((aux /= 10) > 0)
                    key_length++;
            }
        }

        if (key_length > longest_field_key)
            longest_field_key = key_length;
    }

    /* Second pass to print */
    for (l = output_items; l; l = g_list_next (l)) {
        OutputItem         *item_l;
        OutputItemSingle   *single = NULL;
        OutputItemMultiple *multiple = NULL;

        item_l = (OutputItem *)(l->data);
        if (item_l->type == VALUE_TYPE_SINGLE)
            single = (OutputItemSingle *)item_l;
        else if (item_l->type == VALUE_TYPE_MULTIPLE)
            multiple = (OutputItemMultiple *)item_l;
        else
            g_assert_not_reached ();

        if (single) {
            gchar *escaped = NULL;

            if (single->value)
            escaped = g_strescape (single->value, NULL);
            g_print ("%-*.*s : %s\n",
                     longest_field_key, longest_field_key, field_infos[item_l->field].key,
                     escaped ? escaped : "--");
            g_free (escaped);
        } else if (multiple) {
            guint  n;

            n = multiple->values ? g_strv_length (multiple->values) : 0;
            if (n > 0) {
                guint  i;
                gchar *new_key;

                new_key = g_strdup_printf ("%s" KEY_ARRAY_LENGTH_SUFFIX, field_infos[item_l->field].key);
                g_print ("%-*.*s : %u\n", longest_field_key, longest_field_key, new_key, n);
                g_free (new_key);

                for (i = 0; i < n; i++) {
                    gchar *escaped = NULL;

                    /* Printed indices start at 1 */
                    new_key = g_strdup_printf ("%s" KEY_ARRAY_VALUE_SUFFIX "[%u]", field_infos[item_l->field].key, i + 1);
                    escaped = g_strescape (multiple->values[i], NULL);
                    g_print ("%-*.*s : %s\n", longest_field_key, longest_field_key, new_key, escaped);
                    g_free (escaped);
                    g_free (new_key);
                }
            } else
                g_print ("%-*.*s : --\n",
                         longest_field_key, longest_field_key, field_infos[item_l->field].key);
        }
    }
}

static void
dump_output_list_keyvalue (MmcF field)
{
    GList *l;
    guint  key_length;
    guint  n;
    gchar *new_key;

    g_assert (field != MMC_F_UNKNOWN);
    key_length = strlen (field_infos[field].key);

    /* First pass to process */
    for (n = 0, l = output_items; l; l = g_list_next (l), n++) {
        OutputItem         *item_l;
        OutputItemListitem *listitem;

        item_l = (OutputItem *)(l->data);
        g_assert (item_l->type == VALUE_TYPE_LISTITEM);
        listitem = (OutputItemListitem *)item_l;
        g_assert (listitem->value);

        /* All items must be of same type */
        g_assert_cmpint (item_l->field, ==, field);
    }

    if (n > 0) {
        key_length += ((strlen (KEY_ARRAY_VALUE_SUFFIX)) + 3);
        if (n > 10)
            key_length++;
    }

    new_key = g_strdup_printf ("%s" KEY_ARRAY_LENGTH_SUFFIX, field_infos[field].key);
    g_print ("%-*.*s : %u\n", key_length, key_length, new_key, n);
    g_free (new_key);

    /* Second pass to print */
    for (n = 0, l = output_items; l; l = g_list_next (l), n++) {
        OutputItemListitem *listitem;

        listitem = (OutputItemListitem *)(l->data);
        new_key = g_strdup_printf ("%s" KEY_ARRAY_VALUE_SUFFIX "[%u]", field_infos[field].key, n + 1);
        g_print ("%-*.*s : %s\n",
                 key_length, key_length, new_key,
                 listitem->value);
        g_free (new_key);
    }
}

/******************************************************************************/
/* JSON-friendly output */

static gchar *
json_strescape (const gchar *str)
{
    const gchar *p;
    const gchar *end;
    GString *output;
    gsize len;

    len = strlen (str);
    end = str + len;
    output = g_string_sized_new (len);

    for (p = str; p < end; p++) {
        if (*p == '\\' || *p == '"') {
            g_string_append_c (output, '\\');
            g_string_append_c (output, *p);
        } else if ((*p > 0 && *p < 0x1f) || *p == 0x7f) {
            switch (*p) {
                case '\b':
                    g_string_append (output, "\\b");
                    break;
                case '\f':
                    g_string_append (output, "\\f");
                    break;
                case '\n':
                    g_string_append (output, "\\n");
                    break;
                case '\r':
                    g_string_append (output, "\\r");
                    break;
                case '\t':
                    g_string_append (output, "\\t");
                    break;
                default:
                    g_string_append_printf (output, "\\u00%02x", (guint)*p);
                    break;
            }
        } else
            g_string_append_c (output, *p);
    }
    return g_string_free (output, FALSE);
}

static gint
list_sort_by_keys (const OutputItem *item_a,
                   const OutputItem *item_b)
{
    return g_strcmp0 (field_infos[item_a->field].key, field_infos[item_b->field].key);
}

static void
dump_output_json (void)
{
    GList   *l;
    MmcF     current_field = MMC_F_UNKNOWN;
    gchar  **current_path = NULL;
    guint    cur_dlen = 0;

    output_items = g_list_sort (output_items, (GCompareFunc) list_sort_by_keys);

    g_print("{");
    for (l = output_items; l; l = g_list_next (l)) {
        OutputItem *item_l = (OutputItem *)(l->data);

        if (current_field != item_l->field) {
            guint   new_dlen;
            guint   iter = 0;
            gchar **new_path;

            new_path = g_strsplit (field_infos[item_l->field].key, ".", -1);
            new_dlen = g_strv_length (new_path) - 1;
            if (current_path) {
                guint min_dlen;

                min_dlen = MIN (cur_dlen, new_dlen);
                while (iter < min_dlen && g_strcmp0 (current_path[iter], new_path[iter]) == 0)
                    iter++;

                g_strfreev (current_path);

                if (iter < min_dlen || new_dlen < cur_dlen)
                    for (min_dlen = iter; min_dlen < cur_dlen; min_dlen++)
                        g_print ("}");

                g_print (",");
            }

            while (iter < new_dlen)
                g_print ("\"%s\":{", new_path[iter++]);

            cur_dlen = new_dlen;
            current_path = new_path;
            current_field = item_l->field;
        } else {
            g_print (",");
        }

        if (item_l->type == VALUE_TYPE_SINGLE) {
            OutputItemSingle *single = (OutputItemSingle *) item_l;
            gchar            *escaped = NULL;

            if (single->value)
                escaped = json_strescape (single->value);

            g_print ("\"%s\":\"%s\"", current_path[cur_dlen], escaped ? escaped : "--");
            g_free (escaped);
        } else if (item_l->type == VALUE_TYPE_MULTIPLE) {
            OutputItemMultiple *multiple = (OutputItemMultiple *) item_l;
            guint               i, n;

            n = multiple->values ? g_strv_length (multiple->values) : 0;

            g_print ("\"%s\":[", current_path[cur_dlen]);
            for (i = 0; i < n; i++) {
                gchar *escaped;

                escaped = json_strescape (multiple->values[i]);
                g_print("\"%s\"", escaped);
                if (i < n - 1)
                    g_print(",");
                g_free (escaped);
            }
            g_print("]");
        } else
            g_assert_not_reached ();
    }

    while (cur_dlen--)
        g_print ("}");
    g_print("}\n");

    g_strfreev (current_path);
}

static void
dump_output_list_json (MmcF field)
{
    GList *l;

    g_assert (field != MMC_F_UNKNOWN);

    g_print("{\"%s\":[", field_infos[field].key);

    for (l = output_items; l; l = g_list_next (l)) {
        OutputItem         *item_l;
        OutputItemListitem *listitem;

        item_l = (OutputItem *)(l->data);
        g_assert (item_l->type == VALUE_TYPE_LISTITEM);
        listitem = (OutputItemListitem *)item_l;
        g_assert (listitem->value);

        /* All items must be of same type */
        g_assert_cmpint (item_l->field, ==, field);
        g_print("\"%s\"", listitem->value);

        if (g_list_next (l))
            g_print(",");
    }

    g_print("]}\n");
}

/******************************************************************************/
/* Dump output */

void
mmcli_output_dump (void)
{
    switch (selected_type) {
    case MMC_OUTPUT_TYPE_NONE:
        break;
    case MMC_OUTPUT_TYPE_HUMAN:
        dump_output_human ();
        break;
    case MMC_OUTPUT_TYPE_KEYVALUE:
        dump_output_keyvalue ();
        break;
    case MMC_OUTPUT_TYPE_JSON:
        dump_output_json ();
        break;
    default:
        g_assert_not_reached ();
    }

    g_list_free_full (output_items, (GDestroyNotify) output_item_free);
    output_items = NULL;

    fflush (stdout);
}

void
mmcli_output_list_dump (MmcF field)
{
    switch (selected_type) {
    case MMC_OUTPUT_TYPE_NONE:
        break;
    case MMC_OUTPUT_TYPE_HUMAN:
        dump_output_list_human (field);
        break;
    case MMC_OUTPUT_TYPE_KEYVALUE:
        dump_output_list_keyvalue (field);
        break;
    case MMC_OUTPUT_TYPE_JSON:
        dump_output_list_json (field);
        break;
    default:
        g_assert_not_reached ();
    }

    g_list_free_full (output_items, (GDestroyNotify) output_item_free);
    output_items = NULL;

    fflush (stdout);
}
