auth_start_eap_req_tlvs = {  # 32
          16: "AUTH/Start EAP Session Request/Method Mask",
        }

auth_start_eap_rsp_tlvs = {  # 32
          2: "AUTH/Start EAP Session Response/Result Code",
        }

auth_send_eap_req_tlvs = {  # 33
          1: "AUTH/Send EAP Packet Request/Request Packet",
        }

auth_send_eap_rsp_tlvs = {  # 33
          1: "AUTH/Send EAP Packet Response/Response Packet",
          2: "AUTH/Send EAP Packet Response/Result Code",
        }

auth_eap_result_ind_rsp_tlvs = {  # 34
          1: "AUTH/EAP Session Result/Result",
        }

auth_get_eap_keys_rsp_tlvs = {  # 35
          1: "AUTH/Get EAP Session Keys Response/Session Keys",
          2: "AUTH/Get EAP Session Keys Response/Result Code",
        }

auth_end_eap_rsp_tlvs = {  # 36
          2: "AUTH/End EAP Session Response/Result Code",
        }

auth_cmds = {
          32: ("START_EAP", auth_start_eap_req_tlvs, auth_start_eap_rsp_tlvs, None),
          33: ("SEND_EAP", auth_send_eap_req_tlvs, auth_send_eap_rsp_tlvs, None),
          34: ("EAP_RESULT_IND", None, auth_eap_result_ind_rsp_tlvs, None),
          35: ("GET_EAP_KEYS", None, auth_get_eap_keys_rsp_tlvs, None),
          36: ("END_EAP", None, auth_end_eap_rsp_tlvs, None),
          37: ("RUN_AKA", None, None, None),
          38: ("AKA_RESULT_IND", None, None, None),
        }
cat_reset_rsp_tlvs = {  # 0
          2: "CAT/Reset Response/Result Code",
        }

cat_set_event_req_tlvs = {  # 1
          16: "CAT/Set Event Report Request/Report Mask",
        }

cat_set_event_rsp_tlvs = {  # 1
          2: "CAT/Set Event Report Response/Result Code",
          16: "CAT/Set Event Report Response/Reg Status Mask",
        }

cat_set_event_ind_tlvs = {  # 1
          16: "CAT/Event Report/Display Text Event",
          17: "CAT/Event Report/Get Inkey Event",
          18: "CAT/Event Report/Get Input Event",
          19: "CAT/Event Report/Setup Menu Event",
          20: "CAT/Event Report/Select Item Event",
          21: "CAT/Event Report/Alpha ID Available",
          22: "CAT/Event Report/Setup Event List",
          23: "CAT/Event Report/Setup Idle Mode Text Event",
          24: "CAT/Event Report/Language Notification Event",
          25: "CAT/Event Report/Refresh Event",
          26: "CAT/Event Report/End Proactive Session",
        }

cat_get_state_rsp_tlvs = {  # 32
          1: "CAT/Get Service State Response/CAT Service State",
          2: "CAT/Get Service State Response/Result Code",
        }

cat_send_terminal_req_tlvs = {  # 33
          1: "CAT/Send Terminal Response Request/Terminal Response Type",
        }

cat_send_terminal_rsp_tlvs = {  # 33
          2: "CAT/Send Terminal Response Response/Result Code",
        }

cat_send_envelope_req_tlvs = {  # 34
          1: "CAT/Envelope Command Request/Envelope Command",
        }

cat_send_envelope_rsp_tlvs = {  # 34
          2: "CAT/Envelope Command Response/Result Code",
        }

cat_cmds = {
          0: ("RESET", None, cat_reset_rsp_tlvs, None),
          1: ("SET_EVENT", cat_set_event_req_tlvs, cat_set_event_rsp_tlvs, cat_set_event_ind_tlvs),
          32: ("GET_STATE", None, cat_get_state_rsp_tlvs, None),
          33: ("SEND_TERMINAL", cat_send_terminal_req_tlvs, cat_send_terminal_rsp_tlvs, None),
          34: ("SEND_ENVELOPE", cat_send_envelope_req_tlvs, cat_send_envelope_rsp_tlvs, None),
          35: ("GET_EVENT", None, None, None),
          36: ("SEND_DECODED_TERMINAL", None, None, None),
          37: ("SEND_DECODED_ENVELOPE", None, None, None),
          38: ("EVENT_CONFIRMATION", None, None, None),
          39: ("SCWS_OPEN_CHANNEL", None, None, None),
          40: ("SCWS_CLOSE_CHANNEL", None, None, None),
          41: ("SCWS_SEND_DATA", None, None, None),
          42: ("SCWS_DATA_AVAILABLE", None, None, None),
          43: ("SCWS_CHANNEL_STATUS", None, None, None),
        }
ctl_set_instance_id_req_tlvs = {  # 32
          1: "CTL/Set Instance ID Request/Instance",
        }

ctl_set_instance_id_rsp_tlvs = {  # 32
          1: "CTL/Set Instance ID Response/Link",
          2: "CTL/Set Instance ID Response/Result Code",
        }

ctl_get_version_info_rsp_tlvs = {  # 33
          1: "CTL/Get Version Info Response/List",
          2: "CTL/Get Version Info Response/Result Code",
          16: "CTL/Get Version Info Response/Addendum",
        }

ctl_get_client_id_req_tlvs = {  # 34
          1: "CTL/Get Client ID Request/Type",
        }

ctl_get_client_id_rsp_tlvs = {  # 34
          1: "CTL/Get Client ID Response/ID",
          2: "CTL/Get Client ID Response/Result Code",
        }

ctl_release_client_id_req_tlvs = {  # 35
          1: "CTL/Release Client ID Request/ID",
        }

ctl_release_client_id_rsp_tlvs = {  # 35
          1: "CTL/Release Client ID Response/ID",
          2: "CTL/Release Client ID Response/Result Code",
        }

ctl_revoke_client_id_ind_ind_tlvs = {  # 36
          1: "CTL/Release Client ID Indication/ID",
        }

ctl_invalid_client_id_ind_tlvs = {  # 37
          1: "CTL/Invalid Client ID Indication/ID",
        }

ctl_set_data_format_req_tlvs = {  # 38
          1: "CTL/Set Data Format Request/Format",
          16: "CTL/Set Data Format Request/Protocol",
        }

ctl_set_data_format_rsp_tlvs = {  # 38
          2: "CTL/Set Data Format Response/Result Code",
          16: "CTL/Set Data Format Response/Protocol",
        }

ctl_set_event_req_tlvs = {  # 40
          1: "CTL/Set Event Report Request/Report",
        }

ctl_set_event_rsp_tlvs = {  # 40
          2: "CTL/Set Event Report Response/Result Code",
        }

ctl_set_event_ind_tlvs = {  # 40
          1: "CTL/Event Report Indication/Report",
        }

ctl_set_power_save_cfg_req_tlvs = {  # 41
          1: "CTL/Set Power Save Config Request/Descriptor",
          17: "CTL/Set Power Save Config Request/Permitted Set",
        }

ctl_set_power_save_cfg_rsp_tlvs = {  # 41
          2: "CTL/Set Power Save Config Response/Result Code",
        }

ctl_set_power_save_mode_req_tlvs = {  # 42
          1: "CTL/Set Power Save Mode Request/Mode",
        }

ctl_set_power_save_mode_rsp_tlvs = {  # 42
          2: "CTL/Set Power Save Mode Response/Result Code",
        }

ctl_get_power_save_mode_rsp_tlvs = {  # 43
          1: "CTL/Get Power Save Mode Response/Mode",
          2: "CTL/Get Power Save Mode Response/Result Code",
        }

ctl_cmds = {
          32: ("SET_INSTANCE_ID", ctl_set_instance_id_req_tlvs, ctl_set_instance_id_rsp_tlvs, None),
          33: ("GET_VERSION_INFO", None, ctl_get_version_info_rsp_tlvs, None),
          34: ("GET_CLIENT_ID", ctl_get_client_id_req_tlvs, ctl_get_client_id_rsp_tlvs, None),
          35: ("RELEASE_CLIENT_ID", ctl_release_client_id_req_tlvs, ctl_release_client_id_rsp_tlvs, None),
          36: ("REVOKE_CLIENT_ID_IND", None, None, ctl_revoke_client_id_ind_ind_tlvs),
          37: ("INVALID_CLIENT_ID", None, None, ctl_invalid_client_id_ind_tlvs),
          38: ("SET_DATA_FORMAT", ctl_set_data_format_req_tlvs, ctl_set_data_format_rsp_tlvs, None),
          39: ("SYNC", None, None, None),
          40: ("SET_EVENT", ctl_set_event_req_tlvs, ctl_set_event_rsp_tlvs, ctl_set_event_ind_tlvs),
          41: ("SET_POWER_SAVE_CFG", ctl_set_power_save_cfg_req_tlvs, ctl_set_power_save_cfg_rsp_tlvs, None),
          42: ("SET_POWER_SAVE_MODE", ctl_set_power_save_mode_req_tlvs, ctl_set_power_save_mode_rsp_tlvs, None),
          43: ("GET_POWER_SAVE_MODE", None, ctl_get_power_save_mode_rsp_tlvs, None),
        }
dms_reset_rsp_tlvs = {  # 0
          2: "DMS/Reset Response/Result Code",
        }

dms_set_event_req_tlvs = {  # 1
          16: "DMS/Set Event Report Request/Power State",
          17: "DMS/Set Event Report Request/Battery Level",
          18: "DMS/Set Event Report Request/PIN Status",
          19: "DMS/Set Event Report Request/Activation State",
          20: "DMS/Set Event Report Request/Operating Mode",
          21: "DMS/Set Event Report Request/UIM State",
          22: "DMS/Set Event Report Request/Wireless Disable State",
        }

dms_set_event_rsp_tlvs = {  # 1
          2: "DMS/Set Event Report Response/Result Code",
        }

dms_set_event_ind_tlvs = {  # 1
          16: "DMS/Event Report/Power State",
          17: "DMS/Event Report/PIN1 State",
          18: "DMS/Event Report/PIN2 State",
          19: "DMS/Event Report/Activation State",
          20: "DMS/Event Report/Operating Mode",
          21: "DMS/Event Report/UIM State",
          22: "DMS/Event Report/Wireless Disable State",
        }

dms_get_caps_rsp_tlvs = {  # 32
          1: "DMS/Get Device Capabilities Response/Capabilities",
          2: "DMS/Get Device Capabilities Response/Result Code",
        }

dms_get_manufacturer_rsp_tlvs = {  # 33
          1: "DMS/Get Device Manfacturer Response/Manfacturer",
          2: "DMS/Get Device Manfacturer Response/Result Code",
        }

dms_get_model_id_rsp_tlvs = {  # 34
          1: "DMS/Get Device Model Response/Model",
          2: "DMS/Get Device Model Response/Result Code",
        }

dms_get_rev_id_rsp_tlvs = {  # 35
          1: "DMS/Get Device Revision Response/Revision",
          2: "DMS/Get Device Revision Response/Result Code",
          16: "DMS/Get Device Revision Response/Boot Code Revision",
          17: "DMS/Get Device Revision Response/UQCN Revision",
        }

dms_get_number_rsp_tlvs = {  # 36
          1: "DMS/Get Device Voice Number Response/Voice Number",
          2: "DMS/Get Device Voice Number Response/Result Code",
          16: "DMS/Get Device Voice Number Response/Mobile ID Number",
          17: "DMS/Get Device Voice Number Response/IMSI",
        }

dms_get_ids_rsp_tlvs = {  # 37
          2: "DMS/Get Device Serial Numbers Response/Result Code",
          16: "DMS/Get Device Serial Numbers Response/ESN",
          17: "DMS/Get Device Serial Numbers Response/IMEI",
          18: "DMS/Get Device Serial Numbers Response/MEID",
        }

dms_get_power_state_rsp_tlvs = {  # 38
          1: "DMS/Get Power State Response/Power State",
          2: "DMS/Get Power State Response/Result Code",
        }

dms_uim_set_pin_prot_req_tlvs = {  # 39
          1: "DMS/UIM Set PIN Protection Request/Info",
        }

dms_uim_set_pin_prot_rsp_tlvs = {  # 39
          2: "DMS/UIM Set PIN Protection Response/Result Code",
          16: "DMS/UIM Set PIN Protection Response/Retry Info",
        }

dms_uim_pin_verify_req_tlvs = {  # 40
          1: "DMS/UIM Verify PIN Request/Info",
        }

dms_uim_pin_verify_rsp_tlvs = {  # 40
          2: "DMS/UIM Verify PIN Response/Result Code",
          16: "DMS/UIM Verify PIN Response/Retry Info",
        }

dms_uim_pin_unblock_req_tlvs = {  # 41
          1: "DMS/UIM Unblock PIN Request/Info",
        }

dms_uim_pin_unblock_rsp_tlvs = {  # 41
          2: "DMS/UIM Unblock PIN Response/Result Code",
          16: "DMS/UIM Unblock PIN Response/Retry Info",
        }

dms_uim_pin_change_req_tlvs = {  # 42
          1: "DMS/UIM Change PIN Request/Info",
        }

dms_uim_pin_change_rsp_tlvs = {  # 42
          2: "DMS/UIM Change PIN Response/Result Code",
          16: "DMS/UIM Change PIN Response/Retry Info",
        }

dms_uim_get_pin_status_rsp_tlvs = {  # 43
          2: "DMS/UIM Get PIN Status Response/Result Code",
          17: "DMS/UIM Get PIN Status Response/PIN1 Status",
          18: "DMS/UIM Get PIN Status Response/PIN2 Status",
        }

dms_get_msm_id_rsp_tlvs = {  # 44
          1: "DMS/Get Hardware Revision Response/Hardware Revision",
          2: "DMS/Get Hardware Revision Response/Result Code",
        }

dms_get_opertaing_mode_rsp_tlvs = {  # 45
          1: "DMS/Get Operating Mode Response/Operating Mode",
          2: "DMS/Get Operating Mode Response/Result Code",
          16: "DMS/Get Operating Mode Response/Offline Reason",
          17: "DMS/Get Operating Mode Response/Platform Restricted",
        }

dms_set_operating_mode_req_tlvs = {  # 46
          1: "DMS/Set Operating Mode Request/Operating Mode",
        }

dms_set_operating_mode_rsp_tlvs = {  # 46
          2: "DMS/Set Operating Mode Response/Result Code",
        }

dms_get_time_rsp_tlvs = {  # 47
          1: "DMS/Get Timestamp Response/Timestamp",
          2: "DMS/Get Timestamp Response/Result Code",
        }

dms_get_prl_version_rsp_tlvs = {  # 48
          1: "DMS/Get PRL Version Response/PRL Version",
          2: "DMS/Get PRL Version Response/Result Code",
        }

dms_get_activated_state_rsp_tlvs = {  # 49
          1: "DMS/Get Activation State Response/Activation State",
          2: "DMS/Get Activation State Response/Result Code",
        }

dms_activate_automatic_req_tlvs = {  # 50
          1: "DMS/Activate Automatic Request/Activation Code",
        }

