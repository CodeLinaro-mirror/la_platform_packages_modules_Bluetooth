/*
 * Copyright 2020 HIMSA II K/S - www.himsa.com.
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

#include <base/functional/bind.h>
#include <base/functional/callback.h>

#include <list>
#include <map>
#include <memory>
#include <mutex>

#include "btm_dev.h"
#include "btm_iso_api.h"
#include "common/time_util.h"
#include "hci/controller_interface.h"
#include "hci/include/hci_layer.h"
#include "internal_include/stack_config.h"
#include "main/shim/entry.h"
#include "main/shim/hci_layer.h"
#include "osi/include/allocator.h"
#include "osi/include/properties.h"
#include "stack/include/bt_hdr.h"
#include "stack/include/bt_types.h"
#include "stack/include/btm_log_history.h"
#include "stack/include/hci_error_code.h"
#include "stack/include/hcidefs.h"
#include "stack/include/hcimsgs.h"

namespace bluetooth {
namespace hci {
namespace iso_manager {
static constexpr uint8_t kIsoHeaderWithTsLen = 12;
static constexpr uint8_t kIsoHeaderWithoutTsLen = 8;

static constexpr uint8_t kStateFlagsNone = 0x00;
static constexpr uint8_t kStateFlagIsConnecting = 0x01;
static constexpr uint8_t kStateFlagIsConnected = 0x02;
static constexpr uint8_t kStateFlagHasDataPathSet = 0x04;
/* Tracks TX (input) data path independently from kStateFlagHasDataPathSet.
 * Used for enhanced broadcast sink where TX and RX data paths are set up
 * independently on the same BIS connection handle.  kStateFlagHasDataPathSet
 * is cleared when ANY direction's path is removed; this flag ensures TX path
 * removal can still proceed after RX path removal has cleared the shared flag. */
static constexpr uint8_t kStateFlagHasTxDataPathSet = 0x08;
static constexpr uint8_t kStateFlagIsBroadcast = 0x10;
static constexpr uint8_t kStateFlagIsCancelled = 0x20;
static constexpr uint8_t kStateFlagIsBroadcastSync = 0x40;

constexpr char kBtmLogTag[] = "ISO";

struct iso_sync_info {
  uint16_t tx_seq_nb;
  uint16_t rx_seq_nb;
};

struct iso_base {
  union {
    uint8_t cig_id;
    uint8_t big_handle;
  };

  struct iso_sync_info sync_info;
  std::atomic_uint8_t state_flags;
  uint32_t sdu_itv;
  std::atomic_uint16_t used_credits;

  struct credits_stats {
    size_t credits_underflow_bytes = 0;
    size_t credits_underflow_count = 0;
    uint64_t credits_last_underflow_us = 0;
  };

  struct event_stats {
    size_t evt_lost_count = 0;
    size_t seq_nb_mismatch_count = 0;
    uint64_t evt_last_lost_us = 0;
  };

  credits_stats cr_stats;
  event_stats evt_stats;
};

typedef iso_base iso_cis;
typedef iso_base iso_bis;

struct iso_impl {
  iso_impl() {
    iso_credits_ = shim::GetController()->GetControllerIsoBufferSize().total_num_le_packets_;
    iso_buffer_size_ = shim::GetController()->GetControllerIsoBufferSize().le_data_packet_length_;
    log::info("{} created, iso credits: {}, buffer size: {}.", std::format_ptr(this),
              iso_credits_.load(), iso_buffer_size_);
  }

  ~iso_impl() { log::info("{} removed.", std::format_ptr(this)); }

  void handle_register_cis_callbacks(CigCallbacks* callbacks) {
    log::assert_that(callbacks != nullptr, "Invalid CIG callbacks");
    cig_callbacks_ = callbacks;
  }

  void handle_register_big_callbacks(BigCallbacks* callbacks) {
    log::assert_that(callbacks != nullptr, "Invalid BIG callbacks");
    big_callbacks_ = callbacks;
  }

  void handle_register_dbig_callbacks(DbigCallbacks* callbacks) {
    log::assert_that(callbacks != nullptr, "Invalid DBIG callbacks");
    dbig_callbacks_ = callbacks;
  }

  void handle_register_big_sync_callbacks(BigSyncCallbacks* callbacks) {
    log::assert_that(callbacks != nullptr, "Invalid BIG Sync callbacks");
    big_sync_callbacks_ = callbacks;
  }

  void handle_register_vsc_callback(VscCallback* callback) {
    log::assert_that(callback != nullptr, "Invalid VSC callback");
    vsc_callback_ = callback;
  }

  void handle_register_on_iso_traffic_active_callback(void callback(bool)) {
    log::assert_that(callback != nullptr, "Invalid OnIsoTrafficActive callback");
    const std::lock_guard<std::mutex> lock(on_iso_traffic_active_callbacks_list_mutex_);
    on_iso_traffic_active_callbacks_list_.push_back(callback);
  }

  void on_set_cig_params(uint8_t cig_id, uint32_t sdu_itv_mtos, uint8_t* stream, uint16_t len) {
    uint8_t cis_cnt;
    uint16_t conn_handle;
    cig_create_cmpl_evt evt;

    log::assert_that(cig_callbacks_ != nullptr, "Invalid CIG callbacks");
    log::assert_that(len >= 3, "Invalid packet length: {}", len);

    STREAM_TO_UINT8(evt.status, stream);
    STREAM_TO_UINT8(evt.cig_id, stream);
    STREAM_TO_UINT8(cis_cnt, stream);

    uint8_t evt_code =
            IsCigKnown(cig_id) ? kIsoEventCigOnReconfigureCmpl : kIsoEventCigOnCreateCmpl;

    BTM_LogHistory(kBtmLogTag, RawAddress::kEmpty, "CIG Create complete",
                   std::format("cig_id:0x{:02x}, status: {}", evt.cig_id,
                               hci_status_code_text((tHCI_STATUS)(evt.status))));

    if (evt.status == HCI_SUCCESS) {
      log::assert_that(len >= (3) + (cis_cnt * sizeof(uint16_t)), "Invalid CIS count: {}", cis_cnt);

      /* Remove entries for the reconfigured CIG */
      if (evt_code == kIsoEventCigOnReconfigureCmpl) {
        auto cis_it = conn_hdl_to_cis_map_.cbegin();
        while (cis_it != conn_hdl_to_cis_map_.cend()) {
          if (cis_it->second->cig_id == evt.cig_id) {
            cis_it = conn_hdl_to_cis_map_.erase(cis_it);
          } else {
            ++cis_it;
          }
        }
      }

      evt.conn_handles.reserve(cis_cnt);
      for (int i = 0; i < cis_cnt; i++) {
        STREAM_TO_UINT16(conn_handle, stream);

        evt.conn_handles.push_back(conn_handle);

        auto cis = std::unique_ptr<iso_cis>(new iso_cis());
        cis->cig_id = cig_id;
        cis->sdu_itv = sdu_itv_mtos;
        cis->sync_info = {.tx_seq_nb = 0, .rx_seq_nb = 0};
        cis->used_credits = 0;
        cis->state_flags = kStateFlagsNone;
        conn_hdl_to_cis_map_[conn_handle] = std::move(cis);
      }
    }

    cig_callbacks_->OnCigEvent(evt_code, &evt);

    if (evt_code == kIsoEventCigOnCreateCmpl) {
      const std::lock_guard<std::mutex> lock(on_iso_traffic_active_callbacks_list_mutex_);
      for (auto callback : on_iso_traffic_active_callbacks_list_) {
        callback(true);
      }
    }
  }

