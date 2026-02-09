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

#include "set_browsed_player.h"

#include <base/sys_byteorder.h>

#include "internal_include/bt_trace.h"

namespace bluetooth {
namespace avrcp {

std::unique_ptr<SetBrowsedPlayerResponseBuilder> SetBrowsedPlayerResponseBuilder::MakeBuilder(
        Status status, uint16_t uid_counter, uint32_t num_items_in_folder, uint8_t folder_depth,
        std::stack<std::string> &folder_name_list, uint16_t browse_mtu) {
  std::unique_ptr<SetBrowsedPlayerResponseBuilder> builder(new SetBrowsedPlayerResponseBuilder(
          status, uid_counter, num_items_in_folder, folder_depth, folder_name_list, browse_mtu));

  return builder;
}

size_t SetBrowsedPlayerResponseBuilder::GetFolderItemsSize(
                                        std::stack<std::string> &folder_name_list) {
  size_t len = size();
  if (status_ != Status::NO_ERROR) return len;
  // This is only included if the folder returned isn't the root folder

  int i = 0;
  if (folder_depth_ != 0) {
    // copy ordered_folder_list to temp stack
    std::stack<std::string> temp_folder_list(folder_name_list);
    // pushing folders if len + size <= browse_mtu
    while ((!temp_folder_list.empty()) && (len <= browse_mtu_)) {
      len += 2;                                    // Folder Name Size
      len += temp_folder_list.top().size();        // Folder Name
      temp_folder_list.pop();
      i++;
    }
    folder_depth_ = i;
  }
  return len;
}

size_t SetBrowsedPlayerResponseBuilder::size() const {
  size_t len = BrowsePacket::kMinSize();
  len += 1;  // Status

  // If the status isn't success the rest of the fields are ommited
  if (status_ != Status::NO_ERROR) {
    return len;
  }

  len += 2;  // UID Counter
  len += 4;  // Number of items in folder
  len += 2;  // UTF-8 Character Set
  len += 1;  // Folder Depth

  return len;
}

bool SetBrowsedPlayerResponseBuilder::Serialize(const std::shared_ptr<::bluetooth::Packet>& pkt) {
  std::stack<std::string> ordered_folder_list;
  while(!folder_name_list_.empty()) {
    ordered_folder_list.push(folder_name_list_.top());
    folder_name_list_.pop();
  }

  size_t folders_size = GetFolderItemsSize(ordered_folder_list);

  ReserveSpace(pkt, folders_size);

  BrowsePacketBuilder::PushHeader(pkt, folders_size - BrowsePacket::kMinSize());

  AddPayloadOctets1(pkt, (uint8_t)status_);

  if (status_ != Status::NO_ERROR) {
    return true;
  }
  AddPayloadOctets2(pkt, base::ByteSwap(uid_counter_));
  AddPayloadOctets4(pkt, base::ByteSwap(num_items_in_folder_));
  AddPayloadOctets2(pkt, base::ByteSwap((uint16_t)0x006a));  // UTF-8
  AddPayloadOctets1(pkt, folder_depth_);

  // Skip adding the folder name if the folder depth is 0
  if (folder_depth_ == 0) {
    return true;
  }

  while(folder_depth_ > 0) {
    std::string folder_name = ordered_folder_list.top();
    uint16_t folder_name_len = folder_name.size();
    AddPayloadOctets2(pkt, base::ByteSwap(folder_name_len));
    for (auto it = folder_name.begin(); it != folder_name.end(); it++) {
      AddPayloadOctets1(pkt, *it);
    }
    ordered_folder_list.pop();
    folder_depth_--;
  }

  return true;
}

uint16_t SetBrowsedPlayerRequest::GetPlayerId() const {
  auto it = begin() + BrowsePacket::kMinSize();
  return it.extractBE<uint16_t>();
}

bool SetBrowsedPlayerRequest::IsValid() const {
  if (!BrowsePacket::IsValid()) {
    return false;
  }
  return size() == kMinSize();
}

std::string SetBrowsedPlayerRequest::ToString() const {
  std::stringstream ss;
  ss << "SetBrowsedPlayerRequestPacket: " << std::endl;
  ss << "  └ PDU = " << GetPdu() << std::endl;
  ss << "  └ Length = " << GetLength() << std::endl;
  ss << "  └ Player ID = " << loghex(GetPlayerId()) << std::endl;
  ss << std::endl;

  return ss.str();
}

}  // namespace avrcp
}  // namespace bluetooth
