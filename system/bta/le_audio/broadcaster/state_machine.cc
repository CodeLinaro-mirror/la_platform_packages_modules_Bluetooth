/*
 * Copyright 2021 HIMSA II K/S - www.himsa.com.
 * Represented by EHIMA - www.ehima.com
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Changes from Qualcomm Innovation Center, Inc. are provided under the following license:
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "bta/le_audio/broadcaster/state_machine.h"

#include <bind_helpers.h>
#include <bluetooth/log.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "bta/le_audio/broadcaster/broadcaster_types.h"
#include "bta/le_audio/le_audio_types.h"
#include "btm_api_types.h"
#include "btm_iso_api_types.h"
#include "common/strings.h"
#include "hardware/ble_advertiser.h"
#include "hardware/bt_le_audio.h"
#include "hci/le_advertising_manager.h"
#include "hcidefs.h"
#include "main/shim/le_advertising_manager.h"
#include "osi/include/properties.h"
#include "stack/include/btm_iso_api.h"
#include "types/raw_address.h"

using bluetooth::common::ToString;
using bluetooth::hci::IsoManager;
using bluetooth::hci::iso_manager::dbig_create_cmpl_evt;
using bluetooth::hci::iso_manager::big_create_cmpl_evt;
using bluetooth::hci::iso_manager::big_terminate_cmpl_evt;

using namespace bluetooth::le_audio::broadcaster;
using namespace bluetooth;

namespace {

// Advertising channels. These should be kept the same as those defined in the
// stack.
const int kAdvertisingChannel37 = (1 << 0);
const int kAdvertisingChannel38 = (1 << 1);
const int kAdvertisingChannel39 = (1 << 2);
const int kAdvertisingChannelAll =
        (kAdvertisingChannel37 | kAdvertisingChannel38 | kAdvertisingChannel39);

class BroadcastStateMachineImpl : public BroadcastStateMachine {
public:
  BroadcastStateMachineImpl(BroadcastStateMachineConfig msg)
      : active_config_(std::nullopt), sm_config_(std::move(msg)) {}

  ~BroadcastStateMachineImpl() {
    if (GetState() == State::STREAMING) {
      TerminateBig();
    }
    DestroyBroadcastAnnouncement();
    if (callbacks_) {
      callbacks_->OnStateMachineDestroyed(GetBroadcastId());
    }
  }

  bool Initialize() override {
    static constexpr uint8_t sNumBisMax = 31;

    if (sm_config_.config.GetNumBisTotal() > sNumBisMax) {
      log::error(
              "Channel count of {} exceeds the maximum number of possible BISes, "
              "which is {}",
              sm_config_.config.GetNumBisTotal(), sNumBisMax);
      return false;
    }

    CreateBroadcastAnnouncement(sm_config_.is_public, sm_config_.broadcast_name,
                                sm_config_.broadcast_id, sm_config_.public_announcement,
                                sm_config_.announcement, sm_config_.streaming_phy);
    return true;
  }

  const std::vector<BroadcastSubgroupCodecConfig>& GetCodecConfig() const override {
    return sm_config_.config.subgroups;
  }

  const BroadcastConfiguration& GetBroadcastConfig() const override { return sm_config_.config; }

  std::optional<BigConfig> const& GetBigConfig() const override { return active_config_; }

  BroadcastStateMachineConfig const& GetStateMachineConfig() const override { return sm_config_; }

  void RequestOwnAddress(
          base::Callback<void(uint8_t /* address_type*/, RawAddress /*address*/)> cb) override {
    uint8_t advertising_sid = GetAdvertisingSid();
    advertiser_if_->GetOwnAddress(advertising_sid, cb);
  }

  void RequestOwnAddress(void) override {
    auto broadcast_id = GetBroadcastId();
    RequestOwnAddress(base::Bind(&IBroadcastStateMachineCallbacks::OnOwnAddressResponse,
                                 base::Unretained(this->callbacks_), broadcast_id));
  }

  RawAddress GetOwnAddress() override { return addr_; }

  uint8_t GetOwnAddressType() override { return addr_type_; }

  bluetooth::le_audio::BroadcastId GetBroadcastId() const override {
    return sm_config_.broadcast_id;
  }

  std::optional<bluetooth::le_audio::BroadcastCode> GetBroadcastCode() const override {
    return sm_config_.broadcast_code;
  }

  const bluetooth::le_audio::BasicAudioAnnouncementData& GetBroadcastAnnouncement() const override {
    return sm_config_.announcement;
  }

  bool IsPublicBroadcast() override { return sm_config_.is_public; }

  std::string GetBroadcastName() override { return sm_config_.broadcast_name; }

  const bluetooth::le_audio::PublicBroadcastAnnouncementData& GetPublicBroadcastAnnouncement()
          const override {
    return sm_config_.public_announcement;
  }
  void SetStreamingDirection(uint8_t direction) override {
    if (GetBroadcastMode() != BroadcastMode::DUPLEX) {
      log::error("SetStreamingDirection called on non-DUPLEX broadcast (mode={}). This is unexpected operation!",
                 static_cast<uint8_t>(sm_config_.broadcast_mode));
      return;
    }
    log::info("Setting streaming direction to 0x{:02X} for broadcast_id={}",
              direction, sm_config_.broadcast_id);
    sm_config_.streaming_direction = direction;
    streaming_direction_ = direction;
  }
  uint8_t GetStreamingDirection() const override {
    if (GetBroadcastMode() != BroadcastMode::DUPLEX) {
      log::warn("GetStreamingDirection called on non-DUPLEX broadcast (mode={}). This is unexpected operation! Returning NONE.",
                static_cast<uint8_t>(sm_config_.broadcast_mode));
      return kStreamingDirectionNone;
    }
    return streaming_direction_;
  }
  BroadcastMode GetBroadcastMode() const override {
    return sm_config_.broadcast_mode;
  }

  uint8_t GetPaInterval() const override {
    if (GetBroadcastMode() == BroadcastMode::DUPLEX) {
      return GetPaIntervalForDuplex();
    } else {
      return BroadcastStateMachine::kPaIntervalMax;
    }
  }
  void OnCreateAnnouncement(uint8_t advertising_sid, int8_t tx_power, uint8_t status) {
    log::info("advertising_sid={} tx_power={} status={}", advertising_sid, tx_power, status);

    /* If this callback gets called the advertising_sid is valid even though the
     * status can be other than SUCCESS.
     */
    advertising_sid_ = advertising_sid;

    if (status != bluetooth::hci::AdvertisingCallback::AdvertisingStatus::SUCCESS) {
      log::error("Creating Announcement failed");
      callbacks_->OnStateMachineCreateStatus(GetBroadcastId(), false);
      return;
    }

    advertiser_if_->GetOwnAddress(
            advertising_sid,
            base::Bind(&BroadcastStateMachineImpl::OnAddressResponse, base::Unretained(this)));
  }

  void OnEnableAnnouncement(bool enable, uint8_t status) {
    log::info("operation={}, broadcast_id={}, status={}", enable ? "enable" : "disable",
              GetBroadcastId(), status);

    if (status == bluetooth::hci::AdvertisingCallback::AdvertisingStatus::SUCCESS) {
      /* Periodic is enabled but without BIGInfo. Stream is suspended. */
      if (enable) {
        SetState(State::CONFIGURED);
        /* Target state is always STREAMING state - start it now. */
        ProcessMessage(Message::START);
      } else {
        /* User wanted to stop the announcement - report target state reached */
        SetState(State::STOPPED);
        callbacks_->OnStateMachineEvent(GetBroadcastId(), GetState());
      }
    } else {
      // Handle error case
      if (enable) {
        /* Error on enabling */
        SetState(State::STOPPED);
      } else {
        /* Error on disabling */
        SetState(State::CONFIGURED);
      }
      callbacks_->OnStateMachineEvent(GetBroadcastId(), GetState());
    }
  }

  void OnUpdateAnnouncement(uint8_t status) {
    log::info("broadcast_id={}, status={}", GetBroadcastId(), status);

    if (status == bluetooth::hci::AdvertisingCallback::AdvertisingStatus::SUCCESS) {
      callbacks_->OnAnnouncementUpdated(GetBroadcastId());
    } else {
      log::error("Updating Announcement failed");
    }
  }

  void UpdatePublicBroadcastAnnouncement(
          uint32_t broadcast_id, const std::string& broadcast_name,
          const bluetooth::le_audio::PublicBroadcastAnnouncementData& announcement) override {
    std::vector<uint8_t> adv_data;
    PrepareAdvertisingData(true, broadcast_name, broadcast_id, announcement, adv_data);

    sm_config_.broadcast_name = broadcast_name;
    sm_config_.public_announcement = announcement;

    advertiser_if_->SetData(advertising_sid_, false, adv_data, std::vector<uint8_t>(),
                            base::DoNothing());
  }

  void UpdateBroadcastAnnouncement(
          bluetooth::le_audio::BasicAudioAnnouncementData announcement) override {
    std::vector<uint8_t> periodic_data;
    PreparePeriodicData(announcement, periodic_data);

    sm_config_.announcement = std::move(announcement);

    advertiser_if_->SetPeriodicAdvertisingData(advertising_sid_, periodic_data,
                                               std::vector<uint8_t>(), base::DoNothing());
  }

  void ProcessMessage(Message msg, const void* data = nullptr) override {
    log::info("broadcast_id={}, state={}, message={}", GetBroadcastId(), ToString(GetState()),
              ToString(msg));
    switch (msg) {
      case Message::START:
        start_msg_handlers[StateMachine::GetState()](data);
        break;
      case Message::STOP:
        stop_msg_handlers[StateMachine::GetState()](data);
        break;
      case Message::SUSPEND:
        suspend_msg_handlers[StateMachine::GetState()](data);
        break;
    };
  }

  static IBroadcastStateMachineCallbacks* callbacks_;
  static ::BleAdvertiserInterface* advertiser_if_;

