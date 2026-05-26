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
 */

#pragma once

#include <array>
#include <limits>
#include <optional>
#include <type_traits>
#include <vector>

#include "base/functional/callback.h"
#include "broadcaster_types.h"
#include "bta_le_audio_broadcaster_api.h"
#include "main/shim/le_advertising_manager.h"

namespace {
template <int S, typename StateT = uint8_t>
class StateMachine {
public:
  StateMachine() : state_(std::numeric_limits<StateT>::min()) {}

protected:
  StateT GetState() const { return state_; }
  void SetState(StateT state) {
    if (state < S) {
      state_ = state;
    }
  }

private:
  StateT state_;
};
} /* namespace */

/* Broadcast Stream state machine possible states:
 * Stopped    - No broadcast Audio Stream is being transmitted.
 * Configuring- Configuration process was started.
 * Configured - The Broadcast Source has configured its controller for the
 *              broadcast Audio Stream using implementation-specific
 *              information or information provided by a higher-layer
 *              specification. It advertises the information to allow
 *              Broadcast Sinks and Scan Offloaders to detect the Audio Stream
 *              and transmits extended advertisements that contain Broadcast
 *              Audio Announcements, which associate periodic advertising
 *              trains with broadcast Audio Streams, and transmits periodic
 *              advertising trains. The periodic advertising trains carry
 *              Basic Audio Announcements that contain the broadcast Audio
 *              Stream parameters and metadata. No Audio Data packets are sent
 *              over the air from the Broadcast Source in this state. The
 *              periodic advertising trains do not carry the BIGInfo data
 *              required to synchronize to broadcast Audio Streams.
 * Enabling   - Controller configuration is in progress (create BIG, setup data path). Target state
 *              for this intermediate state is Streaming.
 * Disabling  - Controller deconfiguration is in progress (terminate BIG, remove data path). Target
 *              state for this intermediate state is Configured.
 * Stopping   - Broadcast Audio stream and advertisements are being stopped. Target state for this
 *              intermediate state is Stopped.
 * Streaming  - The broadcast Audio Stream is enabled on the Broadcast Source,
 *              allowing audio packets to be transmitted. The Broadcast Source
 *              transmits extended advertisements that contain Broadcast Audio
 *              Announcements, which associate periodic advertising trains with
 *              the broadcast Audio Stream. The Broadcast Source also transmits
 *              Basic Audio Announcements that contain broadcast Audio Stream
 *              parameters and metadata and the BIGInfo data required for
 *              synchronization to the broadcast Audio Stream by using periodic
 *              advertisements while transmitting the broadcast Audio Stream.
 *              The Broadcast Source may also transmit control parameters in
 *              control packets within the broadcast Audio Stream.
 */
