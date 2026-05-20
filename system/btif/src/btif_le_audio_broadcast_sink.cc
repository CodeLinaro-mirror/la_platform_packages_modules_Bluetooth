/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <base/functional/bind.h>
#include <base/logging.h>
#include <bluetooth/log.h>
#include <hardware/bt_le_audio_broadcast_sink.h>

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "bta_le_audio_broadcast_sink_api.h"
#include "btif_common.h"
#include "stack/include/main_thread.h"

using base::Bind;
using base::Unretained;
using bluetooth::le_audio::broadcast_sink::BroadcastCode;
using bluetooth::le_audio::broadcast_sink::BroadcastId;
using bluetooth::le_audio::broadcast_sink::BroadcastMetadata;
using bluetooth::le_audio::broadcast_sink::BroadcastSinkCallbacks;
using bluetooth::le_audio::broadcast_sink::BroadcastSinkInterface;
namespace log = bluetooth::log;

namespace {

class BroadcastSinkInterfaceImpl;
std::unique_ptr<BroadcastSinkInterface> broadcast_sink_instance;

class BroadcastSinkInterfaceImpl
    : public BroadcastSinkInterface,
      public BroadcastSinkCallbacks {
 public:
  BroadcastSinkInterfaceImpl() : callbacks_(nullptr) {}

  ~BroadcastSinkInterfaceImpl() override = default;

  // BroadcastSinkInterface implementation
  void Initialize(BroadcastSinkCallbacks* callbacks,
                  uint8_t max_source_capacity) override {
    log::info("Initialize with max_source_capacity={}", max_source_capacity);
    this->callbacks_ = callbacks;

    do_in_main_thread(Bind(&LeAudioBroadcastSink::Initialize, this,
                           max_source_capacity));
  }

  void Stop(void) override {
    log::info("Stop");
    do_in_main_thread(Bind(&LeAudioBroadcastSink::Stop));
  }

  void Cleanup(void) override {
    log::info("Cleanup");
    do_in_main_thread(Bind(&LeAudioBroadcastSink::Cleanup));
    callbacks_ = nullptr;
  }

  void AddSource(const RawAddress& addr, uint8_t addr_type,
                 uint8_t adv_sid, BroadcastId broadcast_id,
                 int8_t rssi, const std::string& broadcast_name,
                 bool is_public,
                 const std::vector<uint8_t>& public_metadata,
                 uint8_t public_features) override {
    do_in_main_thread(Bind(&LeAudioBroadcastSink::AddSource,
                           Unretained(LeAudioBroadcastSink::Get()),
                           addr, addr_type, adv_sid, broadcast_id, rssi,
                           broadcast_name, is_public, public_metadata,
                           public_features));
  }

  void JoinSource(BroadcastId broadcast_id,
                  const std::optional<BroadcastCode>& broadcast_code,
                  const std::vector<uint8_t>& bis_indices) override {
    do_in_main_thread(Bind(&LeAudioBroadcastSink::JoinSource,
                           Unretained(LeAudioBroadcastSink::Get()),
                           broadcast_id, broadcast_code, bis_indices));
  }

  void LeaveSource(BroadcastId broadcast_id) override {
    do_in_main_thread(Bind(&LeAudioBroadcastSink::LeaveSource,
                           Unretained(LeAudioBroadcastSink::Get()),
                           broadcast_id));
  }

  void RemoveSource(BroadcastId broadcast_id) override {
    do_in_main_thread(Bind(&LeAudioBroadcastSink::RemoveSource,
                           Unretained(LeAudioBroadcastSink::Get()),
                           broadcast_id));
  }

  void DestroySource(BroadcastId broadcast_id) override {
    do_in_main_thread(Bind(&LeAudioBroadcastSink::DestroySource,
                           Unretained(LeAudioBroadcastSink::Get()),
                           broadcast_id));
  }

  void GetSourceMetadata(BroadcastId broadcast_id) override {
    do_in_main_thread(Bind(&LeAudioBroadcastSink::GetSourceMetadata,
                           Unretained(LeAudioBroadcastSink::Get()),
                           broadcast_id));
  }

  void SourcePublicMetadataChanged(BroadcastId broadcast_id,
                           const std::string& broadcast_name,
                           const std::vector<uint8_t>& public_metadata) override {
    log::info("SourcePublicMetadataChanged: broadcast_id=0x{:08x}, broadcast_name={}, public_metadata={} bytes",
              broadcast_id, broadcast_name, public_metadata.size());

    do_in_main_thread(Bind(&LeAudioBroadcastSink::SourcePublicMetadataChanged,
                           Unretained(LeAudioBroadcastSink::Get()),
                           broadcast_id, broadcast_name, public_metadata));
  }

  // LeAudioBroadcastSinkCallbacks implementation (BTA callbacks)
  void OnSourceAddFailed(BroadcastId broadcast_id, uint8_t reason) override {
    log::info("OnSourceAddFailed: broadcast_id=0x{:08x}, reason={}", broadcast_id, reason);
    do_in_jni_thread(Bind(&BroadcastSinkCallbacks::OnSourceAddFailed,
                          Unretained(callbacks_), broadcast_id, reason));
  }

  void OnSourceJoinFailed(BroadcastId broadcast_id, uint8_t reason) override {
    log::info("OnSourceJoinFailed: broadcast_id=0x{:08x}, reason={}",
              broadcast_id, reason);
    do_in_jni_thread(Bind(&BroadcastSinkCallbacks::OnSourceJoinFailed,
                          Unretained(callbacks_), broadcast_id, reason));
  }

  void OnSourceLeaveFailed(BroadcastId broadcast_id, uint8_t reason) override {
    log::info("OnSourceLeaveFailed: broadcast_id=0x{:08x}, reason={}", broadcast_id, reason);
    do_in_jni_thread(Bind(&BroadcastSinkCallbacks::OnSourceLeaveFailed,
                          Unretained(callbacks_), broadcast_id, reason));
  }

  void OnSourceRemoveFailed(BroadcastId broadcast_id, uint8_t reason) override {
    log::info("OnSourceRemoveFailed: broadcast_id=0x{:08x}, reason={}", broadcast_id, reason);
    do_in_jni_thread(Bind(&BroadcastSinkCallbacks::OnSourceRemoveFailed,
                          Unretained(callbacks_), broadcast_id, reason));
  }

  void OnSourceMetadataChanged(BroadcastId broadcast_id,
                               const BroadcastMetadata& broadcast_metadata) override {
    log::info("OnSourceMetadataChanged: broadcast_id=0x{:08x}", broadcast_id);
    do_in_jni_thread(Bind(&BroadcastSinkCallbacks::OnSourceMetadataChanged,
                          Unretained(callbacks_), broadcast_id, broadcast_metadata));
  }

  void OnBroadcastSinkAudioSessionCreated(bool success) override {
    log::info("OnBroadcastSinkAudioSessionCreated: success={}", success);
    do_in_jni_thread(Bind(&BroadcastSinkCallbacks::OnBroadcastSinkAudioSessionCreated,
                          Unretained(callbacks_), success));
  }

  void OnSourceDestroyed(BroadcastId broadcast_id, uint8_t reason) override {
    log::info("OnSourceDestroyed: broadcast_id=0x{:08x}, reason={}", broadcast_id, reason);
    do_in_jni_thread(Bind(&BroadcastSinkCallbacks::OnSourceDestroyed,
                          Unretained(callbacks_), broadcast_id, reason));
  }

  void OnBroadcastSinkStateChanged(BroadcastId broadcast_id, uint8_t state) override {
    log::info("OnBroadcastSinkStateChanged: broadcast_id=0x{:08x}, state={}", broadcast_id, state);
    do_in_jni_thread(Bind(&BroadcastSinkCallbacks::OnBroadcastSinkStateChanged,
                          Unretained(callbacks_), broadcast_id, state));
  }

 private:
  BroadcastSinkCallbacks* callbacks_;
};

}  // namespace

BroadcastSinkInterface* btif_le_audio_broadcast_sink_get_interface() {
  if (!broadcast_sink_instance) {
    broadcast_sink_instance.reset(new BroadcastSinkInterfaceImpl());
  }

  return broadcast_sink_instance.get();
}
