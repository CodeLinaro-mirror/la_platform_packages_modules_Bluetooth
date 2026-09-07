/*
 * Copyright (C) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

#include "common/message_loop_thread.h"

namespace bluetooth {
namespace audio {
namespace qti_hidl {
namespace le_audio_broadcast {

bool is_duplex_broadcast_enabled();
bool is_hal_enabled();

bool init(bluetooth::common::MessageLoopThread* message_loop);
void cleanup();

bool setup_codec(uint32_t sample_rate, uint8_t bits_per_sample, uint8_t channel_count,
                 uint32_t data_interval_us, bool is_encoder);

// Polling-gate session control.
// Source (encoder, is_encoder=true) calls start_session first.
// Sink   (decoder, is_encoder=false) calls start_session second.
// HAL StartSession is issued only when BOTH have called start_session.
// HAL EndSession is issued when BOTH have called stop_session.
bool start_session(bool is_encoder);
bool stop_session(bool is_encoder);
void end_session();

// Direction-aware stream acknowledgments.
// is_encoder=true  -> encoder/source (TX)
// is_encoder=false -> decoder/sink   (RX)
void confirm_streaming_request(bool is_encoder);
void confirm_suspend_request(bool is_encoder);
void cancel_streaming_request(bool is_encoder);

size_t read(uint8_t* p_buf, uint32_t len);
size_t write(const uint8_t* p_buf, uint32_t len);

void set_remote_delay(uint16_t delay_report_ms);

// Separate callback registration for source (encoder/TX) and sink (decoder/RX).
void register_source_callbacks(std::function<bool(bool)> on_resume,
                               std::function<bool()> on_suspend,
                               std::function<bool()> on_stop);

void register_sink_callbacks(std::function<bool(bool)> on_resume,
                             std::function<bool()> on_suspend,
                             std::function<bool()> on_stop);

}  // namespace le_audio_broadcast
}  // namespace qti_hidl
}  // namespace audio
}  // namespace bluetooth
