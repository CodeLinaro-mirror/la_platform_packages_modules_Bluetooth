/*
 * Copyright 2018 The Android Open Source Project
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

#include "avrcp_browse_packet.h"
#include <stack>

namespace bluetooth {
namespace avrcp {

class SetBrowsedPlayerResponseBuilder : public BrowsePacketBuilder {
public:
  virtual ~SetBrowsedPlayerResponseBuilder() = default;

  static std::unique_ptr<SetBrowsedPlayerResponseBuilder> MakeBuilder(Status status,
                                                                      uint16_t uid_counter,
                                                                      uint32_t num_items_in_folder,
                                                                      uint8_t folder_depth,
                                                                      std::stack<std::string> &folder_name,
                                                                      uint16_t browse_mtu);

  virtual size_t size() const override;
  virtual bool Serialize(const std::shared_ptr<::bluetooth::Packet>& pkt) override;
  size_t GetFolderItemsSize(std::stack<std::string> &folder_name_list);

protected:
  Status status_;
  uint16_t uid_counter_;
  uint32_t num_items_in_folder_;
  uint8_t folder_depth_;
  std::stack<std::string> folder_name_list_;
  uint16_t browse_mtu_;

  SetBrowsedPlayerResponseBuilder(Status status, uint16_t uid_counter, uint32_t num_items_in_folder,
                                  uint8_t folder_depth, std::stack<std::string> &folder_name_list,
                                  uint16_t browse_mtu)
      : BrowsePacketBuilder(BrowsePdu::SET_BROWSED_PLAYER),
        status_(status),
        uid_counter_(uid_counter),
        num_items_in_folder_(num_items_in_folder),
        folder_depth_(folder_depth),
        folder_name_list_(folder_name_list),
        browse_mtu_(browse_mtu) {}
};

class SetBrowsedPlayerRequest : public BrowsePacket {
public:
  virtual ~SetBrowsedPlayerRequest() = default;

  /**
   * Avrcp Change Path Packet Layout
   *   BrowsePacket:
   *     uint8_t pdu_;
   *     uint16_t length_;
   *   GetFolderItemsRequest:
   *     uint16_t player_id_;
   */
  static constexpr size_t kMinSize() { return BrowsePacket::kMinSize() + 2; }

  uint16_t GetPlayerId() const;

  virtual bool IsValid() const override;
  virtual std::string ToString() const override;

protected:
  using BrowsePacket::BrowsePacket;
};

}  // namespace avrcp
}  // namespace bluetooth
