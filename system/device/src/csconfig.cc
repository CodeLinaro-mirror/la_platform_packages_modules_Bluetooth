/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#define LOG_TAG "bt_cs_config"
#include "device/include/csconfig.h"
#include <bluetooth/log.h>
#include <stack>
#include "btcore/include/module.h"
#include "osi/include/config.h"
#include "osi/include/future.h"
#include "osi/include/properties.h"

using namespace bluetooth;
void parsecsProcedureSettings(const config_t&);
void parsecsConfigSettings(const config_t&);
bool ReadLocalConfigs(void);
void print_cs_configs(void);
void convertStringToSubEventLen(std::string, uint8_t *);
void convertStringToPreferredAnt(std::string, uint8_t *);
void convertStringToChannelMap(std::string, uint8_t *);
void print_cs_procedure_settings(void);
bool config_used = false;


void print_cs_configs();
void print_cs_procedure_settings();

typedef struct {
    uint8_t main_mode_type;
    uint8_t sub_mode_type;
    uint8_t main_mode_min_steps;
    uint8_t main_mode_max_steps;
    uint8_t main_mode_rep;
    uint8_t mode_0_steps;
    uint8_t role;
    uint8_t rtt_types;
    uint8_t cs_sync_phy;
    uint8_t channel_map_rep;
    uint8_t hop_algo_type;
    uint8_t user_shape;
    uint8_t user_channel_jump;
    uint8_t comp_signal_enable;
    // Note: config_id and channel_map are EXCLUDED (dynamic)
} tCS_CONFIG_STATIC;

typedef struct {
    uint16_t max_proc_duration;
    uint16_t min_period_between_proc;
    uint16_t max_period_between_proc;
    uint16_t max_proc_count;
    uint8_t min_subevent_len[3];
    uint8_t max_subevent_len[3];
    uint8_t phy;
    uint8_t tx_pwr_delta;
    uint8_t snr_control_initiator;
    uint8_t snr_control_reflector;
    // Note: enable, config_id, tone_ant_cfg_selection, preferred_peer_antenna 
    // are EXCLUDED (dynamic)
} tCS_PROCEDURE_STATIC;

/*
 * Static CS Config Table
 * Fields: main_mode_type, sub_mode_type, main_mode_min_steps, main_mode_max_steps,
 *         main_mode_rep, mode_0_steps, role, rtt_types, cs_sync_phy, channel_map_rep,
 *         hop_algo_type, user_shape, user_channel_jump, comp_signal_enable
 */
static const tCS_CONFIG_STATIC cs_config_static_data[] = {
    /* Config 0 (Security Level 1): SubMode=255, Steps=2-3, RTT=0 */
    {2, 255, 2, 3, 1, 2, 0, 0, 1, 1, 0, 0, 2, 0},
    /* Config 1 (Security Level 2): SubMode=1, Steps=5-6, RTT=0 */
    {2, 1, 5, 6, 1, 2, 0, 0, 1, 1, 0, 0, 2, 0},
    /* Config 2 (Security Level 3): SubMode=1, Steps=2-3, RTT=3 */
    {2, 1, 2, 3, 1, 2, 0, 3, 1, 1, 0, 0, 2, 0},
    /* Config 3 (Security Level 4): SubMode=1, Steps=2-3, RTT=4 */
    {2, 1, 2, 3, 1, 2, 0, 4, 1, 1, 0, 0, 2, 0}
};

/*
 * Static CS Procedure Table
 * Fields: max_proc_duration, min_period_between_proc, max_period_between_proc, max_proc_count,
 *         min_subevent_len[3], max_subevent_len[3], phy, tx_pwr_delta,
 *         snr_control_initiator, snr_control_reflector
 * Note: min_period_between_proc and max_period_between_proc are stored as time in milliseconds
 */
