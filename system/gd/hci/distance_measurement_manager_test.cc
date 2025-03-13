/*
 * Copyright 2025 The Android Open Source Project
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

#include "hci/distance_measurement_manager.h"

#include <bluetooth/log.h>
#include <flag_macros.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "hal/ranging_hal.h"
#include "hal/ranging_hal_mock.h"
#include "hci/acl_manager_mock.h"
#include "hci/address.h"
#include "hci/controller.h"
#include "hci/controller_mock.h"
#include "hci/distance_measurement_manager_mock.h"
#include "hci/hci_layer.h"
#include "hci/hci_layer_fake.h"
#include "module.h"
#include "packet/packet_view.h"
#include "ras/ras_packets.h"

using testing::AtLeast;
using testing::Return;

namespace {
constexpr auto kTimeout = std::chrono::seconds(1);
}

namespace bluetooth {
namespace hci {
namespace {
class TestController : public testing::MockController {
protected:
  void Start() override {}
  void Stop() override {}
  void ListDependencies(ModuleList* /* list */) const override {}
};

class TestAclManager : public testing::MockAclManager {
protected:
  void Start() override {}
  void Stop() override {}
  void ListDependencies(ModuleList* /* list */) const override {}
};

struct CsReadCapabilitiesCompleteEvent {
  ErrorCode error_code = ErrorCode::SUCCESS;
  uint8_t num_config_supported = 4;
  uint16_t max_consecutive_procedures_supported = 0;
  uint8_t num_antennas_supported = 2;
  uint8_t max_antenna_paths_supported = 4;
  CsRoleSupported roles_supported = {/*initiator=*/1, /*reflector=*/1};
  CsOptionalModesSupported modes_supported = {/*mode_3=*/1};
  CsRttCapability rtt_capability = {/*rtt_aa_only_n=*/1, /*rtt_sounding_n=*/1,
                                    /*rtt_random_payload_n=*/1};
  uint8_t rtt_aa_only_n = 1;
  uint8_t rtt_sounding_n = 1;
  uint8_t rtt_random_payload_n = 1;
  CsOptionalNadmSoundingCapability nadm_sounding_capability = {
          /*normalized_attack_detector_metric=*/1};
  CsOptionalNadmRandomCapability nadm_random_capability = {/*normalized_attack_detector_metric=*/1};
  CsOptionalCsSyncPhysSupported cs_sync_phys_supported = {/*le_2m_phy=*/1};
  CsOptionalSubfeaturesSupported subfeatures_supported = {/*no_frequency_actuation_error=*/1,
                                                          /*channel_selection_algorithm=*/1,
                                                          /*phase_based_ranging=*/1};
  CsOptionalTIp1TimesSupported t_ip1_times_supported = {
          /*support_10_microsecond=*/1, /*support_20_microsecond=*/1,
          /*support_30_microsecond=*/1, /*support_40_microsecond=*/1,
          /*support_50_microsecond=*/1, /*support_60_microsecond=*/1,
          /*support_80_microsecond=*/1};
  CsOptionalTIp2TimesSupported t_ip2_times_supported = {
          /*support_10_microsecond=*/1, /*support_20_microsecond=*/1,
          /*support_30_microsecond=*/1,
          /*support_40_microsecond=*/1, /*support_50_microsecond=*/1,
          /*support_60_microsecond=*/1, /*support_80_microsecond=*/1};
  CsOptionalTFcsTimesSupported t_fcs_times_supported = {
          /*support_15_microsecond=*/1,  /*support_20_microsecond=*/1,
          /*support_30_microsecond=*/1,  /*support_40_microsecond=*/1,
          /*support_50_microsecond=*/1,
          /*support_60_microsecond=*/1,  /*support_80_microsecond=*/1,
          /*support_100_microsecond=*/1,
          /*support_120_microsecond=*/1};
  CsOptionalTPmTimesSupported t_pm_times_supported = {/*support_10_microsecond=*/1,
                                                      /*support_20_microsecond=*/1};
  uint8_t t_sw_time_supported = 1;
  uint8_t tx_snr_capability = 1;
};

struct StartMeasurementParameters {
  Address remote_address = Address::FromString("12:34:56:78:9a:bc").value();
  uint16_t connection_handle = 64;
  Role local_hci_role = Role::CENTRAL;
  uint16_t interval = 200;  // 200ms
  DistanceMeasurementMethod method = DistanceMeasurementMethod::METHOD_CS;
};

class DistanceMeasurementManagerTest : public ::testing::Test {
protected:
  void SetUp() override {
    test_hci_layer_ = new HciLayerFake;                    // Ownership is transferred to registry
    mock_controller_ = new TestController;                 // Ownership is transferred to registry
    mock_ranging_hal_ = new hal::testing::MockRangingHal;  // Ownership is transferred to registry
    mock_acl_manager_ = new TestAclManager;                // Ownership is transferred to registry
    fake_registry_.InjectTestModule(&hal::RangingHal::Factory, mock_ranging_hal_);
    fake_registry_.InjectTestModule(&Controller::Factory, mock_controller_);
    fake_registry_.InjectTestModule(&HciLayer::Factory, test_hci_layer_);
    fake_registry_.InjectTestModule(&AclManager::Factory, mock_acl_manager_);

    client_handler_ = fake_registry_.GetTestModuleHandler(&HciLayer::Factory);
    ASSERT_NE(client_handler_, nullptr);

    EXPECT_CALL(*mock_controller_, SupportsBleChannelSounding()).WillOnce(Return(true));
    EXPECT_CALL(*mock_ranging_hal_, IsBound()).Times(AtLeast(1)).WillRepeatedly(Return(true));

    handler_ = fake_registry_.GetTestHandler();
    dm_manager_ = fake_registry_.Start<DistanceMeasurementManager>(&thread_, handler_);

    dm_manager_->RegisterDistanceMeasurementCallbacks(&mock_dm_callbacks_);
  }

