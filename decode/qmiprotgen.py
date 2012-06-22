#!/bin/env python
# -*- Mode: python; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-

# Takes an Entity.txt and generates the Python dicts defining the commands
# and the TLVs associated with those commands

import sys

TP_REQUEST = 0
TP_RESPONSE = 1
TP_INDICATION = 2

cmdenum = """
   eQMI_CTL_SET_INSTANCE_ID = 32,   // 32 Set the unique link instance ID
   eQMI_CTL_GET_VERSION_INFO,       // 33 Get supported service version info
   eQMI_CTL_GET_CLIENT_ID,          // 34 Get a unique client ID 
   eQMI_CTL_RELEASE_CLIENT_ID,      // 35 Release the unique client ID 
   eQMI_CTL_REVOKE_CLIENT_ID_IND,   // 36 Indication of client ID revocation
   eQMI_CTL_INVALID_CLIENT_ID,      // 37 Indication of invalid client ID
   eQMI_CTL_SET_DATA_FORMAT,        // 38 Set host driver data format 
   eQMI_CTL_SYNC,                   // 39 Synchronize client/server
   eQMI_CTL_SYNC_IND = 39,          // 39 Synchronize indication
   eQMI_CTL_SET_EVENT,              // 40 Set event report conditions
   eQMI_CTL_EVENT_IND = 40,         // 40 Event report indication
   eQMI_CTL_SET_POWER_SAVE_CFG,     // 41 Set power save config
   eQMI_CTL_SET_POWER_SAVE_MODE,    // 42 Set power save mode
   eQMI_CTL_GET_POWER_SAVE_MODE,    // 43 Get power save mode

   eQMI_WDS_RESET,                // 00 Reset WDS service state variables
   eQMI_WDS_SET_EVENT,            // 01 Set connection state report conditions
   eQMI_WDS_EVENT_IND = 1,        // 01 Connection state report indication
   eQMI_WDS_ABORT,                // 02 Abort previously issued WDS command
   eQMI_WDS_START_NET = 32,       // 32 Start WDS network interface
   eQMI_WDS_STOP_NET,             // 33 Stop WDS network interface
   eQMI_WDS_GET_PKT_STATUS,       // 34 Get packet data connection status
   eQMI_WDS_PKT_STATUS_IND = 34,  // 34 Packet data connection status indication
   eQMI_WDS_GET_RATES,            // 35 Get current bit rates of the connection
   eQMI_WDS_GET_STATISTICS,       // 36 Get the packet data transfer statistics
   eQMI_WDS_G0_DORMANT,           // 37 Go dormant
   eQMI_WDS_G0_ACTIVE,            // 38 Go active
   eQMI_WDS_CREATE_PROFILE,       // 39 Create profile with specified settings
   eQMI_WDS_MODIFY_PROFILE,       // 40 Modify profile with specified settings
   eQMI_WDS_DELETE_PROFILE,       // 41 Delete the specified profile 
   eQMI_WDS_GET_PROFILE_LIST,     // 42 Get all profiles
   eQMI_WDS_GET_PROFILE,          // 43 Get the specified profile
   eQMI_WDS_GET_DEFAULTS,         // 44 Get the default data session settings 
   eQMI_WDS_GET_SETTINGS,         // 45 Get the runtime data session settings 
   eQMI_WDS_SET_MIP,              // 46 Get the mobile IP setting 
   eQMI_WDS_GET_MIP,              // 47 Set the mobile IP setting 
   eQMI_WDS_GET_DORMANCY,         // 48 Get the dormancy status
   eQMI_WDS_GET_AUTOCONNECT = 52, // 52 Get the NDIS autoconnect setting
   eQMI_WDS_GET_DURATION,         // 53 Get the duration of data session
   eQMI_WDS_GET_MODEM_STATUS,     // 54 Get the modem status
   eQMI_WDS_MODEM_IND = 54,       // 54 Modem status indication
   eQMI_WDS_GET_DATA_BEARER,      // 55 Get the data bearer type
   eQMI_WDS_GET_MODEM_INFO,       // 56 Get the modem info
   eQMI_WDS_MODEM_INFO_IND = 56,  // 56 Modem info indication
   eQMI_WDS_GET_ACTIVE_MIP = 60,  // 60 Get the active mobile IP profile
   eQMI_WDS_SET_ACTIVE_MIP,       // 61 Set the active mobile IP profile
   eQMI_WDS_GET_MIP_PROFILE,      // 62 Get mobile IP profile settings
   eQMI_WDS_SET_MIP_PROFILE,      // 63 Set mobile IP profile settings
   eQMI_WDS_GET_MIP_PARAMS,       // 64 Get mobile IP parameters
   eQMI_WDS_SET_MIP_PARAMS,       // 65 Set mobile IP parameters
   eQMI_WDS_GET_LAST_MIP_STATUS,  // 66 Get last mobile IP status
   eQMI_WDS_GET_AAA_AUTH_STATUS,  // 67 Get AN-AAA authentication status
   eQMI_WDS_GET_CUR_DATA_BEARER,  // 68 Get current data bearer
   eQMI_WDS_GET_CALL_LIST,        // 69 Get the call history list
   eQMI_WDS_GET_CALL_ENTRY,       // 70 Get an entry from the call history list
   eQMI_WDS_CLEAR_CALL_LIST,      // 71 Clear the call history list
   eQMI_WDS_GET_CALL_LIST_MAX,    // 72 Get maximum size of call history list
   eQMI_WDS_SET_IP_FAMILY = 77,   // 77 Set the client IP family preference
   eQMI_WDS_SET_AUTOCONNECT = 81, // 81 Set the NDIS autoconnect setting
   eQMI_WDS_GET_DNS,              // 82 Get the DNS setting
   eQMI_WDS_SET_DNS,              // 83 Set the DNS setting
   eQMI_WDS_GET_PRE_DORMANCY,     // 084 Get the CDMA pre-dormancy settings
   eQMI_WDS_SET_CAM_TIMER,        // 085 Set the CAM timer
   eQMI_WDS_GET_CAM_TIMER,        // 086 Get the CAM timer
   eQMI_WDS_SET_SCRM,             // 087 Set SCRM status 
   eQMI_WDS_GET_SCRM,             // 088 Get SCRM status
   eQMI_WDS_SET_RDUD,             // 089 Set RDUD status 
   eQMI_WDS_GET_RDUD,             // 090 Get RDUD status 
   eQMI_WDS_GET_SIPMIP_CALL_TYPE, // 091 Set SIP/MIP call type 
   eQMI_WDS_SET_PM_PERIOD,        // 092 Set EV-DO page monitor period
   eQMI_WDS_SET_FORCE_LONG_SLEEP, // 093 Set EV-DO force long sleep feature
   eQMI_WDS_GET_PM_PERIOD,        // 094 Get EV-DO page monitor period
   eQMI_WDS_GET_CALL_THROTTLE,    // 095 Get call throttle info
   eQMI_WDS_GET_NSAPI,            // 096 Get NSAPI
   eQMI_WDS_SET_DUN_CTRL_PREF,    // 097 Set DUN control preference
   eQMI_WDS_GET_DUN_CTRL_INFO,    // 098 Set DUN control info
   eQMI_WDS_SET_DUN_CTRL_EVENT,   // 099 Set DUN control event preference
   eQMI_WDS_PENDING_DUN_CTRL,     // 100 Control pending DUN call
   eQMI_WDS_GET_DATA_SYS = 105,   // 105 Get preferred data system
   eQMI_WDS_GET_LAST_DATA_STATUS, // 106 Get last data call status
   eQMI_WDS_GET_CURR_DATA_SYS,    // 107 Get current data systems status
   eQMI_WDS_GET_PDN_THROTTLE,     // 108 Get PDN throttle info

   eQMI_DMS_RESET,               // 00 Reset DMS service state variables
   eQMI_DMS_SET_EVENT,           // 01 Set connection state report conditions
   eQMI_DMS_EVENT_IND = 1,       // 01 Connection state report indication
   eQMI_DMS_GET_CAPS = 32,       // 32 Get the device capabilities
   eQMI_DMS_GET_MANUFACTURER,    // 33 Get the device manfacturer
   eQMI_DMS_GET_MODEL_ID,        // 34 Get the device model ID
   eQMI_DMS_GET_REV_ID,          // 35 Get the device revision ID
   eQMI_DMS_GET_NUMBER,          // 36 Get the assigned voice number
   eQMI_DMS_GET_IDS,             // 37 Get the ESN/IMEI/MEID
   eQMI_DMS_GET_POWER_STATE,     // 38 Get the get power state
   eQMI_DMS_UIM_SET_PIN_PROT,    // 39 UIM - Set PIN protection
   eQMI_DMS_UIM_PIN_VERIFY,      // 40 UIM - Verify PIN 
   eQMI_DMS_UIM_PIN_UNBLOCK,     // 41 UIM - Unblock PIN
   eQMI_DMS_UIM_PIN_CHANGE,      // 42 UIM - Change PIN
   eQMI_DMS_UIM_GET_PIN_STATUS,  // 43 UIM - Get PIN status
   eQMI_DMS_GET_MSM_ID = 44,     // 44 Get MSM ID
   eQMI_DMS_GET_OPERTAING_MODE,  // 45 Get the operating mode
   eQMI_DMS_SET_OPERATING_MODE,  // 46 Set the operating mode
   eQMI_DMS_GET_TIME,            // 47 Get timestamp from the device
   eQMI_DMS_GET_PRL_VERSION,     // 48 Get the PRL version
   eQMI_DMS_GET_ACTIVATED_STATE, // 49 Get the activation state 
   eQMI_DMS_ACTIVATE_AUTOMATIC,  // 50 Perform an automatic activation
   eQMI_DMS_ACTIVATE_MANUAL,     // 51 Perform a manual activation
   eQMI_DMS_GET_USER_LOCK_STATE, // 52 Get the lock state
   eQMI_DMS_SET_USER_LOCK_STATE, // 53 Set the lock state
   eQMI_DMS_SET_USER_LOCK_CODE,  // 54 Set the lock PIN
   eQMI_DMS_READ_USER_DATA,      // 55 Read user data
   eQMI_DMS_WRITE_USER_DATA,     // 56 Write user data
   eQMI_DMS_READ_ERI_FILE,       // 57 Read the enhanced roaming indicator file
   eQMI_DMS_FACTORY_DEFAULTS,    // 58 Reset to factory defaults
   eQMI_DMS_VALIDATE_SPC,        // 59 Validate service programming code
   eQMI_DMS_UIM_GET_ICCID,       // 60 Get UIM ICCID
   eQMI_DMS_GET_FIRWARE_ID,      // 61 Get firmware ID
   eQMI_DMS_SET_FIRMWARE_ID,     // 62 Set firmware ID
   eQMI_DMS_GET_HOST_LOCK_ID,    // 63 Get host lock ID
   eQMI_DMS_UIM_GET_CK_STATUS,   // 64 UIM - Get control key status
   eQMI_DMS_UIM_SET_CK_PROT,     // 65 UIM - Set control key protection
   eQMI_DMS_UIM_UNBLOCK_CK,      // 66 UIM - Unblock facility control key
   eQMI_DMS_GET_IMSI,            // 67 Get the IMSI
   eQMI_DMS_UIM_GET_STATE,       // 68 UIM - Get the UIM state
   eQMI_DMS_GET_BAND_CAPS,       // 69 Get the device band capabilities
   eQMI_DMS_GET_FACTORY_ID,      // 70 Get the device factory ID
   eQMI_DMS_GET_FIRMWARE_PREF,   // 71 Get firmware preference 
   eQMI_DMS_SET_FIRMWARE_PREF,   // 72 Set firmware preference 
   eQMI_DMS_LIST_FIRMWARE,       // 73 List all stored firmware
   eQMI_DMS_DELETE_FIRMWARE,     // 74 Delete specified stored firmware
   eQMI_DMS_SET_TIME,            // 75 Set device time
   eQMI_DMS_GET_FIRMWARE_INFO,   // 76 Get stored firmware info
   eQMI_DMS_GET_ALT_NET_CFG,     // 77 Get alternate network config
   eQMI_DMS_SET_ALT_NET_CFG,     // 78 Set alternate network config
   eQMI_DMS_GET_IMG_DLOAD_MODE,  // 79 Get next image download mode
   eQMI_DMS_SET_IMG_DLOAD_MODE,  // 80 Set next image download mod
   eQMI_DMS_GET_SW_VERSION,      // 81 Get software version
   eQMI_DMS_SET_SPC,             // 82 Set SPC

   eQMI_NAS_RESET,               // 00 Reset NAS service state variables
   eQMI_NAS_ABORT,               // 01 Abort previously issued NAS command
   eQMI_NAS_SET_EVENT,           // 02 Set NAS state report conditions
   eQMI_NAS_EVENT_IND = 2,       // 02 Connection state report indication
   eQMI_NAS_SET_REG_EVENT,       // 03 Set NAS registration report conditions
   eQMI_NAS_GET_RSSI = 32,       // 32 Get the signal strength
   eQMI_NAS_SCAN_NETS,           // 33 Scan for visible network
   eQMI_NAS_REGISTER_NET,        // 34 Initiate a network registration
   eQMI_NAS_ATTACH_DETACH,       // 35 Initiate an attach or detach action
   eQMI_NAS_GET_SS_INFO,         // 36 Get info about current serving system
   eQMI_NAS_SS_INFO_IND = 36,    // 36 Current serving system info indication
   eQMI_NAS_GET_HOME_INFO,       // 37 Get info about home network
   eQMI_NAS_GET_NET_PREF_LIST,   // 38 Get the list of preferred networks
   eQMI_NAS_SET_NET_PREF_LIST,   // 39 Set the list of preferred networks
   eQMI_NAS_GET_NET_BAN_LIST,    // 40 Get the list of forbidden networks
   eQMI_NAS_SET_NET_BAN_LIST,    // 41 Set the list of forbidden networks
   eQMI_NAS_SET_TECH_PREF,       // 42 Set the technology preference
   eQMI_NAS_GET_TECH_PREF,       // 43 Get the technology preference
   eQMI_NAS_GET_ACCOLC,          // 44 Get the Access Overload Class
   eQMI_NAS_SET_ACCOLC,          // 45 Set the Access Overload Class 
   eQMI_NAS_GET_SYSPREF,         // 46 Get the CDMA system preference 
   eQMI_NAS_GET_NET_PARAMS,      // 47 Get various network parameters 
   eQMI_NAS_SET_NET_PARAMS,      // 48 Set various network parameters 
   eQMI_NAS_GET_RF_INFO,         // 49 Get the SS radio/band channel info
   eQMI_NAS_GET_AAA_AUTH_STATUS, // 50 Get AN-AAA authentication status
   eQMI_NAS_SET_SYS_SELECT_PREF, // 51 Set system selection preference
   eQMI_NAS_GET_SYS_SELECT_PREF, // 52 Get system selection preference
   eQMI_NAS_SET_DDTM_PREF = 55,  // 55 Set DDTM preference
   eQMI_NAS_GET_DDTM_PREF,       // 56 Get DDTM preference
   eQMI_NAS_GET_PLMN_MODE = 59,  // 59 Get PLMN mode bit from CSP
   eQMI_NAS_PLMN_MODE_IND,       // 60 CSP PLMN mode bit indication
   eQMI_NAS_GET_PLMN_NAME = 68,  // 68 Get operator name for specified network
   eQMI_NAS_BIND_SUBS,           // 69 Bind client to a specific subscription
   eQMI_NAS_MANAGED_ROAMING_IND, // 70 Managed roaming indication
   eQMI_NAS_DSB_PREF_IND,        // 71 Dual standby preference indication
   eQMI_NAS_SUBS_INFO_IND,       // 72 Subscription info indication
   eQMI_NAS_GET_MODE_PREF,       // 73 Get mode preference
   eQMI_NAS_SET_DSB_PREF = 75,   // 75 Set dual standby preference
   eQMI_NAS_NETWORK_TIME_IND,    // 76 Network time indication
   eQMI_NAS_GET_SYSTEM_INFO,     // 77 Get system info
   eQMI_NAS_SYSTEM_INFO_IND,     // 78 System info indication
   eQMI_NAS_GET_SIGNAL_INFO,     // 79 Get signal info
   eQMI_NAS_CFG_SIGNAL_INFO,     // 80 Configure signal info report
   eQMI_NAS_SIGNAL_INFO_IND,     // 81 Signal info indication
   eQMI_NAS_GET_ERROR_RATE,      // 82 Get error rate info
   eQMI_NAS_ERROR_RATE_IND,      // 83 Error rate indication
   eQMI_NAS_EVDO_SESSION_IND,    // 84 CDMA 1xEV-DO session close indication
   eQMI_NAS_EVDO_UATI_IND,       // 85 CDMA 1xEV-DO UATI update indication
   eQMI_NAS_GET_EVDO_SUBTYPE,    // 86 Get CDMA 1xEV-DO protocol subtype
   eQMI_NAS_GET_EVDO_COLOR_CODE, // 87 Get CDMA 1xEV-DO color code
   eQMI_NAS_GET_ACQ_SYS_MODE,    // 88 Get current acquisition system mode
   eQMI_NAS_SET_RX_DIVERSITY,    // 89 Set the RX diversity
   eQMI_NAS_GET_RX_TX_INFO,      // 90 Get detailed RX/TX information
   eQMI_NAS_UPDATE_AKEY_EXT,     // 91 Update the A-KEY (extended)
   eQMI_NAS_GET_DSB_PREF,        // 92 Get dual standby preference
   eQMI_NAS_DETACH_LTE,          // 093 Detach the current LTE system
   eQMI_NAS_BLOCK_LTE_PLMN,      // 094 Block LTE PLMN
   eQMI_NAS_UNBLOCK_LTE_PLMN,    // 095 Unblock LTE PLMN
   eQMI_NAS_RESET_LTE_PLMN_BLK,  // 096 Reset LTE PLMN blocking
   eQMI_NAS_CUR_PLMN_NAME_IND,   // 097 Current PLMN name indication
   eQMI_NAS_CONFIG_EMBMS,        // 098 Configure eMBMS
   eQMI_NAS_GET_EMBMS_STATUS,    // 099 Get eMBMS status
   eQMI_NAS_EMBMS_STATUS_IND,    // 100 eMBMS status indication
   eQMI_NAS_GET_CDMA_POS_INFO,   // 101 Get CDMA position info
   eQMI_NAS_RF_BAND_INFO_IND,    // 102 RF band info indication

   eQMI_WMS_RESET,                  // 00 Reset WMS service state variables
   eQMI_WMS_SET_EVENT,              // 01 Set new message report conditions
   eQMI_WMS_EVENT_IND = 1,          // 01 New message report indication
   eQMI_WMS_RAW_SEND = 32,          // 32 Send a raw message
   eQMI_WMS_RAW_WRITE,              // 33 Write a raw message to the device
   eQMI_WMS_RAW_READ,               // 34 Read a raw message from the device
   eQMI_WMS_MODIFY_TAG,             // 35 Modify message tag on the device
   eQMI_WMS_DELETE,                 // 36 Delete message by index/tag/memory
   eQMI_WMS_GET_MSG_PROTOCOL = 48,  // 48 Get the current message protocol
   eQMI_WMS_GET_MSG_LIST,           // 49 Get list of messages from the device
   eQMI_WMS_SET_ROUTES,             // 50 Set routes for message memory storage
   eQMI_WMS_GET_ROUTES,             // 51 Get routes for message memory storage
   eQMI_WMS_GET_SMSC_ADDR,          // 52 Get SMSC address
   eQMI_WMS_SET_SMSC_ADDR,          // 53 Set SMSC address
   eQMI_WMS_GET_MSG_LIST_MAX,       // 54 Get maximum size of SMS storage
   eQMI_WMS_SEND_ACK,               // 55 Send ACK
   eQMI_WMS_SET_RETRY_PERIOD,       // 56 Set retry period
   eQMI_WMS_SET_RETRY_INTERVAL,     // 57 Set retry interval
   eQMI_WMS_SET_DC_DISCO_TIMER,     // 58 Set DC auto-disconnect timer
   eQMI_WMS_SET_MEMORY_STATUS,      // 59 Set memory storage status
   eQMI_WMS_SET_BC_ACTIVATION,      // 60 Set broadcast activation
   eQMI_WMS_SET_BC_CONFIG,          // 61 Set broadcast config
   eQMI_WMS_GET_BC_CONFIG,          // 62 Get broadcast config
   eQMI_WMS_MEMORY_FULL_IND,        // 63 Memory full indication
   eQMI_WMS_GET_DOMAIN_PREF,        // 64 Get domain preference
   eQMI_WMS_SET_DOMAIN_PREF,        // 65 Set domain preference
   eQMI_WMS_MEMORY_SEND,            // 66 Send message from memory store
   eQMI_WMS_GET_MSG_WAITING,        // 67 Get message waiting info
   eQMI_WMS_MSG_WAITING_IND,        // 68 Message waiting indication
   eQMI_WMS_SET_PRIMARY_CLIENT,     // 69 Set client as primary client
   eQMI_WMS_SMSC_ADDR_IND,          // 70 SMSC address indication
   eQMI_WMS_INDICATOR_REG,          // 71 Register for indicators
   eQMI_WMS_GET_TRANSPORT_INFO,     // 72 Get transport layer info
   eQMI_WMS_TRANSPORT_INFO_IND,     // 73 Transport layer info indication
   eQMI_WMS_GET_NW_REG_INFO,        // 74 Get network registration info
   eQMI_WMS_NW_REG_INFO_IND,        // 75 Network registration info indication
   eQMI_WMS_BIND_SUBSCRIPTION,      // 76 Bind client to a subscription
   eQMI_WMS_GET_INDICATOR_REG,      // 77 Get indicator registration
   eQMI_WMS_GET_SMS_PARAMETERS,     // 78 Get SMS EF-SMSP parameters
   eQMI_WMS_SET_SMS_PARAMETERS,     // 79 Set SMS EF-SMSP parameters
   eQMI_WMS_CALL_STATUS_IND,        // 80 Call status indication

   eQMI_PDS_RESET,                // 00 Reset PDS service state variables
   eQMI_PDS_SET_EVENT,            // 01 Set PDS report conditions
   eQMI_PDS_EVENT_IND = 1,        // 01 PDS report indication
   eQMI_PDS_GET_STATE = 32,       // 32 Return PDS service state
   eQMI_PDS_STATE_IND = 32,       // 32 PDS service state indication
   eQMI_PDS_SET_STATE,            // 33 Set PDS service state
   eQMI_PDS_START_SESSION,        // 34 Start a PDS tracking session
   eQMI_PDS_GET_SESSION_INFO,     // 35 Get PDS tracking session info
   eQMI_PDS_FIX_POSITION,         // 36 Manual tracking session position
   eQMI_PDS_END_SESSION,          // 37 End a PDS tracking session
   eQMI_PDS_GET_NMEA_CFG,         // 38 Get NMEA sentence config
   eQMI_PDS_SET_NMEA_CFG,         // 39 Set NMEA sentence config
   eQMI_PDS_INJECT_TIME,          // 40 Inject a time reference
   eQMI_PDS_GET_DEFAULTS,         // 41 Get default tracking session config
   eQMI_PDS_SET_DEFAULTS,         // 42 Set default tracking session config
   eQMI_PDS_GET_XTRA_PARAMS,      // 43 Get the GPS XTRA parameters 
   eQMI_PDS_SET_XTRA_PARAMS,      // 44 Set the GPS XTRA parameters 
   eQMI_PDS_FORCE_XTRA_DL,        // 45 Force a GPS XTRA database download
   eQMI_PDS_GET_AGPS_CONFIG,      // 46 Get the AGPS mode configuration
   eQMI_PDS_SET_AGPS_CONFIG,      // 47 Set the AGPS mode configuration
   eQMI_PDS_GET_SVC_AUTOTRACK,    // 48 Get the service auto-tracking state
   eQMI_PDS_SET_SVC_AUTOTRACK,    // 49 Set the service auto-tracking state
   eQMI_PDS_GET_COM_AUTOTRACK,    // 50 Get COM port auto-tracking config
   eQMI_PDS_SET_COM_AUTOTRACK,    // 51 Set COM port auto-tracking config
   eQMI_PDS_RESET_DATA,           // 52 Reset PDS service data
   eQMI_PDS_SINGLE_FIX,           // 53 Request single position fix
   eQMI_PDS_GET_VERSION,          // 54 Get PDS service version
   eQMI_PDS_INJECT_XTRA,          // 55 Inject XTRA data
   eQMI_PDS_INJECT_POSITION,      // 56 Inject position data
   eQMI_PDS_INJECT_WIFI,          // 57 Inject Wi-Fi obtained data
   eQMI_PDS_GET_SBAS_CONFIG,      // 58 Get SBAS config
   eQMI_PDS_SET_SBAS_CONFIG,      // 59 Set SBAS config
   eQMI_PDS_SEND_NI_RESPONSE,     // 60 Send network initiated response
   eQMI_PDS_INJECT_ABS_TIME,      // 61 Inject absolute time
   eQMI_PDS_INJECT_EFS,           // 62 Inject EFS data
   eQMI_PDS_GET_DPO_CONFIG,       // 63 Get DPO config
   eQMI_PDS_SET_DPO_CONFIG,       // 64 Set DPO config
   eQMI_PDS_GET_ODP_CONFIG,       // 65 Get ODP config
   eQMI_PDS_SET_ODP_CONFIG,       // 66 Set ODP config
   eQMI_PDS_CANCEL_SINGLE_FIX,    // 67 Cancel single position fix
   eQMI_PDS_GET_GPS_STATE,        // 68 Get GPS state
   eQMI_PDS_GET_METHODS = 80,     // 80 Get GPS position methods state
   eQMI_PDS_SET_METHODS,          // 81 Set GPS position methods state
   eQMI_PDS_INJECT_SENSOR,        // 82 Inject sensor data
   eQMI_PDS_INJECT_TIME_SYNC,     // 83 Inject time sync data
   eQMI_PDS_GET_SENSOR_CFG,       // 84 Get sensor config
   eQMI_PDS_SET_SENSOR_CFG,       // 85 Set sensor config
   eQMI_PDS_GET_NAV_CFG,          // 86 Get navigation config
   eQMI_PDS_SET_NAV_CFG,          // 87 Set navigation config
   eQMI_PDS_SET_WLAN_BLANK = 90,  // 90 Set WLAN blanking
   eQMI_PDS_SET_LBS_SC_RPT,       // 91 Set LBS security challenge reporting
   eQMI_PDS_LBS_SC_IND = 91,      // 91 LBS security challenge indication
   eQMI_PDS_SET_LBS_SC,           // 92 Set LBS security challenge
   eQMI_PDS_GET_LBS_ENCRYPT_CFG,  // 93 Get LBS security encryption config
   eQMI_PDS_SET_LBS_UPDATE_RATE,  // 94 Set LBS security update rate
   eQMI_PDS_SET_CELLDB_CONTROL,   // 95 Set cell database control
   eQMI_PDS_READY_IND,            // 96 Ready indication

   eQMI_AUTH_START_EAP = 32,        // 32 Start the EAP session
   eQMI_AUTH_SEND_EAP,              // 33 Send and receive EAP packets
   eQMI_AUTH_EAP_RESULT_IND,        // 34 EAP session result indication
   eQMI_AUTH_GET_EAP_KEYS,          // 35 Get the EAP session keys
   eQMI_AUTH_END_EAP,               // 36 End the EAP session
   eQMI_AUTH_RUN_AKA,               // 37 Runs the AKA algorithm
   eQMI_AUTH_AKA_RESULT_IND,        // 38 AKA algorithm result indication

   eQMI_VOICE_INDICATION_REG = 3,    // 03 Set indication registration state
   eQMI_VOICE_CALL_ORIGINATE = 32,   // 32 Originate a voice call
   eQMI_VOICE_CALL_END,              // 33 End a voice call
   eQMI_VOICE_CALL_ANSWER,           // 34 Answer incoming voice call
   eQMI_VOICE_GET_CALL_INFO = 36,    // 36 Get call information
   eQMI_VOICE_OTASP_STATUS_IND,      // 37 OTASP/OTAPA event indication
   eQMI_VOICE_INFO_REC_IND,          // 38 New info record indication
   eQMI_VOICE_SEND_FLASH,            // 39 Send a simple flash
   eQMI_VOICE_BURST_DTMF,            // 40 Send a burst DTMF
   eQMI_VOICE_START_CONT_DTMF,       // 41 Starts a continuous DTMF
   eQMI_VOICE_STOP_CONT_DTMF,        // 42 Stops a continuous DTMF
   eQMI_VOICE_DTMF_IND,              // 43 DTMF event indication
   eQMI_VOICE_SET_PRIVACY_PREF,      // 44 Set privacy preference
   eQMI_VOICE_PRIVACY_IND,           // 45 Privacy change indication
   eQMI_VOICE_ALL_STATUS_IND,        // 46 Voice all call status indication
   eQMI_VOICE_GET_ALL_STATUS,        // 47 Get voice all call status
   eQMI_VOICE_MANAGE_CALLS = 49,     // 49 Manage calls
   eQMI_VOICE_SUPS_NOTIFICATION_IND, // 50 Supplementary service notifications
   eQMI_VOICE_SET_SUPS_SERVICE,      // 51 Manage supplementary service
   eQMI_VOICE_GET_CALL_WAITING,      // 52 Query sup service call waiting
   eQMI_VOICE_GET_CALL_BARRING,      // 53 Query sup service call barring
   eQMI_VOICE_GET_CLIP,              // 54 Query sup service CLIP
   eQMI_VOICE_GET_CLIR,              // 55 Query sup service CLIR
   eQMI_VOICE_GET_CALL_FWDING,       // 56 Query sup service call forwarding
   eQMI_VOICE_SET_CALL_BARRING_PWD,  // 57 Set call barring password
   eQMI_VOICE_ORIG_USSD,             // 58 Initiate USSD operation
   eQMI_VOICE_ANSWER_USSD,           // 59 Answer USSD request
   eQMI_VOICE_CANCEL_USSD,           // 60 Cancel USSD operation
   eQMI_VOICE_USSD_RELEASE_IND,      // 61 USSD release indication
   eQMI_VOICE_USSD_IND,              // 62 USSD request/notification indication
   eQMI_VOICE_UUS_IND,               // 63 UUS information indication
   eQMI_VOICE_SET_CONFIG,            // 64 Set config
   eQMI_VOICE_GET_CONFIG,            // 65 Get config
   eQMI_VOICE_SUPS_IND,              // 66 Sup service request indication
   eQMI_VOICE_ASYNC_ORIG_USSD,       // 67 Initiate USSD operation
   eQMI_VOICE_ASYNC_USSD_IND = 67,   // 67 USSD request/notification indication
   eQMI_VOICE_BIND_SUBSCRIPTION,     // 68 Bind subscription
   eQMI_VOICE_ALS_SET_LINE_SW,       // 69 ALS set line switching
   eQMI_VOICE_ALS_SELECT_LINE,       // 70 ALS select line
   eQMI_VOICE_AOC_RESET_ACM,         // 71 AOC reset ACM
   eQMI_VOICE_AOC_SET_ACM_MAX,       // 72 ACM set ACM maximum
   eQMI_VOICE_AOC_GET_CM_INFO,       // 73 AOC get call meter info
   eQMI_VOICE_AOC_LOW_FUNDS_IND,     // 74 AOC low funds indication
   eQMI_VOICE_GET_COLP,              // 75 Get COLP info
   eQMI_VOICE_GET_COLR,              // 76 Get COLR info
   eQMI_VOICE_GET_CNAP,              // 77 Get CNAP info
   eQMI_VOICE_MANAGE_IP_CALLS,       // 78 Manage VoIP calls

   eQMI_CAT_RESET,                  // 00 Reset CAT service state variables
   eQMI_CAT_SET_EVENT,              // 01 Set new message report conditions
   eQMI_CAT_EVENT_IND = 1,          // 01 New message report indication
   eQMI_CAT_GET_STATE = 32,         // 32 Get service state information
   eQMI_CAT_SEND_TERMINAL,          // 33 Send a terminal response
   eQMI_CAT_SEND_ENVELOPE,          // 34 Send an envelope command
   eQMI_CAT_GET_EVENT,              // 35 Get last message report
   eQMI_CAT_SEND_DECODED_TERMINAL,  // 36 Send a decoded terminal response
   eQMI_CAT_SEND_DECODED_ENVELOPE,  // 37 Send a decoded envelope command
   eQMI_CAT_EVENT_CONFIRMATION,     // 38 Event confirmation
   eQMI_CAT_SCWS_OPEN_CHANNEL,      // 39 Open a channel to a SCWS
   eQMI_CAT_SCWS_OPEN_IND = 39,     // 39 SCWS open channel indication
   eQMI_CAT_SCWS_CLOSE_CHANNEL,     // 40 Close a channel to a SCWS
   eQMI_CAT_SCWS_CLOSE_IND = 40,    // 40 SCWS close channel indication
   eQMI_CAT_SCWS_SEND_DATA,         // 41 Send data to a SCWS
   eQMI_CAT_SCWS_SEND_IND = 41,     // 41 SCWS send data indication
   eQMI_CAT_SCWS_DATA_AVAILABLE,    // 42 Indicate that data is available
   eQMI_CAT_SCWS_CHANNEL_STATUS,    // 43 Provide channel status

   eQMI_RMS_RESET,                  // 00 Reset RMS service state variables
   eQMI_RMS_GET_SMS_WAKE = 32,      // 32 Get SMS wake settings
   eQMI_RMS_SET_SMS_WAKE,           // 33 Set SMS wake settings

   eQMI_OMA_RESET,                  // 00 Reset OMA service state variables
   eQMI_OMA_SET_EVENT,              // 01 Set OMA report conditions
   eQMI_OMA_EVENT_IND = 1,          // 01 OMA report indication
   eQMI_OMA_START_SESSION = 32,     // 32 Start client inititated session
   eQMI_OMA_CANCEL_SESSION,         // 33 Cancel session
   eQMI_OMA_GET_SESSION_INFO,       // 34 Get session information
   eQMI_OMA_SEND_SELECTION,         // 35 Send selection for net inititated msg
   eQMI_OMA_GET_FEATURES,           // 36 Get feature settings
   eQMI_OMA_SET_FEATURES,           // 37 Set feature settings
"""