dms_activate_automatic_rsp_tlvs = {  # 50
          2: "DMS/Activate Automatic Response/Result Code",
        }

dms_activate_manual_req_tlvs = {  # 51
          1: "DMS/Activate Manual Request/Activation Data",
          16: "DMS/Activate Manual Request/PRL (Obsolete)",
          17: "DMS/Activate Manual Request/MN-HA Key",
          18: "DMS/Activate Manual Request/MN-AAA Key",
          19: "DMS/Activate Manual Request/PRL",
        }

dms_activate_manual_rsp_tlvs = {  # 51
          2: "DMS/Activate Manual Response/Result Code",
        }

dms_get_user_lock_state_rsp_tlvs = {  # 52
          1: "DMS/Get Lock State Response/Lock State",
          2: "DMS/Get Lock State Response/Result Code",
        }

dms_set_user_lock_state_req_tlvs = {  # 53
          1: "DMS/Set Lock State Request/Lock State",
        }

dms_set_user_lock_state_rsp_tlvs = {  # 53
          2: "DMS/Set Lock State Response/Result Code",
        }

dms_set_user_lock_code_req_tlvs = {  # 54
          1: "DMS/Set Lock Code Request/Lock Code",
        }

dms_set_user_lock_code_rsp_tlvs = {  # 54
          2: "DMS/Set Lock Code Response/Result Code",
        }

dms_read_user_data_rsp_tlvs = {  # 55
          1: "DMS/Read User Data Response/User Data",
          2: "DMS/Read User Data Response/Result Code",
        }

dms_write_user_data_req_tlvs = {  # 56
          1: "DMS/Write User Data Request/User Data",
        }

dms_write_user_data_rsp_tlvs = {  # 56
          2: "DMS/Write User Data Response/Result Code",
        }

dms_read_eri_file_rsp_tlvs = {  # 57
          1: "DMS/Read ERI Data Response/User Data",
          2: "DMS/Read ERI Data Response/Result Code",
        }

dms_factory_defaults_req_tlvs = {  # 58
          1: "DMS/Reset Factory Defaults Request/SPC",
        }

dms_factory_defaults_rsp_tlvs = {  # 58
          2: "DMS/Reset Factory Defaults Response/Result Code",
        }

dms_validate_spc_req_tlvs = {  # 59
          1: "DMS/Validate SPC Request/SPC",
        }

dms_validate_spc_rsp_tlvs = {  # 59
          2: "DMS/Validate SPC Response/Result Code",
        }

dms_uim_get_iccid_rsp_tlvs = {  # 60
          1: "DMS/UIM Get ICCID Response/ICCID",
          2: "DMS/UIM Get ICCID Response/Result Code",
        }

dms_get_firware_id_rsp_tlvs = {  # 61
          1: "DMS/UIM Get Firmware ID Response/ID",
          2: "DMS/UIM Get Firmware ID Response/Result Code",
        }

dms_set_firmware_id_req_tlvs = {  # 62
          1: "DMS/UIM Set Firmware ID Request/ID",
        }

dms_set_firmware_id_rsp_tlvs = {  # 62
          2: "DMS/UIM Set Firmware ID Response/Result Code",
        }

dms_get_host_lock_id_rsp_tlvs = {  # 63
          1: "DMS/UIM Get Host Lock ID Response/ID",
          2: "DMS/UIM Get Host Lock ID Response/Result Code",
        }

dms_uim_get_ck_status_req_tlvs = {  # 64
          1: "DMS/UIM Get Control Key Status Request/Facility",
        }

dms_uim_get_ck_status_rsp_tlvs = {  # 64
          1: "DMS/UIM Get Control Key Status Response/Status",
          2: "DMS/UIM Get Control Key Status Response/Result Code",
          16: "DMS/UIM Get Control Key Status Response/Blocking",
        }

dms_uim_set_ck_prot_req_tlvs = {  # 65
          1: "DMS/UIM Set Control Key Protection Request/Facility",
        }

dms_uim_set_ck_prot_rsp_tlvs = {  # 65
          2: "DMS/UIM Set Control Key Protection Response/Result Code",
          16: "DMS/UIM Set Control Key Protection Response/Status",
        }

dms_uim_unblock_ck_req_tlvs = {  # 66
          1: "DMS/UIM Unblock Control Key Request/Facility",
        }

dms_uim_unblock_ck_rsp_tlvs = {  # 66
          2: "DMS/UIM Unblock Control Key Response/Result Code",
          16: "DMS/UIM Unblock Control Key Response/Status",
        }

dms_get_imsi_rsp_tlvs = {  # 67
          1: "DMS/Get IMSI Response/IMSI",
          2: "DMS/Get IMSI Response/Result Code",
        }

dms_uim_get_state_rsp_tlvs = {  # 68
          1: "DMS/Get UIM State Response/State",
          2: "DMS/Get UIM State Response/Result Code",
        }

dms_get_band_caps_rsp_tlvs = {  # 69
          1: "DMS/Get Band Capabilities Response/Bands",
          2: "DMS/Get Band Capabilities Response/Result Code",
        }

dms_get_factory_id_rsp_tlvs = {  # 70
          1: "DMS/Get Factory Serial Number Response/ID",
          2: "DMS/Get Factory Serial Number Response/Result Code",
        }

dms_get_firmware_pref_rsp_tlvs = {  # 71
          1: "DMS/Get Firmware Preference Response/Image List",
          2: "DMS/Get Firmware Preference Response/Result Code",
        }

dms_set_firmware_pref_req_tlvs = {  # 72
          1: "DMS/Set Firmware Preference Request/Image List",
          16: "DMS/Set Firmware Preference Request/Override",
          17: "DMS/Set Firmware Preference Request/Index",
        }

dms_set_firmware_pref_rsp_tlvs = {  # 72
          1: "DMS/Set Firmware Preference Response/Image List",
          2: "DMS/Set Firmware Preference Response/Result Code",
          16: "DMS/Set Firmware Preference Response/Maximum",
        }

dms_list_firmware_rsp_tlvs = {  # 73
          1: "DMS/List Stored Firmware Response/Image List",
          2: "DMS/List Stored Firmware Response/Result Code",
        }

dms_delete_firmware_req_tlvs = {  # 74
          1: "DMS/Delete Stored Firmware Request/Image",
        }

dms_delete_firmware_rsp_tlvs = {  # 74
          2: "DMS/Delete Stored Firmware Response/Result Code",
        }

dms_set_time_req_tlvs = {  # 75
          1: "DMS/Set Device Time Request/Time",
          16: "DMS/Set Device Time Request/Type",
        }

dms_set_time_rsp_tlvs = {  # 75
          2: "DMS/Set Device Time Response/Result Code",
        }

dms_get_firmware_info_req_tlvs = {  # 76
          1: "DMS/Get Stored Firmware Info Request/Image",
        }

dms_get_firmware_info_rsp_tlvs = {  # 76
          2: "DMS/Get Stored Firmware Info Response/Result Code",
          16: "DMS/Get Stored Firmware Info Response/Boot Version",
          17: "DMS/Get Stored Firmware Info Response/PRI Version",
          18: "DMS/Get Stored Firmware Info Response/OEM Lock ID",
        }

dms_get_alt_net_cfg_rsp_tlvs = {  # 77
          1: "DMS/Get Alternate Net Config Response/Config",
          2: "DMS/Get Alternate Net Config Response/Result Code",
        }

dms_set_alt_net_cfg_req_tlvs = {  # 78
          1: "DMS/Set Alternate Net Config Request/Config",
        }

dms_set_alt_net_cfg_rsp_tlvs = {  # 78
          2: "DMS/Set Alternate Net Config Response/Result Code",
        }

dms_get_img_dload_mode_rsp_tlvs = {  # 79
          2: "DMS/Get Image Download Mode Response/Result Code",
          16: "DMS/Get Image Download Mode Response/Mode",
        }

dms_set_img_dload_mode_req_tlvs = {  # 80
          1: "DMS/Set Image Download Mode Request/Mode",
        }

dms_set_img_dload_mode_rsp_tlvs = {  # 80
          2: "DMS/Set Image Download Mode Response/Result Code",
        }

dms_cmds = {
          0: ("RESET", None, dms_reset_rsp_tlvs, None),
          1: ("SET_EVENT", dms_set_event_req_tlvs, dms_set_event_rsp_tlvs, dms_set_event_ind_tlvs),
          32: ("GET_CAPS", None, dms_get_caps_rsp_tlvs, None),
          33: ("GET_MANUFACTURER", None, dms_get_manufacturer_rsp_tlvs, None),
          34: ("GET_MODEL_ID", None, dms_get_model_id_rsp_tlvs, None),
          35: ("GET_REV_ID", None, dms_get_rev_id_rsp_tlvs, None),
          36: ("GET_NUMBER", None, dms_get_number_rsp_tlvs, None),
          37: ("GET_IDS", None, dms_get_ids_rsp_tlvs, None),
          38: ("GET_POWER_STATE", None, dms_get_power_state_rsp_tlvs, None),
          39: ("UIM_SET_PIN_PROT", dms_uim_set_pin_prot_req_tlvs, dms_uim_set_pin_prot_rsp_tlvs, None),
          40: ("UIM_PIN_VERIFY", dms_uim_pin_verify_req_tlvs, dms_uim_pin_verify_rsp_tlvs, None),
          41: ("UIM_PIN_UNBLOCK", dms_uim_pin_unblock_req_tlvs, dms_uim_pin_unblock_rsp_tlvs, None),
          42: ("UIM_PIN_CHANGE", dms_uim_pin_change_req_tlvs, dms_uim_pin_change_rsp_tlvs, None),
          43: ("UIM_GET_PIN_STATUS", None, dms_uim_get_pin_status_rsp_tlvs, None),
          44: ("GET_MSM_ID", None, dms_get_msm_id_rsp_tlvs, None),
          45: ("GET_OPERTAING_MODE", None, dms_get_opertaing_mode_rsp_tlvs, None),
          46: ("SET_OPERATING_MODE", dms_set_operating_mode_req_tlvs, dms_set_operating_mode_rsp_tlvs, None),
          47: ("GET_TIME", None, dms_get_time_rsp_tlvs, None),
          48: ("GET_PRL_VERSION", None, dms_get_prl_version_rsp_tlvs, None),
          49: ("GET_ACTIVATED_STATE", None, dms_get_activated_state_rsp_tlvs, None),
          50: ("ACTIVATE_AUTOMATIC", dms_activate_automatic_req_tlvs, dms_activate_automatic_rsp_tlvs, None),
          51: ("ACTIVATE_MANUAL", dms_activate_manual_req_tlvs, dms_activate_manual_rsp_tlvs, None),
          52: ("GET_USER_LOCK_STATE", None, dms_get_user_lock_state_rsp_tlvs, None),
          53: ("SET_USER_LOCK_STATE", dms_set_user_lock_state_req_tlvs, dms_set_user_lock_state_rsp_tlvs, None),
          54: ("SET_USER_LOCK_CODE", dms_set_user_lock_code_req_tlvs, dms_set_user_lock_code_rsp_tlvs, None),
          55: ("READ_USER_DATA", None, dms_read_user_data_rsp_tlvs, None),
          56: ("WRITE_USER_DATA", dms_write_user_data_req_tlvs, dms_write_user_data_rsp_tlvs, None),
          57: ("READ_ERI_FILE", None, dms_read_eri_file_rsp_tlvs, None),
          58: ("FACTORY_DEFAULTS", dms_factory_defaults_req_tlvs, dms_factory_defaults_rsp_tlvs, None),
          59: ("VALIDATE_SPC", dms_validate_spc_req_tlvs, dms_validate_spc_rsp_tlvs, None),
          60: ("UIM_GET_ICCID", None, dms_uim_get_iccid_rsp_tlvs, None),
          61: ("GET_FIRWARE_ID", None, dms_get_firware_id_rsp_tlvs, None),
          62: ("SET_FIRMWARE_ID", dms_set_firmware_id_req_tlvs, dms_set_firmware_id_rsp_tlvs, None),
          63: ("GET_HOST_LOCK_ID", None, dms_get_host_lock_id_rsp_tlvs, None),
          64: ("UIM_GET_CK_STATUS", dms_uim_get_ck_status_req_tlvs, dms_uim_get_ck_status_rsp_tlvs, None),
          65: ("UIM_SET_CK_PROT", dms_uim_set_ck_prot_req_tlvs, dms_uim_set_ck_prot_rsp_tlvs, None),
          66: ("UIM_UNBLOCK_CK", dms_uim_unblock_ck_req_tlvs, dms_uim_unblock_ck_rsp_tlvs, None),
          67: ("GET_IMSI", None, dms_get_imsi_rsp_tlvs, None),
          68: ("UIM_GET_STATE", None, dms_uim_get_state_rsp_tlvs, None),
          69: ("GET_BAND_CAPS", None, dms_get_band_caps_rsp_tlvs, None),
          70: ("GET_FACTORY_ID", None, dms_get_factory_id_rsp_tlvs, None),
          71: ("GET_FIRMWARE_PREF", None, dms_get_firmware_pref_rsp_tlvs, None),
          72: ("SET_FIRMWARE_PREF", dms_set_firmware_pref_req_tlvs, dms_set_firmware_pref_rsp_tlvs, None),
          73: ("LIST_FIRMWARE", None, dms_list_firmware_rsp_tlvs, None),
          74: ("DELETE_FIRMWARE", dms_delete_firmware_req_tlvs, dms_delete_firmware_rsp_tlvs, None),
          75: ("SET_TIME", dms_set_time_req_tlvs, dms_set_time_rsp_tlvs, None),
          76: ("GET_FIRMWARE_INFO", dms_get_firmware_info_req_tlvs, dms_get_firmware_info_rsp_tlvs, None),
          77: ("GET_ALT_NET_CFG", None, dms_get_alt_net_cfg_rsp_tlvs, None),
          78: ("SET_ALT_NET_CFG", dms_set_alt_net_cfg_req_tlvs, dms_set_alt_net_cfg_rsp_tlvs, None),
          79: ("GET_IMG_DLOAD_MODE", None, dms_get_img_dload_mode_rsp_tlvs, None),
          80: ("SET_IMG_DLOAD_MODE", dms_set_img_dload_mode_req_tlvs, dms_set_img_dload_mode_rsp_tlvs, None),
          81: ("GET_SW_VERSION", None, None, None),
          82: ("SET_SPC", None, None, None),
        }
nas_reset_rsp_tlvs = {  # 0
          2: "NAS/Reset Response/Result Code",
        }

nas_abort_req_tlvs = {  # 1
          1: "NAS/Abort Request/Transaction ID",
        }

nas_abort_rsp_tlvs = {  # 1
          2: "NAS/Abort Response/Result Code",
        }

nas_set_event_req_tlvs = {  # 2
          16: "NAS/Set Event Report Request/Signal Indicator",
          17: "NAS/Set Event Report Request/RF Indicator",
          18: "NAS/Set Event Report Request/Registration Reject Indicator",
          19: "NAS/Set Event Report Request/RSSI Indicator",
          20: "NAS/Set Event Report Request/ECIO Indicator",
          21: "NAS/Set Event Report Request/IO Indicator",
          22: "NAS/Set Event Report Request/SINR Indicator",
          23: "NAS/Set Event Report Request/Error Rate Indicator",
          24: "NAS/Set Event Report Request/RSRQ Indicator",
        }