  void create_cig(uint8_t cig_id, struct iso_manager::cig_create_params cig_params) {
    log::assert_that(!IsCigKnown(cig_id), "Invalid cig - already exists: {}", cig_id);

    btsnd_hcic_set_cig_params(
            cig_id, cig_params.sdu_itv_mtos, cig_params.sdu_itv_stom, cig_params.sca,
            cig_params.packing, cig_params.framing, cig_params.max_trans_lat_stom,
            cig_params.max_trans_lat_mtos, cig_params.cis_cfgs.size(), cig_params.cis_cfgs.data(),
            base::BindOnce(&iso_impl::on_set_cig_params, weak_factory_.GetWeakPtr(), cig_id,
                           cig_params.sdu_itv_mtos));

    BTM_LogHistory(kBtmLogTag, RawAddress::kEmpty, "CIG Create",
                   std::format("cig_id:0x{:02x}, size: {}", cig_id, cig_params.cis_cfgs.size()));
  }

  void reconfigure_cig(uint8_t cig_id, struct iso_manager::cig_create_params cig_params) {
    log::assert_that(IsCigKnown(cig_id), "No such cig: {}", cig_id);

    btsnd_hcic_set_cig_params(
            cig_id, cig_params.sdu_itv_mtos, cig_params.sdu_itv_stom, cig_params.sca,
            cig_params.packing, cig_params.framing, cig_params.max_trans_lat_stom,
            cig_params.max_trans_lat_mtos, cig_params.cis_cfgs.size(), cig_params.cis_cfgs.data(),
            base::BindOnce(&iso_impl::on_set_cig_params, weak_factory_.GetWeakPtr(), cig_id,
                           cig_params.sdu_itv_mtos));
  }

  void on_remove_cig(uint8_t* stream, uint16_t len) {
    cig_remove_cmpl_evt evt;

    log::assert_that(cig_callbacks_ != nullptr, "Invalid CIG callbacks");
    log::assert_that(len == 2, "Invalid packet length: {}", len);

    STREAM_TO_UINT8(evt.status, stream);
    STREAM_TO_UINT8(evt.cig_id, stream);

    BTM_LogHistory(kBtmLogTag, RawAddress::kEmpty, "CIG Remove complete",
                   std::format("cig_id:0x{:02x}, status: {}", evt.cig_id,
                               hci_status_code_text((tHCI_STATUS)(evt.status))));

    if (evt.status == HCI_SUCCESS) {
      auto cis_it = conn_hdl_to_cis_map_.cbegin();
      while (cis_it != conn_hdl_to_cis_map_.cend()) {
        if (cis_it->second->cig_id == evt.cig_id) {
          cis_it = conn_hdl_to_cis_map_.erase(cis_it);
        } else {
          ++cis_it;
        }
      }
    }

    cig_callbacks_->OnCigEvent(kIsoEventCigOnRemoveCmpl, &evt);

    {
      const std::lock_guard<std::mutex> lock(on_iso_traffic_active_callbacks_list_mutex_);
      for (auto callback : on_iso_traffic_active_callbacks_list_) {
        callback(false);
      }
    }
  }

  void remove_cig(uint8_t cig_id, bool force) {
    if (!force) {
      log::assert_that(IsCigKnown(cig_id), "No such cig: {}", cig_id);
    } else {
      log::warn("Forcing to remove CIG {}", cig_id);
    }

    btsnd_hcic_remove_cig(cig_id,
                          base::BindOnce(&iso_impl::on_remove_cig, weak_factory_.GetWeakPtr()));
    BTM_LogHistory(kBtmLogTag, RawAddress::kEmpty, "CIG Remove",
                   std::format("cig_id:0x{:02x} (f:{})", cig_id, force));
  }

  void on_status_establish_cis(struct iso_manager::cis_establish_params conn_params,
                               uint8_t* stream, uint16_t len) {
    uint8_t status;

    log::assert_that(len == 2, "Invalid packet length: {}", len);

    STREAM_TO_UINT16(status, stream);

    for (auto cis_param : conn_params.conn_pairs) {
      cis_establish_cmpl_evt evt;

      auto cis = GetCisIfKnown(cis_param.cis_conn_handle);
      log::assert_that(cis != nullptr, "No such cis: {}", cis_param.cis_conn_handle);

      auto device_address = cis_hdl_to_addr[evt.cis_conn_hdl];

      if (status != HCI_SUCCESS) {
        evt.status = status;
        evt.cis_conn_hdl = cis_param.cis_conn_handle;
        evt.cig_id = cis->cig_id;
        cis->state_flags &= ~kStateFlagIsConnecting;
        cig_callbacks_->OnCisEvent(kIsoEventCisEstablishCmpl, &evt);

        BTM_LogHistory(kBtmLogTag, device_address, "Establish CIS failed ",
                       std::format("handle:0x{:04x}, status: {}", evt.cis_conn_hdl,
                                   hci_status_code_text((tHCI_STATUS)(status))));
        cis_hdl_to_addr.erase(evt.cis_conn_hdl);
      }

      log::verbose("{}, cis_handle: {:#x}, flags: {:#x}, status {}", device_address,
                   cis_param.cis_conn_handle, cis->state_flags,
                   hci_status_code_text((tHCI_STATUS)(status)));
    }
  }

  void establish_cis(struct iso_manager::cis_establish_params conn_params) {
    for (auto& el : conn_params.conn_pairs) {
      auto cis = GetCisIfKnown(el.cis_conn_handle);
      log::assert_that(cis, "No such cis: {}", el.cis_conn_handle);
      log::assert_that(!(cis->state_flags &
                         (kStateFlagIsConnected | kStateFlagIsConnecting | kStateFlagIsCancelled)),
                       "cis: {} is already connected/connecting/cancelled flags: {}, "
                       "num of cis params: {}",
                       el.cis_conn_handle, cis->state_flags, conn_params.conn_pairs.size());

      cis->state_flags |= kStateFlagIsConnecting;

      tBTM_SEC_DEV_REC* p_rec = btm_find_dev_by_handle(el.acl_conn_handle);
      if (p_rec) {
        cis_hdl_to_addr[el.cis_conn_handle] = p_rec->ble.pseudo_addr;
        BTM_LogHistory(kBtmLogTag, p_rec->ble.pseudo_addr, "Establish CIS",
                       std::format("handle:0x{:04x}", el.acl_conn_handle));
      }
      log::verbose("{}, cis_handle: {:#x}, flags: {:#x}", cis_hdl_to_addr[el.cis_conn_handle],
                   el.cis_conn_handle, cis->state_flags);
    }
    btsnd_hcic_create_cis(conn_params.conn_pairs.size(), conn_params.conn_pairs.data(),
                          base::BindOnce(&iso_impl::on_status_establish_cis,
                                         weak_factory_.GetWeakPtr(), conn_params));
  }

  void disconnect_cis(uint16_t cis_handle, uint8_t reason) {
    auto cis = GetCisIfKnown(cis_handle);
    log::assert_that(cis, "No such cis: {}", cis_handle);
    log::assert_that(
            cis->state_flags & kStateFlagIsConnected || cis->state_flags & kStateFlagIsConnecting,
            "Not connected");

    if (cis->state_flags & kStateFlagIsConnecting) {
      cis->state_flags &= ~kStateFlagIsConnecting;
      cis->state_flags |= kStateFlagIsCancelled;
    }

    bluetooth::legacy::hci::GetInterface().Disconnect(cis_handle, static_cast<tHCI_STATUS>(reason));

    BTM_LogHistory(kBtmLogTag, cis_hdl_to_addr[cis_handle], "Disconnect CIS ",
                   std::format("handle:0x{:04x}, reason:{}", cis_handle,
                               hci_reason_code_text((tHCI_REASON)(reason))));
    log::verbose("{}, cis_handle: {:#x}, flags: {:#x}", cis_hdl_to_addr[cis_handle], cis_handle,
                 cis->state_flags);
  }

  int get_number_of_active_iso() {
    int num_iso = conn_hdl_to_cis_map_.size() + conn_hdl_to_bis_map_.size();
    log::info("Current number of active_iso is {}", num_iso);
    return num_iso;
  }