entities = { 30: TP_REQUEST,    # 30 QMI CTL request
             31: TP_RESPONSE,   # 31 QMI CTL response
             32: TP_INDICATION, # 32 QMI CTL indication
             33: TP_REQUEST,    # 33 QMI WDS request
             34: TP_RESPONSE,   # 34 QMI WDS response
             35: TP_INDICATION, # 35 QMI WDS indication
             36: TP_REQUEST,    # 36 QMI DMS request
             37: TP_RESPONSE,   # 37 QMI DMS response
             38: TP_INDICATION, # 38 QMI DMS indication
             39: TP_REQUEST,    # 39 QMI NAS request
             40: TP_RESPONSE,   # 40 QMI NAS response
             41: TP_INDICATION, # 41 QMI NAS indication
             42: TP_REQUEST,    # 42 QMI QOS request
             43: TP_RESPONSE,   # 43 QMI QOS response
             44: TP_INDICATION, # 44 QMI QOS indication 
             45: TP_REQUEST,    # 45 QMI WMS request
             46: TP_RESPONSE,   # 46 QMI WMS response
             47: TP_INDICATION, # 47 QMI WMS indication
             48: TP_REQUEST,    # 48 QMI PDS request
             49: TP_RESPONSE,   # 49 QMI PDS response
             50: TP_INDICATION, # 50 QMI PDS indication
             51: TP_REQUEST,    # 51 QMI AUTH request
             52: TP_RESPONSE,   # 52 QMI AUTH response
             53: TP_INDICATION, # 53 QMI AUTH indication
             54: TP_REQUEST,    # 54 QMI CAT request
             55: TP_RESPONSE,   # 55 QMI CAT response
             56: TP_INDICATION, # 56 QMI CAT indication
             57: TP_REQUEST,    # 57 QMI RMS request
             58: TP_RESPONSE,   # 58 QMI RMS response
             59: TP_INDICATION, # 59 QMI RMS indication
             60: TP_REQUEST,    # 60 QMI OMA request
             61: TP_RESPONSE,   # 61 QMI OMA response
             62: TP_INDICATION, # 62 QMI OMA indication
             63: TP_REQUEST,    # 63 QMI voice request
             64: TP_RESPONSE,   # 64 QMI voice response
             65: TP_INDICATION  # 65 QMI voice indication
           }