nas_set_event_rsp_tlvs = {  # 2
          2: "NAS/Set Event Report Response/Result Code",
        }

nas_set_event_ind_tlvs = {  # 2
          16: "NAS/Event Report/Signal Strength",
          17: "NAS/Event Report/RF Info",
          18: "NAS/Event Report/Registration Reject",
          19: "NAS/Event Report/RSSI",
          20: "NAS/Event Report/ECIO",
          21: "NAS/Event Report/IO",
          22: "NAS/Event Report/SINR",
          23: "NAS/Event Report/Error Rate",
          24: "NAS/Event Report/RSRQ",
        }

nas_set_reg_event_req_tlvs = {  # 3
          16: "NAS/Set Registration Event Report Request/System Select Indicator",
          18: "NAS/Set Registration Event Report Request/DDTM Indicator",
          19: "NAS/Set Registration Event Report Request/Serving System Indicator",
        }

nas_set_reg_event_rsp_tlvs = {  # 3
          2: "NAS/Set Registration Event Report Response/Result Code",
        }

nas_get_rssi_req_tlvs = {  # 32
          16: "NAS/Get Signal Strength Request/Request Mask",
        }

nas_get_rssi_rsp_tlvs = {  # 32
          1: "NAS/Get Signal Strength Response/Signal Strength",
          2: "NAS/Get Signal Strength Response/Result Code",
          16: "NAS/Get Signal Strength Response/Signal Strength List",
          17: "NAS/Get Signal Strength Response/RSSI List",
          18: "NAS/Get Signal Strength Response/ECIO List",
          19: "NAS/Get Signal Strength Response/IO",
          20: "NAS/Get Signal Strength Response/SINR",
          21: "NAS/Get Signal Strength Response/Error Rate List",
        }

nas_scan_nets_rsp_tlvs = {  # 33
          2: "NAS/Perform Network Scan Response/Result Code",
          16: "NAS/Perform Network Scan Response/Network Info",
          17: "NAS/Perform Network Scan Response/Network RAT",
        }

nas_register_net_req_tlvs = {  # 34
          1: "NAS/Initiate Network Register Request/Action",
          16: "NAS/Initiate Network Register Request/Manual Info",
          17: "NAS/Initiate Network Register Request/Change Duration",
        }

nas_register_net_rsp_tlvs = {  # 34
          2: "NAS/Initiate Network Register Response/Result Code",
        }

nas_attach_detach_req_tlvs = {  # 35
          16: "NAS/Initiate Attach Request/Action",
        }

nas_attach_detach_rsp_tlvs = {  # 35
          2: "NAS/Initiate Attach Response/Result Code",
        }

nas_get_ss_info_rsp_tlvs = {  # 36
          1: "NAS/Get Serving System Response/Serving System",
          2: "NAS/Get Serving System Response/Result Code",
          16: "NAS/Get Serving System Response/Roaming Indicator",
          17: "NAS/Get Serving System Response/Data Services",
          18: "NAS/Get Serving System Response/Current PLMN",
          19: "NAS/Get Serving System Response/System ID",
          20: "NAS/Get Serving System Response/Base Station",
          21: "NAS/Get Serving System Response/Roaming List",
          22: "NAS/Get Serving System Response/Default Roaming",
          23: "NAS/Get Serving System Response/Time Zone",
          24: "NAS/Get Serving System Response/Protocol Revision",
        }

nas_get_ss_info_ind_tlvs = {  # 36
          1: "NAS/Serving System Indication/Serving System",
          16: "NAS/Serving System Indication/Roaming Indicator",
          17: "NAS/Serving System Indication/Data Services",
          18: "NAS/Serving System Indication/Current PLMN",
          19: "NAS/Serving System Indication/System ID",
          20: "NAS/Serving System Indication/Base Station",
          21: "NAS/Serving System Indication/Roaming List",
          22: "NAS/Serving System Indication/Default Roaming",
          23: "NAS/Serving System Indication/Time Zone",
          24: "NAS/Serving System Indication/Protocol Revision",
          25: "NAS/Serving System Indication/PLMN Change",
        }

nas_get_home_info_rsp_tlvs = {  # 37
          1: "NAS/Get Home Network Response/Home Network",
          2: "NAS/Get Home Network Response/Result Code",
          16: "NAS/Get Home Network Response/Home IDs",
          17: "NAS/Get Home Network Response/Extended Home Network",
        }

nas_get_net_pref_list_rsp_tlvs = {  # 38
          2: "NAS/Get Preferred Networks Response/Result Code",
          16: "NAS/Get Preferred Networks Response/Networks",
          17: "NAS/Get Preferred Networks Response/Static Networks",
        }

nas_set_net_pref_list_req_tlvs = {  # 39
          16: "NAS/Set Preferred Networks Request/Networks",
        }

nas_set_net_pref_list_rsp_tlvs = {  # 39
          2: "NAS/Set Preferred Networks Response/Result Code",
        }

nas_get_net_ban_list_rsp_tlvs = {  # 40
          2: "NAS/Get Forbidden Networks Response/Result Code",
          16: "NAS/Get Forbidden Networks Response/Networks",
        }

nas_set_net_ban_list_req_tlvs = {  # 41
          16: "NAS/Set Forbidden Networks Request/Networks",
        }

nas_set_net_ban_list_rsp_tlvs = {  # 41
          2: "NAS/Set Forbidden Networks Response/Result Code",
        }

nas_set_tech_pref_req_tlvs = {  # 42
          1: "NAS/Set Technology Preference Request/Preference",
        }

nas_set_tech_pref_rsp_tlvs = {  # 42
          2: "NAS/Set Technology Preference Response/Result Code",
        }

nas_get_tech_pref_rsp_tlvs = {  # 43
          1: "NAS/Get Technology Preference Response/Active Preference",
          2: "NAS/Get Technology Preference Response/Result Code",
          16: "NAS/Get Technology Preference Response/Persistent Preference",
        }

nas_get_accolc_rsp_tlvs = {  # 44
          1: "NAS/Get ACCOLC Response/ACCOLC",
          2: "NAS/Get ACCOLC Response/Result Code",
        }

nas_set_accolc_req_tlvs = {  # 45
          1: "NAS/Set ACCOLC Request/ACCOLC",
        }

nas_set_accolc_rsp_tlvs = {  # 45
          2: "NAS/Set ACCOLC Response/Result Code",
        }

nas_get_syspref_rsp_tlvs = {  # 46
          1: "NAS/Get System Preference/Pref",
          2: "NAS/Get System Preference/Result Code",
        }

nas_get_net_params_rsp_tlvs = {  # 47
          2: "NAS/Get Network Parameters Response/Result Code",
          17: "NAS/Get Network Parameters Response/SCI",
          18: "NAS/Get Network Parameters Response/SCM",
          19: "NAS/Get Network Parameters Response/Registration",
          20: "NAS/Get Network Parameters Response/CDMA 1xEV-DO Revision",
          21: "NAS/Get Network Parameters Response/CDMA 1xEV-DO SCP Custom",
          22: "NAS/Get Network Parameters Response/Roaming",
        }

nas_set_net_params_req_tlvs = {  # 48
          16: "NAS/Set Network Parameters Request/SPC",
          20: "NAS/Set Network Parameters Request/CDMA 1xEV-DO Revision",
          21: "NAS/Set Network Parameters Request/CDMA 1xEV-DO SCP Custom",
          22: "NAS/Set Network Parameters Request/Roaming",
        }

nas_set_net_params_rsp_tlvs = {  # 48
          2: "NAS/Set Network Parameters Response/Result Code",
        }

nas_get_rf_info_rsp_tlvs = {  # 49
          1: "NAS/Get RF Info Response/RF Info",
          2: "NAS/Get RF Info Response/Result Code",
        }

nas_get_aaa_auth_status_rsp_tlvs = {  # 50
          1: "NAS/Get AN-AAA Authentication Status Response/Status",
          2: "NAS/Get AN-AAA Authentication Status Response/Result Code",
        }

nas_set_sys_select_pref_req_tlvs = {  # 51
          16: "NAS/Set System Selection Pref Request/Emergency Mode",
          17: "NAS/Set System Selection Pref Request/Mode",
          18: "NAS/Set System Selection Pref Request/Band",
          19: "NAS/Set System Selection Pref Request/PRL",
          20: "NAS/Set System Selection Pref Request/Roaming",
        }

nas_set_sys_select_pref_rsp_tlvs = {  # 51
          2: "NAS/Set System Selection Pref Response/Result Code",
        }

nas_get_sys_select_pref_rsp_tlvs = {  # 52
          2: "NAS/Get System Selection Pref Response/Result Code",
          16: "NAS/Get System Selection Pref Response/Emergency Mode",
          17: "NAS/Get System Selection Pref Response/Mode",
          18: "NAS/Get System Selection Pref Response/Band",
          19: "NAS/Get System Selection Pref Response/PRL",
          20: "NAS/Get System Selection Pref Response/Roaming",
        }

nas_get_sys_select_pref_ind_tlvs = {  # 52
          16: "NAS/System Selection Pref Indication/Emergency Mode",
          17: "NAS/System Selection Pref Indication/Mode",
          18: "NAS/System Selection Pref Indication/Band",
          19: "NAS/System Selection Pref Indication/PRL",
          20: "NAS/System Selection Pref Indication/Roaming",
        }

nas_set_ddtm_pref_req_tlvs = {  # 55
          1: "NAS/Set DDTM Preference Request/DDTM",
        }

nas_set_ddtm_pref_rsp_tlvs = {  # 55
          2: "NAS/Set DDTM Preference Response/Result Code",
        }

nas_get_ddtm_pref_rsp_tlvs = {  # 56
          1: "NAS/Get DDTM Preference Response/DDTM",
          2: "NAS/Get DDTM Preference Response/Result Code",
        }

nas_get_ddtm_pref_ind_tlvs = {  # 56
          1: "NAS/DDTM Preference Indication/DDTM",
        }

nas_get_plmn_mode_rsp_tlvs = {  # 59
          2: "NAS/Get CSP PLMN Mode Response/Result Code",
          16: "NAS/Get CSP PLMN Mode Response/Mode",
        }

nas_plmn_mode_ind_ind_tlvs = {  # 60
          16: "NAS/CSP PLMN Mode Indication/Mode",
        }

nas_get_plmn_name_req_tlvs = {  # 68
          1: "NAS/Get PLMN Name Request/PLMN",
        }

nas_get_plmn_name_rsp_tlvs = {  # 68
          2: "NAS/Get PLMN Name Response/Result Code",
          16: "NAS/Get PLMN Name Response/Name",
        }

nas_cmds = {
          0: ("RESET", None, nas_reset_rsp_tlvs, None),
          1: ("ABORT", nas_abort_req_tlvs, nas_abort_rsp_tlvs, None),
          2: ("SET_EVENT", nas_set_event_req_tlvs, nas_set_event_rsp_tlvs, nas_set_event_ind_tlvs),
          3: ("SET_REG_EVENT", nas_set_reg_event_req_tlvs, nas_set_reg_event_rsp_tlvs, None),
          32: ("GET_RSSI", nas_get_rssi_req_tlvs, nas_get_rssi_rsp_tlvs, None),
          33: ("SCAN_NETS", None, nas_scan_nets_rsp_tlvs, None),
          34: ("REGISTER_NET", nas_register_net_req_tlvs, nas_register_net_rsp_tlvs, None),
          35: ("ATTACH_DETACH", nas_attach_detach_req_tlvs, nas_attach_detach_rsp_tlvs, None),
          36: ("GET_SS_INFO", None, nas_get_ss_info_rsp_tlvs, nas_get_ss_info_ind_tlvs),
          37: ("GET_HOME_INFO", None, nas_get_home_info_rsp_tlvs, None),
          38: ("GET_NET_PREF_LIST", None, nas_get_net_pref_list_rsp_tlvs, None),
          39: ("SET_NET_PREF_LIST", nas_set_net_pref_list_req_tlvs, nas_set_net_pref_list_rsp_tlvs, None),
          40: ("GET_NET_BAN_LIST", None, nas_get_net_ban_list_rsp_tlvs, None),
          41: ("SET_NET_BAN_LIST", nas_set_net_ban_list_req_tlvs, nas_set_net_ban_list_rsp_tlvs, None),
          42: ("SET_TECH_PREF", nas_set_tech_pref_req_tlvs, nas_set_tech_pref_rsp_tlvs, None),
          43: ("GET_TECH_PREF", None, nas_get_tech_pref_rsp_tlvs, None),
          44: ("GET_ACCOLC", None, nas_get_accolc_rsp_tlvs, None),
          45: ("SET_ACCOLC", nas_set_accolc_req_tlvs, nas_set_accolc_rsp_tlvs, None),
          46: ("GET_SYSPREF", None, nas_get_syspref_rsp_tlvs, None),
          47: ("GET_NET_PARAMS", None, nas_get_net_params_rsp_tlvs, None),
          48: ("SET_NET_PARAMS", nas_set_net_params_req_tlvs, nas_set_net_params_rsp_tlvs, None),
          49: ("GET_RF_INFO", None, nas_get_rf_info_rsp_tlvs, None),
          50: ("GET_AAA_AUTH_STATUS", None, nas_get_aaa_auth_status_rsp_tlvs, None),
          51: ("SET_SYS_SELECT_PREF", nas_set_sys_select_pref_req_tlvs, nas_set_sys_select_pref_rsp_tlvs, None),
          52: ("GET_SYS_SELECT_PREF", None, nas_get_sys_select_pref_rsp_tlvs, nas_get_sys_select_pref_ind_tlvs),
          55: ("SET_DDTM_PREF", nas_set_ddtm_pref_req_tlvs, nas_set_ddtm_pref_rsp_tlvs, None),
          56: ("GET_DDTM_PREF", None, nas_get_ddtm_pref_rsp_tlvs, nas_get_ddtm_pref_ind_tlvs),
          59: ("GET_PLMN_MODE", None, nas_get_plmn_mode_rsp_tlvs, None),
          60: ("PLMN_MODE_IND", None, None, nas_plmn_mode_ind_ind_tlvs),
          68: ("GET_PLMN_NAME", nas_get_plmn_name_req_tlvs, nas_get_plmn_name_rsp_tlvs, None),
          69: ("BIND_SUBS", None, None, None),
          70: ("MANAGED_ROAMING_IND", None, None, None),
          71: ("DSB_PREF_IND", None, None, None),
          72: ("SUBS_INFO_IND", None, None, None),
          73: ("GET_MODE_PREF", None, None, None),
          75: ("SET_DSB_PREF", None, None, None),
          76: ("NETWORK_TIME_IND", None, None, None),
          77: ("GET_SYSTEM_INFO", None, None, None),
          78: ("SYSTEM_INFO_IND", None, None, None),
          79: ("GET_SIGNAL_INFO", None, None, None),
          80: ("CFG_SIGNAL_INFO", None, None, None),
          81: ("SIGNAL_INFO_IND", None, None, None),
          82: ("GET_ERROR_RATE", None, None, None),
          83: ("ERROR_RATE_IND", None, None, None),
          84: ("EVDO_SESSION_IND", None, None, None),
          85: ("EVDO_UATI_IND", None, None, None),
          86: ("GET_EVDO_SUBTYPE", None, None, None),
          87: ("GET_EVDO_COLOR_CODE", None, None, None),
          88: ("GET_ACQ_SYS_MODE", None, None, None),
          89: ("SET_RX_DIVERSITY", None, None, None),
          90: ("GET_RX_TX_INFO", None, None, None),
          91: ("UPDATE_AKEY_EXT", None, None, None),
          92: ("GET_DSB_PREF", None, None, None),
          93: ("DETACH_LTE", None, None, None),
          94: ("BLOCK_LTE_PLMN", None, None, None),
          95: ("UNBLOCK_LTE_PLMN", None, None, None),
          96: ("RESET_LTE_PLMN_BLK", None, None, None),
          97: ("CUR_PLMN_NAME_IND", None, None, None),
          98: ("CONFIG_EMBMS", None, None, None),
          99: ("GET_EMBMS_STATUS", None, None, None),
          100: ("EMBMS_STATUS_IND", None, None, None),
          101: ("GET_CDMA_POS_INFO", None, None, None),
          102: ("RF_BAND_INFO_IND", None, None, None),
        }