namespace bluetooth::le_audio {
namespace broadcaster {

class IBroadcastStateMachineCallbacks;

struct BigConfig {
  uint8_t status;
  uint8_t big_id;
  uint32_t big_sync_delay;
  uint32_t transport_latency_big;
  uint8_t phy;
  uint8_t nse;
  uint8_t bn;
  uint8_t pto;
  uint8_t irc;
  uint16_t max_pdu;
  uint16_t iso_interval;
  std::vector<uint16_t> connection_handles;
};

enum class BroadcastMode : uint8_t {
  STANDARD = 0,
  DUPLEX = 1,
};

// Streaming direction bit flags for duplex broadcast
static constexpr uint8_t kStreamingDirectionNone = 0x00;  // No streaming
static constexpr uint8_t kStreamingDirectionTx = 0x01;    // TX streaming (bit 0)
static constexpr uint8_t kStreamingDirectionRx = 0x02;    // RX streaming (bit 1)
static constexpr uint8_t kStreamingDirectionBidirectional = 0x03;  // Both TX and RX streaming (bits 0 and 1)

inline bool IsTxStreaming(uint8_t direction) {
  return (direction & kStreamingDirectionTx) != 0;
}

inline bool IsRxStreaming(uint8_t direction) {
  return (direction & kStreamingDirectionRx) != 0;
}

inline bool IsBidirectionalStreaming(uint8_t direction) {
  return direction == kStreamingDirectionBidirectional;
}

inline bool IsValidStreamingDirection(uint8_t direction) {
  return direction <= kStreamingDirectionBidirectional;
}

struct BroadcastStateMachineConfig {
  bool is_public;
  bluetooth::le_audio::BroadcastId broadcast_id;
  std::string broadcast_name;
  uint8_t streaming_phy;
  BroadcastConfiguration config;
  bluetooth::le_audio::PublicBroadcastAnnouncementData public_announcement;
  bluetooth::le_audio::BasicAudioAnnouncementData announcement;
  std::optional<bluetooth::le_audio::BroadcastCode> broadcast_code;
  BroadcastMode broadcast_mode = BroadcastMode::STANDARD;
  uint8_t streaming_direction = kStreamingDirectionNone;
};

class BroadcastStateMachine : public StateMachine<7> {
public:
  static constexpr uint8_t kAdvSidUndefined = 0xFF;
  static constexpr uint8_t kPaIntervalMax = 0xA0; /* 160 * 0.625 = 100ms */
  static constexpr uint8_t kPaIntervalMin = 0x50; /* 80 * 0.625 = 50ms */
  static constexpr uint8_t kPaIntervalDuplex = 0x48; /* 72 * 1.25ms = 90ms - AuraChat */
  // LEA broadcast assigned register id, use positive number 0x1
  // this should not matter since
  // le_advertising_manager will maintain the reg_id together with client_id
  // and java/jni is using negative number
  static constexpr uint8_t kLeAudioBroadcastRegId = 0x1;
  // Matching the ADDRESS_TYPE_* enums from Java
  // ADDRESS_TYPE_RANDOM_NON_RESOLVABLE = 2
  static constexpr int8_t kBroadcastAdvertisingType = 0x2;

  static void Initialize(IBroadcastStateMachineCallbacks*, AdvertisingCallbacks* adv_callbacks);
  static std::unique_ptr<BroadcastStateMachine> CreateInstance(BroadcastStateMachineConfig msg);

  enum class Message : uint8_t {
    START = 0,
    SUSPEND,
    STOP,
  };
  static const std::underlying_type<Message>::type MESSAGE_COUNT =
          static_cast<std::underlying_type<Message>::type>(Message::STOP) + 1;

  enum class State : uint8_t {
    STOPPED = 0,
    CONFIGURING,
    CONFIGURED,
    ENABLING,
    DISABLING,
    STOPPING,
    STREAMING,
  };
  static const std::underlying_type<State>::type STATE_COUNT =
          static_cast<std::underlying_type<State>::type>(State::STREAMING) + 1;

  inline State GetState(void) const { return static_cast<State>(StateMachine::GetState()); }

  virtual uint8_t GetAdvertisingSid() const { return advertising_sid_; }
  virtual uint8_t GetPaInterval() const {
    return (GetBroadcastMode() == BroadcastMode::DUPLEX) ? kPaIntervalDuplex : kPaIntervalMax;
  }