  void on_setup_iso_data_path(uint8_t data_path_dir, uint8_t* stream, uint16_t /* len */) {
    uint8_t status;
    uint16_t conn_handle;

    STREAM_TO_UINT8(status, stream);
    STREAM_TO_UINT16(conn_handle, stream);

    iso_base* iso = GetIsoIfKnown(conn_handle);
    if (iso == nullptr) {
      /* That can happen when ACL has been disconnected while ISO patch was
       * creating */
      log::warn("Invalid connection handle: {}", conn_handle);
      return;
    }

    BTM_LogHistory(kBtmLogTag, cis_hdl_to_addr[conn_handle], "Setup data path complete",
                   std::format("handle:0x{:04x}, status:{}", conn_handle,
                               hci_status_code_text((tHCI_STATUS)(status))));

    log::verbose("{}, iso_handle: {:#x}, flags: {:#x} status {}", cis_hdl_to_addr[conn_handle],
                 conn_handle, iso->state_flags, hci_status_code_text((tHCI_STATUS)(status)));

    if (status == HCI_SUCCESS) {
      iso->state_flags |= kStateFlagHasDataPathSet;
      /* For enhanced broadcast sink (kStateFlagIsBroadcastSync), TX and RX
       * data paths are set up independently on the same BIS connection handle.
       * Track the TX path with a separate flag so that RX path removal does
       * not prevent subsequent TX path removal.  This guard ensures no impact
       * on CIS, BIG source, or standard broadcast sink use cases. */
      if ((iso->state_flags & kStateFlagIsBroadcastSync) &&
          (data_path_dir == kIsoDataPathDirectionIn)) {
        iso->state_flags |= kStateFlagHasTxDataPathSet;
      }
    }
    if (iso->state_flags & kStateFlagIsBroadcastSync) {
      log::assert_that(big_sync_callbacks_ != nullptr, "Invalid BIG Sync callbacks");
      big_sync_callbacks_->OnSetupIsoDataPath(status, conn_handle, iso->big_handle);
    } else if (iso->state_flags & kStateFlagIsBroadcast) {
      log::assert_that(big_callbacks_ != nullptr, "Invalid BIG callbacks");
      big_callbacks_->OnSetupIsoDataPath(status, conn_handle, iso->big_handle);
    } else {
      log::assert_that(cig_callbacks_ != nullptr, "Invalid CIG callbacks");
      cig_callbacks_->OnSetupIsoDataPath(status, conn_handle, iso->cig_id);
    }
  }

  void setup_iso_data_path(uint16_t conn_handle,
                           struct iso_manager::iso_data_path_params path_params) {
    iso_base* iso = GetIsoIfKnown(conn_handle);
    log::assert_that(iso != nullptr, "No such iso connection: {}", conn_handle);

    // For CIS connections, ensure they are established
    // For BIS connections (broadcast or broadcast sync), no connection establishment check needed
    if (!(iso->state_flags & (kStateFlagIsBroadcast | kStateFlagIsBroadcastSync))) {
      log::assert_that(iso->state_flags & kStateFlagIsConnected, "CIS not established");
    }

    btsnd_hcic_setup_iso_data_path(
            conn_handle, path_params.data_path_dir, path_params.data_path_id,
            path_params.codec_id_format, path_params.codec_id_company, path_params.codec_id_vendor,
            path_params.controller_delay, std::move(path_params.codec_conf),
            base::BindOnce(&iso_impl::on_setup_iso_data_path, weak_factory_.GetWeakPtr(),
                           path_params.data_path_dir));
    BTM_LogHistory(kBtmLogTag, cis_hdl_to_addr[conn_handle], "Setup data path",
                   std::format("handle:0x{:04x}, dir:0x{:02x}, path_id:0x{:02x}, codec_id:0x{:02x}",
                               conn_handle, path_params.data_path_dir, path_params.data_path_id,
                               path_params.codec_id_format));
  }

  void on_remove_iso_data_path(uint8_t* stream, uint16_t len) {
    uint8_t status;
    uint16_t conn_handle;

    if (len < 3) {
      log::warn("Malformatted packet received");
      return;
    }
    STREAM_TO_UINT8(status, stream);
    STREAM_TO_UINT16(conn_handle, stream);

    iso_base* iso = GetIsoIfKnown(conn_handle);
    if (iso == nullptr) {
      /* That could happen when ACL has been disconnected while removing data
       * path */
      log::warn("Invalid connection handle: {}", conn_handle);
      return;
    }

    BTM_LogHistory(kBtmLogTag, cis_hdl_to_addr[conn_handle], "Remove data path complete",
                   std::format("handle:0x{:04x}, status:{}", conn_handle,
                               hci_status_code_text((tHCI_STATUS)(status))));
    log::verbose("{}, iso_handle: {:#x}, flags: {:#x} status {}", cis_hdl_to_addr[conn_handle],
                 conn_handle, iso->state_flags, hci_status_code_text((tHCI_STATUS)(status)));

    if (status == HCI_SUCCESS) {
      iso->state_flags &= ~kStateFlagHasDataPathSet;
      /* Enhanced broadcast sink only: if TX path is still active
       * (kStateFlagHasTxDataPathSet), re-set the shared kStateFlagHasDataPathSet
       * flag so that subsequent TX path removal does not hit the
       * "Data path not set" assertion.  Guarded by kStateFlagIsBroadcastSync
       * to avoid any impact on CIS, BIG source, or standard broadcast sink. */
      if ((iso->state_flags & kStateFlagIsBroadcastSync) &&
          (iso->state_flags & kStateFlagHasTxDataPathSet)) {
        iso->state_flags |= kStateFlagHasDataPathSet;
      }
    }

    if (iso->state_flags & kStateFlagIsBroadcastSync) {
      log::assert_that(big_sync_callbacks_ != nullptr, "Invalid BIG Sync callbacks");
      big_sync_callbacks_->OnRemoveIsoDataPath(status, conn_handle, iso->big_handle);
    } else if (iso->state_flags & kStateFlagIsBroadcast) {
      log::assert_that(big_callbacks_ != nullptr, "Invalid BIG callbacks");
      big_callbacks_->OnRemoveIsoDataPath(status, conn_handle, iso->big_handle);
    } else {
      log::assert_that(cig_callbacks_ != nullptr, "Invalid CIG callbacks");
      cig_callbacks_->OnRemoveIsoDataPath(status, conn_handle, iso->cig_id);
    }
  }

  void remove_iso_data_path(uint16_t iso_handle, uint8_t data_path_dir) {
    iso_base* iso = GetIsoIfKnown(iso_handle);
    log::assert_that(iso != nullptr, "No such iso connection: 0x{:x}", iso_handle);
    /* For enhanced broadcast sink (kStateFlagIsBroadcastSync), TX path removal
     * may arrive after RX path removal has cleared kStateFlagHasDataPathSet.
     * In that case, accept kStateFlagHasTxDataPathSet as evidence that the TX
     * data path was set up and re-set the shared flag so the HCI command
     * proceeds correctly.  Guarded by kStateFlagIsBroadcastSync to avoid any
     * impact on CIS, BIG source, or standard broadcast sink use cases. */
    bool has_path = (iso->state_flags & kStateFlagHasDataPathSet) == kStateFlagHasDataPathSet;
    if (!has_path &&
        (iso->state_flags & kStateFlagIsBroadcastSync) &&
        (data_path_dir == kIsoDataPathDirectionIn)) {
      has_path = (iso->state_flags & kStateFlagHasTxDataPathSet) == kStateFlagHasTxDataPathSet;
      if (has_path) {
        iso->state_flags |= kStateFlagHasDataPathSet;
      }
    }
    log::assert_that(has_path, "Data path not set");
    /* Clear TX-specific flag when TX path is being removed (enhanced sink only). */
    if ((iso->state_flags & kStateFlagIsBroadcastSync) &&
        (data_path_dir == kIsoDataPathDirectionIn)) {
      iso->state_flags &= ~kStateFlagHasTxDataPathSet;
    }

    btsnd_hcic_remove_iso_data_path(
            iso_handle, data_path_dir,
            base::BindOnce(&iso_impl::on_remove_iso_data_path, weak_factory_.GetWeakPtr()));

    BTM_LogHistory(kBtmLogTag, cis_hdl_to_addr[iso_handle], "Remove data path",
                   std::format("handle:0x{:04x}, dir:0x{:02x}", iso_handle, data_path_dir));
    log::verbose("{}, iso_handle: {:#x}, flags: {:#x} dir {:#x}", cis_hdl_to_addr[iso_handle],
                 iso_handle, iso->state_flags, data_path_dir);
  }

