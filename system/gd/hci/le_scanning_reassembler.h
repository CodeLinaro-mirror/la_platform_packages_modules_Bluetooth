/*
 * Copyright (C) 2019 The Android Open Source Project
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
#pragma once

#include <gtest/gtest_prod.h>

#include <cstdint>
#include <list>
#include <optional>
#include <vector>

#include "hci/address_with_type.h"
#include "hci/hci_packets.h"

namespace bluetooth::hci {

/// The LE Scanning reassembler is responsible for defragmenting
/// LE advertising reports that are too large to fit inside an HCI event
/// and were fragmented by the controller.
/// The reassembler also joins scan response data with the
/// matching advertising data.

class LeScanningReassembler {
public:
  struct CompleteAdvertisingData {
    uint16_t extended_event_type;
    std::vector<uint8_t> data;
  };

  LeScanningReassembler() {}

  LeScanningReassembler(const LeScanningReassembler&) = delete;

  LeScanningReassembler& operator=(const LeScanningReassembler&) = delete;

  struct PeriodicAdvertisingFragment {
    std::optional<uint16_t> sync_handle;
    std::vector<uint8_t> data;

    PeriodicAdvertisingFragment(uint16_t sync_handle, const std::vector<uint8_t>& data)
        : sync_handle(sync_handle), data(data.begin(), data.end()) {}
  };

  /// Process an incoming advertsing report, extracted from any of the
  /// HCI LE Advertising Report or the HCI LE Extended Advertising Report
  /// events.
  /// Returns the completed advertising data if the event was complete, or the
  /// completion of a fragmented advertising event.

  std::optional<std::vector<uint8_t>> ProcessPeriodicAdvertisingReport(
          uint16_t sync_handle, DataStatus status,
          const std::vector<uint8_t>& periodic_advertising_data);

  std::optional<CompleteAdvertisingData> ProcessAdvertisingReport(
          uint16_t event_type, uint8_t address_type, Address address, uint8_t advertising_sid,
          const std::vector<uint8_t>& advertising_data,
          uint8_t channel_index = kChannelIndexNotPresent);

  /// Configure the scan response filter.
  /// If true all scan responses are ignored.
  void SetIgnoreScanResponses(bool ignore_scan_responses) {
    ignore_scan_responses_ = ignore_scan_responses;
  }

private:
  /// Determine if scan responses should be processed or ignored.
  bool ignore_scan_responses_{false};

  /// Constants for parsing event_type.
  static constexpr uint8_t kConnectableBit = 0;
  static constexpr uint8_t kScannableBit = 1;
  static constexpr uint8_t kDirectedBit = 2;
  static constexpr uint8_t kScanResponseBit = 3;
  static constexpr uint8_t kLegacyBit = 4;
  static constexpr uint8_t kDataStatusBits = 5;

  /// Value of channel_index indicating that the controller did not report an
  /// advertising channel index for this report.
  static constexpr uint8_t kChannelIndexNotPresent = 0xFF;

  /// Manufacturer Specific Data (AD Type 0xFF) company identifier used to wrap
  /// the reported advertising channel index, so it can be read back by apps
  /// via android.bluetooth.le.ScanRecord#getManufacturerSpecificData().
  static constexpr uint16_t kChannelIndexCompanyId = 0x000A;

  /// Size in bytes of the Manufacturer Specific Data AD structure appended by
  /// AppendChannelIndexData: AD length(1) + AD type(1) + company_id(2) +
  /// channel_index(1).
  static constexpr size_t kChannelIndexAdStructureSize = 5;

  /// Maximum size in bytes of the advertising data, matching the extended
  /// advertising PDU size cap. AppendChannelIndexData must not grow
  /// advertising_data past this limit.
  static constexpr size_t kMaxAdvertisingDataSize = 1650;

  /// Appends a Manufacturer Specific Data (AD Type 0xFF) entry carrying the
  /// advertising channel index to the trimmed advertising data, unless no
  /// channel index was reported for this advertisement.
  static std::vector<uint8_t> AppendChannelIndexData(std::vector<uint8_t> advertising_data,
                                                      uint8_t channel_index);

  /// Packs the information necessary to disambiguate advertising events:
  /// - For legacy advertising events, the advertising address and
  ///   advertising address type are used to disambiguate advertisers.
  /// - For extended advertising events, the SID is optionally used to
  ///   differentiate between advertising sets of the same advertiser.
  ///   The advertiser can also be anonymous in which case
  ///   the address is not provided. In this case, and when the SID
  ///   is missing, we trust the controller to send fragments of the same
  ///   advertisement together and not interleaved with that of other
  ///   advertisers.
  struct AdvertisingKey {
    std::optional<AddressWithType> address;
    std::optional<uint8_t> sid;

    AdvertisingKey(Address address, DirectAdvertisingAddressType address_type, uint8_t sid);
    bool operator==(const AdvertisingKey& other);
  };

  /// Packs incomplete advertising data.
  struct AdvertisingFragment {
    AdvertisingKey key;
    uint16_t extended_event_type;
    uint8_t channel_index;
    std::vector<uint8_t> data;

    AdvertisingFragment(const AdvertisingKey& key, uint16_t extended_event_type,
                        uint8_t channel_index, const std::vector<uint8_t>& data)
        : key(key),
          extended_event_type(extended_event_type),
          channel_index(channel_index),
          data(data.begin(), data.end()) {}
  };

  /// Advertising cache for de-fragmenting extended advertising reports,
  /// and joining advertising reports with the matching scan response when
  /// applicable.
  /// The cached advertising data is removed as soon as the complete
  /// advertisement is got (including the scan response).
  static constexpr size_t kMaximumCacheSize = 16;
  std::list<AdvertisingFragment> cache_;

  /// Advertising cache management methods.
  std::list<AdvertisingFragment>::iterator AppendFragment(const AdvertisingKey& key,
                                                          uint16_t extended_event_type,
                                                          uint8_t channel_index,
                                                          const std::vector<uint8_t>& data);

  std::list<PeriodicAdvertisingFragment>::iterator AppendPeriodicFragment(
          uint16_t sync_handle, const std::vector<uint8_t>& data);
  void RemoveFragment(const AdvertisingKey& key);

  bool ContainsFragment(const AdvertisingKey& key);
  bool ContainsPeriodicFragment(uint16_t sync_handle);
  std::list<PeriodicAdvertisingFragment>::iterator FindPeriodicFragment(uint16_t sync_handle);
  std::list<AdvertisingFragment>::iterator FindFragment(const AdvertisingKey& key);

  /// Advertising cache for de-fragmenting periodic advertising reports.
  static constexpr size_t kMaximumPeriodicCacheSize = 16;
  std::list<PeriodicAdvertisingFragment> periodic_cache_;

  /// Trim the advertising data by removing empty or overflowing
  /// GAP Data entries.
  static std::vector<uint8_t> TrimAdvertisingData(const std::vector<uint8_t>& advertising_data);

  FRIEND_TEST(LeScanningReassemblerTest, trim_advertising_data);
};

}  // namespace bluetooth::hci