class Tlv:
    def __init__(self, entno, cmdno, tlvno, service, cmdname, tlvname, direction):
        self.entno = entno
        self.cmdno = cmdno
        self.tlvno = tlvno
        self.service = service
        self.cmdname = cmdname
        self.name = tlvname
        self.direction = direction

    def show(self):
        print (" " * 10) + '%s: "%s/%s/%s",' % (self.tlvno, self.service, self.cmdname, self.name)


class Cmd:
    def __init__(self, service, cmdno, name):
        self.service = service
        self.cmdno = cmdno
        self.name = name
        self.tlvs = { TP_REQUEST: {}, TP_RESPONSE: {}, TP_INDICATION: {} }

    def add_tlv(self, direction, tlv):
        if self.tlvs[direction].has_key(tlv.tlvno):
            old = self.tlvs[direction][tlv.tlvno]
            raise ValueError("Tried to add duplicate TLV [%s %d:%d (%s/%s/%s)] had [%s %d:%d (%s/%s/%s)]" % (self.service, \
                self.cmdno, tlv.tlvno, self.service, self.name, tlv.name, self.service, self.cmdno, old.tlvno, self.service, \
                self.name, old.name))
        self.tlvs[direction][tlv.tlvno] = tlv

    def show_direction(self, direction):
        if len(self.tlvs[direction].keys()) == 0:
            return

        print "%s = {  # %d" % (self.get_array_name(direction), self.cmdno)
        keys = self.tlvs[direction].keys()
        keys.sort()
        for k in keys:
            tlv = self.tlvs[direction][k]
            tlv.show()
        print (" " * 8) + "}\n"

    def show_tlvs(self):
        self.show_direction(TP_REQUEST)
        self.show_direction(TP_RESPONSE)
        self.show_direction(TP_INDICATION)

    def get_array_name(self, direction):
        if len(self.tlvs[direction].keys()) == 0:
            return "None"
        tags = { TP_REQUEST: "req", TP_RESPONSE: "rsp", TP_INDICATION: "ind" }
        return "%s_%s_%s_tlvs" % (self.service, self.name.lower(), tags[direction])

