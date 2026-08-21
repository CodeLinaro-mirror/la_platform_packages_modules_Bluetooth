/*
 * Copyright (C) 2022 The Android Open Source Project
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

#include <gmock/gmock.h>

#include <tuple>

#include "hci/distance_measurement_manager.h"

// Unit test interfaces
namespace bluetooth {
namespace hci {

namespace testing {

class MockDistanceMeasurementCallbacks : public DistanceMeasurementCallbacks {
public:
  using DistanceMeasurementResultArgs =
      std::tuple<Address, uint32_t, double, uint32_t, int, int, int, int, uint64_t, int, int,
                 int8_t, double, DistanceMeasurementDetectedAttackLevel, double,
                 DistanceMeasurementMethod>;

  MOCK_METHOD(void, OnDistanceMeasurementStarted, (Address, uint32_t, DistanceMeasurementMethod),
              (override));
  MOCK_METHOD(void, OnDistanceMeasurementStopped,
              (Address, uint32_t, DistanceMeasurementErrorCode, DistanceMeasurementMethod),
              (override));
  void OnDistanceMeasurementResult(
          Address address, uint32_t session_id, double meter, uint32_t error_centimeter,
          int azimuth_angle, int error_azimuth_angle, int altitude_angle,
          int error_altitude_angle, uint64_t elapsed_realtime_nanos, int remote_tx_power,
          int rssi, int8_t confidence_level, double delayed_spread_meters,
          DistanceMeasurementDetectedAttackLevel detected_attack_level,
          double velocity_meters_per_second, DistanceMeasurementMethod method) override {
    OnDistanceMeasurementResultCallback(std::make_tuple(
            address, session_id, meter, error_centimeter, azimuth_angle, error_azimuth_angle,
            altitude_angle, error_altitude_angle, elapsed_realtime_nanos, remote_tx_power, rssi,
            confidence_level, delayed_spread_meters, detected_attack_level,
            velocity_meters_per_second, method));
  }
  MOCK_METHOD(void, OnDistanceMeasurementResultCallback, (DistanceMeasurementResultArgs));
  MOCK_METHOD(void, OnRasFragmentReady, (Address, uint16_t, bool, std::vector<uint8_t>),
              (override));
  MOCK_METHOD(void, OnVendorSpecificCharacteristics,
              (std::vector<hal::VendorSpecificCharacteristic>), (override));
  MOCK_METHOD(void, OnVendorSpecificReply,
              (Address, std::vector<bluetooth::hal::VendorSpecificCharacteristic>), (override));
  MOCK_METHOD(void, OnHandleVendorSpecificReplyComplete, (Address, bool), (override));
  MOCK_METHOD(void, OnRangingHardwareOffloadEnabled, (), (override));
};

class MockDistanceMeasurementManager : public DistanceMeasurementManager {
public:
  MOCK_METHOD(void, RegisterDistanceMeasurementCallbacks,
              (DistanceMeasurementCallbacks * callbacks), (override));
  MOCK_METHOD(void, StartDistanceMeasurement,
              (int32_t app_uid, uint32_t session_id, const Address&, uint16_t connection_handle,
               hci::Role local_hci_role, uint16_t interval, DistanceMeasurementMethod method,
               DistanceMeasurementSightType sight_type,
               DistanceMeasurementLocationType location_type),
              (override));
  MOCK_METHOD(void, StopDistanceMeasurement,
              (uint32_t session_id, const Address& address, uint16_t connection_handle,
               DistanceMeasurementMethod method),
              (override));
  MOCK_METHOD(void, HandleRasClientConnectedEvent,
              (const Address& address, uint16_t connection_handle, uint16_t att_handle,
               const std::vector<hal::VendorSpecificCharacteristic>& vendor_specific_data,
               uint16_t conn_interval),
              (override));
  MOCK_METHOD(void, HandleRasClientDisconnectedEvent,
              (const Address& address, const ras::RasDisconnectReason& ras_disconnect_reason),
              (override));
  MOCK_METHOD(void, HandleVendorSpecificReply,
              (const Address& address, uint16_t connection_handle,
               const std::vector<hal::VendorSpecificCharacteristic>& vendor_specific_reply),
              (override));
  MOCK_METHOD(void, HandleRasServerConnected,
              (const Address& identity_address, uint16_t connection_handle,
               hci::Role local_hci_role),
              (override));
  MOCK_METHOD(void, HandleMtuChanged, (uint16_t connection_handle, uint16_t mtu), (override));
  MOCK_METHOD(void, HandleRasServerDisconnected,
              (const Address& identity_address, uint16_t connection_handle), (override));
  MOCK_METHOD(void, HandleVendorSpecificReplyComplete,
              (const Address& address, uint16_t connection_handle, bool success), (override));
  MOCK_METHOD(void, HandleRemoteData,
              (const Address& address, uint16_t connection_handle,
               const std::vector<uint8_t>& raw_data),
              (override));
  MOCK_METHOD(void, HandleRemoteDataTimeout, (const Address& address, uint16_t connection_handle),
              (override));
  MOCK_METHOD(void, HandleConnIntervalUpdated,
              (const Address& address, uint16_t connection_handle, uint16_t conn_interval),
              (override));
  MOCK_METHOD(void, SetCsParams,
              (const Address&, uint16_t connection_handle, int mSightType, int mLocationType,
                   int mCsSecurityLevel, int mFrequency, int mDuration), (override));
};

}  // namespace testing
}  // namespace hci
}  // namespace bluetooth