private:
  std::optional<BigConfig> active_config_;
  BroadcastStateMachineConfig sm_config_;
  /* TExitDbig mode for the current user-initiated stop. Defaults to TERMINATE.
   * Set from ProcessMessage(STOP, &mode) data so the mode from stopEnhancedBroadcast()
   * reaches TerminateBig() after the async ISO teardown completes. */
  uint8_t pending_stop_mode_ = HCI_TEXIT_MODE_TERMINATE;

  /* Message handlers for each possible state */
  typedef std::function<void(const void*)> msg_handler_t;
  const std::array<msg_handler_t, BroadcastStateMachine::STATE_COUNT> start_msg_handlers{
          /* in STOPPED state */
          [this](const void*) {
            SetState(State::CONFIGURING);
            callbacks_->OnStateMachineEvent(GetBroadcastId(), GetState());
            EnableAnnouncement();
          },
          /* in CONFIGURING state */
          [](const void*) { /* Do nothing */ },
          /* in CONFIGURED state */
          [this](const void*) {
            SetState(State::ENABLING);
            if(GetBroadcastMode() == BroadcastMode::DUPLEX){
              CreateDbig();
            }
            else{
              CreateBig();
            }
          },
          /* in ENABLING state */
          [](const void*) { /* Do nothing */ },
          /* in DISABLING state */
          [this](const void*) { SetState(State::ENABLING); },
          /* in STOPPING state */
          [](const void*) { /* Do nothing */ },
          /* in STREAMING state */
          [this](const void*) {
             if(GetBroadcastMode() == BroadcastMode::DUPLEX && 
                IsTxStreaming(GetStreamingDirection()) && 
                !IsRxStreaming(GetStreamingDirection())){
                if(active_config_ != std::nullopt){
                  TriggerIsoDatapathSetup(active_config_->connection_handles[0]);
                }
             }
           }};
  const std::array<msg_handler_t, BroadcastStateMachine::STATE_COUNT> stop_msg_handlers{
          /* in STOPPED state */
          [](const void*) { /* Already stopped */ },
          /* in CONFIGURING state */
          [](const void*) { /* Do nothing */ },
          /* in CONFIGURED state */
          [this](const void*) {
            SetState(State::STOPPING);
            callbacks_->OnStateMachineEvent(GetBroadcastId(), GetState());
            DisableAnnouncement();
          },
          /* in ENABLING state */
          [this](const void*) {
            SetState(State::STOPPING);
            callbacks_->OnStateMachineEvent(GetBroadcastId(), GetState());
          },
          /* in DISABLING state */
          [this](const void*) {
            SetState(State::STOPPING);
            callbacks_->OnStateMachineEvent(GetBroadcastId(), GetState());
          },
          /* in STOPPING state */
          [](const void*) { /* Do nothing */ },
          /* in STREAMING state */
          [this](const void* data) {
            if (data) pending_stop_mode_ = *static_cast<const uint8_t*>(data);
            SetState(State::STOPPING);
            callbacks_->OnStateMachineEvent(GetBroadcastId(), GetState());
            TriggerIsoDatapathTeardown(active_config_->connection_handles[0]);
          }};

  const std::array<msg_handler_t, BroadcastStateMachine::STATE_COUNT> suspend_msg_handlers{
          /* in STOPPED state */
          [](const void*) { /* Do nothing */ },
          /* in CONFIGURING state */
          [](const void*) { /* Do nothing */ },
          /* in CONFIGURED state */
          [](const void*) { /* Already suspended */ },
          /* in ENABLING state */
          [this](const void*) {
            SetState(State::DISABLING);
          },
          /* in DISABLING state */
          [](const void*) { /* Do nothing */ },
          /* in STOPPING state */
          [](const void*) { /* Do nothing */ },
          /* in STREAMING state */
          [this](const void*) {
            if(GetBroadcastMode() == BroadcastMode::DUPLEX && IsRxStreaming(GetStreamingDirection())){
              //Only remove RX ISO_Datapath state will remain streaming.
              TriggerIsoDatapathTeardown(active_config_->connection_handles[0]);
            }
            else{
              SetState(State::DISABLING);
              TriggerIsoDatapathTeardown(active_config_->connection_handles[0]);
            }
          }};

  const std::array<msg_handler_t, BroadcastStateMachine::STATE_COUNT> resume_msg_handlers{
          /* in STOPPED state */
          [](const void*) { /* Do nothing */ },
          /* in CONFIGURING state */
          [](const void*) { /* Do nothing */ },
          /* in CONFIGURED state */
          [this](const void*) {
            SetState(State::ENABLING);
            CreateBig();
          },
          /* in ENABLING state */
          [](const void*) { /* Do nothing */ },
          /* in DISABLING state */
          [](const void*) { /* Do nothing */ },
          /* in STOPPING state */
          [](const void*) { /* Do nothing */ },
          /* in STREAMING state */
          [](const void*) { /* Already streaming */ }};

  void OnAddressResponse(uint8_t addr_type, RawAddress addr) {
    log::info("own address={}, type={}", addr, addr_type);
    addr_ = addr;
    addr_type_ = addr_type;

    /* Ext. advertisings are already on */
    SetState(State::CONFIGURED);

    callbacks_->OnStateMachineCreateStatus(GetBroadcastId(), true);
    callbacks_->OnStateMachineEvent(GetBroadcastId(), State::CONFIGURED);
  }

  void CreateBroadcastAnnouncement(
          bool is_public, const std::string& broadcast_name,
          bluetooth::le_audio::BroadcastId& broadcast_id,
          const bluetooth::le_audio::PublicBroadcastAnnouncementData& public_announcement,
          const bluetooth::le_audio::BasicAudioAnnouncementData& announcement,
          uint8_t streaming_phy) {
    log::info("is_public={}, broadcast_name={}, public_features={}",
              is_public ? "public" : "non-public", broadcast_name, public_announcement.features);
    if (advertiser_if_ != nullptr) {
      ::AdvertiseParameters adv_params;
      ::PeriodicAdvertisingParameters periodic_params;
      std::vector<uint8_t> adv_data;
      std::vector<uint8_t> periodic_data;

      PrepareAdvertisingData(is_public, broadcast_name, broadcast_id, public_announcement,
                             adv_data);
      PreparePeriodicData(announcement, periodic_data);

      adv_params.min_interval = 0x00A0; /* 160 * 0,625 = 100ms */
      adv_params.max_interval = 0x0140; /* 320 * 0,625 = 200ms */
      adv_params.advertising_event_properties = 0;
      adv_params.channel_map = kAdvertisingChannelAll;
      adv_params.tx_power = 8;

      bool mBroadCastMode = (sm_config_.broadcast_mode == BroadcastMode::DUPLEX);
      bool mBroadCastCodedPhy = IsCodedPhyEnabled();
      if (mBroadCastMode && mBroadCastCodedPhy) {
        log::info("Setting coded phy for DUPLEX broadcast");
        adv_params.primary_advertising_phy = PHY_LE_CODED;
        adv_params.secondary_advertising_phy = PHY_LE_CODED;
      } else {
        adv_params.primary_advertising_phy = PHY_LE_1M;
        adv_params.secondary_advertising_phy = streaming_phy;
      }
      log::info("Advertising PHYs: primary={}, secondary={}",
                adv_params.primary_advertising_phy, adv_params.secondary_advertising_phy);

      adv_params.scan_request_notification_enable = 0;
      adv_params.own_address_type = kBroadcastAdvertisingType;

      if (sm_config_.broadcast_mode == BroadcastMode::DUPLEX) {
        uint16_t pa_interval = GetPaIntervalForDuplex();
        periodic_params.max_interval = pa_interval;
        periodic_params.min_interval = pa_interval;
        periodic_params.periodic_advertising_properties = 0x40;  // Bit 6: Include TxPower
        log::info("Duplex mode: PA interval={} ({}ms)", pa_interval, (pa_interval * 1.25));
      } else {
        periodic_params.max_interval = BroadcastStateMachine::kPaIntervalMax;
        periodic_params.min_interval = BroadcastStateMachine::kPaIntervalMin;
        periodic_params.periodic_advertising_properties = 0;
      }
      periodic_params.enable = true;

      /* Status and timeout callbacks are handled by OnAdvertisingSetStarted()
       * which returns the status and handle to be used later in CreateBIG
       * command.
       */
      advertiser_if_->StartAdvertisingSet(
              kAdvertiserClientIdLeAudio, kLeAudioBroadcastRegId, base::DoNothing(), adv_params,
              adv_data, std::vector<uint8_t>(), std::vector<uint8_t>(), std::vector<uint8_t>(),
              periodic_params, periodic_data, std::vector<uint8_t>(), 0 /* duration */,
              0 /* maxExtAdvEvents */, std::vector<uint8_t>(), base::DoNothing());
    }
  }

  void DestroyBroadcastAnnouncement() { advertiser_if_->Unregister(GetAdvertisingSid()); }

  void EnableAnnouncement() {
    log::info("broadcast_id={}", GetBroadcastId());
    // Callback is handled by OnAdvertisingEnabled() which returns the status
    advertiser_if_->Enable(GetAdvertisingSid(), true, base::DoNothing(), 0,
                           0, /* Enable until stopped */
                           base::DoNothing());
  }

  // Helper function to determine if Coded PHY is enabled for duplex broadcast
  bool IsCodedPhyEnabled() const {
    if (GetBroadcastMode() != BroadcastMode::DUPLEX) {
      return false;
    }
    return osi_property_get_bool("persist.vendor.qcom.bluetooth.enable_ba_coded_phy", false);
  }

  // Helper function to get transport latency from property or config
  // For DUPLEX mode: reads property persist.vendor.btstack.transport_latency
  // For Auracast: uses config value only
  uint16_t GetTransportLatency() const {
    uint16_t mtl;

    if (GetBroadcastMode() == BroadcastMode::DUPLEX) {
      // DUPLEX mode: Read from property (overrides config value)
      mtl = (uint16_t)osi_property_get_int32("persist.vendor.btstack.transport_latency", 0);

      // If property is 0, use value from config as fallback
      if (mtl == 0) {
        mtl = sm_config_.config.qos.getMaxTransportLatency();
      }
    } else {
      // Auracast: Always use config value
      mtl = sm_config_.config.qos.getMaxTransportLatency();
    }

    return mtl;
  }

  // Helper function to calculate PA interval based on transport latency and PHY type
  // PA Interval format: value * 1.25ms (e.g., 72 * 1.25ms = 90ms)
  // Reads property persist.vendor.btstack.transport_latency
  uint16_t GetPaIntervalForDuplex() const {
    uint16_t mtl = GetTransportLatency();
    bool coded_phy = IsCodedPhyEnabled();

    log::info("transport_latency={} ms, coded_phy={}", mtl, coded_phy);

    if (coded_phy) {
      // Coded PHY: transport_latency = 15ms / 25ms / 35ms
      if (mtl == 15) {
        return 72;  // 72 * 1.25ms = 90ms
      } else if (mtl == 25 || mtl == 35) {
        return 96;  // 96 * 1.25ms = 120ms
      }
    } else {
      // LE2M PHY: transport_latency = 10ms / 20ms / 30ms
      if (mtl == 10) {
        return 72;  // 72 * 1.25ms = 90ms
      } else if (mtl == 20 || mtl == 30) {
        return 96;  // 96 * 1.25ms = 120ms
      }
    }

    // Default fallback
    log::warn("Unmatched transport_latency/PHY combination (mtl={}, coded={}), using default PA interval 90ms", mtl, coded_phy);
    return BroadcastStateMachine::kPaIntervalDuplex;
  }

  // Helper function to get number of BIS based on PHY type for duplex mode
  uint8_t GetNumBisForDuplex() const {
    bool coded_phy = IsCodedPhyEnabled();
    uint8_t num_bis = coded_phy ? 3 : 4;  // 3 BIS for Coded PHY, 4 for LE2M
    log::info("Number of BIS for duplex mode: {}, coded_phy={}", num_bis, coded_phy);
    return num_bis;
  }

  void CreateBig(void) {
    log::info("broadcast_id={}", GetBroadcastId());
    /* TODO: Figure out how to decide on the currently hard-codded params. */
    struct bluetooth::hci::iso_manager::big_create_params big_params = {
            .adv_handle = GetAdvertisingSid(),
            .num_bis = sm_config_.config.GetNumBisTotal(),
            .sdu_itv = sm_config_.config.GetSduIntervalUs(),
            .max_sdu_size = sm_config_.config.GetMaxSduOctets(),
            .max_transport_latency = GetTransportLatency(),  // Read from property
            .rtn = sm_config_.config.qos.getRetransmissionNumber(),
            .phy = sm_config_.streaming_phy,
            .packing = 0x01, /* Interleaved */
            .framing = 0x00, /* Unframed */
            .enc = static_cast<uint8_t>(sm_config_.broadcast_code ? 1 : 0),
            .enc_code = sm_config_.broadcast_code ? *sm_config_.broadcast_code
                                                  : std::array<uint8_t, 16>({0}),
    };
    if(GetBroadcastMode() == BroadcastMode::DUPLEX){
      big_params.packing = 0x00;  // Sequential packing for duplex
      big_params.num_bis = GetNumBisForDuplex();  // 3 for Coded PHY, 4 for LE2M

      // Configure PHY and RTN based on Coded PHY property
      if (IsCodedPhyEnabled()) {
        big_params.phy = PHY_LE_CODED;  // PHY = 4 (Coded S2)
        big_params.rtn = 0;  // RTN = 0 for Coded PHY
        log::info("Duplex mode: Using Coded PHY (S2), PHY=4, RTN=0");
      } else {
        big_params.phy = PHY_LE_2M;  // PHY = 2 (LE2M)
        big_params.rtn = 1;  // RTN = 1 for LE2M
        log::info("Duplex mode: Using LE2M PHY, PHY=2, RTN=1");
      }
    }
    log::info("Number of BISES={}, PHY={}, RTN={}, max_transport_latency={}",
              big_params.num_bis, big_params.phy, big_params.rtn, big_params.max_transport_latency);
    IsoManager::GetInstance()->CreateBig(GetAdvertisingSid(), std::move(big_params));
  }
  void CreateDbig(void) {
    log::info("broadcast_id={}, creating DBIG for duplex mode", GetBroadcastId());
    // All params were pre-seeded in ReadSupportedStates() with calculated bis_control_event_interval.
    // The handle is updated dynamically here.
    auto params = IsoManager::GetInstance()->GetStoredDbigParams();
    params.dbig_handle = GetAdvertisingSid();
    log::info("DBIG params: handle={}, bis_control_event_interval={}",
              params.dbig_handle, params.bis_control_event_interval);
    IsoManager::GetInstance()->CreateDbig(std::move(params));
  }
  void DisableAnnouncement(void) {
    log::info("broadcast_id={}", GetBroadcastId());
    // Callback is handled by OnAdvertisingEnabled() which returns the status
    advertiser_if_->Enable(GetAdvertisingSid(), false, base::DoNothing(), 0, 0, base::DoNothing());
  }

  void TerminateBig(uint8_t mode = HCI_TEXIT_MODE_TERMINATE) {
    log::info("mode=0x{:02x}, disabling={}", mode,
              GetState() == BroadcastStateMachine::State::DISABLING);
    if (GetBroadcastMode() == BroadcastMode::DUPLEX) {
      /* For DUPLEX, send HCI_VS_LE_Texit_DBIG with the supplied mode.
       * Mode meanings for PGO role:
       *   TERMINATE (0x02) — PGO terminates the entire DBIG group for all members
       *   EXIT      (0x01) — PGO gracefully exits without affecting other members (future)
       * Note: removing a specific PGP from the DBIG is a separate operation via
       *   HCI_VS_LE_Remove_Device_DBIG, handled by RemoveDeviceDbig() — not this path.
       * The mode is propagated from stopEnhancedBroadcast(mode) via pending_stop_mode_.
       * Error/cleanup paths pass HCI_TEXIT_MODE_TERMINATE explicitly. */
      bluetooth::hci::iso_manager::dbig_texit_params params{
        .dbig_handle = static_cast<uint8_t>(GetAdvertisingSid()),
        .texit_mode  = mode,
        .reason      = 0x13,
        .p_cb        = nullptr
      };
      log::info("TerminateBig: DUPLEX — TExitDbig dbig_handle={}, texit_mode=0x{:02x}",
                params.dbig_handle, params.texit_mode);
      IsoManager::GetInstance()->TExitDbig(params);
    } else {
      /* Standard broadcast: unchanged — HCI_BLE_TERMINATE_BIG */
      IsoManager::GetInstance()->TerminateBig(GetAdvertisingSid(), 0x13);
    }
  }

  void OnSetupIsoDataPath(uint8_t status, uint16_t conn_hdl) override {
    log::assert_that(active_config_ != std::nullopt,
                     "assert failed: active_config_ != std::nullopt");

    if (status != 0) {
      log::error("Failure creating data path. Tearing down the BIG now.");
      SetState(State::DISABLING);
      TerminateBig();
      return;
    }

    /* Look for the next BIS handle */
    auto handle_it = std::find_if(active_config_->connection_handles.begin(),
                                  active_config_->connection_handles.end(),
                                  [conn_hdl](const auto& handle) { return conn_hdl == handle; });
    log::assert_that(handle_it != active_config_->connection_handles.end(),
                     "assert failed: handle_it != active_config_->connection_handles.end()");
    handle_it = std::next(handle_it);

    if (handle_it == active_config_->connection_handles.end()) {
      if (GetState() == BroadcastStateMachine::State::STOPPING ||
          GetState() == BroadcastStateMachine::State::DISABLING) {
        // All ISO setup completed, but we're in stopping state, we need to tear down all ISO
        log::warn("ISO setup in stopping or disabling state. Tearing down ISO data path.");
        // Remain in STOPPING/DISABLING, BIG will be terminated in OnRemoveIsoDataPath
        TriggerIsoDatapathTeardown(active_config_->connection_handles[0]);
        return;
      }
      /* It was the last BIS to set up - change state to streaming */
      SetState(State::STREAMING);
      if(GetBroadcastMode() == BroadcastMode::DUPLEX){
        uint8_t current_direction = GetStreamingDirection();
        if(current_direction == kStreamingDirectionNone){
          SetStreamingDirection(kStreamingDirectionTx);
        }
        else if(current_direction == kStreamingDirectionTx){
          SetStreamingDirection(kStreamingDirectionBidirectional);
        }
      }
      callbacks_->OnStateMachineEvent(GetBroadcastId(), GetState(), nullptr);
    } else {
      /* Note: We would feed a watchdog here if we had one */
      /* There are more BISes to set up data path for */
      log::info("There is more data paths to set up.");
      TriggerIsoDatapathSetup(*handle_it);
    }
  }

  void OnRemoveIsoDataPath(uint8_t status, uint16_t conn_handle) override {
    log::assert_that(active_config_ != std::nullopt,
                     "assert failed: active_config_ != std::nullopt");

    if (status != 0) {
      log::error("Failure removing data path. Tearing down the BIG now.");
      TerminateBig();
      return;
    }

    /* Look for the next BIS handle */
    auto handle_it = std::find_if(
            active_config_->connection_handles.begin(), active_config_->connection_handles.end(),
            [conn_handle](const auto& handle) { return conn_handle == handle; });
    log::assert_that(handle_it != active_config_->connection_handles.end(),
                     "assert failed: handle_it != active_config_->connection_handles.end()");
    handle_it = std::next(handle_it);

    if (handle_it == active_config_->connection_handles.end()) {
      /* It was the last one to set up - start tearing down the BIG */
      if(GetBroadcastMode() == BroadcastMode::DUPLEX && IsRxStreaming(GetStreamingDirection())){
        log::info("RX teardown complete for broadcast_id={}, sending ACK", GetBroadcastId());
        // Remove RX streaming direction (only TX remains active)
        SetStreamingDirection(GetStreamingDirection() & ~kStreamingDirectionRx);
        callbacks_->OnStateMachineEvent(GetBroadcastId(), GetState(), this);
        return;
      }
      TerminateBig(pending_stop_mode_);
    } else {
      /* Note: We would feed a watchdog here if we had one */
      /* There are more BISes to tear down data path for */
      log::info("There is more data paths to tear down.");
      TriggerIsoDatapathTeardown(*handle_it);
    }
  }

  void TriggerIsoDatapathSetup(uint16_t conn_handle) {
    log::info("conn_hdl={}", conn_handle);
    log::assert_that(active_config_ != std::nullopt,
                     "assert failed: active_config_ != std::nullopt");

    /* Note: If coding format is transparent, 'codec_id_company' and
     * 'codec_id_vendor' shall be ignored.
     */
    auto& iso_datapath_config = sm_config_.config.data_path.isoDataPathConfig;
    bluetooth::hci::iso_manager::iso_data_path_params param = {
            .data_path_dir = bluetooth::hci::iso_manager::kIsoDataPathDirectionIn,
            .data_path_id = static_cast<uint8_t>(sm_config_.config.data_path.dataPathId),
            .codec_id_format = static_cast<uint8_t>(
                    iso_datapath_config.isTransparent ? bluetooth::hci::kIsoCodingFormatTransparent
                                                      : iso_datapath_config.codecId.coding_format),
            .codec_id_company =
                    static_cast<uint16_t>(iso_datapath_config.isTransparent
                                                  ? 0x0000
                                                  : iso_datapath_config.codecId.vendor_company_id),
            .codec_id_vendor =
                    static_cast<uint16_t>(iso_datapath_config.isTransparent
                                                  ? 0x0000
                                                  : iso_datapath_config.codecId.vendor_codec_id),
            .controller_delay = iso_datapath_config.controllerDelayUs,
            .codec_conf = iso_datapath_config.configuration,
    };
    if(GetBroadcastMode() == BroadcastMode::DUPLEX && GetState() == BroadcastStateMachine::State::STREAMING){
      param.data_path_dir = bluetooth::hci::iso_manager::kIsoDataPathDirectionOut;
    }
    IsoManager::GetInstance()->SetupIsoDataPath(conn_handle, std::move(param));
  }

  void TriggerIsoDatapathTeardown(uint16_t conn_handle) {
    log::info("conn_hdl={}", conn_handle);
    log::assert_that(active_config_ != std::nullopt,
                     "assert failed: active_config_ != std::nullopt");

    SetMuted(true);
    if(GetBroadcastMode() == BroadcastMode::DUPLEX && IsRxStreaming(GetStreamingDirection())){
          IsoManager::GetInstance()->RemoveIsoDataPath(
            conn_handle, bluetooth::hci::iso_manager::kRemoveIsoDataPathDirectionOutput);
            return;
      }
    IsoManager::GetInstance()->RemoveIsoDataPath(
            conn_handle, bluetooth::hci::iso_manager::kRemoveIsoDataPathDirectionInput);
  }

  void HandleTexitDbigCmpl() override {
    log::info("HandleTexitDbigCmpl: state={}", ToString(GetState()));
    if (GetState() == BroadcastStateMachine::State::STOPPING ||
        GetState() == BroadcastStateMachine::State::DISABLING) {
      /* TExitDbig was sent by StopEnhancedAudioBroadcast — proceed with
       * announcement teardown exactly as HCI_BLE_TERM_BIG_CPL_EVT does.
       * Handles both STOPPING (explicit user stop) and DISABLING (the
       * 0ms big_terminate_timer_ fired SuspendAudioBroadcasts() which put
       * the state into DISABLING before the STOP message arrived). */
      active_config_ = std::nullopt;
      if (GetBroadcastMode() == BroadcastMode::DUPLEX) {
        SetStreamingDirection(kStreamingDirectionNone);
      }
      DisableAnnouncement();
    }
  }

  void HandleHciEvent(uint16_t event, void* data) override {
    switch (event) {
      case HCI_BLE_CREATE_BIG_CPL_EVT: {
        auto* evt = static_cast<big_create_cmpl_evt*>(data);

        if (evt->big_id != GetAdvertisingSid()) {
          log::error("State={}, Event={}, Unknown big, big_id={}", ToString(GetState()), event,
                     evt->big_id);
          break;
        }

        if (evt->status == 0x00) {
          log::info("BIG create BIG complete, big_id={}", evt->big_id);
          active_config_ = {
                  .status = evt->status,
                  .big_id = evt->big_id,
                  .big_sync_delay = evt->big_sync_delay,
                  .transport_latency_big = evt->transport_latency_big,
                  .phy = evt->phy,
                  .nse = evt->nse,
                  .bn = evt->bn,
                  .pto = evt->pto,
                  .irc = evt->irc,
                  .max_pdu = evt->max_pdu,
                  .iso_interval = evt->iso_interval,
                  .connection_handles = evt->conn_handles,
          };

          if (GetState() == BroadcastStateMachine::State::DISABLING ||
              GetState() == BroadcastStateMachine::State::STOPPING) {
            log::info("Terminating BIG in state={}, big_id={}", ToString(GetState()), evt->big_id);
            TerminateBig(pending_stop_mode_);
          } else {
            callbacks_->OnBigCreated(evt->conn_handles);
            TriggerIsoDatapathSetup(evt->conn_handles[0]);
          }
        } else {
          log::error("State={} Event={}. Unable to create big, big_id={}, status={}",
                     ToString(GetState()), event, evt->big_id, evt->status);
        }
      } break;
      case HCI_VS_LE_DBIG_CREATE_CPL_EVT: {
          auto* evt = static_cast<dbig_create_cmpl_evt*>(data);
          if (evt->dbig_handle != GetAdvertisingSid()) {
            log::error("State={}, Event={}, Unknown dbig, dbig_handle={}", ToString(GetState()), event,
                      evt->dbig_handle);
            break;
          }
          if (evt->status == 0x00) {
            log::info("DBIG create complete, big_id={}", evt->dbig_handle);
            CreateBig();
          } else {
            log::error("State={} Event={}. Unable to create dbig, big_id={}, status={}",
                      ToString(GetState()), event, evt->dbig_handle, evt->status);
            // Handle DBIG creation failure
            //callbacks_->OnStateMachineEvent(GetBroadcastId(), GetState(), evt);
          }
      } break;
      case HCI_BLE_TERM_BIG_CPL_EVT: {
        auto* evt = static_cast<big_terminate_cmpl_evt*>(data);

        log::info("BIG terminate BIG cmpl in state={}, reason={} big_id={}", ToString(GetState()),
                  evt->reason, evt->big_id);

        if (evt->big_id != GetAdvertisingSid()) {
          log::error("State={} Event={}, unknown adv.sid={}", ToString(GetState()), event,
                     evt->big_id);
          break;
        }

        active_config_ = std::nullopt;
        bool disabling = GetState() == BroadcastStateMachine::State::DISABLING;
        if(GetBroadcastMode() == BroadcastMode::DUPLEX){
          SetStreamingDirection(kStreamingDirectionNone);
        }
        /* Go back to configured if BIG is inactive (we are still announcing) and state is not
         * stopping*/
        if (GetState() != BroadcastStateMachine::State::STOPPING) {
          SetState(State::CONFIGURED);
        }

        /* Check if we got this HCI event due to STOP or SUSPEND message. */
        if (disabling) {
          callbacks_->OnStateMachineEvent(GetBroadcastId(), GetState(), evt);
        } else {
          DisableAnnouncement();
        }
      } break;
      default:
        log::error("State={} Unknown event={}", ToString(GetState()), event);
        break;
    }
  }
};