# parse list of services and their commands
services = {}
for l in cmdenum.split("\n"):
    l = l.strip()
    if not len(l):
        continue
    l = l.replace("eQMI_", "")
    svcend = l.find("_")
    if svcend < 0:
        raise ValueError("Failed to get service")
    svc = l[:svcend].lower()
    l = l[svcend + 1:]

    comma = l.find(",")
    space = l.find(" =")
    idx = -1
    if comma >= 0 and (space < 0 or comma < space):
        idx = comma
    elif space >= 0 and (comma < 0 or space < comma):
        idx = space
    else:
        raise ValueError("Couldn't determine command name")
    cmdname = l[:idx]
    l = l[idx:]
    comment = l.index("// ")
    l = l[comment + 3:]
    end = l.index(" ")
    cmdno = int(l[:end])

    cmd = Cmd(svc, cmdno, cmdname)

    if not services.has_key(svc):
        services[svc] = {}
    if services[svc].has_key(cmdno):
        # ignore duplicat indication numbers
        if cmdname.find("_IND") >= 0:
            continue
        raise KeyError("Already have %s/%s/%d" % (svc, cmdname, cmdno))
    services[svc][cmdno] = cmd

# read in Entity.txt
f = open(sys.argv[1])
lines = f.readlines()
f.close()