static const tCS_PROCEDURE_STATIC cs_procedure_static_data[] = {
    /* Procedure 0 (Frequency 0): MaxDuration=0x2710 (10000), min=1000ms, max=5000ms - LOW frequency */
    {0x2710, 1000, 5000, 0, {0xE2, 0x04, 0x00}, {0x80, 0x84, 0x1E}, 1, 128, 255, 255},
    /* Procedure 1 (Frequency 1): MaxDuration=0x2710 (10000), min=500ms, max=1000ms - MEDIUM frequency */
    {0x2710, 500, 1000, 0, {0xE2, 0x04, 0x00}, {0x80, 0x84, 0x1E}, 1, 128, 255, 255},
    /* Procedure 2 (Frequency 2): MaxDuration=0x2710 (10000), min=150ms, max=500ms - HIGH frequency */
    {0x2710, 150, 500, 0, {0xE2, 0x04, 0x00}, {0x80, 0x84, 0x1E}, 1, 128, 255, 255}
};





std::vector<tCS_PROCEDURE_PARAM> cs_procedure_settings;
std::vector<tCS_CONFIG> cs_config_settings;
unsigned long cs_config_settings_count, cs_procedure_settings_count;


void InitializecsConfigSettings(void) {
    cs_config_settings.clear();
    for (size_t i = 0; i < sizeof(cs_config_static_data) / sizeof(tCS_CONFIG_STATIC); i++) {
        tCS_CONFIG config;
        const tCS_CONFIG_STATIC* static_data = &cs_config_static_data[i];

        config.config_id = i;  // Default, will be computed dynamically
        config.main_mode_type = static_data->main_mode_type;
        config.sub_mode_type = static_data->sub_mode_type;
        config.main_mode_min_steps = static_data->main_mode_min_steps;
        config.main_mode_max_steps = static_data->main_mode_max_steps;
        config.main_mode_rep = static_data->main_mode_rep;
        config.mode_0_steps = static_data->mode_0_steps;
        config.role = static_data->role;
        config.rtt_types = static_data->rtt_types;
        config.cs_sync_phy = static_data->cs_sync_phy;
        config.channel_map_rep = static_data->channel_map_rep;
        config.hop_algo_type = static_data->hop_algo_type;
        config.user_shape = static_data->user_shape;
        config.user_channel_jump = static_data->user_channel_jump;
        config.comp_signal_enable = static_data->comp_signal_enable;

        // Set default channel_map (will be overridden dynamically)
        if (i == 0) {
            uint8_t default_map[10] = {252, 255, 127, 252, 255, 255, 255, 255, 255, 31};
            memcpy(config.channel_map, default_map, 10);
        } else {
            uint8_t default_map[10] = {84, 85, 85, 84, 85, 85, 85, 85, 85, 21};
            memcpy(config.channel_map, default_map, 10);
        }

        cs_config_settings.push_back(config);
    }
    log::info("All CS Configs are parsed successfully\n");
    print_cs_configs();
}

void InitializecsProcedureSettings(void) {
    cs_procedure_settings.clear();
    for (size_t i = 0; i < sizeof(cs_procedure_static_data) / sizeof(tCS_PROCEDURE_STATIC); i++) {
        tCS_PROCEDURE_PARAM proc;
        const tCS_PROCEDURE_STATIC* static_data = &cs_procedure_static_data[i];

        proc.enable = 0;  // Default, will be computed dynamically
        proc.config_id = 0;  // Default, will be computed dynamically
        proc.max_proc_duration = static_data->max_proc_duration;
        proc.min_period_between_proc = static_data->min_period_between_proc;
        proc.max_period_between_proc = static_data->max_period_between_proc;
        proc.max_proc_count = static_data->max_proc_count;
        memcpy(proc.min_subevent_len, static_data->min_subevent_len, 3);
        memcpy(proc.max_subevent_len, static_data->max_subevent_len, 3);
        proc.tone_ant_cfg_selection = 7;  // Default, will be computed dynamically
        proc.phy = static_data->phy;
        proc.tx_pwr_delta = static_data->tx_pwr_delta;
        proc.preferred_peer_antenna = 0x03;  // Default, will be computed dynamically
        proc.snr_control_initiator = static_data->snr_control_initiator;
        proc.snr_control_reflector = static_data->snr_control_reflector;

        cs_procedure_settings.push_back(proc);
    }

    log::info("All CS Procedure Settings are parsed successfully\n");
    print_cs_procedure_settings();
}