oma_reset_rsp_tlvs = {  # 0
          2: "OMA/Reset Response/Result Code",
        }

oma_set_event_req_tlvs = {  # 1
          16: "OMA/Set Event Report Request/NIA",
          17: "OMA/Set Event Report Request/Status",
        }

oma_set_event_rsp_tlvs = {  # 1
          2: "OMA/Set Event Report Response/Result Code",
        }

oma_set_event_ind_tlvs = {  # 1
          16: "OMA/Event Report/NIA",
          17: "OMA/Event Report/Status",
          18: "OMA/Event Report/Failure",
        }

oma_start_session_req_tlvs = {  # 32
          16: "OMA/Start Session Request/Type",
        }

oma_start_session_rsp_tlvs = {  # 32
          2: "OMA/Start Session Response/Result Code",
        }

oma_cancel_session_rsp_tlvs = {  # 33
          2: "OMA/Cancel Session Response/Result Code",
        }

oma_get_session_info_rsp_tlvs = {  # 34
          2: "OMA/Get Session Info Response/Result Code",
          16: "OMA/Get Session Info Response/Info",
          17: "OMA/Get Session Info Response/Failure",
          18: "OMA/Get Session Info Response/Retry",
          19: "OMA/Get Session Info Response/NIA",
        }

oma_send_selection_req_tlvs = {  # 35
          16: "OMA/Send Selection Request/Type",
        }

oma_send_selection_rsp_tlvs = {  # 35
          2: "OMA/Send Selection Response/Result Code",
        }

oma_get_features_rsp_tlvs = {  # 36
          2: "OMA/Get Features Response/Result Code",
          16: "OMA/Get Features Response/Provisioning",
          17: "OMA/Get Features Response/PRL Update",
          18: "OMA/Get Features Response/HFA Feature",
          19: "OMA/Get Features Response/HFA Done State",
        }

oma_set_features_req_tlvs = {  # 37
          16: "OMA/Set Features Response/Provisioning",
          17: "OMA/Set Features Response/PRL Update",
          18: "OMA/Set Features Response/HFA Feature",
        }

oma_set_features_rsp_tlvs = {  # 37
          2: "OMA/Set Features Response/Result Code",
        }

oma_cmds = {
          0: ("RESET", None, oma_reset_rsp_tlvs, None),
          1: ("SET_EVENT", oma_set_event_req_tlvs, oma_set_event_rsp_tlvs, oma_set_event_ind_tlvs),
          32: ("START_SESSION", oma_start_session_req_tlvs, oma_start_session_rsp_tlvs, None),
          33: ("CANCEL_SESSION", None, oma_cancel_session_rsp_tlvs, None),
          34: ("GET_SESSION_INFO", None, oma_get_session_info_rsp_tlvs, None),
          35: ("SEND_SELECTION", oma_send_selection_req_tlvs, oma_send_selection_rsp_tlvs, None),
          36: ("GET_FEATURES", None, oma_get_features_rsp_tlvs, None),
          37: ("SET_FEATURES", oma_set_features_req_tlvs, oma_set_features_rsp_tlvs, None),
        }
pds_reset_rsp_tlvs = {  # 0
          2: "PDS/Reset Response/Result Code",
        }

pds_set_event_req_tlvs = {  # 1
          16: "PDS/Set Event Report Request/NMEA Indicator",
          17: "PDS/Set Event Report Request/Mode Indicator",
          18: "PDS/Set Event Report Request/Raw Indicator",
          19: "PDS/Set Event Report Request/XTRA Request Indicator",
          20: "PDS/Set Event Report Request/Time Injection Indicator",
          21: "PDS/Set Event Report Request/Wi-Fi Indicator",
          22: "PDS/Set Event Report Request/Satellite Indicator",
          23: "PDS/Set Event Report Request/VX Network Indicator",
          24: "PDS/Set Event Report Request/SUPL Network Indicator",
          25: "PDS/Set Event Report Request/UMTS CP Network Indicator",
          26: "PDS/Set Event Report Request/PDS Comm Indicator",
        }

pds_set_event_rsp_tlvs = {  # 1
          2: "PDS/Set Event Report Response/Result Code",
        }

pds_set_event_ind_tlvs = {  # 1
          16: "PDS/Event Report/NMEA Sentence",
          17: "PDS/Event Report/NMEA Sentence Plus Mode",
          18: "PDS/Event Report/Position Session Status",
          19: "PDS/Event Report/Parsed Position Data",
          20: "PDS/Event Report/External XTRA Request",
          21: "PDS/Event Report/External Time Injection Request",
          22: "PDS/Event Report/External Wi-Fi Position Request",
          23: "PDS/Event Report/Satellite Info",
          24: "PDS/Event Report/VX Network Initiated Prompt",
          25: "PDS/Event Report/SUPL Network Initiated Prompt",
          26: "PDS/Event Report/UMTS CP Network Initiated Prompt",
          27: "PDS/Event Report/Comm Events",
        }

pds_get_state_rsp_tlvs = {  # 32
          1: "PDS/Get Service State Response/State",
          2: "PDS/Get Service State Response/Result Code",
        }

pds_get_state_ind_tlvs = {  # 32
          1: "PDS/Service State Indication/State",
        }

pds_set_state_req_tlvs = {  # 33
          1: "PDS/Set Service State Request/State",
        }

pds_set_state_rsp_tlvs = {  # 33
          2: "PDS/Set Service State Response/Result Code",
        }

pds_start_session_req_tlvs = {  # 34
          1: "PDS/Start Tracking Session Request/Session",
        }

pds_start_session_rsp_tlvs = {  # 34
          2: "PDS/Start Tracking Session Response/Result Code",
        }

pds_get_session_info_rsp_tlvs = {  # 35
          1: "PDS/Get Tracking Session Info Response/Info",
          2: "PDS/Get Tracking Session Info Response/Result Code",
        }

pds_fix_position_rsp_tlvs = {  # 36
          2: "PDS/Fix Position Response/Result Code",
        }

pds_end_session_rsp_tlvs = {  # 37
          2: "PDS/End Tracking Session Response/Result Code",
        }

pds_get_nmea_cfg_rsp_tlvs = {  # 38
          1: "PDS/Get NMEA Config Response/Config",
          2: "PDS/Get NMEA Config Response/Result Code",
        }

pds_set_nmea_cfg_req_tlvs = {  # 39
          1: "PDS/Set NMEA Config Request/Config",
        }

pds_set_nmea_cfg_rsp_tlvs = {  # 39
          2: "PDS/Set NMEA Config Response/Result Code",
        }

pds_inject_time_req_tlvs = {  # 40
          1: "PDS/Inject Time Reference Request/Time",
        }

pds_inject_time_rsp_tlvs = {  # 40
          2: "PDS/Inject Time Reference Response/Result Code",
        }

pds_get_defaults_rsp_tlvs = {  # 41
          1: "PDS/Get Defaults Response/Defaults",
          2: "PDS/Get Defaults Response/Result Code",
        }

pds_set_defaults_req_tlvs = {  # 42
          1: "PDS/Set Defaults Request/Defaults",
        }

pds_set_defaults_rsp_tlvs = {  # 42
          2: "PDS/Set Defaults Response/Result Code",
        }

pds_get_xtra_params_rsp_tlvs = {  # 43
          2: "PDS/Get XTRA Parameters Response/Result Code",
          16: "PDS/Get XTRA Parameters Response/Automatic",
          17: "PDS/Get XTRA Parameters Response/Medium",
          18: "PDS/Get XTRA Parameters Response/Network",
          19: "PDS/Get XTRA Parameters Response/Validity",
          20: "PDS/Get XTRA Parameters Response/Embedded",
        }

pds_set_xtra_params_req_tlvs = {  # 44
          16: "PDS/Set XTRA Parameters Request/Automatic",
          17: "PDS/Set XTRA Parameters Request/Medium",
          18: "PDS/Set XTRA Parameters Request/Network",
          20: "PDS/Set XTRA Parameters Request/Embedded",
        }

pds_set_xtra_params_rsp_tlvs = {  # 44
          2: "PDS/Set XTRA Parameters Response/Result Code",
        }

pds_force_xtra_dl_rsp_tlvs = {  # 45
          2: "PDS/Force XTRA Download Response/Result Code",
        }

pds_get_agps_config_req_tlvs = {  # 46
          18: "PDS/Get AGPS Config Request/Network Mode",
        }

pds_get_agps_config_rsp_tlvs = {  # 46
          2: "PDS/Get AGPS Config Response/Result Code",
          16: "PDS/Get AGPS Config Response/Server",
          17: "PDS/Get AGPS Config Response/Server URL",
        }

pds_set_agps_config_req_tlvs = {  # 47
          16: "PDS/Set AGPS Config Request/Server",
          17: "PDS/Set AGPS Config Request/Server URL",
          18: "PDS/Set AGPS Config Request/Network Mode",
        }

pds_set_agps_config_rsp_tlvs = {  # 47
          2: "PDS/Set AGPS Config Response/Result Code",
        }

pds_get_svc_autotrack_rsp_tlvs = {  # 48
          1: "PDS/Get Service Auto-Tracking State Response/State",
          2: "PDS/Get Service Auto-Tracking State Response/Result Code",
        }

pds_set_svc_autotrack_req_tlvs = {  # 49
          1: "PDS/Set Service Auto-Tracking State Request/State",
        }

pds_set_svc_autotrack_rsp_tlvs = {  # 49
          2: "PDS/Set Service Auto-Tracking State Response/Result Code",
        }

pds_get_com_autotrack_rsp_tlvs = {  # 50
          1: "PDS/Get COM Port Auto-Tracking Config Response/Config",
          2: "PDS/Get COM Port Auto-Tracking Config Response/Result Code",
        }

pds_set_com_autotrack_req_tlvs = {  # 51
          1: "PDS/Set COM Port Auto-Tracking Config Request/Config",
        }

pds_set_com_autotrack_rsp_tlvs = {  # 51
          2: "PDS/Set COM Port Auto-Tracking Config Response/Result Code",
        }

pds_reset_data_req_tlvs = {  # 52
          16: "PDS/Reset PDS Data Request/GPS Data",
          17: "PDS/Reset PDS Data Request/Cell Data",
        }

pds_reset_data_rsp_tlvs = {  # 52
          2: "PDS/Reset PDS Data Response/Result Code",
        }

pds_single_fix_req_tlvs = {  # 53
          16: "PDS/Single Position Fix Request/Mode",
          17: "PDS/Single Position Fix Request/Timeout",
          18: "PDS/Single Position Fix Request/Accuracy",
        }

pds_single_fix_rsp_tlvs = {  # 53
          2: "PDS/Single Position Fix Response/Result Code",
        }

pds_get_version_rsp_tlvs = {  # 54
          1: "PDS/Get Service Version Response/Version",
          2: "PDS/Get Service Version Response/Result Code",
        }

pds_inject_xtra_req_tlvs = {  # 55
          1: "PDS/Inject XTRA Data Request/Data",
        }

pds_inject_xtra_rsp_tlvs = {  # 55
          2: "PDS/Inject XTRA Data Response/Result Code",
        }

pds_inject_position_req_tlvs = {  # 56
          16: "PDS/Inject Position Data Request/Timestamp",
          17: "PDS/Inject Position Data Request/Latitude",
          18: "PDS/Inject Position Data Request/Longitude",
          19: "PDS/Inject Position Data Request/Altitude Ellipsoid",
          20: "PDS/Inject Position Data Request/Altitude Sea Level",
          21: "PDS/Inject Position Data Request/Horizontal Uncertainty",
          22: "PDS/Inject Position Data Request/Vertical Uncertainty",
          23: "PDS/Inject Position Data Request/Horizontal Confidence",
          24: "PDS/Inject Position Data Request/Vertical Confidence",
          25: "PDS/Inject Position Data Request/Source",
        }

pds_inject_position_rsp_tlvs = {  # 56
          2: "PDS/Inject Position Data Response/Result Code",
        }

pds_inject_wifi_req_tlvs = {  # 57
          16: "PDS/Inject Wi-Fi Position Data Request/Time",
          17: "PDS/Inject Wi-Fi Position Data Request/Position",
          18: "PDS/Inject Wi-Fi Position Data Request/AP Info",
        }

pds_inject_wifi_rsp_tlvs = {  # 57
          2: "PDS/Inject Wi-Fi Position Data Response/Result Code",
        }

pds_get_sbas_config_rsp_tlvs = {  # 58
          2: "PDS/Get SBAS Config Response/Result Code",
          16: "PDS/Get SBAS Config Response/Config",
        }

pds_set_sbas_config_req_tlvs = {  # 59
          16: "PDS/Set SBAS Config Request/Config",
        }

pds_set_sbas_config_rsp_tlvs = {  # 59
          2: "PDS/Set SBAS Config Response/Result Code",
        }

pds_send_ni_response_req_tlvs = {  # 60
          1: "PDS/Send Network Initiated Response Request/Action",
          16: "PDS/Send Network Initiated Response Request/VX",
          17: "PDS/Send Network Initiated Response Request/SUPL",
          18: "PDS/Send Network Initiated Response Request/UMTS CP",
        }

pds_send_ni_response_rsp_tlvs = {  # 60
          2: "PDS/Send Network Initiated Response Response/Result Code",
        }

pds_inject_abs_time_req_tlvs = {  # 61
          1: "PDS/Inject Absolute Time Request/Time",
        }

pds_inject_abs_time_rsp_tlvs = {  # 61
          2: "PDS/Inject Absolute Time Response/Result Code",
        }

pds_inject_efs_req_tlvs = {  # 62
          1: "PDS/Inject EFS Data Request/Date File",
        }

pds_inject_efs_rsp_tlvs = {  # 62
          2: "PDS/Inject EFS Data Response/Result Code",
        }

