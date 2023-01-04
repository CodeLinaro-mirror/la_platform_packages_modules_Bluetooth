/*
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 *
*/

#ifndef ANDROID_INCLUDE_BT_ATP_LOCATOR_H
#define ANDROID_INCLUDE_BT_ATP_LOCATOR_H

#include <hardware/bluetooth.h>

#define BT_PROFILE_ATP_LOCATOR_ID "atp_locator"

namespace bluetooth {
namespace atp_locator {

enum class ConnectionState {
  DISCONNECTED = 0,
  CONNECTING,
  CONNECTED,
  DISCONNECTING
};

class AtpLocatorCallbacks {
 public:
  virtual ~AtpLocatorCallbacks() = default;

  /** Callback for profile connection state change */
  virtual void OnConnectionState(ConnectionState state,
                                 const RawAddress& address) = 0;

  virtual void OnEnableBleDirectionFinding(uint8_t status, const RawAddress& address) = 0;

  virtual void OnLeAoaResults(uint8_t status, double azimuth, uint8_t azimuth_unc,
                              double elevation, uint8_t elevation_unc,
                              const RawAddress& bd_addr) = 0;
};

class AtpLocatorInterface {
 public:
  virtual ~AtpLocatorInterface() = default;

  /** Register the ATP Locator callbacks */
  virtual void Init(AtpLocatorCallbacks* callbacks) = 0;

  /** Connect to Asset Tag device */
  virtual void Connect(const RawAddress& address, bool isDirect) = 0;

  /** Disconnect from Asset Tag device */
  virtual void Disconnect(const RawAddress& address) = 0;

  virtual void EnableBleDirectionFinding(uint8_t sampling_enable,
      uint8_t slot_durations, uint8_t enable, uint16_t cte_req_int,
      uint8_t req_cte_len, uint8_t direction_finding_type,
      const RawAddress& address) = 0;

  /** Closes the interface. */
  virtual void Cleanup(void) = 0;
};

}  // namespace atp_locator
}  // namespace bluetooth

#endif /* ANDROID_INCLUDE_BT_ATP_LOCATOR_H */