void InitializeConfigs(void) {
    InitializecsConfigSettings();
    InitializecsProcedureSettings();
}

void convertStringToChannelMap (std::string content, uint8_t *channelMap) {
    size_t pos = 0;
    std::string token;
    std::vector<std::string> res;
    while ((pos = content.find(" ")) != std::string::npos) {
        token = content.substr(0, pos);
        log::info("token : {}\n", token.c_str());
        content.erase(0, pos + 1);
        res.push_back(token);
    }
    res.push_back(content);
    for (size_t i=0; i<res.size(); i++) {
        channelMap[i] = atoi(res[i].c_str());
    }

}

void convertStringToPreferredAnt (std::string content, uint8_t *preferred_peer_antenna) {
    size_t pos = 0;
    std::string token;
    std::vector<std::string> res;
    while ((pos = content.find(" ")) != std::string::npos) {
        token = content.substr(0, pos);
        log::info("token :{}\n", token.c_str());
        content.erase(0, pos + 1);
        res.push_back(token);
    }
    res.push_back(content);

    uint8_t temp_preferred_peer_antenna = 0;
    for (size_t i=0; i<res.size(); i++) {
      temp_preferred_peer_antenna = (temp_preferred_peer_antenna | ((atoi(res[i].c_str()) << i)));
    }
    *preferred_peer_antenna = temp_preferred_peer_antenna;
}

void convertStringToSubEventLen (std::string content, uint8_t *subEventLen) {
    size_t pos = 0;
    std::string token;
    std::vector<std::string> res;
    while ((pos = content.find(" ")) != std::string::npos) {
        token = content.substr(0, pos);
        log::info("token :{}\n", token.c_str());
        content.erase(0, pos + 1);
        res.push_back(token);
    }
    res.push_back(content);
    for (size_t i=0; i<res.size(); i++) {
        subEventLen[i] = atoi(res[i].c_str());
        log::info("subEventLen {}: {}\n", i, subEventLen[i]);
    }
}

void print_cs_procedure_settings() {
    log::info("size : {}\n", (int)cs_procedure_settings.size());
    for (int i=0; i<(int)cs_procedure_settings.size(); i++) {
        log::info("***** CS_PROC_SETTINGS {} START ****************\n", i);
        log::info("ConfigId: {}\n", cs_procedure_settings[i].config_id);
        log::info("max_proc_duration: {}\n", cs_procedure_settings[i].max_proc_duration);
        log::info("min_period_between_proc: {}\n", cs_procedure_settings[i].min_period_between_proc);
        log::info("max_period_between_proc: {}\n", cs_procedure_settings[i].max_period_between_proc);
        log::info("max_proc_count: {}\n", cs_procedure_settings[i].max_proc_count);

        for (size_t j=0; j< CS_SUBEVENT_LEN_SIZE; j++) {
            log::info("min_subevent_len[{}]: {}\n", j, cs_procedure_settings[i].min_subevent_len[j]);
        }
        for (size_t j=0; j<CS_SUBEVENT_LEN_SIZE; j++) {
            log::info("max_subevent_len[{}]: {}\n", j, cs_procedure_settings[i].max_subevent_len[j]);
        }
        log::info("tone_ant_cfg_selection: {}\n", cs_procedure_settings[i].tone_ant_cfg_selection);
        log::info("phy: {}\n", cs_procedure_settings[i].phy);
        log::info("tx_pwr_delta: {}\n", cs_procedure_settings[i].tx_pwr_delta);
	log::info("preferred_peer_antenna : {} \n", cs_procedure_settings[i].preferred_peer_antenna);
	log::info("snr_control_initiator : {} \n", cs_procedure_settings[i].snr_control_initiator);
	log::info("snr_control_reflector : {} \n", cs_procedure_settings[i].snr_control_reflector);
        log::info("***** CS_PROC_SETTINGS {} END ****************\n", i);
    }
}

