/******************************************************************************
 *
 *  Copyright 2016 Google, Inc.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at:
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 ******************************************************************************/

#include "common/metrics.h"

#include <base/base64.h>
#include <bluetooth/log.h>
#include <frameworks/proto_logging/stats/enums/bluetooth/le/enums.pb.h>
#include <include/hardware/bt_av.h>
#include <statslog_bt.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>  // NOLINT
#include <utility>

#include "common/address_obfuscator.h"
#include "common/leaky_bonded_queue.h"
#include "common/time_util.h"
#include "hci/address.h"
#include "main/shim/metric_id_api.h"
#include "osi/include/osi.h"
#include "types/raw_address.h"

namespace std {
template <>
struct formatter<android::bluetooth::DirectionEnum>
    : enum_formatter<android::bluetooth::DirectionEnum> {};
template <>
struct formatter<android::bluetooth::SocketConnectionstateEnum>
    : enum_formatter<android::bluetooth::SocketConnectionstateEnum> {};
template <>
struct formatter<android::bluetooth::SocketRoleEnum>
    : enum_formatter<android::bluetooth::SocketRoleEnum> {};
template <>
struct formatter<android::bluetooth::AddressTypeEnum>
    : enum_formatter<android::bluetooth::AddressTypeEnum> {};
template <>
struct formatter<android::bluetooth::DeviceInfoSrcEnum>
    : enum_formatter<android::bluetooth::DeviceInfoSrcEnum> {};
template <>
struct formatter<android::bluetooth::SocketErrorEnum>
    : enum_formatter<android::bluetooth::SocketErrorEnum> {};
}  // namespace std