pds_get_dpo_config_rsp_tlvs = {  # 63
          2: "PDS/Get DPO Config Response/Result Code",
          16: "PDS/Get DPO Config Response/Config",
        }

pds_set_dpo_config_req_tlvs = {  # 64
          1: "PDS/Set DPO Config Request/Config",
        }

pds_set_dpo_config_rsp_tlvs = {  # 64
          2: "PDS/Set DPO Config Response/Result Code",
        }

pds_get_odp_config_rsp_tlvs = {  # 65
          2: "PDS/Get ODP Config Response/Result Code",
          16: "PDS/Get ODP Config Response/Config",
        }

pds_set_odp_config_req_tlvs = {  # 66
          16: "PDS/Set ODP Config Request/Config",
        }

pds_set_odp_config_rsp_tlvs = {  # 66
          2: "PDS/Set ODP Config Response/Result Code",
        }

pds_cancel_single_fix_rsp_tlvs = {  # 67
          2: "PDS/Cancel Single Position Fix Response/Result Code",
        }

pds_get_gps_state_rsp_tlvs = {  # 68
          2: "PDS/Get GPS State Response/Result Code",
          16: "PDS/Get GPS State Response/State",
        }

pds_get_methods_rsp_tlvs = {  # 80
          2: "PDS/Get Position Methods State Response/Result Code",
          16: "PDS/Get Position Methods State Response/XTRA Time",
          17: "PDS/Get Position Methods State Response/XTRA Data",
          18: "PDS/Get Position Methods State Response/Wi-Fi",
        }

pds_set_methods_req_tlvs = {  # 81
          16: "PDS/Set Position Methods State Request/XTRA Time",
          17: "PDS/Set Position Methods State Request/XTRA Data",
          18: "PDS/Set Position Methods State Request/Wi-Fi",
        }

pds_set_methods_rsp_tlvs = {  # 81
          2: "PDS/Set Position Methods State Response/Result Code",
        }

pds_cmds = {
          0: ("RESET", None, pds_reset_rsp_tlvs, None),
          1: ("SET_EVENT", pds_set_event_req_tlvs, pds_set_event_rsp_tlvs, pds_set_event_ind_tlvs),
          32: ("GET_STATE", None, pds_get_state_rsp_tlvs, pds_get_state_ind_tlvs),
          33: ("SET_STATE", pds_set_state_req_tlvs, pds_set_state_rsp_tlvs, None),
          34: ("START_SESSION", pds_start_session_req_tlvs, pds_start_session_rsp_tlvs, None),
          35: ("GET_SESSION_INFO", None, pds_get_session_info_rsp_tlvs, None),
          36: ("FIX_POSITION", None, pds_fix_position_rsp_tlvs, None),
          37: ("END_SESSION", None, pds_end_session_rsp_tlvs, None),
          38: ("GET_NMEA_CFG", None, pds_get_nmea_cfg_rsp_tlvs, None),
          39: ("SET_NMEA_CFG", pds_set_nmea_cfg_req_tlvs, pds_set_nmea_cfg_rsp_tlvs, None),
          40: ("INJECT_TIME", pds_inject_time_req_tlvs, pds_inject_time_rsp_tlvs, None),
          41: ("GET_DEFAULTS", None, pds_get_defaults_rsp_tlvs, None),
          42: ("SET_DEFAULTS", pds_set_defaults_req_tlvs, pds_set_defaults_rsp_tlvs, None),
          43: ("GET_XTRA_PARAMS", None, pds_get_xtra_params_rsp_tlvs, None),
          44: ("SET_XTRA_PARAMS", pds_set_xtra_params_req_tlvs, pds_set_xtra_params_rsp_tlvs, None),
          45: ("FORCE_XTRA_DL", None, pds_force_xtra_dl_rsp_tlvs, None),
          46: ("GET_AGPS_CONFIG", pds_get_agps_config_req_tlvs, pds_get_agps_config_rsp_tlvs, None),
          47: ("SET_AGPS_CONFIG", pds_set_agps_config_req_tlvs, pds_set_agps_config_rsp_tlvs, None),
          48: ("GET_SVC_AUTOTRACK", None, pds_get_svc_autotrack_rsp_tlvs, None),
          49: ("SET_SVC_AUTOTRACK", pds_set_svc_autotrack_req_tlvs, pds_set_svc_autotrack_rsp_tlvs, None),
          50: ("GET_COM_AUTOTRACK", None, pds_get_com_autotrack_rsp_tlvs, None),
          51: ("SET_COM_AUTOTRACK", pds_set_com_autotrack_req_tlvs, pds_set_com_autotrack_rsp_tlvs, None),
          52: ("RESET_DATA", pds_reset_data_req_tlvs, pds_reset_data_rsp_tlvs, None),
          53: ("SINGLE_FIX", pds_single_fix_req_tlvs, pds_single_fix_rsp_tlvs, None),
          54: ("GET_VERSION", None, pds_get_version_rsp_tlvs, None),
          55: ("INJECT_XTRA", pds_inject_xtra_req_tlvs, pds_inject_xtra_rsp_tlvs, None),
          56: ("INJECT_POSITION", pds_inject_position_req_tlvs, pds_inject_position_rsp_tlvs, None),
          57: ("INJECT_WIFI", pds_inject_wifi_req_tlvs, pds_inject_wifi_rsp_tlvs, None),
          58: ("GET_SBAS_CONFIG", None, pds_get_sbas_config_rsp_tlvs, None),
          59: ("SET_SBAS_CONFIG", pds_set_sbas_config_req_tlvs, pds_set_sbas_config_rsp_tlvs, None),
          60: ("SEND_NI_RESPONSE", pds_send_ni_response_req_tlvs, pds_send_ni_response_rsp_tlvs, None),
          61: ("INJECT_ABS_TIME", pds_inject_abs_time_req_tlvs, pds_inject_abs_time_rsp_tlvs, None),
          62: ("INJECT_EFS", pds_inject_efs_req_tlvs, pds_inject_efs_rsp_tlvs, None),
          63: ("GET_DPO_CONFIG", None, pds_get_dpo_config_rsp_tlvs, None),
          64: ("SET_DPO_CONFIG", pds_set_dpo_config_req_tlvs, pds_set_dpo_config_rsp_tlvs, None),
          65: ("GET_ODP_CONFIG", None, pds_get_odp_config_rsp_tlvs, None),
          66: ("SET_ODP_CONFIG", pds_set_odp_config_req_tlvs, pds_set_odp_config_rsp_tlvs, None),
          67: ("CANCEL_SINGLE_FIX", None, pds_cancel_single_fix_rsp_tlvs, None),
          68: ("GET_GPS_STATE", None, pds_get_gps_state_rsp_tlvs, None),
          80: ("GET_METHODS", None, pds_get_methods_rsp_tlvs, None),
          81: ("SET_METHODS", pds_set_methods_req_tlvs, pds_set_methods_rsp_tlvs, None),
          82: ("INJECT_SENSOR", None, None, None),
          83: ("INJECT_TIME_SYNC", None, None, None),
          84: ("GET_SENSOR_CFG", None, None, None),
          85: ("SET_SENSOR_CFG", None, None, None),
          86: ("GET_NAV_CFG", None, None, None),
          87: ("SET_NAV_CFG", None, None, None),
          90: ("SET_WLAN_BLANK", None, None, None),
          91: ("SET_LBS_SC_RPT", None, None, None),
          92: ("SET_LBS_SC", None, None, None),
          93: ("GET_LBS_ENCRYPT_CFG", None, None, None),
          94: ("SET_LBS_UPDATE_RATE", None, None, None),
          95: ("SET_CELLDB_CONTROL", None, None, None),
          96: ("READY_IND", None, None, None),
        }
rms_reset_rsp_tlvs = {  # 0
          2: "RMS/Reset Response/Result Code",
        }

rms_get_sms_wake_rsp_tlvs = {  # 32
          2: "RMS/Get SMS Wake Response/Result Code",
          16: "RMS/Get SMS Wake Response/State",
          17: "RMS/Get SMS Wake Request/Mask",
        }

rms_set_sms_wake_req_tlvs = {  # 33
          16: "RMS/Set SMS Wake Request/State",
          17: "RMS/Set SMS Wake Request/Mask",
        }

rms_set_sms_wake_rsp_tlvs = {  # 33
          2: "RMS/Set SMS Wake Response/Result Code",
        }

rms_cmds = {
          0: ("RESET", None, rms_reset_rsp_tlvs, None),
          32: ("GET_SMS_WAKE", None, rms_get_sms_wake_rsp_tlvs, None),
          33: ("SET_SMS_WAKE", rms_set_sms_wake_req_tlvs, rms_set_sms_wake_rsp_tlvs, None),
        }
voice_orig_ussd_req_tlvs = {  # 58
          1: "Voice/Initiate USSD Request/Info",
        }

voice_orig_ussd_rsp_tlvs = {  # 58
          2: "Voice/Initiate USSD Response/Result Code",
          16: "Voice/Initiate USSD Response/Fail Cause",
          17: "Voice/Initiate USSD Response/Alpha ID",
          18: "Voice/Initiate USSD Response/Data",
        }

voice_answer_ussd_req_tlvs = {  # 59
          1: "Voice/Answer USSD Request/Info",
        }

voice_answer_ussd_rsp_tlvs = {  # 59
          2: "Voice/Answer USSD Response/Result Code",
        }

voice_cancel_ussd_rsp_tlvs = {  # 60
          2: "Voice/Cancel USSD Response/Result Code",
        }

voice_ussd_ind_ind_tlvs = {  # 62
          1: "Voice/USSD Indication/Type",
          16: "Voice/USSD Indication/Data",
        }

voice_async_orig_ussd_req_tlvs = {  # 67
          1: "Voice/Async Initiate USSD Request/Info",
        }

voice_async_orig_ussd_rsp_tlvs = {  # 67
          2: "Voice/Async Initiate USSD Response/Result Code",
        }

voice_async_orig_ussd_ind_tlvs = {  # 67
          16: "Voice/USSD Async Indication/Error",
          17: "Voice/USSD Async Indication/Fail Cause",
          18: "Voice/USSD Async Indication/Info",
          19: "Voice/USSD Async Indication/Alpha ID",
        }

voice_cmds = {
          3: ("INDICATION_REG", None, None, None),
          32: ("CALL_ORIGINATE", None, None, None),
          33: ("CALL_END", None, None, None),
          34: ("CALL_ANSWER", None, None, None),
          36: ("GET_CALL_INFO", None, None, None),
          37: ("OTASP_STATUS_IND", None, None, None),
          38: ("INFO_REC_IND", None, None, None),
          39: ("SEND_FLASH", None, None, None),
          40: ("BURST_DTMF", None, None, None),
          41: ("START_CONT_DTMF", None, None, None),
          42: ("STOP_CONT_DTMF", None, None, None),
          43: ("DTMF_IND", None, None, None),
          44: ("SET_PRIVACY_PREF", None, None, None),
          45: ("PRIVACY_IND", None, None, None),
          46: ("ALL_STATUS_IND", None, None, None),
          47: ("GET_ALL_STATUS", None, None, None),
          49: ("MANAGE_CALLS", None, None, None),
          50: ("SUPS_NOTIFICATION_IND", None, None, None),
          51: ("SET_SUPS_SERVICE", None, None, None),
          52: ("GET_CALL_WAITING", None, None, None),
          53: ("GET_CALL_BARRING", None, None, None),
          54: ("GET_CLIP", None, None, None),
          55: ("GET_CLIR", None, None, None),
          56: ("GET_CALL_FWDING", None, None, None),
          57: ("SET_CALL_BARRING_PWD", None, None, None),
          58: ("ORIG_USSD", voice_orig_ussd_req_tlvs, voice_orig_ussd_rsp_tlvs, None),
          59: ("ANSWER_USSD", voice_answer_ussd_req_tlvs, voice_answer_ussd_rsp_tlvs, None),
          60: ("CANCEL_USSD", None, voice_cancel_ussd_rsp_tlvs, None),
          61: ("USSD_RELEASE_IND", None, None, None),
          62: ("USSD_IND", None, None, voice_ussd_ind_ind_tlvs),
          63: ("UUS_IND", None, None, None),
          64: ("SET_CONFIG", None, None, None),
          65: ("GET_CONFIG", None, None, None),
          66: ("SUPS_IND", None, None, None),
          67: ("ASYNC_ORIG_USSD", voice_async_orig_ussd_req_tlvs, voice_async_orig_ussd_rsp_tlvs, voice_async_orig_ussd_ind_tlvs),
          68: ("BIND_SUBSCRIPTION", None, None, None),
          69: ("ALS_SET_LINE_SW", None, None, None),
          70: ("ALS_SELECT_LINE", None, None, None),
          71: ("AOC_RESET_ACM", None, None, None),
          72: ("AOC_SET_ACM_MAX", None, None, None),
          73: ("AOC_GET_CM_INFO", None, None, None),
          74: ("AOC_LOW_FUNDS_IND", None, None, None),
          75: ("GET_COLP", None, None, None),
          76: ("GET_COLR", None, None, None),
          77: ("GET_CNAP", None, None, None),
          78: ("MANAGE_IP_CALLS", None, None, None),
        }
wds_reset_rsp_tlvs = {  # 0
          2: "WDS/Reset Response/Result Code",
        }

wds_set_event_req_tlvs = {  # 1
          16: "WDS/Set Event Report Request/Channel Rate Indicator",
          17: "WDS/Set Event Report Request/Transfer Statistics Indicator",
          18: "WDS/Set Event Report Request/Data Bearer Technology Indicator",
          19: "WDS/Set Event Report Request/Dormancy Status Indicator",
          20: "WDS/Set Event Report Request/MIP Status Indicator",
          21: "WDS/Set Event Report Request/Current Data Bearer Technology Indicator",
        }

wds_set_event_rsp_tlvs = {  # 1
          2: "WDS/Set Event Report Response/Result Code",
        }

wds_set_event_ind_tlvs = {  # 1
          16: "WDS/Event Report/TX Packet Successes",
          17: "WDS/Event Report/RX Packet Successes",
          18: "WDS/Event Report/TX Packet Errors",
          19: "WDS/Event Report/RX Packet Errors",
          20: "WDS/Event Report/TX Overflows",
          21: "WDS/Event Report/RX Overflows",
          22: "WDS/Event Report/Channel Rates",
          23: "WDS/Event Report/Data Bearer Technology",
          24: "WDS/Event Report/Dormancy Status",
          25: "WDS/Event Report/TX Bytes",
          26: "WDS/Event Report/RX Bytes",
          27: "WDS/Event Report/MIP Status",
          29: "WDS/Event Report/Current Data Bearer Technology",
        }

wds_abort_req_tlvs = {  # 2
          1: "WDS/Abort Request/Transaction ID",
        }

wds_abort_rsp_tlvs = {  # 2
          2: "WDS/Abort Response/Result Code",
        }