void parsecsConfigSettings(const config_t& config) {
   tCS_CONFIG temp_cs_config;

   for (int section_idx = 0; section_idx < 100; section_idx++) {
     char section_name[32];
     snprintf(section_name, sizeof(section_name), "cs_config_%d", section_idx);

     if (!config_has_section(config, section_name)) {
       break;
     }

     memset(&temp_cs_config, 0, sizeof(tCS_CONFIG));

     temp_cs_config.config_id = config_get_int(config, section_name, "config_id", 0);
     temp_cs_config.main_mode_type = config_get_int(config, section_name, "main_mode_type", 0);
     temp_cs_config.sub_mode_type = config_get_int(config, section_name, "sub_mode_type", 0);
     temp_cs_config.main_mode_min_steps = config_get_int(config, section_name, "main_mode_min_steps", 0);
     temp_cs_config.main_mode_max_steps = config_get_int(config, section_name, "main_mode_max_steps", 0);
     temp_cs_config.main_mode_rep = config_get_int(config, section_name, "main_mode_repetition", 0);
     temp_cs_config.mode_0_steps = config_get_int(config, section_name, "mode0_steps", 0);
     temp_cs_config.role = config_get_int(config, section_name, "role", 0);
     temp_cs_config.rtt_types = config_get_int(config, section_name, "rtt_types", 0);
     temp_cs_config.cs_sync_phy = config_get_int(config, section_name, "cs_sync_phy", 0);
     temp_cs_config.channel_map_rep = config_get_int(config, section_name, "channel_map_repetition", 0);
     temp_cs_config.hop_algo_type = config_get_int(config, section_name, "channel_selection_type", 0);
     temp_cs_config.user_shape = config_get_int(config, section_name, "channel_shape", 0);
     temp_cs_config.user_channel_jump = config_get_int(config, section_name, "channel_jump", 0);
     temp_cs_config.comp_signal_enable = config_get_int(config, section_name, "companion_signal_enable", 0);

     const std::string* channel_map_str = config_get_string(config, section_name, "channel_map", nullptr);
     if (channel_map_str && !channel_map_str->empty()) {
       convertStringToChannelMap(*channel_map_str, temp_cs_config.channel_map);
     }
     cs_config_settings.push_back(temp_cs_config);
   }

   cs_config_settings_count = cs_config_settings.size();
   log::info("Found and parsed {} CS Config settings.",
             cs_config_settings_count);
   print_cs_configs();
}

void parsecsProcedureSettings(const config_t& config) {
   tCS_PROCEDURE_PARAM temp_cs_proc_param;

   for (int section_idx = 0; section_idx < 100; section_idx++) {
     char section_name[32];
     snprintf(section_name, sizeof(section_name), "cs_procedure_%d", section_idx);

     if (!config_has_section(config, section_name)) {
       break;
     }

     memset(&temp_cs_proc_param, 0, sizeof(tCS_PROCEDURE_PARAM));

     temp_cs_proc_param.enable = 1;
     temp_cs_proc_param.config_id = section_idx;
     temp_cs_proc_param.max_proc_duration = config_get_int(config, section_name, "max_procedure_duration", 0);
     temp_cs_proc_param.min_period_between_proc = config_get_int(config, section_name, "min_period_between_procedures", 0);
     temp_cs_proc_param.max_period_between_proc = config_get_int(config, section_name, "max_period_between_procedures", 0);
     temp_cs_proc_param.max_proc_count = config_get_int(config, section_name, "max_procedure_count", 0);
     temp_cs_proc_param.tone_ant_cfg_selection = config_get_int(config, section_name, "tone_antenna_config_selection", 0);
     temp_cs_proc_param.phy = config_get_int(config, section_name, "phy", 0);
     temp_cs_proc_param.tx_pwr_delta = config_get_int(config, section_name, "tx_power_delta", 0);
     temp_cs_proc_param.snr_control_initiator = config_get_int(config, section_name, "snr_control_initiator", 0);
     temp_cs_proc_param.snr_control_reflector = config_get_int(config, section_name, "snr_control_reflector", 0);

     const std::string* min_subevent_str = config_get_string(config, section_name, "min_sub_event_len", nullptr);
     if (min_subevent_str && !min_subevent_str->empty()) {
       convertStringToSubEventLen(*min_subevent_str, temp_cs_proc_param.min_subevent_len);
     }

     const std::string* max_subevent_str = config_get_string(config, section_name, "max_sub_event_len", nullptr);
     if (max_subevent_str && !max_subevent_str->empty()) {
       convertStringToSubEventLen(*max_subevent_str, temp_cs_proc_param.max_subevent_len);
     }

     const std::string* preferred_ant_str = config_get_string(config, section_name, "preferred_peer_antenna", nullptr);
     if (preferred_ant_str && !preferred_ant_str->empty()) {
       convertStringToPreferredAnt(*preferred_ant_str, &temp_cs_proc_param.preferred_peer_antenna);
     }

     cs_procedure_settings.push_back(temp_cs_proc_param);
   }

   cs_procedure_settings_count = cs_procedure_settings.size();
   log::info("Found and parsed {} CS Procedure settings.",
             cs_procedure_settings_count);
   print_cs_procedure_settings();
}