for line in lines:
    parts = line.split("^")
    struct = int(parts[3])

    entno = int(parts[0])

    ids = parts[1].replace('"', "").split(",")
    cmdno = int(ids[0])
    tlvno = int(ids[1])

    name = parts[2].replace('"', "").split("/")
    service = name[0]
    cmdname = name[1]
    tlvname = name[2]

    direction = entities[entno]

    tlv = Tlv(entno, cmdno, tlvno, service, cmdname, tlvname, direction)
    services[service.lower()][cmdno].add_tlv(direction, tlv)

svcsorted = services.keys()
svcsorted.sort()
for s in svcsorted:
    # print each service's command's TLVs
    cmdssorted = services[s].keys()
    cmdssorted.sort()
    for c in cmdssorted:
        cmd = services[s][c]
        cmd.show_tlvs()

    # print each service's command dict
    print "%s_cmds = {" % s
    for c in cmdssorted:
        cmd = services[s][c]
        print '          %d: ("%s", %s, %s, %s),' % (cmd.cmdno, cmd.name, \
            cmd.get_array_name(TP_REQUEST), cmd.get_array_name(TP_RESPONSE), cmd.get_array_name(TP_INDICATION))
    print "        }"
print ""

# print out services
slist = { 0: "ctl", 1: "wds", 2: "dms", 3: "nas", 4: "qos", 5: "wms",
          6: "pds", 7: "auth", 9: "voice", 224: "cat", 225: "rms", 226: "oma" }

slistsorted = slist.keys()
slistsorted.sort()
print "services = {"
for s in slistsorted:
    cmdlistname = "None"
    if slist[s] != "qos":
        # QoS doesn't appear to have any commands
        cmdlistname = slist[s] + "_cmds"
    print '          %d: ("%s", %s),' % (s, slist[s], cmdlistname)
print "        }"