  void on_iso_link_quality_read(uint8_t* stream, uint16_t len) {
    uint8_t status;
    uint16_t conn_handle;
    uint32_t txUnackedPackets;
    uint32_t txFlushedPackets;
    uint32_t txLastSubeventPackets;
    uint32_t retransmittedPackets;
    uint32_t crcErrorPackets;
    uint32_t rxUnreceivedPackets;
    uint32_t duplicatePackets;

    // 1 + 2 + 4 * 7
#define ISO_LINK_QUALITY_SIZE 31
    if (len < ISO_LINK_QUALITY_SIZE) {
      log::error("Malformated link quality format, len={}", len);
      return;
    }

    STREAM_TO_UINT8(status, stream);
    if (status != HCI_SUCCESS) {
      log::error("Failed to Read ISO Link Quality, status: 0x{:x}", status);
      return;
    }

    STREAM_TO_UINT16(conn_handle, stream);

    iso_base* iso = GetIsoIfKnown(conn_handle);
    if (iso == nullptr) {
      /* That could happen when ACL has been disconnected while waiting on the
       * read respose */
      log::warn("Invalid connection handle: {}", conn_handle);
      return;
    }

    STREAM_TO_UINT32(txUnackedPackets, stream);
    STREAM_TO_UINT32(txFlushedPackets, stream);
    STREAM_TO_UINT32(txLastSubeventPackets, stream);
    STREAM_TO_UINT32(retransmittedPackets, stream);
    STREAM_TO_UINT32(crcErrorPackets, stream);
    STREAM_TO_UINT32(rxUnreceivedPackets, stream);
    STREAM_TO_UINT32(duplicatePackets, stream);

    log::assert_that(cig_callbacks_ != nullptr, "Invalid CIG callbacks");
    cig_callbacks_->OnIsoLinkQualityRead(
            conn_handle, iso->cig_id, txUnackedPackets, txFlushedPackets, txLastSubeventPackets,
            retransmittedPackets, crcErrorPackets, rxUnreceivedPackets, duplicatePackets);
  }

  void read_iso_link_quality(uint16_t iso_handle) {
    iso_base* iso = GetIsoIfKnown(iso_handle);
    if (iso == nullptr) {
      log::error("No such iso connection: 0x{:x}", iso_handle);
      return;
    }

    btsnd_hcic_read_iso_link_quality(iso_handle, base::BindOnce(&iso_impl::on_iso_link_quality_read,
                                                                weak_factory_.GetWeakPtr()));
  }

  BT_HDR* prepare_hci_packet(uint16_t iso_handle, uint16_t seq_nb, uint16_t data_len) {
    /* Add 2 for packet seq., 2 for length */
    uint16_t iso_data_load_len = data_len + 4;

    /* Add 2 for handle, 2 for length */
    uint16_t iso_full_len = iso_data_load_len + 4;
    BT_HDR* packet = (BT_HDR*)osi_malloc(iso_full_len + sizeof(BT_HDR));
    packet->len = iso_full_len;
    packet->offset = 0;
    packet->event = MSG_STACK_TO_HC_HCI_ISO;
    packet->layer_specific = 0;

    uint8_t* packet_data = packet->data;
    UINT16_TO_STREAM(packet_data, iso_handle);
    UINT16_TO_STREAM(packet_data, iso_data_load_len);

    UINT16_TO_STREAM(packet_data, seq_nb);
    UINT16_TO_STREAM(packet_data, data_len);

    return packet;
  }

  void send_iso_data(uint16_t iso_handle, const uint8_t* data, uint16_t data_len) {
    iso_base* iso = GetIsoIfKnown(iso_handle);
    log::assert_that(iso != nullptr, "No such iso connection handle: 0x{:x}", iso_handle);

    if (!(iso->state_flags & kStateFlagIsBroadcast)) {
      if (!(iso->state_flags & kStateFlagIsConnected)) {
        log::warn("Cis handle: 0x{:x} not established", iso_handle);
        return;
      }
    }

    if (!(iso->state_flags & kStateFlagHasDataPathSet)) {
      log::warn("Data path not set for handle: 0x{:04x}", iso_handle);
      return;
    }

    /* Calculate sequence number for the ISO data packet.
     * It should be incremented by 1 every SDU Interval.
     */
    uint16_t seq_nb = iso->sync_info.tx_seq_nb;
    iso->sync_info.tx_seq_nb = (seq_nb + 1) & 0xffff;

    if (iso_credits_ == 0 || data_len > iso_buffer_size_) {
      iso->cr_stats.credits_underflow_bytes += data_len;
      iso->cr_stats.credits_underflow_count++;
      iso->cr_stats.credits_last_underflow_us = bluetooth::common::time_get_os_boottime_us();

      log::warn(", dropping ISO packet, len: {}, iso credits: {}, iso handle: 0x{:x}",
                static_cast<int>(data_len), static_cast<int>(iso_credits_), iso_handle);
      return;
    }

    iso_credits_--;
    iso->used_credits++;

    BT_HDR* packet = prepare_hci_packet(iso_handle, seq_nb, data_len);
    memcpy(packet->data + kIsoHeaderWithoutTsLen, data, data_len);
    auto hci = bluetooth::shim::hci_layer_get_interface();
    packet->event = MSG_STACK_TO_HC_HCI_ISO | 0x0001;
    hci->transmit_downward(packet, iso_buffer_size_);
  }

  void process_cis_est_pkt(uint8_t len, uint8_t* data) {
    cis_establish_cmpl_evt evt;

    log::assert_that(len == 28, "Invalid packet length: {}", len);
    log::assert_that(cig_callbacks_ != nullptr, "Invalid CIG callbacks");

    STREAM_TO_UINT8(evt.status, data);
    STREAM_TO_UINT16(evt.cis_conn_hdl, data);

    auto cis = GetCisIfKnown(evt.cis_conn_hdl);
    log::assert_that(cis != nullptr, "No such cis: {}", evt.cis_conn_hdl);

    BTM_LogHistory(kBtmLogTag, cis_hdl_to_addr[evt.cis_conn_hdl], "CIS established event",
                   std::format("cis_handle:0x{:04x} status:{} flags:{:#x}", evt.cis_conn_hdl,
                               hci_error_code_text((tHCI_STATUS)(evt.status)), cis->state_flags));

    STREAM_TO_UINT24(evt.cig_sync_delay, data);
    STREAM_TO_UINT24(evt.cis_sync_delay, data);
    STREAM_TO_UINT24(evt.trans_lat_mtos, data);
    STREAM_TO_UINT24(evt.trans_lat_stom, data);
    STREAM_TO_UINT8(evt.phy_mtos, data);
    STREAM_TO_UINT8(evt.phy_stom, data);
    STREAM_TO_UINT8(evt.nse, data);
    STREAM_TO_UINT8(evt.bn_mtos, data);
    STREAM_TO_UINT8(evt.bn_stom, data);
    STREAM_TO_UINT8(evt.ft_mtos, data);
    STREAM_TO_UINT8(evt.ft_stom, data);
    STREAM_TO_UINT16(evt.max_pdu_mtos, data);
    STREAM_TO_UINT16(evt.max_pdu_stom, data);
    STREAM_TO_UINT16(evt.iso_itv, data);

    if (evt.status == HCI_SUCCESS) {
      cis->state_flags |= kStateFlagIsConnected;
    } else {
      if (evt.status == HCI_ERR_CANCELLED_BY_LOCAL_HOST) {
        /* kStateFlagIsCancelled is cleared in disconnection complete event which shall also arrive
         * during CIS cancel procedure.
         * If flag is cleared it means that Disconnection Complete Event arrived
         * before this CIS established event. This is also fine. In such case clear address to
         * handle mapping (which is used only for logs). Otherwise, wait with clearing it when
         * Disconnect Complete event arrives
         */
        if (!(cis->state_flags & kStateFlagIsCancelled)) {
          log::info(
                  "Flag kStateFlagIsCancelled already cleared, means Disconnect Complete arrived "
                  "before this event.");
          cis_hdl_to_addr.erase(evt.cis_conn_hdl);
        }
      } else {
        cis_hdl_to_addr.erase(evt.cis_conn_hdl);
      }
    }

    cis->state_flags &= ~kStateFlagIsConnecting;

    evt.cig_id = cis->cig_id;
    cig_callbacks_->OnCisEvent(kIsoEventCisEstablishCmpl, &evt);
  }