bool ReadLocalConfigs(void){
  const char* config_path = CS_CONFIG_PATH_LOCAL;

  std::unique_ptr<config_t> config = config_new(config_path);
  if (!config) {
    log::error("Could not parse the CS config file {}", config_path);
    return false;
  }

  log::info("loading the CS config file {}", config_path);

  cs_config_settings.clear();
  cs_procedure_settings.clear();

  parsecsConfigSettings(*config);
  parsecsProcedureSettings(*config);

  if (cs_config_settings.empty() || cs_procedure_settings.empty()) {
    log::error("CS config file {} is present but empty or malformed.",
              config_path);
    return false;
  }

  return true;
}

void print_cs_configs() {
    log::info("size : {}\n", (int)cs_config_settings.size());
    for (int i=0; i<(int)cs_config_settings.size(); i++) {
        log::info("***** CS_CONFIG {} START ****************\n", i);
        log::info("ConfigId: {}\n", cs_config_settings[i].config_id);
        log::info("MainModeType: {}\n", cs_config_settings[i].main_mode_type);
        log::info("SubModeType: {}\n", cs_config_settings[i].sub_mode_type);
        log::info("MainModeMinSteps: {}\n", cs_config_settings[i].main_mode_min_steps);
        log::info("MainModeMaxSteps: {}\n", cs_config_settings[i].main_mode_max_steps);
        log::info("MainModeRepetition: {}\n", cs_config_settings[i].main_mode_rep);
        log::info("Mode0Steps: {}\n", cs_config_settings[i].mode_0_steps);
        log::info("Role: {}\n", cs_config_settings[i].role);

        log::info("RTTTypes: {}\n", cs_config_settings[i].rtt_types);
        log::info("CsSyncPhy: {}\n", cs_config_settings[i].cs_sync_phy);
        for (size_t j=0; j<CS_CHANNEL_MAP_SIZE; j++) {
            log::info("channelMap{}: {}\n", j, cs_config_settings[i].channel_map[j]);
        }
        log::info("ChannelMapRepetition: {}\n", cs_config_settings[i].channel_map_rep);
        log::info("ChannelSelectionType: {}\n", cs_config_settings[i].hop_algo_type);
        log::info("ChannelShape: {}\n", cs_config_settings[i].user_shape);
        log::info("ChannelJump: {}\n", cs_config_settings[i].user_channel_jump);
        log::info("CompanionSignalEnable: {}\n", cs_config_settings[i].comp_signal_enable);
        log::info("***** CS_CONFIG {} END ****************\n", i);
    }
}