IBroadcastStateMachineCallbacks* BroadcastStateMachineImpl::callbacks_ = nullptr;
::BleAdvertiserInterface* BroadcastStateMachineImpl::advertiser_if_ = nullptr;
} /* namespace */

std::unique_ptr<BroadcastStateMachine> BroadcastStateMachine::CreateInstance(
        BroadcastStateMachineConfig msg) {
  return std::make_unique<BroadcastStateMachineImpl>(std::move(msg));
}

void BroadcastStateMachine::Initialize(IBroadcastStateMachineCallbacks* callbacks,
                                       AdvertisingCallbacks* adv_callbacks) {
  BroadcastStateMachineImpl::callbacks_ = callbacks;
  /* Get gd le advertiser interface */
  BroadcastStateMachineImpl::advertiser_if_ = bluetooth::shim::get_ble_advertiser_instance();
  if (BroadcastStateMachineImpl::advertiser_if_ != nullptr) {
    log::info("Advertiser_instance acquired");
    BroadcastStateMachineImpl::advertiser_if_->RegisterCallbacksNative(adv_callbacks,
                                                                       kAdvertiserClientIdLeAudio);
  } else {
    log::error("Could not acquire advertiser_instance!");
    BroadcastStateMachineImpl::advertiser_if_ = nullptr;
  }
}