  void disconnection_complete(uint16_t handle, uint8_t reason) {
    /* Check if this is an ISO handle */
    auto cis = GetCisIfKnown(handle);
    if (cis == nullptr) {
      return;
    }

    log::assert_that(cig_callbacks_ != nullptr, "Invalid CIG callbacks");

    log::info("{}, cis_handle {:#x} flags: {}", cis_hdl_to_addr[handle], handle, cis->state_flags);

    BTM_LogHistory(kBtmLogTag, cis_hdl_to_addr[handle], "CIS disconnected",
                   std::format("cis_handle:0x{:04x}, reason:{}", handle,
                               hci_error_code_text((tHCI_REASON)(reason))));

    if (cis->state_flags & kStateFlagIsConnecting) {
      log::info("{}, cis_handle: {:#x} waiting for cis established event with cancel status",
                cis_hdl_to_addr[handle], handle);
    } else {
      cis_hdl_to_addr.erase(handle);
    }

    if (cis->state_flags & kStateFlagIsConnected || cis->state_flags & kStateFlagIsCancelled) {
      cis_disconnected_evt evt = {
              .reason = reason,
              .cig_id = cis->cig_id,
              .cis_conn_hdl = handle,
      };

      cig_callbacks_->OnCisEvent(kIsoEventCisDisconnected, &evt);
      cis->state_flags &= ~kStateFlagIsConnected;
      cis->state_flags &= ~kStateFlagIsCancelled;

      /* return used credits */
      iso_credits_ += cis->used_credits;
      cis->used_credits = 0;

      /* Data path is considered still valid, but can be reconfigured only once
       * CIS is reestablished.
       */
    }
  }

  void handle_gd_num_completed_pkts(uint16_t handle, uint16_t credits) {
    auto iter = conn_hdl_to_cis_map_.find(handle);
    if (iter != conn_hdl_to_cis_map_.end()) {
      iter->second->used_credits -= credits;
      iso_credits_ += credits;
      return;
    }

    iter = conn_hdl_to_bis_map_.find(handle);
    if (iter != conn_hdl_to_bis_map_.end()) {
      iter->second->used_credits -= credits;
      iso_credits_ += credits;
    }
  }

  void process_create_big_cmpl_pkt(uint8_t len, uint8_t* data) {
    struct big_create_cmpl_evt evt;

    log::assert_that(len >= 18, "Invalid packet length: {}", len);
    log::assert_that(big_callbacks_ != nullptr, "Invalid BIG callbacks");

    STREAM_TO_UINT8(evt.status, data);
    STREAM_TO_UINT8(evt.big_id, data);
    STREAM_TO_UINT24(evt.big_sync_delay, data);
    STREAM_TO_UINT24(evt.transport_latency_big, data);
    STREAM_TO_UINT8(evt.phy, data);
    STREAM_TO_UINT8(evt.nse, data);
    STREAM_TO_UINT8(evt.bn, data);
    STREAM_TO_UINT8(evt.pto, data);
    STREAM_TO_UINT8(evt.irc, data);
    STREAM_TO_UINT16(evt.max_pdu, data);
    STREAM_TO_UINT16(evt.iso_interval, data);

    uint8_t num_bis;
    STREAM_TO_UINT8(num_bis, data);

    log::assert_that(num_bis != 0, "Bis count is 0");
    log::assert_that(len == (18 + num_bis * sizeof(uint16_t)),
                     "Invalid packet length: {}. Number of bis: {}", len, num_bis);

    for (auto i = 0; i < num_bis; ++i) {
      uint16_t conn_handle;
      STREAM_TO_UINT16(conn_handle, data);
      evt.conn_handles.push_back(conn_handle);
      log::info("received BIS conn_hdl {}", conn_handle);

      if (evt.status == HCI_SUCCESS) {
        auto bis = std::unique_ptr<iso_bis>(new iso_bis());
        bis->big_handle = evt.big_id;
        bis->sdu_itv = last_big_create_req_sdu_itv_;
        bis->sync_info = {.tx_seq_nb = 0, .rx_seq_nb = 0};
        bis->used_credits = 0;
        bis->state_flags = kStateFlagIsBroadcast;

        log::verbose("BIG_ID {}, bis_handle: {:#x}, flags: {:#x}, status {}", evt.big_id,
                     conn_handle, bis->state_flags,
                     hci_status_code_text((tHCI_STATUS)(evt.status)));

        conn_hdl_to_bis_map_[conn_handle] = std::move(bis);
      }
    }

    if (evt.status == HCI_SUCCESS) {
      big_callbacks_->OnBigEvent(kIsoEventBigOnCreateCmpl, &evt);
    } else {
      big_callbacks_->OnBigEvent(kIsoEventBigOnCreateFail, &evt);
    }

    {
      const std::lock_guard<std::mutex> lock(on_iso_traffic_active_callbacks_list_mutex_);
      for (auto callbacks : on_iso_traffic_active_callbacks_list_) {
        callbacks(true);
      }
    }
  }

  void process_terminate_big_cmpl_pkt(uint8_t len, uint8_t* data) {
    struct big_terminate_cmpl_evt evt;

    log::assert_that(len == 2, "Invalid packet length: {}", len);
    log::assert_that(big_callbacks_ != nullptr, "Invalid BIG callbacks");

    STREAM_TO_UINT8(evt.big_id, data);
    STREAM_TO_UINT8(evt.reason, data);

    bool is_known_handle = false;
    auto bis_it = conn_hdl_to_bis_map_.cbegin();
    while (bis_it != conn_hdl_to_bis_map_.cend()) {
      if (bis_it->second->big_handle == evt.big_id) {
        bis_it = conn_hdl_to_bis_map_.erase(bis_it);
        is_known_handle = true;
      } else {
        ++bis_it;
      }
    }

    log::assert_that(is_known_handle, "No such big: {}", evt.big_id);
    big_callbacks_->OnBigEvent(kIsoEventBigTerminated, &evt);

    {
      const std::lock_guard<std::mutex> lock(on_iso_traffic_active_callbacks_list_mutex_);
      for (auto callbacks : on_iso_traffic_active_callbacks_list_) {
        callbacks(false);
      }
    }
  }