  virtual bool Initialize() = 0;
  virtual const std::vector<BroadcastSubgroupCodecConfig>& GetCodecConfig() const = 0;
  virtual const BroadcastConfiguration& GetBroadcastConfig() const = 0;
  virtual std::optional<BigConfig> const& GetBigConfig() const = 0;
  virtual BroadcastStateMachineConfig const& GetStateMachineConfig() const = 0;
  virtual void RequestOwnAddress(
          base::Callback<void(uint8_t /* address_type*/, RawAddress /*address*/)> cb) = 0;
  virtual void RequestOwnAddress() = 0;
  virtual RawAddress GetOwnAddress() = 0;
  virtual uint8_t GetOwnAddressType() = 0;
  virtual std::optional<bluetooth::le_audio::BroadcastCode> GetBroadcastCode() const = 0;
  virtual bluetooth::le_audio::BroadcastId GetBroadcastId() const = 0;
  virtual const bluetooth::le_audio::BasicAudioAnnouncementData& GetBroadcastAnnouncement()
          const = 0;
  virtual void UpdateBroadcastAnnouncement(
          bluetooth::le_audio::BasicAudioAnnouncementData announcement) = 0;
  virtual bool IsPublicBroadcast() = 0;
  virtual std::string GetBroadcastName() = 0;
  virtual const bluetooth::le_audio::PublicBroadcastAnnouncementData&
  GetPublicBroadcastAnnouncement() const = 0;
  virtual void UpdatePublicBroadcastAnnouncement(
          uint32_t broadcast_id, const std::string& broadcast_name,
          const bluetooth::le_audio::PublicBroadcastAnnouncementData& announcement) = 0;
  virtual void OnCreateAnnouncement(uint8_t advertising_sid, int8_t tx_power, uint8_t status) = 0;
  virtual void OnEnableAnnouncement(bool enable, uint8_t status) = 0;
  virtual void OnUpdateAnnouncement(uint8_t status) = 0;
  void SetMuted(bool muted) { is_muted_ = muted; }
  bool IsMuted() const { return is_muted_; }

  virtual void SetStreamingDirection(uint8_t direction) = 0;
  virtual uint8_t GetStreamingDirection() const = 0;
  virtual BroadcastMode GetBroadcastMode() const = 0;

  virtual void HandleHciEvent(uint16_t event, void* data) = 0;
  virtual void OnSetupIsoDataPath(uint8_t status, uint16_t conn_handle) = 0;
  virtual void OnRemoveIsoDataPath(uint8_t status, uint16_t conn_handle) = 0;
  /* Called by broadcaster when TExitDbig_Complete is received in STOPPING or DISABLING state. */
  virtual void HandleTexitDbigCmpl() = 0;

  virtual void ProcessMessage(Message event, const void* data = nullptr) = 0;
  virtual ~BroadcastStateMachine() {}

protected:
  BroadcastStateMachine() = default;

  void SetState(State state) {
    StateMachine::SetState(static_cast<std::underlying_type<State>::type>(state));
  }

  uint8_t advertising_sid_ = kAdvSidUndefined;
  bool is_muted_ = false;
  uint8_t streaming_direction_ = kStreamingDirectionNone;

  RawAddress addr_ = RawAddress::kEmpty;
  uint8_t addr_type_ = 0;
};

class IBroadcastStateMachineCallbacks {
public:
  IBroadcastStateMachineCallbacks() = default;
  virtual ~IBroadcastStateMachineCallbacks() = default;
  virtual void OnStateMachineCreateStatus(uint32_t broadcast_id, bool initialized) = 0;
  virtual void OnStateMachineDestroyed(uint32_t broadcast_id) = 0;
  virtual void OnStateMachineEvent(uint32_t broadcast_id, BroadcastStateMachine::State state,
                                   const void* data = nullptr) = 0;
  virtual void OnOwnAddressResponse(uint32_t broadcast_id, uint8_t addr_type,
                                    RawAddress address) = 0;
  virtual void OnBigCreated(const std::vector<uint16_t>& conn_handle) = 0;
  virtual void OnAnnouncementUpdated(uint32_t broadcast_id) = 0;
};

std::ostream& operator<<(
        std::ostream& os,
        const bluetooth::le_audio::broadcaster::BroadcastStateMachine::Message& state);

std::ostream& operator<<(
        std::ostream& os,
        const bluetooth::le_audio::broadcaster::BroadcastStateMachine::State& state);

std::ostream& operator<<(std::ostream& os,
                         const bluetooth::le_audio::broadcaster::BroadcastStateMachine& machine);

std::ostream& operator<<(std::ostream& os,
                         const bluetooth::le_audio::broadcaster::BigConfig& machine);

std::ostream& operator<<(
        std::ostream& os,
        const bluetooth::le_audio::broadcaster::BroadcastStateMachineConfig& machine);

} /* namespace broadcaster */
}  // namespace bluetooth::le_audio
