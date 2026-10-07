/*
 * Copyright 2022 The Android Open Source Project
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

#include "stack/include/a2dp_vendor_ldac.h"

#include <gtest/gtest.h>

#include <tuple>

#include "common/time_util.h"
#include "osi/include/allocator.h"
#include "stack/include/a2dp_vendor_ldac_constants.h"
#include "stack/include/avdt_api.h"
#include "stack/include/bt_hdr.h"
#include "test_util.h"
#include "wav_reader.h"

namespace {
constexpr uint32_t kA2dpTickUs = 23 * 1000;
constexpr char kWavFile[] = "test/a2dp/raw_data/pcm1644s.wav";
constexpr uint8_t kCodecInfoLdacCapability[AVDT_CODEC_SIZE] = {
        A2DP_LDAC_CODEC_LEN,
        AVDT_MEDIA_TYPE_AUDIO,
        A2DP_MEDIA_CT_NON_A2DP,
        0x2D,  // A2DP_LDAC_VENDOR_ID
        0x01,  // A2DP_LDAC_VENDOR_ID
        0x00,  // A2DP_LDAC_VENDOR_ID
        0x00,  // A2DP_LDAC_VENDOR_ID
        0xAA,  // A2DP_LDAC_CODEC_ID
        0x00,  // A2DP_LDAC_CODEC_ID,
        A2DP_LDAC_SAMPLING_FREQ_44100,
        A2DP_LDAC_CHANNEL_MODE_STEREO,
};
uint8_t* Data(BT_HDR* packet) { return packet->data + packet->offset; }
}  // namespace
namespace bluetooth {
namespace testing {

// static BT_HDR* packet = nullptr;
static WavReader wav_reader = WavReader(GetWavFilePath(kWavFile).c_str());

class A2dpLdacTest : public ::testing::Test {
protected:
  void SetUp() override {
    SetCodecConfig();
    encoder_iface_ = const_cast<tA2DP_ENCODER_INTERFACE*>(
            A2DP_VendorGetEncoderInterfaceLdac(kCodecInfoLdacCapability));
    ASSERT_NE(encoder_iface_, nullptr);
  }

  void TearDown() override {
    if (a2dp_codecs_ != nullptr) {
      delete a2dp_codecs_;
    }
    if (encoder_iface_ != nullptr) {
      encoder_iface_->encoder_cleanup();
    }
  }

  // NOTE: Make a super func for all codecs
  void SetCodecConfig() {
    uint8_t source_codec_info_result[AVDT_CODEC_SIZE];
    a2dp_codecs_ = new A2dpCodecs(std::vector<btav_a2dp_codec_config_t>());

    ASSERT_TRUE(a2dp_codecs_->init());

    source_codec_config_ = a2dp_codecs_->findSourceCodecConfig(kCodecInfoLdacCapability);
    ASSERT_NE(source_codec_config_, nullptr);
    ASSERT_TRUE(a2dp_codecs_->setCodecConfig(kCodecInfoLdacCapability, true,
                                             source_codec_info_result, true));
    ASSERT_EQ(a2dp_codecs_->getCurrentCodecConfig(), source_codec_config_);
    // Compare the result codec with the local test codec info
    for (size_t i = 0; i < kCodecInfoLdacCapability[0] + 1; i++) {
      ASSERT_EQ(source_codec_info_result[i], kCodecInfoLdacCapability[i]);
    }
    ASSERT_NE(source_codec_config_->getAudioBitsPerSample(), 0);
  }

  void InitializeEncoder(a2dp_source_read_callback_t read_cb,
                         a2dp_source_enqueue_callback_t enqueue_cb) {
    tA2DP_ENCODER_INIT_PEER_PARAMS peer_params = {true, true, 1000};
    encoder_iface_->encoder_init(&peer_params, source_codec_config_, read_cb, enqueue_cb);
  }

  BT_HDR* AllocateL2capPacket(const std::vector<uint8_t> data) const {
    auto packet = AllocatePacket(data.size());
    std::copy(data.cbegin(), data.cend(), Data(packet));
    return packet;
  }

  BT_HDR* AllocatePacket(size_t packet_length) const {
    BT_HDR* packet = static_cast<BT_HDR*>(osi_calloc(sizeof(BT_HDR) + packet_length));
    packet->len = packet_length;
    return packet;
  }
  A2dpCodecConfig* source_codec_config_;
  A2dpCodecs* a2dp_codecs_;
  tA2DP_ENCODER_INTERFACE* encoder_iface_;
};

TEST_F(A2dpLdacTest, a2dp_source_read_underflow) {
  static int enqueue_cb_invoked = 0;

  auto read_cb = +[](uint8_t* /*p_buf*/, uint32_t /*len*/) -> uint32_t { return 0; };

  auto enqueue_cb = +[](BT_HDR* /*p_buf*/, size_t /*frames_n*/, uint32_t /*len*/) -> bool {
    enqueue_cb_invoked += 1;
    return false;
  };

  InitializeEncoder(read_cb, enqueue_cb);
  uint64_t timestamp_us = bluetooth::common::time_gettimeofday_us();
  encoder_iface_->send_frames(timestamp_us);
  encoder_iface_->send_frames(timestamp_us + kA2dpTickUs);

  ASSERT_EQ(enqueue_cb_invoked, 0);
}