namespace bluetooth::le_audio {
namespace broadcaster {

std::ostream& operator<<(std::ostream& os, const BroadcastStateMachine::Message& msg) {
  static const char* char_value_[BroadcastStateMachine::MESSAGE_COUNT] = {"START", "SUSPEND",
                                                                          "STOP"};
  os << char_value_[static_cast<uint8_t>(msg)];
  return os;
}

std::ostream& operator<<(std::ostream& os, const BroadcastStateMachine::State& state) {
  static const char* char_value_[BroadcastStateMachine::STATE_COUNT] = {
          "STOPPED", "CONFIGURING", "CONFIGURED", "ENABLING", "DISABLING", "STOPPING", "STREAMING"};
  os << char_value_[static_cast<uint8_t>(state)];
  return os;
}

std::ostream& operator<<(std::ostream& os,
                         const bluetooth::le_audio::broadcaster::BigConfig& config) {
  os << "\n";
  os << "        Status: 0x" << std::hex << +config.status << std::dec << "\n";
  os << "        BIG ID: " << +config.big_id << "\n";
  os << "        Sync delay: " << config.big_sync_delay << "\n";
  os << "        Transport Latency: " << config.transport_latency_big << "\n";
  os << "        Phy: " << +config.phy << "\n";
  os << "        Nse: " << +config.nse << "\n";
  os << "        Bn: " << +config.bn << "\n";
  os << "        Pto: " << +config.pto << "\n";
  os << "        Irc: " << +config.irc << "\n";
  os << "        Max pdu: " << config.max_pdu << "\n";
  os << "        Iso interval: " << config.iso_interval << "\n";
  os << "        Connection handles (BISes): [";
  for (auto& el : config.connection_handles) {
    os << std::hex << +el << std::dec << ":";
  }
  os << "]";
  return os;
}

std::ostream& operator<<(
        std::ostream& os,
        const bluetooth::le_audio::broadcaster::BroadcastStateMachineConfig& config) {
  const char* const PHYS[] = {"NONE", "1M", "2M", "CODED"};

  os << "\n";
  os << "        Broadcast ID: " << config.broadcast_id << "\n";
  os << "        Streaming PHY: "
     << ((config.streaming_phy > 3) ? std::to_string(config.streaming_phy)
                                    : PHYS[config.streaming_phy])
     << "\n";
  os << "        Subgroups: {\n";
  for (auto const& subgroup : config.config.subgroups) {
    os << "          " << subgroup << "\n";
  }
  os << "        }\n";
  os << "        Qos Config: " << config.config.qos << "\n";
  if (config.broadcast_code) {
    os << "        Broadcast Code: [";
    for (auto& el : *config.broadcast_code) {
      os << std::hex << +el << ":";
    }
    os << "]\n";
  } else {
    os << "        Broadcast Code: NONE\n";
  }

  std::vector<uint8_t> an_raw;
  ToRawPacket(config.announcement, an_raw);
  os << "        Announcement RAW: [";
  for (auto& el : an_raw) {
    os << std::hex << +el << ":";
  }
  os << "]";

  return os;
}

std::ostream& operator<<(std::ostream& os,
                         const bluetooth::le_audio::broadcaster::BroadcastStateMachine& machine) {
  os << "    Broadcast state machine: {"
     << "      Advertising SID: " << +machine.GetAdvertisingSid() << "\n"
     << "      State: " << machine.GetState() << "\n";
  os << "      State Machine Config: " << machine.GetStateMachineConfig() << "\n";

  if (machine.GetBigConfig()) {
    os << "      BigConfig: " << *machine.GetBigConfig() << "\n";
  } else {
    os << "      BigConfig: NONE\n";
  }
  os << "    }\n";
  return os;
}

}  // namespace broadcaster
}  // namespace bluetooth::le_audio