bool get_cs_config_settings(int index, tCS_CONFIG *cs_config_setting) {
    if (index >= (int)cs_config_settings.size()) {
      log::warn("selected procedure parameters are not available in config");
      return false;
    }

    cs_config_setting->config_id = cs_config_settings[index].config_id;
    cs_config_setting->main_mode_type = cs_config_settings[index].main_mode_type;
    cs_config_setting->sub_mode_type =  cs_config_settings[index].sub_mode_type;
    cs_config_setting->main_mode_min_steps = cs_config_settings[index].main_mode_min_steps;
    cs_config_setting->main_mode_max_steps = cs_config_settings[index].main_mode_max_steps;
    cs_config_setting->main_mode_rep = cs_config_settings[index].main_mode_rep;
    cs_config_setting->mode_0_steps = cs_config_settings[index].mode_0_steps;
    cs_config_setting->role = cs_config_settings[index].role;
    cs_config_setting->rtt_types = cs_config_settings[index].rtt_types;

    cs_config_setting->cs_sync_phy = cs_config_settings[index].cs_sync_phy;
    for (size_t i=0; i<CS_CHANNEL_MAP_SIZE; i++) {
        cs_config_setting->channel_map[i] = cs_config_settings[index].channel_map[i];
    }
    cs_config_setting->channel_map_rep = cs_config_settings[index].channel_map_rep;
    cs_config_setting->hop_algo_type = cs_config_settings[index].hop_algo_type;
    cs_config_setting->user_shape = cs_config_settings[index].user_shape;
    cs_config_setting->cs_sync_phy = cs_config_settings[index].cs_sync_phy;
    cs_config_setting->user_channel_jump = cs_config_settings[index].user_channel_jump;
    cs_config_setting->comp_signal_enable = cs_config_settings[index].comp_signal_enable;
    return true;
}

bool get_cs_procedure_settings(int index,
		tCS_PROCEDURE_PARAM *cs_proc_setting) {
    if (index >= (int)cs_procedure_settings.size()) {
      log::warn("selected procedure parameters are not available in config");
      return false;
    }
    log::warn("local config used :{}", config_used);
    log::warn("index :{}", index);
    cs_proc_setting->max_proc_duration = cs_procedure_settings[index].max_proc_duration;
    cs_proc_setting->min_period_between_proc = cs_procedure_settings[index].min_period_between_proc;
    cs_proc_setting->max_period_between_proc = cs_procedure_settings[index].max_period_between_proc;
    cs_proc_setting->max_proc_count = cs_procedure_settings[index].max_proc_count;
    for (size_t i=0; i<CS_SUBEVENT_LEN_SIZE; i++) {
        cs_proc_setting->min_subevent_len[i] = cs_procedure_settings[index].min_subevent_len[i];
    }
    for (size_t i=0; i<CS_SUBEVENT_LEN_SIZE; i++) {
        cs_proc_setting->max_subevent_len[i] = cs_procedure_settings[index].max_subevent_len[i];
    }
    cs_proc_setting->tone_ant_cfg_selection = cs_procedure_settings[index].tone_ant_cfg_selection;
    cs_proc_setting->phy = cs_procedure_settings[index].phy;
    cs_proc_setting->tx_pwr_delta = cs_procedure_settings[index].tx_pwr_delta;
    cs_proc_setting->preferred_peer_antenna = cs_procedure_settings[index].preferred_peer_antenna;
    cs_proc_setting->snr_control_initiator = cs_procedure_settings[index].snr_control_initiator;
    cs_proc_setting->snr_control_reflector = cs_procedure_settings[index].snr_control_reflector;
    return true;
}

void readConfigs() {
  bool local_config = false;
  char value[PROPERTY_VALUE_MAX];
  if (osi_property_get("persist.vendor.service.bt.config.local", value, "false")) {
      if (strncmp(value, "true", PROPERTY_VALUE_MAX) == 0)
       local_config = true;
  }

  if (local_config) {
    log::info("Attempting to load CS config from local config file.");
    config_used = true;
    if (!ReadLocalConfigs()) {
      log::warn(
          "Failed to load local config, falling back to static tables.");
      InitializeConfigs();
    }
  } else {
    log::info("Loading CS config from static tables.");
    InitializeConfigs();
  }
}


future_t* cs_config_module_init(void) {
  log::info("");
  cs_config_settings_count = 0;
  cs_procedure_settings_count = 0;
  readConfigs();
  return future_new_immediate(FUTURE_SUCCESS);
}

future_t* cs_config_module_clean_up(void) {
  log::info("");
  return future_new_immediate(FUTURE_SUCCESS);
}

EXPORT_SYMBOL module_t cs_config_module = {
    .name = CS_CONFIG_MODULE,
    .init = cs_config_module_init,
    .start_up = NULL,
    .shut_down = NULL,
    .clean_up = cs_config_module_clean_up};