// LDAC Dev-UI quality modes (btav_a2dp_codec_config_t::codec_specific_1).
constexpr int64_t kLdacQualityHigh = 1000;
constexpr int64_t kLdacQualityMid = 1001;
constexpr int64_t kLdacQualityLow = 1002;
constexpr int64_t kLdacQualityAbr = 1003;

// Constant-bitrate modes: min == max, sample-rate dependent value.
TEST(A2dpLdacBitRateRangeTest, ConstantBitrateHighQuality) {
  // 44100 / 88200 select the lower ceiling; any other rate uses the higher one.
  EXPECT_EQ(A2DP_VendorGetBitRateRangeLdac(kLdacQualityHigh, 44100).min_bitrate, 909000);
  EXPECT_EQ(A2DP_VendorGetBitRateRangeLdac(kLdacQualityHigh, 44100).max_bitrate, 909000);
  EXPECT_EQ(A2DP_VendorGetBitRateRangeLdac(kLdacQualityHigh, 88200).max_bitrate, 909000);
  EXPECT_EQ(A2DP_VendorGetBitRateRangeLdac(kLdacQualityHigh, 48000).min_bitrate, 990000);
  EXPECT_EQ(A2DP_VendorGetBitRateRangeLdac(kLdacQualityHigh, 48000).max_bitrate, 990000);
  EXPECT_EQ(A2DP_VendorGetBitRateRangeLdac(kLdacQualityHigh, 96000).max_bitrate, 990000);
}

TEST(A2dpLdacBitRateRangeTest, ConstantBitrateMidQuality) {
  EXPECT_EQ(A2DP_VendorGetBitRateRangeLdac(kLdacQualityMid, 44100).min_bitrate, 606000);
  EXPECT_EQ(A2DP_VendorGetBitRateRangeLdac(kLdacQualityMid, 44100).max_bitrate, 606000);
  EXPECT_EQ(A2DP_VendorGetBitRateRangeLdac(kLdacQualityMid, 88200).max_bitrate, 606000);
  EXPECT_EQ(A2DP_VendorGetBitRateRangeLdac(kLdacQualityMid, 48000).min_bitrate, 660000);
  EXPECT_EQ(A2DP_VendorGetBitRateRangeLdac(kLdacQualityMid, 48000).max_bitrate, 660000);
}

TEST(A2dpLdacBitRateRangeTest, ConstantBitrateLowQuality) {
  EXPECT_EQ(A2DP_VendorGetBitRateRangeLdac(kLdacQualityLow, 44100).min_bitrate, 303000);
  EXPECT_EQ(A2DP_VendorGetBitRateRangeLdac(kLdacQualityLow, 44100).max_bitrate, 303000);
  EXPECT_EQ(A2DP_VendorGetBitRateRangeLdac(kLdacQualityLow, 88200).max_bitrate, 303000);
  EXPECT_EQ(A2DP_VendorGetBitRateRangeLdac(kLdacQualityLow, 48000).min_bitrate, 330000);
  EXPECT_EQ(A2DP_VendorGetBitRateRangeLdac(kLdacQualityLow, 48000).max_bitrate, 330000);
}

// For every constant-bitrate mode the AIDL contract requires min == max.
class A2dpLdacCbrMinEqualsMaxTest : public ::testing::TestWithParam<std::tuple<int64_t, int>> {};