wds_start_net_req_tlvs = {  # 32
          16: "WDS/Start Network Interface Request/Primary DNS",
          17: "WDS/Start Network Interface Request/Secondary DNS",
          18: "WDS/Start Network Interface Request/Primary NBNS",
          19: "WDS/Start Network Interface Request/Secondary NBNS",
          20: "WDS/Start Network Interface Request/Context APN Name",
          21: "WDS/Start Network Interface Request/IP Address",
          22: "WDS/Start Network Interface Request/Authentication",
          23: "WDS/Start Network Interface Request/Username",
          24: "WDS/Start Network Interface Request/Password",
          25: "WDS/Start Network Interface Request/IP Family",
          48: "WDS/Start Network Interface Request/Technology Preference",
          49: "WDS/Start Network Interface Request/3GPP Profile Identifier",
          50: "WDS/Start Network Interface Request/3GPP2 Profile Identifier",
          51: "WDS/Start Network Interface Request/Autoconnect",
          52: "WDS/Start Network Interface Request/Extended Technology Preference",
          53: "WDS/Start Network Interface Request/Call Type",
        }

wds_start_net_rsp_tlvs = {  # 32
          1: "WDS/Start Network Interface Response/Packet Data Handle",
          2: "WDS/Start Network Interface Response/Result Code",
          16: "WDS/Start Network Interface Response/Call End Reason",
          17: "WDS/Start Network Interface Response/Verbose Call End Reason",
        }

wds_stop_net_req_tlvs = {  # 33
          1: "WDS/Stop Network Interface Request/Packet Data Handle",
          16: "WDS/Stop Network Interface Request/Autoconnect",
        }

wds_stop_net_rsp_tlvs = {  # 33
          2: "WDS/Stop Network Interface Response/Result Code",
        }

wds_get_pkt_status_rsp_tlvs = {  # 34
          1: "WDS/Get Packet Service Status Response/Status",
          2: "WDS/Get Packet Service Status Response/Result Code",
        }

wds_get_pkt_status_ind_tlvs = {  # 34
          1: "WDS/Packet Service Status Report/Status",
          16: "WDS/Packet Service Status Report/Call End Reason",
          17: "WDS/Packet Service Status Report/Verbose Call End Reason",
        }

wds_get_rates_rsp_tlvs = {  # 35
          1: "WDS/Get Channel Rates Response/Channel Rates",
          2: "WDS/Get Channel Rates Response/Result Code",
        }

wds_get_statistics_req_tlvs = {  # 36
          1: "WDS/Get Packet Statistics Request/Packet Stats Mask",
        }

wds_get_statistics_rsp_tlvs = {  # 36
          2: "WDS/Get Packet Statistics Response/Result Code",
          16: "WDS/Get Packet Statistics Response/TX Packet Successes",
          17: "WDS/Get Packet Statistics Response/RX Packet Successes",
          18: "WDS/Get Packet Statistics Response/TX Packet Errors",
          19: "WDS/Get Packet Statistics Response/RX Packet Errors",
          20: "WDS/Get Packet Statistics Response/TX Overflows",
          21: "WDS/Get Packet Statistics Response/RX Overflows",
          25: "WDS/Get Packet Statistics Response/TX Bytes",
          26: "WDS/Get Packet Statistics Response/RX Bytes",
          27: "WDS/Get Packet Statistics Response/Previous TX Bytes",
          28: "WDS/Get Packet Statistics Response/Previous RX Bytes",
        }

wds_g0_dormant_rsp_tlvs = {  # 37
          2: "WDS/Go Dormant Response/Result Code",
        }

wds_g0_active_rsp_tlvs = {  # 38
          2: "WDS/Go Active Response/Result Code",
        }

wds_create_profile_req_tlvs = {  # 39
          1: "WDS/Create Profile Request/Profile Type",
          16: "WDS/Create Profile Request/Profile Name",
          17: "WDS/Create Profile Request/PDP Type",
          20: "WDS/Create Profile Request/APN Name",
          21: "WDS/Create Profile Request/Primary DNS",
          22: "WDS/Create Profile Request/Secondary DNS",
          23: "WDS/Create Profile Request/UMTS Requested QoS",
          24: "WDS/Create Profile Request/UMTS Minimum QoS",
          25: "WDS/Create Profile Request/GPRS Requested QoS",
          26: "WDS/Create Profile Request/GPRS Minimum QoS",
          27: "WDS/Create Profile Request/Username",
          28: "WDS/Create Profile Request/Password",
          29: "WDS/Create Profile Request/Authentication",
          30: "WDS/Create Profile Request/IP Address",
          31: "WDS/Create Profile Request/P-CSCF",
        }

wds_create_profile_rsp_tlvs = {  # 39
          1: "WDS/Create Profile Response/Profile Identifier",
          2: "WDS/Create Profile Response/Result Code",
        }

wds_modify_profile_req_tlvs = {  # 40
          1: "WDS/Modify Profile Request/Profile Identifier",
          16: "WDS/Modify Profile Request/Profile Name",
          17: "WDS/Modify Profile Request/PDP Type",
          20: "WDS/Modify Profile Request/APN Name",
          21: "WDS/Modify Profile Request/Primary DNS",
          22: "WDS/Modify Profile Request/Secondary DNS",
          23: "WDS/Modify Profile Request/UMTS Requested QoS",
          24: "WDS/Modify Profile Request/UMTS Minimum QoS",
          25: "WDS/Modify Profile Request/GPRS Requested QoS",
          26: "WDS/Modify Profile Request/GPRS Minimum QoS",
          27: "WDS/Modify Profile Request/Username",
          28: "WDS/Modify Profile Request/Password",
          29: "WDS/Modify Profile Request/Authentication",
          30: "WDS/Modify Profile Request/IP Address",
          31: "WDS/Modify Profile Request/P-CSCF",
          32: "WDS/Modify Profile Request/PDP Access Control Flag",
          33: "WDS/Modify Profile Request/P-CSCF Address Using DHCP",
          34: "WDS/Modify Profile Request/IM CN Flag",
          35: "WDS/Modify Profile Request/Traffic Flow Template ID1 Parameters",
          36: "WDS/Modify Profile Request/Traffic Flow Template ID2 Parameters",
          37: "WDS/Modify Profile Request/PDP Context Number",
          38: "WDS/Modify Profile Request/PDP Context Secondary Flag",
          39: "WDS/Modify Profile Request/PDP Context Primary ID",
          40: "WDS/Modify Profile Request/IPv6 Address",
          41: "WDS/Modify Profile Request/Requested QoS",
          42: "WDS/Modify Profile Request/Minimum QoS",
          43: "WDS/Modify Profile Request/Primary IPv6",
          44: "WDS/Modify Profile Request/Secondary IPv6",
          45: "WDS/Modify Profile Request/Address Allocation Preference",
          46: "WDS/Modify Profile Request/LTE QoS Parameters",
          144: "WDS/Modify Profile Request/Negotiate DNS Server Prefrence",
          145: "WDS/Modify Profile Request/PPP Session Close Timer DO",
          146: "WDS/Modify Profile Request/PPP Session Close Timer 1X",
          147: "WDS/Modify Profile Request/Allow Linger",
          148: "WDS/Modify Profile Request/LCP ACK Timeout",
          149: "WDS/Modify Profile Request/IPCP ACK Timeout",
          150: "WDS/Modify Profile Request/Authentication Timeout",
          154: "WDS/Modify Profile Request/Authentication Protocol",
          155: "WDS/Modify Profile Request/User ID",
          156: "WDS/Modify Profile Request/Authentication Password",
          157: "WDS/Modify Profile Request/Data Rate",
          158: "WDS/Modify Profile Request/Application Type",
          159: "WDS/Modify Profile Request/Data Mode",
          160: "WDS/Modify Profile Request/Application Priority",
          161: "WDS/Modify Profile Request/APN String",
          162: "WDS/Modify Profile Request/PDN Type",
          163: "WDS/Modify Profile Request/P-CSCF Address Needed",
          164: "WDS/Modify Profile Request/Primary IPv4 Address",
          165: "WDS/Modify Profile Request/Secondary IPv4 Address",
          166: "WDS/Modify Profile Request/Primary IPv6 Address",
          167: "WDS/Modify Profile Request/Secondary IPv6 Address",
        }

wds_modify_profile_rsp_tlvs = {  # 40
          2: "WDS/Modify Profile Response/Result Code",
          151: "WDS/Modify Profile Request/LCP Config Retry Count",
          152: "WDS/Modify Profile Request/IPCP Config Retry Count",
          153: "WDS/Modify Profile Request/Authentication Retry",
          224: "WDS/Modify Profile Request/Extended Error Code",
        }

wds_delete_profile_req_tlvs = {  # 41
          1: "WDS/Delete Profile Request/Profile Identifier",
        }

wds_delete_profile_rsp_tlvs = {  # 41
          2: "WDS/Delete Profile Response/Result Code",
        }

wds_get_profile_list_rsp_tlvs = {  # 42
          1: "WDS/Get Profile List Response/Profile List",
          2: "WDS/Get Profile List Response/Result Code",
        }

wds_get_profile_req_tlvs = {  # 43
          1: "WDS/Get Profile Settings Request/Profile Identifier",
        }

wds_get_profile_rsp_tlvs = {  # 43
          2: "WDS/Get Profile Settings Response/Result Code",
          16: "WDS/Get Profile Settings Response/Profile Name",
          17: "WDS/Get Profile Settings Response/PDP Type",
          20: "WDS/Get Profile Settings Response/APN Name",
          21: "WDS/Get Profile Settings Response/Primary DNS",
          22: "WDS/Get Profile Settings Response/Secondary DNS",
          23: "WDS/Get Profile Settings Response/UMTS Requested QoS",
          24: "WDS/Get Profile Settings Response/UMTS Minimum QoS",
          25: "WDS/Get Profile Settings Response/GPRS Requested QoS",
          26: "WDS/Get Profile Settings Response/GPRS Minimum QoS",
          27: "WDS/Get Profile Settings Response/Username",
          29: "WDS/Get Profile Settings Response/Authentication",
          30: "WDS/Get Profile Settings Response/IP Address",
          31: "WDS/Get Profile Settings Response/P-CSCF",
        }

wds_get_defaults_req_tlvs = {  # 44
          1: "WDS/Get Default Settings Request/Profile Type",
        }

wds_get_defaults_rsp_tlvs = {  # 44
          2: "WDS/Get Default Settings Response/Result Code",
          16: "WDS/Get Default Settings Response/Profile Name",
          17: "WDS/Get Default Settings Response/PDP Type",
          20: "WDS/Get Default Settings Response/APN Name",
          21: "WDS/Get Default Settings Response/Primary DNS",
          22: "WDS/Get Default Settings Response/Secondary DNS",
          23: "WDS/Get Default Settings Response/UMTS Requested QoS",
          24: "WDS/Get Default Settings Response/UMTS Minimum QoS",
          25: "WDS/Get Default Settings Response/GPRS Requested QoS",
          26: "WDS/Get Default Settings Response/GPRS Minimum QoS",
          27: "WDS/Get Default Settings Response/Username",
          28: "WDS/Get Default Settings Response/Password",
          29: "WDS/Get Default Settings Response/Authentication",
          30: "WDS/Get Default Settings Response/IP Address",
          31: "WDS/Get Default Settings Response/P-CSCF",
          32: "WDS/Get Default Settings Response/PDP Access Control Flag",
          33: "WDS/Get Default Settings Response/P-CSCF Address Using DHCP",
          34: "WDS/Get Default Settings Response/IM CN Flag",
          35: "WDS/Get Default Settings Response/Traffic Flow Template ID1 Parameters",
          36: "WDS/Get Default Settings Response/Traffic Flow Template ID2 Parameters",
          37: "WDS/Get Default Settings Response/PDP Context Number",
          38: "WDS/Get Default Settings Response/PDP Context Secondary Flag",
          39: "WDS/Get Default Settings Response/PDP Context Primary ID",
          40: "WDS/Get Default Settings Response/IPv6 Address",
          41: "WDS/Get Default Settings Response/Requested QoS",
          42: "WDS/Get Default Settings Response/Minimum QoS",
          43: "WDS/Get Default Settings Response/Primary DNS IPv6 Address",
          44: "WDS/Get Default Settings Response/Secondary DNS IPv6 Address",
          45: "WDS/Get Default Settings Response/DHCP NAS Preference",
          46: "WDS/Get Default Settings Response/LTE QoS Parameters",
          144: "WDS/Get Default Settings Response/Negotiate DSN Server Preferences",
          145: "WDS/Get Default Settings Response/PPP Session CLose Timer DO",
          146: "WDS/Get Default Settings Response/PPP Session Close Timer 1X",
          147: "WDS/Get Default Settings Response/Allow Lingering Interface",
          148: "WDS/Get Default Settings Response/LCP ACK Timeout",
          149: "WDS/Get Default Settings Response/IPCP ACK Timeout",
          150: "WDS/Get Default Settings Response/Authentication Timeout",
          151: "WDS/Get Default Settings Response/LCP Config Retry Count",
          152: "WDS/Get Default Settings Response/IPCP Config Retry Count",
          153: "WDS/Get Default Settings Response/Authentication Retry",
          154: "WDS/Get Default Settings Response/Authentication Protocol",
          155: "WDS/Get Default Settings Response/User ID",
          156: "WDS/Get Default Settings Response/Authentication Password",
          157: "WDS/Get Default Settings Response/Data Rate",
          158: "WDS/Get Default Settings Response/Application Type",
          159: "WDS/Get Default Settings Response/Data Mode",
          160: "WDS/Get Default Settings Response/Application Priority",
          161: "WDS/Get Default Settings Response/APN String",
          162: "WDS/Get Default Settings Response/PDN Type",
          163: "WDS/Get Default Settings Response/P-CSCF Address Needed",
          164: "WDS/Get Default Settings Response/Primary DNS Address",
          165: "WDS/Get Default Settings Response/Secondary DNS Address",
          166: "WDS/Get Default Settings Response/Primary IPv6 Address",
          167: "WDS/Get Default Settings Response/Secondary IPv6 Address",
          224: "WDS/Get Default Settings Response/Extended Error Code",
        }

wds_get_settings_req_tlvs = {  # 45
          16: "WDS/Get Current Settings Request/Requested Settings",
        }

wds_get_settings_rsp_tlvs = {  # 45
          2: "WDS/Get Current Settings Response/Result Code",
          16: "WDS/Get Current Settings Response/Profile Name",
          17: "WDS/Get Current Settings Response/PDP Type",
          20: "WDS/Get Current Settings Response/APN Name",
          21: "WDS/Get Current Settings Response/Primary DNS",
          22: "WDS/Get Current Settings Response/Secondary DNS",
          23: "WDS/Get Current Settings Response/UMTS Granted QoS",
          25: "WDS/Get Current Settings Response/GPRS Granted QoS",
          27: "WDS/Get Current Settings Response/Username",
          29: "WDS/Get Current Settings Response/Authentication",
          30: "WDS/Get Current Settings Response/IP Address",
          31: "WDS/Get Current Settings Response/Profile ID",
          32: "WDS/Get Current Settings Response/Gateway Address",
          33: "WDS/Get Current Settings Response/Gateway Subnet Mask",
          34: "WDS/Get Current Settings Response/P-CSCF",
          35: "WDS/Get Current Settings Response/P-CSCF Server Address List",
          36: "WDS/Get Current Settings Response/P-CSCF Domain Name List",
          37: "WDS/Get Current Settings Response/IPv6 Address",
          38: "WDS/Get Current Settings Response/IPv6 Gateway Address",
          39: "WDS/Get Current Settings Response/Primary IPv6 DNS",
          40: "WDS/Get Current Settings Response/Secondary IPv6 DNS",
          41: "WDS/Get Current Settings Response/MTU",
          42: "WDS/Get Current Settings Response/Domain Name List",
          43: "WDS/Get Current Settings Response/IP Family",
          44: "WDS/Get Current Settings Response/IM CN Flag",
          45: "WDS/Get Current Settings Response/Extended Technology",
          46: "WDS/Get Current Settings Response/P-CSCF IPv6 Address List",
        }