  void create_big(uint8_t big_id, struct big_create_params big_params) {
    log::assert_that(!IsBigKnown(big_id), "Invalid big - already exists: {}", big_id);

    if (stack_config_get_interface()->get_pts_unencrypt_broadcast()) {
      log::info("Force create broadcst without encryption for PTS test");
      big_params.enc = 0;
      big_params.enc_code = {0};
    }

    // Apply default values from tBAP_BA_BIG_PARAMS if not set
    if (big_params.sdu_itv == 0) {
      big_params.sdu_itv = 10000;  // Default SDU interval
    }
    if (big_params.max_sdu_size == 0) {
      big_params.max_sdu_size = 100;  // Default max SDU size
    }
    if (big_params.max_transport_latency == 0) {
      big_params.max_transport_latency = 10;  // Default max transport latency
    }
    if (big_params.rtn == 0) {
      big_params.rtn = 2;  // Default RTN
    }
    if (big_params.phy == 0) {
      big_params.phy = 2;  // Default PHY (LE 2M)
    }
    if (big_params.packing == 0xFF) {  // Use 0xFF as uninitialized value
      big_params.packing = 1;  // Default packing (Interleaved)
    }
    if (big_params.framing == 0xFF) {  // Use 0xFF as uninitialized value
      big_params.framing = 0;  // Default framing (Unframed)
    }

    last_big_create_req_sdu_itv_ = big_params.sdu_itv;
    btsnd_hcic_create_big(big_id, big_params.adv_handle, big_params.num_bis, big_params.sdu_itv,
                          big_params.max_sdu_size, big_params.max_transport_latency, big_params.rtn,
                          big_params.phy, big_params.packing, big_params.framing, big_params.enc,
                          big_params.enc_code);
    
    BTM_LogHistory(kBtmLogTag, RawAddress::kEmpty, "BIG Create",
                   std::format("big_id:0x{:02x}, sdu_itv:{}, max_sdu:{}, latency:{}, rtn:{}, phy:{}, packing:{}, framing:{}",
                               big_id, big_params.sdu_itv, big_params.max_sdu_size, 
                               big_params.max_transport_latency, big_params.rtn, big_params.phy,
                               big_params.packing, big_params.framing));
  }

  void terminate_big(uint8_t big_id, uint8_t reason) {
    log::assert_that(IsBigKnown(big_id), "No such big: {}", big_id);

    btsnd_hcic_term_big(big_id, reason);
  }

  void big_create_sync(uint8_t big_handle, struct big_sync_params big_params) {
    log::assert_that(!IsBigKnown(big_handle), "Invalid big - already exists: {}", big_handle);

    btsnd_hcic_big_create_sync(big_handle, big_params.sync_handle, big_params.encryption,
                                big_params.broadcast_code, big_params.mse,
                                big_params.big_sync_timeout, big_params.bis);
  }

  void big_terminate_sync(uint8_t big_handle) {
    log::assert_that(IsBigKnown(big_handle), "No such big: {}", big_handle);

    btsnd_hcic_big_terminate_sync(
            big_handle, base::BindOnce(&iso_impl::on_big_terminate_sync_cmpl,
                                       weak_factory_.GetWeakPtr(), big_handle));
  }

  void on_big_terminate_sync_cmpl(uint8_t big_handle, uint8_t* stream, uint16_t len) {
    uint8_t status;
    big_terminate_sync_cmpl_evt evt;

    log::assert_that(len == 2, "Invalid packet length: {}", len);
    log::assert_that(big_sync_callbacks_ != nullptr, "Invalid BIG Sync callbacks");

    STREAM_TO_UINT8(status, stream);
    STREAM_TO_UINT8(big_handle, stream);

    evt.status = status;
    evt.big_handle = big_handle;

    if (status == HCI_SUCCESS) {
      // BIS will be removed when BIG Sync Lost event is received
      log::info("BIG terminate sync command successful for big_handle: {}", big_handle);
      bool is_known_handle = false;
      auto bis_it = conn_hdl_to_bis_map_.cbegin();
      while (bis_it != conn_hdl_to_bis_map_.cend()) {
        if (bis_it->second->big_handle == evt.big_handle) {
          log::info("Removing BIS handle {} for BIG Sync {}", bis_it->first, big_handle);
          bis_it = conn_hdl_to_bis_map_.erase(bis_it);
          is_known_handle = true;
        } else {
          ++bis_it;
        }
        log::assert_that(is_known_handle, "No such big sync handle: {}", big_handle);
      }
    } else {
      log::error("BIG terminate sync command failed, status: {}, big_handle: {}", status,
                 big_handle);
    }

    big_sync_callbacks_->OnBigSyncEvent(kIsoEventBigOnTerminateSyncCmpl, &evt);
  }

  void process_big_sync_established_pkt(uint8_t len, uint8_t* data) {
    struct big_sync_established_evt evt;

    log::assert_that(len >= 16, "Invalid packet length: {}", len);
    log::assert_that(big_sync_callbacks_ != nullptr, "Invalid BIG Sync callbacks");

    STREAM_TO_UINT8(evt.status, data);
    STREAM_TO_UINT8(evt.big_handle, data);
    STREAM_TO_UINT24(evt.transport_latency_big, data);
    STREAM_TO_UINT8(evt.nse, data);
    STREAM_TO_UINT8(evt.bn, data);
    STREAM_TO_UINT8(evt.pto, data);
    STREAM_TO_UINT8(evt.irc, data);
    STREAM_TO_UINT16(evt.max_pdu, data);
    STREAM_TO_UINT16(evt.iso_interval, data);

    uint8_t num_bis;
    STREAM_TO_UINT8(num_bis, data);

    log::assert_that(num_bis != 0, "Bis count is 0");
    log::assert_that(len == (14 + num_bis * sizeof(uint16_t)),
                     "Invalid packet length: {}. Number of bis: {}", len, num_bis);

    if (evt.status == HCI_SUCCESS) {
      for (auto i = 0; i < num_bis; ++i) {
        uint16_t conn_handle;
        STREAM_TO_UINT16(conn_handle, data);
        evt.conn_handles.push_back(conn_handle);
        log::info("Synced to BIS conn_hdl {}", conn_handle);

        auto bis = std::unique_ptr<iso_bis>(new iso_bis());
        bis->big_handle = evt.big_handle;
        bis->sdu_itv = 0;  // Will be updated from BASE if needed
        bis->sync_info = {.tx_seq_nb = 0, .rx_seq_nb = 0};
        bis->used_credits = 0;
        bis->state_flags = kStateFlagIsBroadcastSync;

        log::verbose("BIG_HANDLE {}, bis_handle: {:#x}, flags: {:#x}, status {}", evt.big_handle,
                     conn_handle, bis->state_flags,
                     hci_status_code_text((tHCI_STATUS)(evt.status)));

        conn_hdl_to_bis_map_[conn_handle] = std::move(bis);
      }
    }

    big_sync_callbacks_->OnBigSyncEvent(kIsoEventBigOnSyncEstablished, &evt);

    if (evt.status == HCI_SUCCESS) {
      const std::lock_guard<std::mutex> lock(on_iso_traffic_active_callbacks_list_mutex_);
      for (auto callbacks : on_iso_traffic_active_callbacks_list_) {
        callbacks(true);
      }
    }
  }

  void process_big_sync_lost_pkt(uint8_t len, uint8_t* data) {
    struct big_sync_lost_evt evt;

    log::assert_that(len == 2, "Invalid packet length: {}", len);
    log::assert_that(big_sync_callbacks_ != nullptr, "Invalid BIG Sync callbacks");

    STREAM_TO_UINT8(evt.big_handle, data);
    STREAM_TO_UINT8(evt.reason, data);

    bool is_known_handle = false;
    auto bis_it = conn_hdl_to_bis_map_.cbegin();
    while (bis_it != conn_hdl_to_bis_map_.cend()) {
      if (bis_it->second->big_handle == evt.big_handle) {
        log::info("Removing BIS handle {} for BIG {}", bis_it->first, evt.big_handle);
        bis_it = conn_hdl_to_bis_map_.erase(bis_it);
        is_known_handle = true;
      } else {
        ++bis_it;
      }
    }

    log::assert_that(is_known_handle, "No such big: {}", evt.big_handle);
    big_sync_callbacks_->OnBigSyncEvent(kIsoEventBigOnSyncLost, &evt);

    {
      const std::lock_guard<std::mutex> lock(on_iso_traffic_active_callbacks_list_mutex_);
      for (auto callbacks : on_iso_traffic_active_callbacks_list_) {
        callbacks(false);
      }
    }
  }