TEST_P(A2dpLdacCbrMinEqualsMaxTest, ConstantBitrateMinEqualsMax) {
  auto [mode, rate] = GetParam();
  auto range = A2DP_VendorGetBitRateRangeLdac(mode, rate);
  EXPECT_EQ(range.min_bitrate, range.max_bitrate);
}

INSTANTIATE_TEST_SUITE_P(A2dpLdacBitRateRangeTest, A2dpLdacCbrMinEqualsMaxTest,
                         ::testing::Combine(::testing::Values(kLdacQualityHigh, kLdacQualityMid,
                                                              kLdacQualityLow),
                                            ::testing::Values(44100, 48000, 88200, 96000)));

// ABR: the stream bitrate varies between the LOW and HIGH bounds. The AIDL
// contract requires 0 < minBitrate <= maxBitrate for ABR.
class A2dpLdacAbrSampleRateTest : public ::testing::TestWithParam<int> {};

TEST_P(A2dpLdacAbrSampleRateTest, AbrRateAwareRange) {
  int rate = GetParam();
  auto range = A2DP_VendorGetBitRateRangeLdac(kLdacQualityAbr, rate);
  EXPECT_GT(range.min_bitrate, 0);
  EXPECT_LE(range.min_bitrate, range.max_bitrate);
}

INSTANTIATE_TEST_SUITE_P(A2dpLdacBitRateRangeTest, A2dpLdacAbrSampleRateTest,
                         ::testing::Values(44100, 48000, 88200, 96000));

TEST(A2dpLdacBitRateRangeTest, AbrRateAwareRangeExactValues) {
  auto low_rate = A2DP_VendorGetBitRateRangeLdac(kLdacQualityAbr, 44100);
  EXPECT_EQ(low_rate.min_bitrate, 303000);
  EXPECT_EQ(low_rate.max_bitrate, 909000);
  EXPECT_EQ(A2DP_VendorGetBitRateRangeLdac(kLdacQualityAbr, 88200).min_bitrate, 303000);
  EXPECT_EQ(A2DP_VendorGetBitRateRangeLdac(kLdacQualityAbr, 88200).max_bitrate, 909000);

  auto high_rate = A2DP_VendorGetBitRateRangeLdac(kLdacQualityAbr, 48000);
  EXPECT_EQ(high_rate.min_bitrate, 330000);
  EXPECT_EQ(high_rate.max_bitrate, 990000);
  EXPECT_EQ(A2DP_VendorGetBitRateRangeLdac(kLdacQualityAbr, 96000).min_bitrate, 330000);
  EXPECT_EQ(A2DP_VendorGetBitRateRangeLdac(kLdacQualityAbr, 96000).max_bitrate, 990000);
}

// codec_specific_1 == 0 means the quality mode is unset ("don't care"). The
// encoder falls back to a system-property default in that case (see
// a2dp_vendor_ldac_encoder.cc's `codec_specific_1 != 0` guard), so the helper
// must not treat 0 as "mode 0 == HIGH" via the %10 mapping below; it returns
// the undefined {0, 0} range so the audio HAL is not given a bitrate hint.
TEST(A2dpLdacBitRateRangeTest, UnsetModeReturnsUndefinedRange) {
  auto range = A2DP_VendorGetBitRateRangeLdac(0, 44100);
  EXPECT_EQ(range.min_bitrate, 0);
  EXPECT_EQ(range.max_bitrate, 0);
}

// codec_specific_1 values outside 100x (but nonzero) still resolve through the
// low decimal digit, matching the encoder's `codec_specific_1 % 10` mapping.
// Digits 0/1/2/3 alias the HIGH/MID/LOW/ABR modes; other digits fall through
// to the ABR range (the switch default).
class A2dpLdacUnmappedModeTest : public ::testing::TestWithParam<int64_t> {};

TEST_P(A2dpLdacUnmappedModeTest, NonzeroUnmappedModeUsesAbrRange) {
  int64_t mode = GetParam();
  auto range = A2DP_VendorGetBitRateRangeLdac(mode, 44100);
  EXPECT_EQ(range.min_bitrate, 303000);
  EXPECT_EQ(range.max_bitrate, 909000);
}

INSTANTIATE_TEST_SUITE_P(A2dpLdacBitRateRangeTest, A2dpLdacUnmappedModeTest,
                         ::testing::Values(int64_t{55}, int64_t{9999}));

}  // namespace testing
}  // namespace bluetooth