wds_set_mip_req_tlvs = {  # 46
          1: "WDS/Set MIP Mode Request/Mobile IP Mode",
        }

wds_set_mip_rsp_tlvs = {  # 46
          2: "WDS/Set MIP Mode Response/Result Code",
        }

wds_get_mip_rsp_tlvs = {  # 47
          1: "WDS/Get MIP Mode Response/Mobile IP Mode",
          2: "WDS/Get MIP Mode Response/Result Code",
        }

wds_get_dormancy_rsp_tlvs = {  # 48
          1: "WDS/Get Dormancy Response/Dormancy Status",
          2: "WDS/Get Dormancy Response/Result Code",
        }

wds_get_autoconnect_rsp_tlvs = {  # 52
          1: "WDS/Get Autoconnect Setting Response/Autoconnect",
          2: "WDS/Get Autoconnect Setting Response/Result Code",
          16: "WDS/Get Autoconnect Setting Response/Roam",
        }

wds_get_duration_rsp_tlvs = {  # 53
          1: "WDS/Get Data Session Duration Response/Duration",
          2: "WDS/Get Data Session Duration Response/Result Code",
          16: "WDS/Get Data Session Duration Response/Previous Duration",
          17: "WDS/Get Data Session Duration Response/Active Duration",
          18: "WDS/Get Data Session Duration Response/Previous Active Duration",
        }

wds_get_modem_status_rsp_tlvs = {  # 54
          1: "WDS/Get Modem Status Response/Status",
          2: "WDS/Get Modem Status Response/Result Code",
          16: "WDS/Get Modem Status Response/Call End Reason",
        }

wds_get_modem_status_ind_tlvs = {  # 54
          1: "WDS/Modem Status Report/Status",
          16: "WDS/Modem Status Report/Call End Reason",
        }

wds_get_data_bearer_rsp_tlvs = {  # 55
          1: "WDS/Get Data Bearer Technology Response/Technology",
          2: "WDS/Get Data Bearer Technology Response/Result Code",
          16: "WDS/Get Data Bearer Technology Response/Last Call Technology",
        }

wds_get_modem_info_req_tlvs = {  # 56
          1: "WDS/Get Modem Info Request/Requested Status",
          16: "WDS/Get Modem Info Request/Connection Status Indicator",
          17: "WDS/Get Modem Info Request/Transfer Statistics Indicator",
          18: "WDS/Get Modem Info Request/Dormancy Status Indicator",
          19: "WDS/Get Modem Info Request/Data Bearer Technology Indicator",
          20: "WDS/Get Modem Info Request/Channel Rate Indicator",
        }

wds_get_modem_info_rsp_tlvs = {  # 56
          2: "WDS/Get Modem Info Response/Result Code",
          16: "WDS/Get Modem Info Response/Status",
          17: "WDS/Get Modem Info Response/Call End Reason",
          18: "WDS/Get Modem Info Response/TX Bytes",
          19: "WDS/Get Modem Info Response/RX Bytes",
          20: "WDS/Get Modem Info Response/Dormancy Status",
          21: "WDS/Get Modem Info Response/Technology",
          22: "WDS/Get Modem Info Response/Rates",
          23: "WDS/Get Modem Info Response/Previous TX Bytes",
          24: "WDS/Get Modem Info Response/Previous RX Bytes",
          25: "WDS/Get Modem Info Duration Response/Active Duration",
        }

wds_get_modem_info_ind_tlvs = {  # 56
          16: "WDS/Modem Info Report/Status",
          17: "WDS/Modem Info Report/Call End Reason",
          18: "WDS/Modem Info Report/TX Bytes",
          19: "WDS/Modem Info Report/RX Bytes",
          20: "WDS/Modem Info Report/Dormancy Status",
          21: "WDS/Modem Info Report/Technology",
          22: "WDS/Modem Info Report/Rates",
        }

wds_get_active_mip_rsp_tlvs = {  # 60
          1: "WDS/Get Active MIP Profile Response/Index",
          2: "WDS/Get Active MIP Profile Response/Result Code",
        }

wds_set_active_mip_req_tlvs = {  # 61
          1: "WDS/Set Active MIP Profile Request/Index",
        }

wds_set_active_mip_rsp_tlvs = {  # 61
          2: "WDS/Set Active MIP Profile Response/Result Code",
        }

wds_get_mip_profile_req_tlvs = {  # 62
          1: "WDS/Get MIP Profile Request/Index",
        }

wds_get_mip_profile_rsp_tlvs = {  # 62
          2: "WDS/Get MIP Profile Response/Result Code",
          16: "WDS/Get MIP Profile Response/State",
          17: "WDS/Get MIP Profile Response/Home Address",
          18: "WDS/Get MIP Profile Response/Primary Home Agent Address",
          19: "WDS/Get MIP Profile Response/Secondary Home Agent Address",
          20: "WDS/Get MIP Profile Response/Reverse Tunneling",
          21: "WDS/Get MIP Profile Response/NAI",
          22: "WDS/Get MIP Profile Response/HA SPI",
          23: "WDS/Get MIP Profile Response/AAA SPI",
          26: "WDS/Get MIP Profile Response/HA State",
          27: "WDS/Get MIP Profile Response/AAA State",
        }

wds_set_mip_profile_req_tlvs = {  # 63
          1: "WDS/Set MIP Profile Request/Index",
          16: "WDS/Set MIP Profile Request/State",
          17: "WDS/Set MIP Profile Request/Home Address",
          18: "WDS/Set MIP Profile Request/Primary Home Agent Address",
          19: "WDS/Set MIP Profile Request/Secondary Home Agent Address",
          20: "WDS/Set MIP Profile Request/Reverse Tunneling",
          21: "WDS/Set MIP Profile Request/NAI",
          22: "WDS/Set MIP Profile Request/HA SPI",
          23: "WDS/Set MIP Profile Requeste/AAA SPI",
          24: "WDS/Set MIP Profile Request/MN-HA",
          25: "WDS/Set MIP Profile Request/MN-AAA",
        }

wds_set_mip_profile_rsp_tlvs = {  # 63
          2: "WDS/Set MIP Profile Response/Result Code",
        }

wds_get_mip_params_rsp_tlvs = {  # 64
          2: "WDS/Get MIP Parameters Response/Result Code",
          16: "WDS/Get MIP Parameters Response/Mobile IP Mode",
          17: "WDS/Get MIP Parameters Response/Retry Attempt Limit",
          18: "WDS/Get MIP Parameters Response/Retry Attempt Interval",
          19: "WDS/Get MIP Parameters Response/Re-Registration Period",
          20: "WDS/Get MIP Parameters Response/Re-Registration Only With Traffic",
          21: "WDS/Get MIP Parameters Response/MN-HA Authenticator Calculator",
          22: "WDS/Get MIP Parameters Response/MN-HA RFC 2002 BIS Authentication",
        }

wds_set_mip_params_req_tlvs = {  # 65
          1: "WDS/Set MIP Parameters Request/SPC",
          16: "WDS/Set MIP Parameters Request/Mobile IP Mode",
          17: "WDS/Set MIP Parameters Request/Retry Attempt Limit",
          18: "WDS/Set MIP Parameters Request/Retry Attempt Interval",
          19: "WDS/Set MIP Parameters Request/Re-Registration Period",
          20: "WDS/Set MIP Parameters Request/Re-Registration Only With Traffic",
          21: "WDS/Set MIP Parameters Request/MN-HA Authenticator Calculator",
          22: "WDS/Set MIP Parameters Request/MN-HA RFC 2002 BIS Authentication",
        }

wds_set_mip_params_rsp_tlvs = {  # 65
          2: "WDS/Set MIP Parameters Response/Result Code",
        }

wds_get_last_mip_status_rsp_tlvs = {  # 66
          1: "WDS/Get Last MIP Status Response/Status",
          2: "WDS/Get Last MIP Status Response/Result Code",
        }

wds_get_aaa_auth_status_rsp_tlvs = {  # 67
          1: "WDS/Get AN-AAA Authentication Status Response/Status",
          2: "WDS/Get AN-AAA Authentication Status Response/Result Code",
        }

wds_get_cur_data_bearer_rsp_tlvs = {  # 68
          1: "WDS/Get Current Data Bearer Technology Response/Technology",
          2: "WDS/Get Current Data Bearer Technology Response/Result Code",
        }

wds_get_call_list_req_tlvs = {  # 69
          16: "WDS/Get Call List Request/List Type",
        }

wds_get_call_list_rsp_tlvs = {  # 69
          2: "WDS/Get Call List Response/Result Code",
          16: "WDS/Get Call List Response/Full List",
          17: "WDS/Get Call List Response/ID List",
        }

wds_get_call_entry_req_tlvs = {  # 70
          1: "WDS/Get Call Record Request/Record ID",
        }

wds_get_call_entry_rsp_tlvs = {  # 70
          1: "WDS/Get Call Record Response/Record",
          2: "WDS/Get Call Record Response/Result Code",
        }

wds_clear_call_list_rsp_tlvs = {  # 71
          2: "WDS/Clear Call List Response/Result Code",
        }

wds_get_call_list_max_rsp_tlvs = {  # 72
          1: "WDS/Get Call List Max Size Response/Maximum",
          2: "WDS/Get Call List Max Size Response/Result Code",
        }

wds_set_autoconnect_req_tlvs = {  # 81
          1: "WDS/Set Autoconnect Setting Request/Autoconnect",
          16: "WDS/Set Autoconnect Setting Request/Roam",
        }

wds_set_autoconnect_rsp_tlvs = {  # 81
          2: "WDS/Set Autoconnect Setting Response/Result Code",
        }

wds_get_dns_rsp_tlvs = {  # 82
          2: "WDS/Get DNS Setting Response/Result Code",
          16: "WDS/Get DNS Setting Response/Primary",
          17: "WDS/Get DNS Setting Response/Secondary",
          18: "WDS/Get DNS Setting Response/Primary IPv6",
          19: "WDS/Get DNS Setting Response/Secondary IPv6",
        }

wds_set_dns_req_tlvs = {  # 83
          16: "WDS/Set DNS Setting Request/Primary",
          17: "WDS/Set DNS Setting Request/Secondary",
          18: "WDS/Set DNS Setting Request/Primary IPv6 Address",
          19: "WDS/Set DNS Setting Request/Secondary IPv6 Address",
        }

wds_set_dns_rsp_tlvs = {  # 83
          2: "WDS/Set DNS Setting Response/Result Code",
        }

wds_cmds = {
          0: ("RESET", None, wds_reset_rsp_tlvs, None),
          1: ("SET_EVENT", wds_set_event_req_tlvs, wds_set_event_rsp_tlvs, wds_set_event_ind_tlvs),
          2: ("ABORT", wds_abort_req_tlvs, wds_abort_rsp_tlvs, None),
          32: ("START_NET", wds_start_net_req_tlvs, wds_start_net_rsp_tlvs, None),
          33: ("STOP_NET", wds_stop_net_req_tlvs, wds_stop_net_rsp_tlvs, None),
          34: ("GET_PKT_STATUS", None, wds_get_pkt_status_rsp_tlvs, wds_get_pkt_status_ind_tlvs),
          35: ("GET_RATES", None, wds_get_rates_rsp_tlvs, None),
          36: ("GET_STATISTICS", wds_get_statistics_req_tlvs, wds_get_statistics_rsp_tlvs, None),
          37: ("G0_DORMANT", None, wds_g0_dormant_rsp_tlvs, None),
          38: ("G0_ACTIVE", None, wds_g0_active_rsp_tlvs, None),
          39: ("CREATE_PROFILE", wds_create_profile_req_tlvs, wds_create_profile_rsp_tlvs, None),
          40: ("MODIFY_PROFILE", wds_modify_profile_req_tlvs, wds_modify_profile_rsp_tlvs, None),
          41: ("DELETE_PROFILE", wds_delete_profile_req_tlvs, wds_delete_profile_rsp_tlvs, None),
          42: ("GET_PROFILE_LIST", None, wds_get_profile_list_rsp_tlvs, None),
          43: ("GET_PROFILE", wds_get_profile_req_tlvs, wds_get_profile_rsp_tlvs, None),
          44: ("GET_DEFAULTS", wds_get_defaults_req_tlvs, wds_get_defaults_rsp_tlvs, None),
          45: ("GET_SETTINGS", wds_get_settings_req_tlvs, wds_get_settings_rsp_tlvs, None),
          46: ("SET_MIP", wds_set_mip_req_tlvs, wds_set_mip_rsp_tlvs, None),
          47: ("GET_MIP", None, wds_get_mip_rsp_tlvs, None),
          48: ("GET_DORMANCY", None, wds_get_dormancy_rsp_tlvs, None),
          52: ("GET_AUTOCONNECT", None, wds_get_autoconnect_rsp_tlvs, None),
          53: ("GET_DURATION", None, wds_get_duration_rsp_tlvs, None),
          54: ("GET_MODEM_STATUS", None, wds_get_modem_status_rsp_tlvs, wds_get_modem_status_ind_tlvs),
          55: ("GET_DATA_BEARER", None, wds_get_data_bearer_rsp_tlvs, None),
          56: ("GET_MODEM_INFO", wds_get_modem_info_req_tlvs, wds_get_modem_info_rsp_tlvs, wds_get_modem_info_ind_tlvs),
          60: ("GET_ACTIVE_MIP", None, wds_get_active_mip_rsp_tlvs, None),
          61: ("SET_ACTIVE_MIP", wds_set_active_mip_req_tlvs, wds_set_active_mip_rsp_tlvs, None),
          62: ("GET_MIP_PROFILE", wds_get_mip_profile_req_tlvs, wds_get_mip_profile_rsp_tlvs, None),
          63: ("SET_MIP_PROFILE", wds_set_mip_profile_req_tlvs, wds_set_mip_profile_rsp_tlvs, None),
          64: ("GET_MIP_PARAMS", None, wds_get_mip_params_rsp_tlvs, None),
          65: ("SET_MIP_PARAMS", wds_set_mip_params_req_tlvs, wds_set_mip_params_rsp_tlvs, None),
          66: ("GET_LAST_MIP_STATUS", None, wds_get_last_mip_status_rsp_tlvs, None),
          67: ("GET_AAA_AUTH_STATUS", None, wds_get_aaa_auth_status_rsp_tlvs, None),
          68: ("GET_CUR_DATA_BEARER", None, wds_get_cur_data_bearer_rsp_tlvs, None),
          69: ("GET_CALL_LIST", wds_get_call_list_req_tlvs, wds_get_call_list_rsp_tlvs, None),
          70: ("GET_CALL_ENTRY", wds_get_call_entry_req_tlvs, wds_get_call_entry_rsp_tlvs, None),
          71: ("CLEAR_CALL_LIST", None, wds_clear_call_list_rsp_tlvs, None),
          72: ("GET_CALL_LIST_MAX", None, wds_get_call_list_max_rsp_tlvs, None),
          77: ("SET_IP_FAMILY", None, None, None),
          81: ("SET_AUTOCONNECT", wds_set_autoconnect_req_tlvs, wds_set_autoconnect_rsp_tlvs, None),
          82: ("GET_DNS", None, wds_get_dns_rsp_tlvs, None),
          83: ("SET_DNS", wds_set_dns_req_tlvs, wds_set_dns_rsp_tlvs, None),
          84: ("GET_PRE_DORMANCY", None, None, None),
          85: ("SET_CAM_TIMER", None, None, None),
          86: ("GET_CAM_TIMER", None, None, None),
          87: ("SET_SCRM", None, None, None),
          88: ("GET_SCRM", None, None, None),
          89: ("SET_RDUD", None, None, None),
          90: ("GET_RDUD", None, None, None),
          91: ("GET_SIPMIP_CALL_TYPE", None, None, None),
          92: ("SET_PM_PERIOD", None, None, None),
          93: ("SET_FORCE_LONG_SLEEP", None, None, None),
          94: ("GET_PM_PERIOD", None, None, None),
          95: ("GET_CALL_THROTTLE", None, None, None),
          96: ("GET_NSAPI", None, None, None),
          97: ("SET_DUN_CTRL_PREF", None, None, None),
          98: ("GET_DUN_CTRL_INFO", None, None, None),
          99: ("SET_DUN_CTRL_EVENT", None, None, None),
          100: ("PENDING_DUN_CTRL", None, None, None),
          105: ("GET_DATA_SYS", None, None, None),
          106: ("GET_LAST_DATA_STATUS", None, None, None),
          107: ("GET_CURR_DATA_SYS", None, None, None),
          108: ("GET_PDN_THROTTLE", None, None, None),
        }