  void TearDown() override {
    fake_registry_.SynchronizeModuleHandler(&DistanceMeasurementManager::Factory,
                                            std::chrono::milliseconds(20));
    fake_registry_.StopAll();
  }

  std::future<void> GetDmSessionFuture() {
    log::assert_that(dm_session_promise_ == nullptr, "Promises promises ... Only one at a time");
    dm_session_promise_ = std::make_unique<std::promise<void>>();
    return dm_session_promise_->get_future();
  }

  void sync_client_handler() {
    log::assert_that(thread_.GetReactor()->WaitForIdle(kTimeout),
                     "assert failed: thread_.GetReactor()->WaitForIdle(kTimeout)");
  }

  static std::unique_ptr<LeCsReadLocalSupportedCapabilitiesCompleteBuilder>
  GetLocalSupportedCapabilitiesCompleteEvent(
          const CsReadCapabilitiesCompleteEvent& cs_cap_complete_event) {
    return LeCsReadLocalSupportedCapabilitiesCompleteBuilder::Create(
            /*num_hci_command_packets=*/0xFF, cs_cap_complete_event.error_code,
            cs_cap_complete_event.num_config_supported,
            cs_cap_complete_event.max_consecutive_procedures_supported,
            cs_cap_complete_event.num_antennas_supported,
            cs_cap_complete_event.max_antenna_paths_supported,
            cs_cap_complete_event.roles_supported, cs_cap_complete_event.modes_supported,
            cs_cap_complete_event.rtt_capability, cs_cap_complete_event.rtt_aa_only_n,
            cs_cap_complete_event.rtt_sounding_n, cs_cap_complete_event.rtt_random_payload_n,
            cs_cap_complete_event.nadm_sounding_capability,
            cs_cap_complete_event.nadm_random_capability,
            cs_cap_complete_event.cs_sync_phys_supported,
            cs_cap_complete_event.subfeatures_supported,
            cs_cap_complete_event.t_ip1_times_supported,
            cs_cap_complete_event.t_ip2_times_supported,
            cs_cap_complete_event.t_fcs_times_supported, cs_cap_complete_event.t_pm_times_supported,
            cs_cap_complete_event.t_sw_time_supported, cs_cap_complete_event.tx_snr_capability);
  }

  void StartMeasurement(const StartMeasurementParameters& params) {
    dm_manager_->StartDistanceMeasurement(params.remote_address, params.connection_handle,
                                          params.local_hci_role, params.interval, params.method);
  }

protected:
  TestModuleRegistry fake_registry_;
  HciLayerFake* test_hci_layer_ = nullptr;
  TestController* mock_controller_ = nullptr;
  TestAclManager* mock_acl_manager_ = nullptr;
  hal::testing::MockRangingHal* mock_ranging_hal_ = nullptr;
  os::Thread& thread_ = fake_registry_.GetTestThread();
  os::Handler* client_handler_ = nullptr;
  os::Handler* handler_ = nullptr;

  DistanceMeasurementManager* dm_manager_ = nullptr;
  testing::MockDistanceMeasurementCallbacks mock_dm_callbacks_;
  std::unique_ptr<std::promise<void>> dm_session_promise_;
};

TEST_F(DistanceMeasurementManagerTest, setup_teardown) {
  EXPECT_NE(mock_ranging_hal_->GetRangingHalCallback(), nullptr);
}

TEST_F(DistanceMeasurementManagerTest, fail_read_local_cs_capabilities) {
  StartMeasurementParameters params;
  auto dm_session_future = GetDmSessionFuture();
  EXPECT_CALL(mock_dm_callbacks_,
              OnDistanceMeasurementStopped(params.remote_address,
                                           DistanceMeasurementErrorCode::REASON_INTERNAL_ERROR,
                                           DistanceMeasurementMethod::METHOD_CS))
          .WillOnce([this](const Address& /*address*/, DistanceMeasurementErrorCode /*error_code*/,
                           DistanceMeasurementMethod /*method*/) {
            ASSERT_NE(dm_session_promise_, nullptr);
            dm_session_promise_->set_value();
            dm_session_promise_.reset();
          });

  CsReadCapabilitiesCompleteEvent read_cs_complete_event;
  read_cs_complete_event.error_code = ErrorCode::COMMAND_DISALLOWED;
  test_hci_layer_->IncomingEvent(
          GetLocalSupportedCapabilitiesCompleteEvent(read_cs_complete_event));

  StartMeasurement(params);

  dm_session_future.wait_for(kTimeout);
  sync_client_handler();
}
}  // namespace
}  // namespace hci
}  // namespace bluetooth