  void on_iso_event(uint8_t code, uint8_t* packet, uint16_t packet_len) {
    switch (code) {
      case HCI_BLE_CIS_EST_EVT:
        process_cis_est_pkt(packet_len, packet);
        break;
      case HCI_BLE_CREATE_BIG_CPL_EVT:
        process_create_big_cmpl_pkt(packet_len, packet);
        break;
      case HCI_BLE_TERM_BIG_CPL_EVT:
        process_terminate_big_cmpl_pkt(packet_len, packet);
        break;
      case HCI_BLE_CIS_REQ_EVT:
        /* Not supported */
        break;
      case HCI_BLE_BIG_SYNC_EST_EVT:
        process_big_sync_established_pkt(packet_len, packet);
        break;
      case HCI_BLE_BIG_SYNC_LOST_EVT:
        process_big_sync_lost_pkt(packet_len, packet);
        break;
      default:
        log::error("Unhandled event code {}", code);
    }
  }

  void on_vs_codec_settings_event(uint8_t mode,
      uint16_t delay, uint64_t bdAddr) {
    if (vsc_callback_ == nullptr) return;
    vsc_callback_->OnVscEvent(delay, mode, bdAddr);
  }

  void on_set_dbig_parameters_cmd_complete(uint8_t* stream, uint16_t len) {
    // Some implementations return (status, sub_opcode, dbig_handle). Keep it flexible.
    log::assert_that(len >= 2, "Invalid DBIG cmd complete length: {}", len);

    uint8_t status = 0xFF;
    uint8_t sub_opcode = 0xFF;
    uint8_t dbig_handle = 0xFF;

    STREAM_TO_UINT8(status, stream);
    STREAM_TO_UINT8(sub_opcode, stream);
    if (len >= 3) {
      STREAM_TO_UINT8(dbig_handle, stream);
    }

    BTM_LogHistory(
            kBtmLogTag, RawAddress::kEmpty, "DBIG Params complete",
            std::format("status:{}, sub_opcode:0x{:02x}, dbig_handle:0x{:02x}",
                        hci_status_code_text((tHCI_STATUS)(status)), sub_opcode, dbig_handle));

    // Forward cmd-complete as an ISO Manager DBIG event (similar in spirit to BIG create cmpl)
    if (dbig_callbacks_ != nullptr) {
      dbig_create_cmpl_evt evt = {
              .status = status,
              .sub_opcode = sub_opcode,
              .dbig_handle = dbig_handle,
      };
      dbig_callbacks_->OnDbigEvent(kIsoEventDbigCreateCmpl, &evt);
    }
  }

  void set_dbig_parameters(struct dbig_create_params dbig_params) {
    // Gate DBIG HCI command based on duplex property.
    // If duplex is disabled, we treat it as "DBIG configured" and immediately continue.
    const bool is_duplex =
            osi_property_get_bool("persist.vendor.service.bt.dbig.duplex", false);
    if (!is_duplex) {
      log::info("DBIG duplex disabled; skipping SetDbigParameters. dbig_handle=0x{:02x}",
                dbig_params.dbig_handle);
      return;
    }

    // HCI VS cmd: HCI_VS_LE_SET_DBIG_PARAMETERS
    // NOTE: btsnd_hcic_ble_create_dbig takes a Repeating Callback signature.
    btsnd_hcic_ble_create_dbig(
            dbig_params.dbig_handle, dbig_params.dbig_feature_set, 
            dbig_params.bis_detection_attempts, dbig_params.max_payload_dbig_control,
            dbig_params.bis_control_event_interval, dbig_params.send_exit,
            dbig_params.pgp_timeout, dbig_params.pgo_timeout, 
            dbig_params.sgo_timeout, dbig_params.tx_power,
            base::BindRepeating(&iso_impl::on_set_dbig_parameters_cmd_complete,
                                weak_factory_.GetWeakPtr()));

    BTM_LogHistory(kBtmLogTag, RawAddress::kEmpty, "DBIG Params set",
                   std::format("dbig_handle:0x{:02x}, feature_set:{}, detection_attempts:{}, max_payload:{}, "
                               "control_interval:{}, send_exit:{}, pgp_timeout:{}, pgo_timeout:{}, sgo_timeout:{}, tx_power:{}",
                               dbig_params.dbig_handle, dbig_params.dbig_feature_set, 
                               dbig_params.bis_detection_attempts, dbig_params.max_payload_dbig_control,
                               dbig_params.bis_control_event_interval, dbig_params.send_exit,
                               dbig_params.pgp_timeout, dbig_params.pgo_timeout, 
                               dbig_params.sgo_timeout, dbig_params.tx_power));
  }

  void on_dbig_update_event(uint8_t* stream, uint16_t len) {
    dbig_update_evt evt;

    log::assert_that(dbig_callbacks_ != nullptr, "Invalid DBIG callbacks");
    log::assert_that(len >= 5, "Invalid DBIG packet length: {}", len);

    STREAM_TO_UINT8(evt.status, stream);
    STREAM_TO_UINT8(evt.big_handle, stream);
    STREAM_TO_UINT8(evt.bis_state, stream);
    STREAM_TO_UINT8(evt.timing_source, stream);
    STREAM_TO_UINT8(evt.local_bis_id, stream);

    BTM_LogHistory(kBtmLogTag, RawAddress::kEmpty, "DBIG Update event",
                   std::format("big_handle:0x{:02x}, status:{}, bis_state:{}, timing_source:{}, local_bis_id:{}",
                               evt.big_handle, evt.status, evt.bis_state, evt.timing_source, evt.local_bis_id));

    dbig_callbacks_->OnDbigEvent(kIsoEventDbigUpdate, &evt);
  }