wms_reset_rsp_tlvs = {  # 0
          2: "WMS/Reset Response/Result Code",
        }

wms_set_event_req_tlvs = {  # 1
          16: "WMS/Set Event Report Request/New MT Message Indicator",
        }

wms_set_event_rsp_tlvs = {  # 1
          2: "WMS/Set Event Report Response/Result Code",
        }

wms_set_event_ind_tlvs = {  # 1
          16: "WMS/Event Report/Received MT Message",
          17: "WMS/Event Report/Transfer Route MT Message",
          18: "WMS/Event Report/Message Mode",
        }

wms_raw_send_req_tlvs = {  # 32
          1: "WMS/Raw Send Request/Message Data",
          16: "WMS/Raw Send Request/Force On DC",
          17: "WMS/Raw Send Request/Follow On DC",
          18: "WMS/Raw Send Request/Link Control",
        }

wms_raw_send_rsp_tlvs = {  # 32
          2: "WMS/Raw Send Response/Result Code",
          16: "WMS/Raw Send Response/Cause Code",
          17: "WMS/Raw Send Response/Error Class",
          18: "WMS/Raw Send Response/Cause Info",
        }

wms_raw_write_req_tlvs = {  # 33
          1: "WMS/Raw Write Request/Message Data",
        }

wms_raw_write_rsp_tlvs = {  # 33
          1: "WMS/Raw Write Response/Message Index",
          2: "WMS/Raw Write Response/Result Code",
        }

wms_raw_read_req_tlvs = {  # 34
          1: "WMS/Raw Read Request/Message Index",
          16: "WMS/Raw Read Request/Message Mode",
        }

wms_raw_read_rsp_tlvs = {  # 34
          1: "WMS/Raw Read Response/Message Data",
          2: "WMS/Raw Read Response/Result Code",
        }

wms_modify_tag_req_tlvs = {  # 35
          1: "WMS/Modify Tag Request/Message Tag",
          16: "WMS/Modify Tag Request/Message Mode",
        }

wms_modify_tag_rsp_tlvs = {  # 35
          2: "WMS/Modify Tag Response/Result Code",
        }

wms_delete_req_tlvs = {  # 36
          1: "WMS/Delete Request/Memory Storage",
          16: "WMS/Delete Request/Message Index",
          17: "WMS/Delete Request/Message Tag",
          18: "WMS/Delete Request/Message Mode",
        }

wms_delete_rsp_tlvs = {  # 36
          2: "WMS/Delete Response/Result Code",
        }

wms_get_msg_protocol_rsp_tlvs = {  # 48
          1: "WMS/Get Message Protocol Response/Message Protocol",
          2: "WMS/Get Message Protocol Response/Result Code",
        }

wms_get_msg_list_req_tlvs = {  # 49
          1: "WMS/List Messages Request/Memory Storage",
          16: "WMS/List Messages Request/Message Tag",
          17: "WMS/List Messages Request/Message Mode",
        }

wms_get_msg_list_rsp_tlvs = {  # 49
          1: "WMS/List Messages Response/Message List",
          2: "WMS/List Messages Response/Result Code",
        }

wms_set_routes_req_tlvs = {  # 50
          1: "WMS/Set Routes Request/Route List",
          16: "WMS/Set Routes Request/Transfer Status Report",
        }

wms_set_routes_rsp_tlvs = {  # 50
          2: "WMS/Set Routes Response/Result Code",
        }

wms_get_routes_rsp_tlvs = {  # 51
          1: "WMS/Get Routes Response/Route List",
          2: "WMS/Get Routes Response/Result Code",
          16: "WMS/Get Routes Response/Transfer Status Report",
        }

wms_get_smsc_addr_rsp_tlvs = {  # 52
          1: "WMS/Get SMSC Address Response/Address",
          2: "WMS/Get SMSC Address Response/Result Code",
        }

wms_set_smsc_addr_req_tlvs = {  # 53
          1: "WMS/Set SMSC Address Request/Address",
          16: "WMS/Set SMSC Address Request/Address Type",
        }

wms_get_msg_list_max_req_tlvs = {  # 54
          1: "WMS/Get Storage Max Size Request/Memory Storage",
          16: "WMS/Get Storage Max Size Request/Message Mode",
        }

wms_get_msg_list_max_rsp_tlvs = {  # 54
          1: "WMS/Get Storage Max Size Response/Max Size",
          2: "WMS/Get Storage Max Size Response/Result Code",
          16: "WMS/Get Storage Max Size Response/Available Size",
        }

wms_send_ack_req_tlvs = {  # 55
          1: "WMS/Send ACK Request/ACK",
          16: "WMS/Send ACK Request/3GPP2 Failure Info",
          17: "WMS/Send ACK Request/3GPP Failure Info",
        }

wms_send_ack_rsp_tlvs = {  # 55
          2: "WMS/Send ACK Response/Result Code",
        }

wms_set_retry_period_req_tlvs = {  # 56
          1: "WMS/Set Retry Period Request/Period",
        }

wms_set_retry_period_rsp_tlvs = {  # 56
          2: "WMS/Set Retry Period Response/Result Code",
        }

wms_set_retry_interval_req_tlvs = {  # 57
          1: "WMS/Set Retry Interval Request/Interval",
        }

wms_set_retry_interval_rsp_tlvs = {  # 57
          2: "WMS/Set Retry Interval Response/Result Code",
        }

wms_set_dc_disco_timer_req_tlvs = {  # 58
          1: "WMS/Set DC Disconnect Timer Request/Timer",
        }

wms_set_dc_disco_timer_rsp_tlvs = {  # 58
          2: "WMS/Set DC Disconnect Timer Response/Result Code",
        }

wms_set_memory_status_req_tlvs = {  # 59
          1: "WMS/Set Memory Status Request/Status",
        }

wms_set_memory_status_rsp_tlvs = {  # 59
          2: "WMS/Set Memory Status Response/Result Code",
        }

wms_set_bc_activation_req_tlvs = {  # 60
          1: "WMS/Set Broadcast Activation Request/BC Info",
        }

wms_set_bc_activation_rsp_tlvs = {  # 60
          2: "WMS/Set Broadcast Activation Response/Result Code",
        }

wms_set_bc_config_req_tlvs = {  # 61
          1: "WMS/Set Broadcast Config Request/Mode",
          16: "WMS/Set Broadcast Config Request/3GPP Info",
          17: "WMS/Set Broadcast Config Request/3GPP2 Info",
        }

wms_set_bc_config_rsp_tlvs = {  # 61
          2: "WMS/Set Broadcast Config Response/Result Code",
        }

wms_get_bc_config_req_tlvs = {  # 62
          1: "WMS/Get Broadcast Config Request/Mode",
        }

wms_get_bc_config_rsp_tlvs = {  # 62
          2: "WMS/Get Broadcast Config Response/Result Code",
          16: "WMS/Get Broadcast Config Response/3GPP Info",
          17: "WMS/Get Broadcast Config Response/3GPP2 Info",
        }

wms_memory_full_ind_ind_tlvs = {  # 63
          1: "WMS/Memory Full Indication/Info",
        }

wms_get_domain_pref_rsp_tlvs = {  # 64
          1: "WMS/Get Domain Preference Response/Pref",
          2: "WMS/Get Domain Preference Response/Result Code",
        }

wms_set_domain_pref_req_tlvs = {  # 65
          1: "WMS/Set Domain Preference Request/Pref",
        }

wms_set_domain_pref_rsp_tlvs = {  # 65
          2: "WMS/Set Domain Preference Response/Result Code",
        }

wms_memory_send_req_tlvs = {  # 66
          1: "WMS/Send From Memory Store Request/Info",
        }

wms_memory_send_rsp_tlvs = {  # 66
          2: "WMS/Send From Memory Store Response/Result Code",
          16: "WMS/Send From Memory Store Response/Message ID",
          17: "WMS/Send From Memory Store Response/Cause Code",
          18: "WMS/Send From Memory Store Response/Error Class",
          19: "WMS/Send From Memory Store Response/Cause Info",
        }

wms_smsc_addr_ind_ind_tlvs = {  # 70
          1: "WMS/SMSC Address Indication/Address",
        }

wms_cmds = {
          0: ("RESET", None, wms_reset_rsp_tlvs, None),
          1: ("SET_EVENT", wms_set_event_req_tlvs, wms_set_event_rsp_tlvs, wms_set_event_ind_tlvs),
          32: ("RAW_SEND", wms_raw_send_req_tlvs, wms_raw_send_rsp_tlvs, None),
          33: ("RAW_WRITE", wms_raw_write_req_tlvs, wms_raw_write_rsp_tlvs, None),
          34: ("RAW_READ", wms_raw_read_req_tlvs, wms_raw_read_rsp_tlvs, None),
          35: ("MODIFY_TAG", wms_modify_tag_req_tlvs, wms_modify_tag_rsp_tlvs, None),
          36: ("DELETE", wms_delete_req_tlvs, wms_delete_rsp_tlvs, None),
          48: ("GET_MSG_PROTOCOL", None, wms_get_msg_protocol_rsp_tlvs, None),
          49: ("GET_MSG_LIST", wms_get_msg_list_req_tlvs, wms_get_msg_list_rsp_tlvs, None),
          50: ("SET_ROUTES", wms_set_routes_req_tlvs, wms_set_routes_rsp_tlvs, None),
          51: ("GET_ROUTES", None, wms_get_routes_rsp_tlvs, None),
          52: ("GET_SMSC_ADDR", None, wms_get_smsc_addr_rsp_tlvs, None),
          53: ("SET_SMSC_ADDR", wms_set_smsc_addr_req_tlvs, None, None),
          54: ("GET_MSG_LIST_MAX", wms_get_msg_list_max_req_tlvs, wms_get_msg_list_max_rsp_tlvs, None),
          55: ("SEND_ACK", wms_send_ack_req_tlvs, wms_send_ack_rsp_tlvs, None),
          56: ("SET_RETRY_PERIOD", wms_set_retry_period_req_tlvs, wms_set_retry_period_rsp_tlvs, None),
          57: ("SET_RETRY_INTERVAL", wms_set_retry_interval_req_tlvs, wms_set_retry_interval_rsp_tlvs, None),
          58: ("SET_DC_DISCO_TIMER", wms_set_dc_disco_timer_req_tlvs, wms_set_dc_disco_timer_rsp_tlvs, None),
          59: ("SET_MEMORY_STATUS", wms_set_memory_status_req_tlvs, wms_set_memory_status_rsp_tlvs, None),
          60: ("SET_BC_ACTIVATION", wms_set_bc_activation_req_tlvs, wms_set_bc_activation_rsp_tlvs, None),
          61: ("SET_BC_CONFIG", wms_set_bc_config_req_tlvs, wms_set_bc_config_rsp_tlvs, None),
          62: ("GET_BC_CONFIG", wms_get_bc_config_req_tlvs, wms_get_bc_config_rsp_tlvs, None),
          63: ("MEMORY_FULL_IND", None, None, wms_memory_full_ind_ind_tlvs),
          64: ("GET_DOMAIN_PREF", None, wms_get_domain_pref_rsp_tlvs, None),
          65: ("SET_DOMAIN_PREF", wms_set_domain_pref_req_tlvs, wms_set_domain_pref_rsp_tlvs, None),
          66: ("MEMORY_SEND", wms_memory_send_req_tlvs, wms_memory_send_rsp_tlvs, None),
          67: ("GET_MSG_WAITING", None, None, None),
          68: ("MSG_WAITING_IND", None, None, None),
          69: ("SET_PRIMARY_CLIENT", None, None, None),
          70: ("SMSC_ADDR_IND", None, None, wms_smsc_addr_ind_ind_tlvs),
          71: ("INDICATOR_REG", None, None, None),
          72: ("GET_TRANSPORT_INFO", None, None, None),
          73: ("TRANSPORT_INFO_IND", None, None, None),
          74: ("GET_NW_REG_INFO", None, None, None),
          75: ("NW_REG_INFO_IND", None, None, None),
          76: ("BIND_SUBSCRIPTION", None, None, None),
          77: ("GET_INDICATOR_REG", None, None, None),
          78: ("GET_SMS_PARAMETERS", None, None, None),
          79: ("SET_SMS_PARAMETERS", None, None, None),
          80: ("CALL_STATUS_IND", None, None, None),
        }

services = {
          0: ("ctl", ctl_cmds),
          1: ("wds", wds_cmds),
          2: ("dms", dms_cmds),
          3: ("nas", nas_cmds),
          4: ("qos", None),
          5: ("wms", wms_cmds),
          6: ("pds", pds_cmds),
          7: ("auth", auth_cmds),
          9: ("voice", voice_cmds),
          224: ("cat", cat_cmds),
          225: ("rms", rms_cmds),
          226: ("oma", oma_cmds),
        }