namespace bluetooth {
namespace common {

using bluetooth::hci::Address;

void LogLinkLayerConnectionEvent(const RawAddress* address, uint32_t connection_handle,
                                 android::bluetooth::DirectionEnum direction, uint16_t link_type,
                                 uint32_t hci_cmd, uint16_t hci_event, uint16_t hci_ble_event,
                                 uint16_t cmd_status, uint16_t reason_code) {
  std::string obfuscated_id;
  int metric_id = 0;
  if (address != nullptr) {
    obfuscated_id = AddressObfuscator::GetInstance()->Obfuscate(*address);
    metric_id = bluetooth::shim::AllocateIdFromMetricIdAllocator(*address);
  }
  // nullptr and size 0 represent missing value for obfuscated_id
  BytesField bytes_field(address != nullptr ? obfuscated_id.c_str() : nullptr,
                         address != nullptr ? obfuscated_id.size() : 0);
  int ret = stats_write(BLUETOOTH_LINK_LAYER_CONNECTION_EVENT, bytes_field, connection_handle,
                        direction, link_type, hci_cmd, hci_event, hci_ble_event, cmd_status,
                        reason_code, metric_id);
  if (ret < 0) {
    log::warn(
            "failed to log status 0x{:x}, reason 0x{:x} from cmd 0x{:x}, event "
            "0x{:x}, ble_event 0x{:x} for {}, handle {}, type 0x{:x}, error {}",
            cmd_status, reason_code, hci_cmd, hci_event, hci_ble_event, *address, connection_handle,
            link_type, ret);
  }
}

void LogHciTimeoutEvent(uint32_t hci_cmd) {
  int ret = stats_write(BLUETOOTH_HCI_TIMEOUT_REPORTED, static_cast<int64_t>(hci_cmd));
  if (ret < 0) {
    log::warn("failed for opcode 0x{:x}, error {}", hci_cmd, ret);
  }
}

void LogRemoteVersionInfo(uint16_t handle, uint8_t status, uint8_t version,
                          uint16_t manufacturer_name, uint16_t subversion) {
  int ret = stats_write(BLUETOOTH_REMOTE_VERSION_INFO_REPORTED, handle, status, version,
                        manufacturer_name, subversion);
  if (ret < 0) {
    log::warn(
            "failed for handle {}, status 0x{:x}, version 0x{:x}, "
            "manufacturer_name 0x{:x}, subversion 0x{:x}, error {}",
            handle, status, version, manufacturer_name, subversion, ret);
  }
}

void LogA2dpAudioUnderrunEvent(const RawAddress& address, uint64_t encoding_interval_millis,
                               int num_missing_pcm_bytes) {
  std::string obfuscated_id;
  int metric_id = 0;
  if (!address.IsEmpty()) {
    obfuscated_id = AddressObfuscator::GetInstance()->Obfuscate(address);
    metric_id = bluetooth::shim::AllocateIdFromMetricIdAllocator(address);
  }
  // nullptr and size 0 represent missing value for obfuscated_id
  BytesField bytes_field(address.IsEmpty() ? nullptr : obfuscated_id.c_str(),
                         address.IsEmpty() ? 0 : obfuscated_id.size());
  int64_t encoding_interval_nanos = encoding_interval_millis * 1000000;
  int ret = stats_write(BLUETOOTH_A2DP_AUDIO_UNDERRUN_REPORTED, bytes_field,
                        encoding_interval_nanos, num_missing_pcm_bytes, metric_id);
  if (ret < 0) {
    log::warn(
            "failed for {}, encoding_interval_nanos {}, num_missing_pcm_bytes {}, "
            "error {}",
            address, encoding_interval_nanos, num_missing_pcm_bytes, ret);
  }
}

void LogA2dpAudioOverrunEvent(const RawAddress& address, uint64_t encoding_interval_millis,
                              int num_dropped_buffers, int num_dropped_encoded_frames,
                              int num_dropped_encoded_bytes) {
  std::string obfuscated_id;
  int metric_id = 0;
  if (!address.IsEmpty()) {
    obfuscated_id = AddressObfuscator::GetInstance()->Obfuscate(address);
    metric_id = bluetooth::shim::AllocateIdFromMetricIdAllocator(address);
  }
  // nullptr and size 0 represent missing value for obfuscated_id
  BytesField bytes_field(address.IsEmpty() ? nullptr : obfuscated_id.c_str(),
                         address.IsEmpty() ? 0 : obfuscated_id.size());
  int64_t encoding_interval_nanos = encoding_interval_millis * 1000000;
  int ret = stats_write(BLUETOOTH_A2DP_AUDIO_OVERRUN_REPORTED, bytes_field, encoding_interval_nanos,
                        num_dropped_buffers, num_dropped_encoded_frames, num_dropped_encoded_bytes,
                        metric_id);
  if (ret < 0) {
    log::warn(
            "failed to log for {}, encoding_interval_nanos {}, num_dropped_buffers "
            "{}, num_dropped_encoded_frames {}, num_dropped_encoded_bytes {}, "
            "error {}",
            address, encoding_interval_nanos, num_dropped_buffers, num_dropped_encoded_frames,
            num_dropped_encoded_bytes, ret);
  }
}

void LogA2dpPlaybackEvent(const RawAddress& address, int playback_state, int audio_coding_mode) {
  std::string obfuscated_id;
  int metric_id = 0;
  if (!address.IsEmpty()) {
    obfuscated_id = AddressObfuscator::GetInstance()->Obfuscate(address);
    metric_id = bluetooth::shim::AllocateIdFromMetricIdAllocator(address);
  }
  // nullptr and size 0 represent missing value for obfuscated_id
  BytesField bytes_field(address.IsEmpty() ? nullptr : obfuscated_id.c_str(),
                         address.IsEmpty() ? 0 : obfuscated_id.size());
  int ret = stats_write(BLUETOOTH_A2DP_PLAYBACK_STATE_CHANGED, bytes_field, playback_state,
                        audio_coding_mode, metric_id);
  if (ret < 0) {
    log::warn(
            "failed to log for {}, playback_state {}, audio_coding_mode {}, error "
            "{}",
            address, playback_state, audio_coding_mode, ret);
  }
}

void LogReadRssiResult(const RawAddress& address, uint16_t handle, uint32_t cmd_status,
                       int8_t rssi) {
  std::string obfuscated_id;
  int metric_id = 0;
  if (!address.IsEmpty()) {
    obfuscated_id = AddressObfuscator::GetInstance()->Obfuscate(address);
    metric_id = bluetooth::shim::AllocateIdFromMetricIdAllocator(address);
  }
  // nullptr and size 0 represent missing value for obfuscated_id
  BytesField bytes_field(address.IsEmpty() ? nullptr : obfuscated_id.c_str(),
                         address.IsEmpty() ? 0 : obfuscated_id.size());
  int ret = stats_write(BLUETOOTH_DEVICE_RSSI_REPORTED, bytes_field, handle, cmd_status, rssi,
                        metric_id);
  if (ret < 0) {
    log::warn("failed for {}, handle {}, status 0x{:x}, rssi {} dBm, error {}", address, handle,
              cmd_status, rssi, ret);
  }
}

void LogReadFailedContactCounterResult(const RawAddress& address, uint16_t handle,
                                       uint32_t cmd_status, int32_t failed_contact_counter) {
  std::string obfuscated_id;
  int metric_id = 0;
  if (!address.IsEmpty()) {
    obfuscated_id = AddressObfuscator::GetInstance()->Obfuscate(address);
    metric_id = bluetooth::shim::AllocateIdFromMetricIdAllocator(address);
  }
  // nullptr and size 0 represent missing value for obfuscated_id
  BytesField bytes_field(address.IsEmpty() ? nullptr : obfuscated_id.c_str(),
                         address.IsEmpty() ? 0 : obfuscated_id.size());
  int ret = stats_write(BLUETOOTH_DEVICE_FAILED_CONTACT_COUNTER_REPORTED, bytes_field, handle,
                        cmd_status, failed_contact_counter, metric_id);
  if (ret < 0) {
    log::warn(
            "failed for {}, handle {}, status 0x{:x}, failed_contact_counter {} "
            "packets, error {}",
            address, handle, cmd_status, failed_contact_counter, ret);
  }
}

void LogReadTxPowerLevelResult(const RawAddress& address, uint16_t handle, uint32_t cmd_status,
                               int32_t transmit_power_level) {
  std::string obfuscated_id;
  int metric_id = 0;
  if (!address.IsEmpty()) {
    obfuscated_id = AddressObfuscator::GetInstance()->Obfuscate(address);
    metric_id = bluetooth::shim::AllocateIdFromMetricIdAllocator(address);
  }
  // nullptr and size 0 represent missing value for obfuscated_id
  BytesField bytes_field(address.IsEmpty() ? nullptr : obfuscated_id.c_str(),
                         address.IsEmpty() ? 0 : obfuscated_id.size());
  int ret = stats_write(BLUETOOTH_DEVICE_TX_POWER_LEVEL_REPORTED, bytes_field, handle, cmd_status,
                        transmit_power_level, metric_id);
  if (ret < 0) {
    log::warn(
            "failed for {}, handle {}, status 0x{:x}, transmit_power_level {} "
            "packets, error {}",
            address, handle, cmd_status, transmit_power_level, ret);
  }
}

void LogSmpPairingEvent(const RawAddress& address, uint8_t smp_cmd,
                        android::bluetooth::DirectionEnum direction, uint8_t smp_fail_reason) {
  std::string obfuscated_id;
  int metric_id = 0;
  if (!address.IsEmpty()) {
    obfuscated_id = AddressObfuscator::GetInstance()->Obfuscate(address);
    metric_id = bluetooth::shim::AllocateIdFromMetricIdAllocator(address);
  }
  // nullptr and size 0 represent missing value for obfuscated_id
  BytesField obfuscated_id_field(address.IsEmpty() ? nullptr : obfuscated_id.c_str(),
                                 address.IsEmpty() ? 0 : obfuscated_id.size());
  int ret = stats_write(BLUETOOTH_SMP_PAIRING_EVENT_REPORTED, obfuscated_id_field, smp_cmd,
                        direction, smp_fail_reason, metric_id);
  if (ret < 0) {
    log::warn(
            "failed for {}, smp_cmd 0x{:x}, direction {}, smp_fail_reason 0x{:x}, "
            "error {}",
            address, smp_cmd, direction, smp_fail_reason, ret);
  }
}

void LogClassicPairingEvent(const RawAddress& address, uint16_t handle, uint32_t hci_cmd,
                            uint16_t hci_event, uint16_t cmd_status, uint16_t reason_code,
                            int64_t event_value) {
  std::string obfuscated_id;
  int metric_id = 0;
  if (!address.IsEmpty()) {
    obfuscated_id = AddressObfuscator::GetInstance()->Obfuscate(address);
    metric_id = bluetooth::shim::AllocateIdFromMetricIdAllocator(address);
  }
  // nullptr and size 0 represent missing value for obfuscated_id
  BytesField obfuscated_id_field(address.IsEmpty() ? nullptr : obfuscated_id.c_str(),
                                 address.IsEmpty() ? 0 : obfuscated_id.size());
  int ret = stats_write(BLUETOOTH_CLASSIC_PAIRING_EVENT_REPORTED, obfuscated_id_field, handle,
                        hci_cmd, hci_event, cmd_status, reason_code, event_value, metric_id);
  if (ret < 0) {
    log::warn(
            "failed for {}, handle {}, hci_cmd 0x{:x}, hci_event 0x{:x}, "
            "cmd_status 0x{:x}, reason 0x{:x}, event_value {}, error {}",
            address, handle, hci_cmd, hci_event, cmd_status, reason_code, event_value, ret);
  }
}

void LogSdpAttribute(const RawAddress& address, uint16_t protocol_uuid, uint16_t attribute_id,
                     size_t attribute_size, const char* attribute_value) {
  std::string obfuscated_id;
  int metric_id = 0;
  if (!address.IsEmpty()) {
    obfuscated_id = AddressObfuscator::GetInstance()->Obfuscate(address);
    metric_id = bluetooth::shim::AllocateIdFromMetricIdAllocator(address);
  }
  // nullptr and size 0 represent missing value for obfuscated_id
  BytesField obfuscated_id_field(address.IsEmpty() ? nullptr : obfuscated_id.c_str(),
                                 address.IsEmpty() ? 0 : obfuscated_id.size());
  BytesField attribute_field(attribute_value, attribute_size);
  int ret = stats_write(BLUETOOTH_SDP_ATTRIBUTE_REPORTED, obfuscated_id_field, protocol_uuid,
                        attribute_id, attribute_field, metric_id);
  if (ret < 0) {
    log::warn("failed for {}, protocol_uuid 0x{:x}, attribute_id 0x{:x}, error {}", address,
              protocol_uuid, attribute_id, ret);
  }
}

void LogSocketConnectionState(const RawAddress& address, int port, int type,
                              android::bluetooth::SocketConnectionstateEnum connection_state,
                              int64_t tx_bytes, int64_t rx_bytes, int uid, int server_port,
                              android::bluetooth::SocketRoleEnum socket_role,
                              uint64_t connection_duration_ms,
                              android::bluetooth::SocketErrorEnum error_code,
                              bool is_hardware_offload) {
  std::string obfuscated_id;
  int metric_id = 0;
  if (!address.IsEmpty()) {
    obfuscated_id = AddressObfuscator::GetInstance()->Obfuscate(address);
    metric_id = bluetooth::shim::AllocateIdFromMetricIdAllocator(address);
  }
  // nullptr and size 0 represent missing value for obfuscated_id
  BytesField obfuscated_id_field(address.IsEmpty() ? nullptr : obfuscated_id.c_str(),
                                 address.IsEmpty() ? 0 : obfuscated_id.size());
  int ret = stats_write(BLUETOOTH_SOCKET_CONNECTION_STATE_CHANGED, obfuscated_id_field, port, type,
                        connection_state, tx_bytes, rx_bytes, uid, server_port, socket_role,
                        metric_id, static_cast<int64_t>(connection_duration_ms), error_code,
                        is_hardware_offload);
  if (ret < 0) {
    log::warn(
            "failed for {}, port {}, type {}, state {}, tx_bytes {}, rx_bytes {}, "
            "uid {}, server_port {}, socket_role {}, error {}, connection_duration_ms {}, "
            "socket_error_code {}, "
            "is_hardware_offload {}",
            address, port, type, connection_state, tx_bytes, rx_bytes, uid, server_port,
            socket_role, ret, connection_duration_ms, error_code, is_hardware_offload);
  }
}

void LogManufacturerInfo(const RawAddress& address,
                         android::bluetooth::AddressTypeEnum address_type,
                         android::bluetooth::DeviceInfoSrcEnum source_type,
                         const std::string& source_name, const std::string& manufacturer,
                         const std::string& model, const std::string& hardware_version,
                         const std::string& software_version) {
  std::string obfuscated_id;
  int metric_id = 0;
  if (!address.IsEmpty()) {
    obfuscated_id = AddressObfuscator::GetInstance()->Obfuscate(address);
    metric_id = bluetooth::shim::AllocateIdFromMetricIdAllocator(address);
  }
  // nullptr and size 0 represent missing value for obfuscated_id
  BytesField obfuscated_id_field(address.IsEmpty() ? nullptr : obfuscated_id.c_str(),
                                 address.IsEmpty() ? 0 : obfuscated_id.size());
  int ret = stats_write(BLUETOOTH_DEVICE_INFO_REPORTED, obfuscated_id_field, source_type,
                        source_name.c_str(), manufacturer.c_str(), model.c_str(),
                        hardware_version.c_str(), software_version.c_str(), metric_id, address_type,
                        address.address[5], address.address[4], address.address[3]);
  if (ret < 0) {
    log::warn(
            "failed for {}, source_type {}, source_name {}, manufacturer {}, model "
            "{}, hardware_version {}, software_version {} MAC address type {} MAC "
            "address prefix {} {} {}, error {}",
            address, source_type, source_name, manufacturer, model, hardware_version,
            software_version, address_type, address.address[5], address.address[4],
            address.address[3], ret);
  }
}

void LogBluetoothHalCrashReason(const RawAddress& address, uint32_t error_code,
                                uint32_t vendor_error_code) {
  std::string obfuscated_id;
  if (!address.IsEmpty()) {
    obfuscated_id = AddressObfuscator::GetInstance()->Obfuscate(address);
  }
  // nullptr and size 0 represent missing value for obfuscated_id
  BytesField obfuscated_id_field(address.IsEmpty() ? nullptr : obfuscated_id.c_str(),
                                 address.IsEmpty() ? 0 : obfuscated_id.size());
  int ret = stats_write(BLUETOOTH_HAL_CRASH_REASON_REPORTED, 0, obfuscated_id_field, error_code,
                        vendor_error_code);
  if (ret < 0) {
    log::warn("failed for {}, error_code 0x{:x}, vendor_error_code 0x{:x}, error {}", address,
              error_code, vendor_error_code, ret);
  }
}

void LogLeAudioConnectionSessionReported(
        int32_t group_size, int32_t group_metric_id, int64_t connection_duration_nanos,
        const std::vector<int64_t>& device_connecting_offset_nanos,
        const std::vector<int64_t>& device_connected_offset_nanos,
        const std::vector<int64_t>& device_connection_duration_nanos,
        const std::vector<int32_t>& device_connection_status,
        const std::vector<int32_t>& device_disconnection_status,
        const std::vector<RawAddress>& device_address,
        const std::vector<int64_t>& streaming_offset_nanos,
        const std::vector<int64_t>& streaming_duration_nanos,
        const std::vector<int32_t>& streaming_context_type) {
  std::vector<int32_t> device_metric_id(device_address.size());
  for (uint64_t i = 0; i < device_address.size(); i++) {
    if (!device_address[i].IsEmpty()) {
      device_metric_id[i] = bluetooth::shim::AllocateIdFromMetricIdAllocator(device_address[i]);
    } else {
      device_metric_id[i] = 0;
    }
  }
  int ret = stats_write(LE_AUDIO_CONNECTION_SESSION_REPORTED, group_size, group_metric_id,
                        connection_duration_nanos, device_connecting_offset_nanos,
                        device_connected_offset_nanos, device_connection_duration_nanos,
                        device_connection_status, device_disconnection_status, device_metric_id,
                        streaming_offset_nanos, streaming_duration_nanos, streaming_context_type);
  if (ret < 0) {
    log::warn(
            "failed for group {}device_connecting_offset_nanos[{}], "
            "device_connected_offset_nanos[{}], "
            "device_connection_duration_nanos[{}], device_connection_status[{}], "
            "device_disconnection_status[{}], device_metric_id[{}], "
            "streaming_offset_nanos[{}], streaming_duration_nanos[{}], "
            "streaming_context_type[{}]",
            group_metric_id, device_connecting_offset_nanos.size(),
            device_connected_offset_nanos.size(), device_connection_duration_nanos.size(),
            device_connection_status.size(), device_disconnection_status.size(),
            device_metric_id.size(), streaming_offset_nanos.size(), streaming_duration_nanos.size(),
            streaming_context_type.size());
  }
}

void LogLeAudioBroadcastSessionReported(int64_t duration_nanos) {
  int ret = stats_write(LE_AUDIO_BROADCAST_SESSION_REPORTED, duration_nanos);
  if (ret < 0) {
    log::warn("failed for duration={}", duration_nanos);
  }
}

}  // namespace common

}  // namespace bluetooth