  void handle_iso_data(BT_HDR* p_msg) {
    const uint8_t* stream = p_msg->data;
    uint16_t handle, seq_nb;

    if (p_msg->len <= ((p_msg->layer_specific & BT_ISO_HDR_CONTAINS_TS) ? kIsoHeaderWithTsLen
                                                                        : kIsoHeaderWithoutTsLen)) {
      return;
    }

    STREAM_TO_UINT16(handle, stream);
    uint16_t iso_handle = HCID_GET_HANDLE(handle);

    iso_base* iso = GetIsoIfKnown(iso_handle);
    if (iso == nullptr) {
      log::error("Received data for non-registered ISO handle: 0x{:04x}", iso_handle);
      return;
    }

    // Check if this is BIS or CIS
    if (iso->state_flags & kStateFlagIsBroadcastSync) {
      // Handle BIS data for sink (synced BIG)
      bis_data_evt evt;
      evt.bis_conn_hdl = iso_handle;
      evt.big_handle = iso->big_handle;

      STREAM_SKIP_UINT16(stream);
      if (p_msg->layer_specific & BT_ISO_HDR_CONTAINS_TS) {
        STREAM_TO_UINT32(evt.ts, stream);
      } else {
        evt.ts = 0;
      }

      STREAM_TO_UINT16(seq_nb, stream);

      uint16_t expected_seq_nb = iso->sync_info.rx_seq_nb;
      iso->sync_info.rx_seq_nb = (seq_nb + 1) & 0xffff;

      evt.evt_lost = ((1 << 16) + seq_nb - expected_seq_nb) & 0xffff;
      if (evt.evt_lost > 0) {
        iso->evt_stats.evt_lost_count += evt.evt_lost;
        iso->evt_stats.evt_last_lost_us = bluetooth::common::time_get_os_boottime_us();

        log::warn("{} BIS packets lost.", evt.evt_lost);
        iso->evt_stats.seq_nb_mismatch_count++;
      }

      evt.p_msg = p_msg;
      evt.seq_nb = seq_nb;

      // BIS data is only received for sink (synced BIG), not for source (created BIG)
      log::assert_that(big_sync_callbacks_ != nullptr, "Invalid BIG Sync callbacks");
      big_sync_callbacks_->OnBisEvent(kIsoEventBisDataAvailable, &evt);
      return;
    }

    // Handle CIS data
    log::assert_that(cig_callbacks_ != nullptr, "Invalid CIG callbacks");

    cis_data_evt evt;
    evt.cis_conn_hdl = iso_handle;

    STREAM_SKIP_UINT16(stream);
    if (p_msg->layer_specific & BT_ISO_HDR_CONTAINS_TS) {
      STREAM_TO_UINT32(evt.ts, stream);
    } else {
      evt.ts = 0;
    }

    STREAM_TO_UINT16(seq_nb, stream);

    uint16_t expected_seq_nb = iso->sync_info.rx_seq_nb;
    iso->sync_info.rx_seq_nb = (seq_nb + 1) & 0xffff;

    evt.evt_lost = ((1 << 16) + seq_nb - expected_seq_nb) & 0xffff;
    if (evt.evt_lost > 0) {
      iso->evt_stats.evt_lost_count += evt.evt_lost;
      iso->evt_stats.evt_last_lost_us = bluetooth::common::time_get_os_boottime_us();

      log::warn("{} packets lost.", evt.evt_lost);
      iso->evt_stats.seq_nb_mismatch_count++;
    }

    evt.p_msg = p_msg;
    evt.cig_id = iso->cig_id;
    evt.seq_nb = seq_nb;
    cig_callbacks_->OnCisEvent(kIsoEventCisDataAvailable, &evt);
  }

  iso_cis* GetCisIfKnown(uint16_t cis_conn_handle) {
    auto cis_it = conn_hdl_to_cis_map_.find(cis_conn_handle);
    return (cis_it != conn_hdl_to_cis_map_.end()) ? cis_it->second.get() : nullptr;
  }

  iso_bis* GetBisIfKnown(uint16_t bis_conn_handle) {
    auto bis_it = conn_hdl_to_bis_map_.find(bis_conn_handle);
    return (bis_it != conn_hdl_to_bis_map_.end()) ? bis_it->second.get() : nullptr;
  }

  iso_base* GetIsoIfKnown(uint16_t iso_handle) {
    struct iso_base* iso = GetCisIfKnown(iso_handle);
    return (iso != nullptr) ? iso : GetBisIfKnown(iso_handle);
  }

  bool IsCigKnown(uint8_t cig_id) const {
    auto const cis_it =
            std::find_if(conn_hdl_to_cis_map_.cbegin(), conn_hdl_to_cis_map_.cend(),
                         [&cig_id](auto& kv_pair) { return kv_pair.second->cig_id == cig_id; });
    return cis_it != conn_hdl_to_cis_map_.cend();
  }

  bool IsBigKnown(uint8_t big_id) const {
    auto bis_it =
            std::find_if(conn_hdl_to_bis_map_.cbegin(), conn_hdl_to_bis_map_.cend(),
                         [&big_id](auto& kv_pair) { return kv_pair.second->big_handle == big_id; });
    return bis_it != conn_hdl_to_bis_map_.cend();
  }

  static void dump_credits_stats(int fd, const iso_base::credits_stats& stats) {
    uint64_t now_us = bluetooth::common::time_get_os_boottime_us();

    dprintf(fd, "        Credits Stats:\n");
    dprintf(fd, "          Credits underflow (count): %zu\n", stats.credits_underflow_count);
    dprintf(fd, "          Credits underflow (bytes): %zu\n", stats.credits_underflow_bytes);
    dprintf(fd, "          Last underflow time ago (ms): %llu\n",
            (stats.credits_last_underflow_us > 0
                     ? (unsigned long long)(now_us - stats.credits_last_underflow_us) / 1000
                     : 0llu));
  }

  static void dump_event_stats(int fd, const iso_base::event_stats& stats) {
    uint64_t now_us = bluetooth::common::time_get_os_boottime_us();

    dprintf(fd, "        Event Stats:\n");
    dprintf(fd, "          Sequence number mismatch (count): %zu\n", stats.seq_nb_mismatch_count);
    dprintf(fd, "          Event lost (count): %zu\n", stats.evt_lost_count);
    dprintf(fd, "          Last event lost time ago (ms): %llu\n",
            (stats.evt_last_lost_us > 0
                     ? (unsigned long long)(now_us - stats.evt_last_lost_us) / 1000
                     : 0llu));
  }

  void dump(int fd) const {
    dprintf(fd, "  ----------------\n ");
    dprintf(fd, "  ISO Manager:\n");
    dprintf(fd, "    Available credits: %d\n", iso_credits_.load());
    dprintf(fd, "    Controller buffer size: %d\n", iso_buffer_size_);
    dprintf(fd, "    Num of ISO traffic callbacks: %lu\n",
            static_cast<unsigned long>(on_iso_traffic_active_callbacks_list_.size()));
    dprintf(fd, "    CISes:\n");
    for (auto const& cis_pair : conn_hdl_to_cis_map_) {
      dprintf(fd, "      CIS Connection handle: %d\n", cis_pair.first);
      dprintf(fd, "        CIG ID: %d\n", cis_pair.second->cig_id);
      dprintf(fd, "        Used Credits: %d\n", cis_pair.second->used_credits.load());
      dprintf(fd, "        SDU Interval: %d\n", cis_pair.second->sdu_itv);
      dprintf(fd, "        State Flags: 0x%02hx\n", cis_pair.second->state_flags.load());
      dump_credits_stats(fd, cis_pair.second->cr_stats);
      dump_event_stats(fd, cis_pair.second->evt_stats);
    }
    dprintf(fd, "    BISes:\n");
    for (auto const& cis_pair : conn_hdl_to_bis_map_) {
      dprintf(fd, "      BIS Connection handle: %d\n", cis_pair.first);
      dprintf(fd, "        BIG Handle: %d\n", cis_pair.second->big_handle);
      dprintf(fd, "        Used Credits: %d\n", cis_pair.second->used_credits.load());
      dprintf(fd, "        SDU Interval: %d\n", cis_pair.second->sdu_itv);
      dprintf(fd, "        State Flags: 0x%02hx\n", cis_pair.second->state_flags.load());
      dump_credits_stats(fd, cis_pair.second->cr_stats);
      dump_event_stats(fd, cis_pair.second->evt_stats);
    }
    dprintf(fd, "  ----------------\n ");
  }

  std::map<uint16_t, std::unique_ptr<iso_cis>> conn_hdl_to_cis_map_;
  std::map<uint16_t, std::unique_ptr<iso_bis>> conn_hdl_to_bis_map_;
  std::map<uint16_t, RawAddress> cis_hdl_to_addr;

  std::atomic_uint16_t iso_credits_;
  uint16_t iso_buffer_size_;
  uint32_t last_big_create_req_sdu_itv_;

  CigCallbacks* cig_callbacks_ = nullptr;
  VscCallback* vsc_callback_ = nullptr;
  BigCallbacks* big_callbacks_ = nullptr;
  DbigCallbacks* dbig_callbacks_ = nullptr;

  BigSyncCallbacks* big_sync_callbacks_ = nullptr;
  std::mutex on_iso_traffic_active_callbacks_list_mutex_;
  std::list<void (*)(bool)> on_iso_traffic_active_callbacks_list_;
  base::WeakPtrFactory<iso_impl> weak_factory_{this};
};

}  // namespace iso_manager
}  // namespace hci
}  // namespace bluetooth
