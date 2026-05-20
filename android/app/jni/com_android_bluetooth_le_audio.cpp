/*   Copyright 2019 HIMSA II K/S - www.himsa.com
 * Represented by EHIMA - www.ehima.com
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "BluetoothLeAudioServiceJni"

#include <bluetooth/log.h>
#include <jni.h>
#include <nativehelper/JNIHelp.h>
#include <nativehelper/scoped_local_ref.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <map>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <type_traits>
#include <vector>

#include "com_android_bluetooth.h"
#include "hardware/bluetooth.h"
#include "hardware/bt_le_audio.h"
#include "hardware/bt_le_audio_broadcast_sink.h"
#include "stack/include/btm_vendor_api.h"
#include "types/raw_address.h"

using bluetooth::le_audio::BroadcastId;
using bluetooth::le_audio::BroadcastState;
using bluetooth::le_audio::btle_audio_bits_per_sample_index_t;
using bluetooth::le_audio::btle_audio_channel_count_index_t;
using bluetooth::le_audio::btle_audio_codec_config_t;
using bluetooth::le_audio::btle_audio_codec_index_t;
using bluetooth::le_audio::btle_audio_frame_duration_index_t;
using bluetooth::le_audio::btle_audio_sample_rate_index_t;
using bluetooth::le_audio::ConnectionState;
using bluetooth::le_audio::GroupNodeStatus;
using bluetooth::le_audio::GroupStatus;
using bluetooth::le_audio::GroupStreamStatus;
using bluetooth::le_audio::LeAudioBroadcasterCallbacks;
using bluetooth::le_audio::LeAudioBroadcasterInterface;
using bluetooth::le_audio::LeAudioClientCallbacks;
using bluetooth::le_audio::LeAudioClientInterface;
using bluetooth::le_audio::UnicastMonitorModeStatus;

namespace android {
static jmethodID method_onInitialized;
static jmethodID method_onConnectionStateChanged;
static jmethodID method_onGroupStatus;
static jmethodID method_onGroupNodeStatus;
static jmethodID method_onAudioConf;
static jmethodID method_onSinkAudioLocationAvailable;
static jmethodID method_onAudioLocalCodecCapabilities;
static jmethodID method_onAudioGroupCurrentCodecConf;
static jmethodID method_onAudioGroupSelectableCodecConf;
static jmethodID method_OnMetadataUpdate;
static jmethodID method_onHealthBasedRecommendationAction;
static jmethodID method_onHealthBasedGroupRecommendationAction;
static jmethodID method_onUnicastMonitorModeStatus;
static jmethodID method_onGroupStreamStatus;

static struct {
  jclass clazz;
  jmethodID constructor;
  jmethodID getCodecType;
  jmethodID getSampleRate;
  jmethodID getBitsPerSample;
  jmethodID getChannelCount;
  jmethodID getFrameDuration;
  jmethodID getOctetsPerFrame;
  jmethodID getCodecPriority;
} android_bluetooth_BluetoothLeAudioCodecConfig;

static struct {
  jclass clazz;
  jmethodID constructor;
} android_bluetooth_BluetoothLeAudioCodecConfigMetadata;

static struct {
  jclass clazz;
  jmethodID constructor;
  jmethodID add;
} java_util_ArrayList;

static struct {
  jclass clazz;
  jmethodID constructor;
} android_bluetooth_BluetoothLeBroadcastChannel;

static struct {
  jclass clazz;
  jmethodID constructor;
} android_bluetooth_BluetoothLeBroadcastSubgroup;

static struct {
  jclass clazz;
  jmethodID constructor;
} android_bluetooth_BluetoothLeAudioContentMetadata;

static struct {
  jclass clazz;
  jmethodID constructor;
} android_bluetooth_BluetoothLeBroadcastMetadata;

static struct {
  jclass clazz;
  jmethodID constructor;
} android_bluetooth_BluetoothDevice;

static LeAudioClientInterface* sLeAudioClientInterface = nullptr;
static std::shared_timed_mutex interface_mutex;

static jobject mCallbacksObj = nullptr;
static std::shared_timed_mutex callbacks_mutex;

static jclass class_LeAudioNativeInterface;

static jobject prepareCodecConfigObj(JNIEnv* env, btle_audio_codec_config_t codecConfig) {
  log::info(
          "ct: {}, codec_priority: {}, sample_rate: {}, bits_per_sample: {}, "
          "channel_count: {}, frame_duration: {}, octets_per_frame: {}",
          codecConfig.codec_type, codecConfig.codec_priority, codecConfig.sample_rate,
          codecConfig.bits_per_sample, codecConfig.channel_count, codecConfig.frame_duration,
          codecConfig.octets_per_frame);

  jobject codecConfigObj =
          env->NewObject(android_bluetooth_BluetoothLeAudioCodecConfig.clazz,
                         android_bluetooth_BluetoothLeAudioCodecConfig.constructor,
                         (jint)codecConfig.codec_type, (jint)codecConfig.codec_priority,
                         (jint)codecConfig.sample_rate, (jint)codecConfig.bits_per_sample,
                         (jint)codecConfig.channel_count, (jint)codecConfig.frame_duration,
                         (jint)codecConfig.octets_per_frame, 0, 0, 0L, 0L, 0L, 0L);
  return codecConfigObj;
}

static jobjectArray prepareArrayOfCodecConfigs(
        JNIEnv* env, std::vector<btle_audio_codec_config_t> codecConfigs) {
  jsize i = 0;
  jobjectArray CodecConfigArray = env->NewObjectArray(
          (jsize)codecConfigs.size(), android_bluetooth_BluetoothLeAudioCodecConfig.clazz, nullptr);

  for (auto const& cap : codecConfigs) {
    jobject Obj = prepareCodecConfigObj(env, cap);

    env->SetObjectArrayElement(CodecConfigArray, i++, Obj);
    env->DeleteLocalRef(Obj);
  }

  return CodecConfigArray;
}

class LeAudioClientCallbacksImpl : public LeAudioClientCallbacks {
public:
  ~LeAudioClientCallbacksImpl() = default;

  void OnInitialized(void) override {
    log::info("");
    std::shared_lock<std::shared_timed_mutex> lock(callbacks_mutex);
    CallbackEnv sCallbackEnv(__func__);
    if (!sCallbackEnv.valid() || mCallbacksObj == nullptr) {
      return;
    }
    sCallbackEnv->CallStaticVoidMethod(class_LeAudioNativeInterface, method_onInitialized);
  }

  void OnConnectionState(ConnectionState state, const RawAddress& bd_addr) override {
    log::info("state:{}, addr: {}", int(state), bd_addr.ToRedactedStringForLogging());

    std::shared_lock<std::shared_timed_mutex> lock(callbacks_mutex);
    CallbackEnv sCallbackEnv(__func__);
    if (!sCallbackEnv.valid() || mCallbacksObj == nullptr) {
      return;
    }

    ScopedLocalRef<jbyteArray> addr(sCallbackEnv.get(),
                                    sCallbackEnv->NewByteArray(sizeof(RawAddress)));
    if (!addr.get()) {
      log::error("Failed to new jbyteArray bd addr for connection state");
      return;
    }

    sCallbackEnv->SetByteArrayRegion(addr.get(), 0, sizeof(RawAddress), (jbyte*)&bd_addr);
    sCallbackEnv->CallVoidMethod(mCallbacksObj, method_onConnectionStateChanged, (jint)state,
                                 addr.get());
  }

  void OnGroupStatus(int group_id, GroupStatus group_status) override {
    log::info("");

    std::shared_lock<std::shared_timed_mutex> lock(callbacks_mutex);
    CallbackEnv sCallbackEnv(__func__);
    if (!sCallbackEnv.valid() || mCallbacksObj == nullptr) {
      return;
    }

    sCallbackEnv->CallVoidMethod(mCallbacksObj, method_onGroupStatus, (jint)group_id,
                                 (jint)group_status);
  }

  void OnGroupNodeStatus(const RawAddress& bd_addr, int group_id,
                         GroupNodeStatus node_status) override {
    log::info("");

    std::shared_lock<std::shared_timed_mutex> lock(callbacks_mutex);
    CallbackEnv sCallbackEnv(__func__);
    if (!sCallbackEnv.valid() || mCallbacksObj == nullptr) {
      return;
    }

    ScopedLocalRef<jbyteArray> addr(sCallbackEnv.get(),
                                    sCallbackEnv->NewByteArray(sizeof(RawAddress)));
    if (!addr.get()) {
      log::error("Failed to new jbyteArray bd addr for group status");
      return;
    }

    sCallbackEnv->SetByteArrayRegion(addr.get(), 0, sizeof(RawAddress), (jbyte*)&bd_addr);
    sCallbackEnv->CallVoidMethod(mCallbacksObj, method_onGroupNodeStatus, addr.get(),
                                 (jint)group_id, (jint)node_status);
  }

  void OnAudioConf(uint8_t direction, int group_id,
                   std::optional<std::bitset<32>> sink_audio_location,
                   std::optional<std::bitset<32>> source_audio_location,
                   uint16_t avail_cont) override {
    log::info("");

    std::shared_lock<std::shared_timed_mutex> lock(callbacks_mutex);
    CallbackEnv sCallbackEnv(__func__);
    if (!sCallbackEnv.valid() || mCallbacksObj == nullptr) {
      return;
    }

    jint jni_sink_audio_location = sink_audio_location ? sink_audio_location->to_ulong() : -1;
    jint jni_source_audio_location = source_audio_location ? source_audio_location->to_ulong() : -1;
    sCallbackEnv->CallVoidMethod(mCallbacksObj, method_onAudioConf, (jint)direction, (jint)group_id,
                                 jni_sink_audio_location, jni_source_audio_location,
                                 (jint)avail_cont);
  }

  void OnSinkAudioLocationAvailable(const RawAddress& bd_addr,
                                    std::optional<std::bitset<32>> sink_audio_location) override {
    log::info("");

    std::shared_lock<std::shared_timed_mutex> lock(callbacks_mutex);
    CallbackEnv sCallbackEnv(__func__);
    if (!sCallbackEnv.valid() || mCallbacksObj == nullptr) {
      return;
    }

    ScopedLocalRef<jbyteArray> addr(sCallbackEnv.get(),
                                    sCallbackEnv->NewByteArray(sizeof(RawAddress)));
    if (!addr.get()) {
      log::error("Failed to new jbyteArray bd addr for group status");
      return;
    }

    sCallbackEnv->SetByteArrayRegion(addr.get(), 0, sizeof(RawAddress), (jbyte*)&bd_addr);
    jint jni_sink_audio_location = sink_audio_location ? sink_audio_location->to_ulong() : -1;
    sCallbackEnv->CallVoidMethod(mCallbacksObj, method_onSinkAudioLocationAvailable, addr.get(),
                                 jni_sink_audio_location);
  }

  void OnAudioLocalCodecCapabilities(
          std::vector<btle_audio_codec_config_t> local_input_capa_codec_conf,
          std::vector<btle_audio_codec_config_t> local_output_capa_codec_conf) override {
    log::info("");

    std::shared_lock<std::shared_timed_mutex> lock(callbacks_mutex);
    CallbackEnv sCallbackEnv(__func__);
    if (!sCallbackEnv.valid() || mCallbacksObj == nullptr) {
      return;
    }

    jobject localInputCapCodecConfigArray =
            prepareArrayOfCodecConfigs(sCallbackEnv.get(), local_input_capa_codec_conf);

    jobject localOutputCapCodecConfigArray =
            prepareArrayOfCodecConfigs(sCallbackEnv.get(), local_output_capa_codec_conf);

    sCallbackEnv->CallVoidMethod(mCallbacksObj, method_onAudioLocalCodecCapabilities,
                                 localInputCapCodecConfigArray, localOutputCapCodecConfigArray);
  }

  void OnAudioGroupCurrentCodecConf(int group_id, btle_audio_codec_config_t input_codec_conf,
                                    btle_audio_codec_config_t output_codec_conf) override {
    log::info("");

    std::shared_lock<std::shared_timed_mutex> lock(callbacks_mutex);
    CallbackEnv sCallbackEnv(__func__);
    if (!sCallbackEnv.valid() || mCallbacksObj == nullptr) {
      return;
    }

    jobject inputCodecConfigObj = prepareCodecConfigObj(sCallbackEnv.get(), input_codec_conf);
    jobject outputCodecConfigObj = prepareCodecConfigObj(sCallbackEnv.get(), output_codec_conf);

    sCallbackEnv->CallVoidMethod(mCallbacksObj, method_onAudioGroupCurrentCodecConf, (jint)group_id,
                                 inputCodecConfigObj, outputCodecConfigObj);
  }

  void OnAudioGroupSelectableCodecConf(
          int group_id, std::vector<btle_audio_codec_config_t> input_selectable_codec_conf,
          std::vector<btle_audio_codec_config_t> output_selectable_codec_conf) override {
    log::info("");

    std::shared_lock<std::shared_timed_mutex> lock(callbacks_mutex);
    CallbackEnv sCallbackEnv(__func__);
    if (!sCallbackEnv.valid() || mCallbacksObj == nullptr) {
      return;
    }

    jobject inputSelectableCodecConfigArray =
            prepareArrayOfCodecConfigs(sCallbackEnv.get(), input_selectable_codec_conf);
    jobject outputSelectableCodecConfigArray =
            prepareArrayOfCodecConfigs(sCallbackEnv.get(), output_selectable_codec_conf);

    sCallbackEnv->CallVoidMethod(mCallbacksObj, method_onAudioGroupSelectableCodecConf,
                                 (jint)group_id, inputSelectableCodecConfigArray,
                                 outputSelectableCodecConfigArray);
  }

  void OnMetadataUpdate(uint16_t context) override {
    log::info("");
    std::shared_lock<std::shared_timed_mutex> lock(callbacks_mutex);
    CallbackEnv sCallbackEnv(__func__);
    if (!sCallbackEnv.valid() || mCallbacksObj == nullptr) {
      return;
    }

    return sCallbackEnv->CallVoidMethod(mCallbacksObj, method_OnMetadataUpdate, (jint)context);
  }

  void OnHealthBasedRecommendationAction(
          const RawAddress& bd_addr,
          bluetooth::le_audio::LeAudioHealthBasedAction action) override {
    log::info("");

    std::shared_lock<std::shared_timed_mutex> lock(callbacks_mutex);
    CallbackEnv sCallbackEnv(__func__);
    if (!sCallbackEnv.valid() || mCallbacksObj == nullptr) {
      return;
    }

    ScopedLocalRef<jbyteArray> addr(sCallbackEnv.get(),
                                    sCallbackEnv->NewByteArray(sizeof(RawAddress)));
    if (!addr.get()) {
      log::error("Failed to new jbyteArray bd addr for group status");
      return;
    }

    sCallbackEnv->SetByteArrayRegion(addr.get(), 0, sizeof(RawAddress), (jbyte*)&bd_addr);
    sCallbackEnv->CallVoidMethod(mCallbacksObj, method_onHealthBasedRecommendationAction,
                                 addr.get(), (jint)action);
  }

  void OnHealthBasedGroupRecommendationAction(
          int group_id, bluetooth::le_audio::LeAudioHealthBasedAction action) override {
    log::info("");

    std::shared_lock<std::shared_timed_mutex> lock(callbacks_mutex);
    CallbackEnv sCallbackEnv(__func__);
    if (!sCallbackEnv.valid() || mCallbacksObj == nullptr) {
      return;
    }

    sCallbackEnv->CallVoidMethod(mCallbacksObj, method_onHealthBasedGroupRecommendationAction,
                                 (jint)group_id, (jint)action);
  }

  void OnUnicastMonitorModeStatus(uint8_t direction, UnicastMonitorModeStatus status) override {
    log::info("");

    std::shared_lock<std::shared_timed_mutex> lock(callbacks_mutex);
    CallbackEnv sCallbackEnv(__func__);
    if (!sCallbackEnv.valid() || mCallbacksObj == nullptr) {
      return;
    }

    sCallbackEnv->CallVoidMethod(mCallbacksObj, method_onUnicastMonitorModeStatus, (jint)direction,
                                 (jint)status);
  }

  void OnGroupStreamStatus(int group_id, GroupStreamStatus group_stream_status) override {
    log::info("");

    std::shared_lock<std::shared_timed_mutex> lock(callbacks_mutex);
    CallbackEnv sCallbackEnv(__func__);
    if (!sCallbackEnv.valid() || mCallbacksObj == nullptr) {
      return;
    }

    sCallbackEnv->CallVoidMethod(mCallbacksObj, method_onGroupStreamStatus, (jint)group_id,
                                 (jint)group_stream_status);
  }
};

static LeAudioClientCallbacksImpl sLeAudioClientCallbacks;

static std::vector<btle_audio_codec_config_t> prepareCodecPreferences(
        JNIEnv* env, jobject /* object */, jobjectArray codecConfigArray) {
  std::vector<btle_audio_codec_config_t> codec_preferences;

  int numConfigs = env->GetArrayLength(codecConfigArray);
  for (int i = 0; i < numConfigs; i++) {
    jobject jcodecConfig = env->GetObjectArrayElement(codecConfigArray, i);
    if (jcodecConfig == nullptr) {
      continue;
    }
    if (!env->IsInstanceOf(jcodecConfig, android_bluetooth_BluetoothLeAudioCodecConfig.clazz)) {
      log::error("Invalid BluetoothLeAudioCodecConfig instance");
      continue;
    }
    jint codecType = env->CallIntMethod(jcodecConfig,
                                        android_bluetooth_BluetoothLeAudioCodecConfig.getCodecType);

    btle_audio_codec_config_t codec_config = {
            .codec_type = static_cast<btle_audio_codec_index_t>(codecType)};

    codec_preferences.push_back(codec_config);
  }
  return codec_preferences;
}

static void initNative(JNIEnv* env, jobject object, jobjectArray codecOffloadingArray) {
  std::unique_lock<std::shared_timed_mutex> interface_lock(interface_mutex);
  std::unique_lock<std::shared_timed_mutex> callbacks_lock(callbacks_mutex);

  const bt_interface_t* btInf = getBluetoothInterface();
  if (btInf == nullptr) {
    log::error("Bluetooth module is not loaded");
    return;
  }

  jclass tmpControllerInterface =
          env->FindClass("com/android/bluetooth/le_audio/LeAudioNativeInterface");
  class_LeAudioNativeInterface = (jclass)env->NewGlobalRef(tmpControllerInterface);

  if (mCallbacksObj != nullptr) {
    log::info("Cleaning up LeAudio callback object");
    env->DeleteGlobalRef(mCallbacksObj);
    mCallbacksObj = nullptr;
  }

  if ((mCallbacksObj = env->NewGlobalRef(object)) == nullptr) {
    log::error("Failed to allocate Global Ref for LeAudio Callbacks");
    return;
  }

  android_bluetooth_BluetoothLeAudioCodecConfig.clazz = (jclass)env->NewGlobalRef(
          env->FindClass("android/bluetooth/BluetoothLeAudioCodecConfig"));
  if (android_bluetooth_BluetoothLeAudioCodecConfig.clazz == nullptr) {
    log::error("Failed to allocate Global Ref for BluetoothLeAudioCodecConfig class");
    return;
  }

  sLeAudioClientInterface =
          (LeAudioClientInterface*)btInf->get_profile_interface(BT_PROFILE_LE_AUDIO_ID);
  if (sLeAudioClientInterface == nullptr) {
    log::error("Failed to get Bluetooth LeAudio Interface");
    return;
  }

  std::vector<btle_audio_codec_config_t> codec_offloading =
          prepareCodecPreferences(env, object, codecOffloadingArray);

  sLeAudioClientInterface->Initialize(&sLeAudioClientCallbacks, codec_offloading);
}

static void cleanupNative(JNIEnv* env, jobject /* object */) {
  std::unique_lock<std::shared_timed_mutex> interface_lock(interface_mutex);
  std::unique_lock<std::shared_timed_mutex> callbacks_lock(callbacks_mutex);

  const bt_interface_t* btInf = getBluetoothInterface();
  if (btInf == nullptr) {
    log::error("Bluetooth module is not loaded");
    return;
  }

  if (sLeAudioClientInterface != nullptr) {
    sLeAudioClientInterface->Cleanup();
    sLeAudioClientInterface = nullptr;
  }

  env->DeleteGlobalRef(android_bluetooth_BluetoothLeAudioCodecConfig.clazz);
  android_bluetooth_BluetoothLeAudioCodecConfig.clazz = nullptr;

  if (mCallbacksObj != nullptr) {
    env->DeleteGlobalRef(mCallbacksObj);
    mCallbacksObj = nullptr;
  }
}

static jboolean connectLeAudioNative(JNIEnv* env, jobject /* object */, jbyteArray address) {
  log::info("");
  std::shared_lock<std::shared_timed_mutex> lock(interface_mutex);
  if (!sLeAudioClientInterface) {
    return JNI_FALSE;
  }

  jbyte* addr = env->GetByteArrayElements(address, nullptr);
  if (!addr) {
    jniThrowIOException(env, EINVAL);
    return JNI_FALSE;
  }

  RawAddress* tmpraw = (RawAddress*)addr;
  sLeAudioClientInterface->Connect(*tmpraw);
  env->ReleaseByteArrayElements(address, addr, 0);
  return JNI_TRUE;
}

static jboolean disconnectLeAudioNative(JNIEnv* env, jobject /* object */, jbyteArray address) {
  log::info("");
  std::shared_lock<std::shared_timed_mutex> lock(interface_mutex);
  if (!sLeAudioClientInterface) {
    return JNI_FALSE;
  }

  jbyte* addr = env->GetByteArrayElements(address, nullptr);
  if (!addr) {
    jniThrowIOException(env, EINVAL);
    return JNI_FALSE;
  }

  RawAddress* tmpraw = (RawAddress*)addr;
  sLeAudioClientInterface->Disconnect(*tmpraw);
  env->ReleaseByteArrayElements(address, addr, 0);
  return JNI_TRUE;
}

static jboolean setEnableStateNative(JNIEnv* env, jobject /* object */, jbyteArray address,
                                     jboolean enabled) {
  std::shared_lock<std::shared_timed_mutex> lock(interface_mutex);
  jbyte* addr = env->GetByteArrayElements(address, nullptr);

  if (!sLeAudioClientInterface) {
    log::error("Failed to get the Bluetooth LeAudio Interface");
    return JNI_FALSE;
  }

  if (!addr) {
    jniThrowIOException(env, EINVAL);
    return JNI_FALSE;
  }

  RawAddress* tmpraw = (RawAddress*)addr;
  sLeAudioClientInterface->SetEnableState(*tmpraw, enabled);
  env->ReleaseByteArrayElements(address, addr, 0);
  return JNI_TRUE;
}

static jboolean groupAddNodeNative(JNIEnv* env, jobject /* object */, jint group_id,
                                   jbyteArray address) {
  std::shared_lock<std::shared_timed_mutex> lock(interface_mutex);
  jbyte* addr = env->GetByteArrayElements(address, nullptr);

  if (!sLeAudioClientInterface) {
    log::error("Failed to get the Bluetooth LeAudio Interface");
    return JNI_FALSE;
  }

  if (!addr) {
    jniThrowIOException(env, EINVAL);
    return JNI_FALSE;
  }

  RawAddress* tmpraw = (RawAddress*)addr;
  sLeAudioClientInterface->GroupAddNode(group_id, *tmpraw);
  env->ReleaseByteArrayElements(address, addr, 0);

  return JNI_TRUE;
}

static jboolean groupRemoveNodeNative(JNIEnv* env, jobject /* object */, jint group_id,
                                      jbyteArray address) {
  std::shared_lock<std::shared_timed_mutex> lock(interface_mutex);
  if (!sLeAudioClientInterface) {
    log::error("Failed to get the Bluetooth LeAudio Interface");
    return JNI_FALSE;
  }

  jbyte* addr = env->GetByteArrayElements(address, nullptr);
  if (!addr) {
    jniThrowIOException(env, EINVAL);
    return JNI_FALSE;
  }

  RawAddress* tmpraw = (RawAddress*)addr;
  sLeAudioClientInterface->GroupRemoveNode(group_id, *tmpraw);
  env->ReleaseByteArrayElements(address, addr, 0);
  return JNI_TRUE;
}

static void groupSetActiveNative(JNIEnv* /* env */, jobject /* object */, jint group_id) {
  log::info("");
  std::shared_lock<std::shared_timed_mutex> lock(interface_mutex);

  if (!sLeAudioClientInterface) {
    log::error("Failed to get the Bluetooth LeAudio Interface");
    return;
  }

  sLeAudioClientInterface->GroupSetActive(group_id);
}

static void setCodecConfigPreferenceNative(JNIEnv* env, jobject /* object */, jint group_id,
                                           jobject inputCodecConfig, jobject outputCodecConfig) {
  std::shared_lock<std::shared_timed_mutex> lock(interface_mutex);

  if (!env->IsInstanceOf(inputCodecConfig, android_bluetooth_BluetoothLeAudioCodecConfig.clazz) ||
      !env->IsInstanceOf(outputCodecConfig, android_bluetooth_BluetoothLeAudioCodecConfig.clazz)) {
    log::error("Invalid BluetoothLeAudioCodecConfig instance");
    return;
  }

  jint inputCodecType = env->CallIntMethod(
          inputCodecConfig, android_bluetooth_BluetoothLeAudioCodecConfig.getCodecType);

  jint inputSampleRate = env->CallIntMethod(
          inputCodecConfig, android_bluetooth_BluetoothLeAudioCodecConfig.getSampleRate);

  jint inputBitsPerSample = env->CallIntMethod(
          inputCodecConfig, android_bluetooth_BluetoothLeAudioCodecConfig.getBitsPerSample);

  jint inputChannelCount = env->CallIntMethod(
          inputCodecConfig, android_bluetooth_BluetoothLeAudioCodecConfig.getChannelCount);

  jint inputFrameDuration = env->CallIntMethod(
          inputCodecConfig, android_bluetooth_BluetoothLeAudioCodecConfig.getFrameDuration);

  jint inputOctetsPerFrame = env->CallIntMethod(
          inputCodecConfig, android_bluetooth_BluetoothLeAudioCodecConfig.getOctetsPerFrame);

  jint inputCodecPriority = env->CallIntMethod(
          inputCodecConfig, android_bluetooth_BluetoothLeAudioCodecConfig.getCodecPriority);

  btle_audio_codec_config_t input_codec_config = {
          .codec_type = static_cast<btle_audio_codec_index_t>(inputCodecType),
          .sample_rate = static_cast<btle_audio_sample_rate_index_t>(inputSampleRate),
          .bits_per_sample = static_cast<btle_audio_bits_per_sample_index_t>(inputBitsPerSample),
          .channel_count = static_cast<btle_audio_channel_count_index_t>(inputChannelCount),
          .frame_duration = static_cast<btle_audio_frame_duration_index_t>(inputFrameDuration),
          .octets_per_frame = static_cast<uint16_t>(inputOctetsPerFrame),
          .codec_priority = static_cast<int32_t>(inputCodecPriority),
  };

  jint outputCodecType = env->CallIntMethod(
          outputCodecConfig, android_bluetooth_BluetoothLeAudioCodecConfig.getCodecType);

  jint outputSampleRate = env->CallIntMethod(
          outputCodecConfig, android_bluetooth_BluetoothLeAudioCodecConfig.getSampleRate);

  jint outputBitsPerSample = env->CallIntMethod(
          outputCodecConfig, android_bluetooth_BluetoothLeAudioCodecConfig.getBitsPerSample);

  jint outputChannelCount = env->CallIntMethod(
          outputCodecConfig, android_bluetooth_BluetoothLeAudioCodecConfig.getChannelCount);

  jint outputFrameDuration = env->CallIntMethod(
          outputCodecConfig, android_bluetooth_BluetoothLeAudioCodecConfig.getFrameDuration);

  jint outputOctetsPerFrame = env->CallIntMethod(
          outputCodecConfig, android_bluetooth_BluetoothLeAudioCodecConfig.getOctetsPerFrame);

  jint outputCodecPriority = env->CallIntMethod(
          outputCodecConfig, android_bluetooth_BluetoothLeAudioCodecConfig.getCodecPriority);

  btle_audio_codec_config_t output_codec_config = {
          .codec_type = static_cast<btle_audio_codec_index_t>(outputCodecType),
          .sample_rate = static_cast<btle_audio_sample_rate_index_t>(outputSampleRate),
          .bits_per_sample = static_cast<btle_audio_bits_per_sample_index_t>(outputBitsPerSample),
          .channel_count = static_cast<btle_audio_channel_count_index_t>(outputChannelCount),
          .frame_duration = static_cast<btle_audio_frame_duration_index_t>(outputFrameDuration),
          .octets_per_frame = static_cast<uint16_t>(outputOctetsPerFrame),
          .codec_priority = static_cast<int32_t>(outputCodecPriority),
  };

  sLeAudioClientInterface->SetCodecConfigPreference(group_id, input_codec_config,
                                                    output_codec_config);
}

static void setCcidInformationNative(JNIEnv* /* env */, jobject /* object */, jint ccid,
                                     jint contextType) {
  std::shared_lock<std::shared_timed_mutex> lock(interface_mutex);
  if (!sLeAudioClientInterface) {
    log::error("Failed to get the Bluetooth LeAudio Interface");
    return;
  }

  sLeAudioClientInterface->SetCcidInformation(ccid, contextType);
}

static void setInCallNative(JNIEnv* /* env */, jobject /* object */, jboolean inCall) {
  std::shared_lock<std::shared_timed_mutex> lock(interface_mutex);
  if (!sLeAudioClientInterface) {
    log::error("Failed to get the Bluetooth LeAudio Interface");
    return;
  }

  sLeAudioClientInterface->SetInCall(inCall);
}

static void setUnicastMonitorModeNative(JNIEnv* /* env */, jobject /* object */, jint direction,
                                        jboolean enable) {
  std::shared_lock<std::shared_timed_mutex> lock(interface_mutex);
  if (!sLeAudioClientInterface) {
    log::error("Failed to get the Bluetooth LeAudio Interface");
    return;
  }

  sLeAudioClientInterface->SetUnicastMonitorMode(direction, enable);
}

static void sendAudioProfilePreferencesNative(JNIEnv* /* env */, jobject /* object */, jint groupId,
                                              jboolean isOutputPreferenceLeAudio,
                                              jboolean isDuplexPreferenceLeAudio) {
  std::shared_lock<std::shared_timed_mutex> lock(interface_mutex);
  if (!sLeAudioClientInterface) {
    log::error("Failed to get the Bluetooth LeAudio Interface");
    return;
  }

  sLeAudioClientInterface->SendAudioProfilePreferences(groupId, isOutputPreferenceLeAudio,
                                                       isDuplexPreferenceLeAudio);
}

static void setGroupAllowedContextMaskNative(JNIEnv* /* env */, jobject /* object */, jint groupId,
                                             jint sinkContextTypes, jint sourceContextTypes) {
  std::shared_lock<std::shared_timed_mutex> lock(interface_mutex);
  if (!sLeAudioClientInterface) {
    log::error("Failed to get the Bluetooth LeAudio Interface");
    return;
  }

  log::info("group_id: {}, sink context types: {}, source context types: {}", groupId,
            sinkContextTypes, sourceContextTypes);

  sLeAudioClientInterface->SetGroupAllowedContextMask(groupId, sinkContextTypes,
                                                      sourceContextTypes);
}

/* Le Audio Broadcaster */
static jmethodID method_onBroadcastCreated;
static jmethodID method_onBroadcastDestroyed;
static jmethodID method_onBroadcastStateChanged;
static jmethodID method_onBroadcastMetadataChanged;
static jmethodID method_onBroadcastAudioSessionCreated;
static jmethodID method_onDbigStatusChanged;

static LeAudioBroadcasterInterface* sLeAudioBroadcasterInterface = nullptr;
static std::shared_timed_mutex sBroadcasterInterfaceMutex;

static jobject sBroadcasterCallbacksObj = nullptr;
static std::shared_timed_mutex sBroadcasterCallbacksMutex;

#define VEC_UINT8_TO_UINT32(vec) \
  ((vec.data()[3] << 24) + (vec.data()[2] << 16) + (vec.data()[1] << 8) + vec.data()[0])

#define VEC_UINT8_TO_UINT16(vec) (((vec).data()[1] << 8) + ((vec).data()[0]))

static size_t RawPacketSize(const std::map<uint8_t, std::vector<uint8_t>>& values) {
  size_t bytes = 0;
  for (auto const& value : values) {
    bytes += (/* ltv_len + ltv_type */ 2 + value.second.size());
  }
  return bytes;
}

static jbyteArray prepareRawLtvArray(JNIEnv* env,
                                     const std::map<uint8_t, std::vector<uint8_t>>& metadata) {
  auto raw_meta_size = RawPacketSize(metadata);

  jbyteArray raw_metadata = env->NewByteArray(raw_meta_size);
  if (!raw_metadata) {
    log::error("Failed to create new jbyteArray for raw LTV");
    return nullptr;
  }

  jsize offset = 0;
  for (auto const& kv_pair : metadata) {
    // Length
    const jbyte ltv_sz = kv_pair.second.size() + 1;
    env->SetByteArrayRegion(raw_metadata, offset, 1, &ltv_sz);
    offset += 1;
    // Type
    env->SetByteArrayRegion(raw_metadata, offset, 1, (const jbyte*)&kv_pair.first);
    offset += 1;
    // Value
    env->SetByteArrayRegion(raw_metadata, offset, kv_pair.second.size(),
                            (const jbyte*)kv_pair.second.data());
    offset += kv_pair.second.size();
  }

  return raw_metadata;
}

static jlong getAudioLocationOrDefault(const std::map<uint8_t, std::vector<uint8_t>>& metadata,
                                       jlong default_location) {
  if (metadata.count(bluetooth::le_audio::kLeAudioLtvTypeAudioChannelAllocation) == 0) {
    return default_location;
  }

  auto& vec = metadata.at(bluetooth::le_audio::kLeAudioLtvTypeAudioChannelAllocation);
  return VEC_UINT8_TO_UINT32(vec);
}

static jint getSamplingFrequencyOrDefault(const std::map<uint8_t, std::vector<uint8_t>>& metadata,
                                          jint default_sampling_frequency) {
  if (metadata.count(bluetooth::le_audio::kLeAudioLtvTypeSamplingFreq) == 0) {
    return default_sampling_frequency;
  }

  auto& vec = metadata.at(bluetooth::le_audio::kLeAudioLtvTypeSamplingFreq);
  return (jint)(vec.data()[0]);
}

static jint getFrameDurationOrDefault(const std::map<uint8_t, std::vector<uint8_t>>& metadata,
                                      jint default_frame_duration) {
  if (metadata.count(bluetooth::le_audio::kLeAudioLtvTypeFrameDuration) == 0) {
    return default_frame_duration;
  }

  auto& vec = metadata.at(bluetooth::le_audio::kLeAudioLtvTypeFrameDuration);
  return (jint)(vec.data()[0]);
}

static jint getOctetsPerFrameOrDefault(const std::map<uint8_t, std::vector<uint8_t>>& metadata,
                                       jint default_octets_per_frame) {
  if (metadata.count(bluetooth::le_audio::kLeAudioLtvTypeOctetsPerCodecFrame) == 0) {
    return default_octets_per_frame;
  }

  auto& vec = metadata.at(bluetooth::le_audio::kLeAudioLtvTypeOctetsPerCodecFrame);
  return VEC_UINT8_TO_UINT16(vec);
}

static jobject prepareLeAudioCodecConfigMetadataObject(
        JNIEnv* env, const std::map<uint8_t, std::vector<uint8_t>>& metadata) {
  jlong audio_location = getAudioLocationOrDefault(metadata, -1);
  jint sampling_frequency = getSamplingFrequencyOrDefault(metadata, 0);
  jint frame_duration = getFrameDurationOrDefault(metadata, -1);
  jint octets_per_frame = getOctetsPerFrameOrDefault(metadata, 0);
  ScopedLocalRef<jbyteArray> raw_metadata(env, prepareRawLtvArray(env, metadata));
  if (!raw_metadata.get()) {
    log::error("Failed to create raw metadata jbyteArray");
    return nullptr;
  }

  jobject obj = env->NewObject(android_bluetooth_BluetoothLeAudioCodecConfigMetadata.clazz,
                               android_bluetooth_BluetoothLeAudioCodecConfigMetadata.constructor,
                               audio_location, sampling_frequency, frame_duration, octets_per_frame,
                               raw_metadata.get());

  return obj;
}

static jobject prepareLeBroadcastChannelObject(
        JNIEnv* env, const bluetooth::le_audio::BasicAudioAnnouncementBisConfig& bis_config) {
  ScopedLocalRef<jobject> meta_object(
          env, prepareLeAudioCodecConfigMetadataObject(env, bis_config.codec_specific_params));
  if (!meta_object.get()) {
    log::error("Failed to create new metadata object for bis config");
    return nullptr;
  }

  jobject obj = env->NewObject(android_bluetooth_BluetoothLeBroadcastChannel.clazz,
                               android_bluetooth_BluetoothLeBroadcastChannel.constructor, false,
                               bis_config.bis_index, meta_object.get());

  return obj;
}

static jobject prepareLeAudioContentMetadataObject(
        JNIEnv* env, const std::map<uint8_t, std::vector<uint8_t>>& metadata) {
  jstring program_info_str = nullptr;
  if (metadata.count(bluetooth::le_audio::kLeAudioMetadataTypeProgramInfo)) {
    // Convert the metadata vector to string with null terminator
    std::string p_str(
            (const char*)metadata.at(bluetooth::le_audio::kLeAudioMetadataTypeProgramInfo).data(),
            metadata.at(bluetooth::le_audio::kLeAudioMetadataTypeProgramInfo).size());

    program_info_str = env->NewStringUTF(p_str.c_str());
    if (!program_info_str) {
      log::error("Failed to create new preset name String for preset name");
      return nullptr;
    }
  }

  jstring language_str = nullptr;
  if (metadata.count(bluetooth::le_audio::kLeAudioMetadataTypeLanguage)) {
    // Convert the metadata vector to string with null terminator
    std::string l_str(
            (const char*)metadata.at(bluetooth::le_audio::kLeAudioMetadataTypeLanguage).data(),
            metadata.at(bluetooth::le_audio::kLeAudioMetadataTypeLanguage).size());

    language_str = env->NewStringUTF(l_str.c_str());
    if (!language_str) {
      log::error("Failed to create new preset name String for language");
      return nullptr;
    }
  }

  // This can be nullptr
  ScopedLocalRef<jbyteArray> raw_metadata(env, prepareRawLtvArray(env, metadata));
  if (!raw_metadata.get()) {
    log::error("Failed to create raw_metadata jbyteArray");
    return nullptr;
  }

  jobject obj = env->NewObject(android_bluetooth_BluetoothLeAudioContentMetadata.clazz,
                               android_bluetooth_BluetoothLeAudioContentMetadata.constructor,
                               program_info_str, language_str, raw_metadata.get());

  if (program_info_str) {
    env->DeleteLocalRef(program_info_str);
  }

  if (language_str) {
    env->DeleteLocalRef(language_str);
  }

  return obj;
}

static jobject prepareLeBroadcastChannelListObject(
        JNIEnv* env,
        const std::vector<bluetooth::le_audio::BasicAudioAnnouncementBisConfig>& bis_configs) {
  jobject array = env->NewObject(java_util_ArrayList.clazz, java_util_ArrayList.constructor);
  if (!array) {
    log::error("Failed to create array for subgroups");
    return nullptr;
  }

  for (const auto& el : bis_configs) {
    ScopedLocalRef<jobject> channel_obj(env, prepareLeBroadcastChannelObject(env, el));
    if (!channel_obj.get()) {
      log::error("Failed to create new channel object");
      return nullptr;
    }

    env->CallBooleanMethod(array, java_util_ArrayList.add, channel_obj.get());
  }
  return array;
}

static jobject prepareLeBroadcastSubgroupObject(
        JNIEnv* env, const bluetooth::le_audio::BasicAudioAnnouncementSubgroup& subgroup) {
  // Serialize codec ID
  jlong jlong_codec_id = subgroup.codec_config.codec_id |
                         ((jlong)subgroup.codec_config.vendor_company_id << 16) |
                         ((jlong)subgroup.codec_config.vendor_codec_id << 32);

  ScopedLocalRef<jobject> codec_config_meta_obj(
          env, prepareLeAudioCodecConfigMetadataObject(
                       env, subgroup.codec_config.codec_specific_params));
  if (!codec_config_meta_obj.get()) {
    log::error("Failed to create new codec config metadata");
    return nullptr;
  }

  ScopedLocalRef<jobject> content_meta_obj(
          env, prepareLeAudioContentMetadataObject(env, subgroup.metadata));
  if (!content_meta_obj.get()) {
    log::error("Failed to create new codec config metadata");
    return nullptr;
  }

  ScopedLocalRef<jobject> channel_list_obj(
          env, prepareLeBroadcastChannelListObject(env, subgroup.bis_configs));
  if (!channel_list_obj.get()) {
    log::error("Failed to create new codec config metadata");
    return nullptr;
  }

  // Create the subgroup
  return env->NewObject(android_bluetooth_BluetoothLeBroadcastSubgroup.clazz,
                        android_bluetooth_BluetoothLeBroadcastSubgroup.constructor, jlong_codec_id,
                        codec_config_meta_obj.get(), content_meta_obj.get(),
                        channel_list_obj.get());
}

static jobject prepareLeBroadcastSubgroupListObject(
        JNIEnv* env,
        const std::vector<bluetooth::le_audio::BasicAudioAnnouncementSubgroup>& subgroup_configs) {
  jobject array = env->NewObject(java_util_ArrayList.clazz, java_util_ArrayList.constructor);
  if (!array) {
    log::error("Failed to create array for subgroups");
    return nullptr;
  }

  for (const auto& el : subgroup_configs) {
    ScopedLocalRef<jobject> subgroup_obj(env, prepareLeBroadcastSubgroupObject(env, el));
    if (!subgroup_obj.get()) {
      log::error("Failed to create new subgroup object");
      return nullptr;
    }

    env->CallBooleanMethod(array, java_util_ArrayList.add, subgroup_obj.get());
  }
  return array;
}

static jobject prepareBluetoothDeviceObject(JNIEnv* env, const RawAddress& addr, int addr_type) {
  // The address string has to be uppercase or the BluetoothDevice constructor
  // will treat it as invalid.
  auto addr_str = addr.ToString();
  std::transform(addr_str.begin(), addr_str.end(), addr_str.begin(),
                 [](unsigned char c) { return std::toupper(c); });

  ScopedLocalRef<jstring> addr_jstr(env, env->NewStringUTF(addr_str.c_str()));
  if (!addr_jstr.get()) {
    log::error("Failed to create new preset name String for preset name");
    return nullptr;
  }

  return env->NewObject(android_bluetooth_BluetoothDevice.clazz,
                        android_bluetooth_BluetoothDevice.constructor, addr_jstr.get(),
                        (jint)addr_type);
}

static jobject prepareBluetoothLeBroadcastMetadataObject(
        JNIEnv* env, const bluetooth::le_audio::BroadcastMetadata& broadcast_metadata) {
  ScopedLocalRef<jobject> device_obj(
          env,
          prepareBluetoothDeviceObject(env, broadcast_metadata.addr, broadcast_metadata.addr_type));
  if (!device_obj.get()) {
    log::error("Failed to create new BluetoothDevice");
    return nullptr;
  }

  ScopedLocalRef<jobject> subgroup_list_obj(
          env, prepareLeBroadcastSubgroupListObject(
                       env, broadcast_metadata.basic_audio_announcement.subgroup_configs));
  if (!subgroup_list_obj.get()) {
    log::error("Failed to create new Subgroup array");
    return nullptr;
  }

  // Remove the ending null char bytes
  int nativeCodeSize = 16;
  if (broadcast_metadata.broadcast_code) {
    auto& nativeCode = broadcast_metadata.broadcast_code.value();
    nativeCodeSize =
            std::find_if(nativeCode.cbegin(), nativeCode.cend(), [](int x) { return x == 0x00; }) -
            nativeCode.cbegin();
  }

  ScopedLocalRef<jbyteArray> code(env, env->NewByteArray(nativeCodeSize));
  if (!code.get()) {
    log::error("Failed to create new jbyteArray for the broadcast code");
    return nullptr;
  }

  if (broadcast_metadata.broadcast_code) {
    env->SetByteArrayRegion(code.get(), 0, nativeCodeSize,
                            (const jbyte*)broadcast_metadata.broadcast_code->data());
    log::assert_that(!env->ExceptionCheck(), "assert failed: !env->ExceptionCheck()");
  }

  ScopedLocalRef<jstring> broadcast_name(
          env, env->NewStringUTF(broadcast_metadata.broadcast_name.c_str()));
  if (!broadcast_name.get()) {
    log::error("Failed to create new broadcast name String");
    return nullptr;
  }

  jint audio_cfg_quality = 0;
  if (broadcast_metadata.public_announcement.features &
      bluetooth::le_audio::kLeAudioQualityStandard) {
    // Set bit 0 for AUDIO_CONFIG_QUALITY_STANDARD
    audio_cfg_quality |= 0x1 << bluetooth::le_audio::QUALITY_STANDARD;
  }
  if (broadcast_metadata.public_announcement.features & bluetooth::le_audio::kLeAudioQualityHigh) {
    // Set bit 1 for AUDIO_CONFIG_QUALITY_HIGH
    audio_cfg_quality |= 0x1 << bluetooth::le_audio::QUALITY_HIGH;
  }

  ScopedLocalRef<jobject> public_meta_obj(
          env, prepareLeAudioContentMetadataObject(
                       env, broadcast_metadata.public_announcement.metadata));
  if (!public_meta_obj.get()) {
    log::error("Failed to create new public metadata obj");
    return nullptr;
  }

  return env->NewObject(
          android_bluetooth_BluetoothLeBroadcastMetadata.clazz,
          android_bluetooth_BluetoothLeBroadcastMetadata.constructor,
          (jint)broadcast_metadata.addr_type, device_obj.get(), (jint)broadcast_metadata.adv_sid,
          (jint)broadcast_metadata.broadcast_id, (jint)broadcast_metadata.pa_interval,
          broadcast_metadata.is_encrypted ? true : false, broadcast_metadata.is_public,
          broadcast_name.get(), broadcast_metadata.broadcast_code ? code.get() : nullptr,
          (jint)broadcast_metadata.basic_audio_announcement.presentation_delay_us,
          audio_cfg_quality, (jint)bluetooth::le_audio::kLeAudioSourceRssiUnknown,
          public_meta_obj.get(), subgroup_list_obj.get());
}

class LeAudioBroadcasterCallbacksImpl : public LeAudioBroadcasterCallbacks {
public:
  ~LeAudioBroadcasterCallbacksImpl() = default;

  void OnBroadcastCreated(uint32_t broadcast_id, bool success) override {
    log::info("");

    std::shared_lock<std::shared_timed_mutex> lock(sBroadcasterCallbacksMutex);
    CallbackEnv sCallbackEnv(__func__);

    if (!sCallbackEnv.valid() || sBroadcasterCallbacksObj == nullptr) {
      return;
    }
    sCallbackEnv->CallVoidMethod(sBroadcasterCallbacksObj, method_onBroadcastCreated,
                                 (jint)broadcast_id, success ? JNI_TRUE : JNI_FALSE);
  }

  void OnBroadcastDestroyed(uint32_t broadcast_id) override {
    log::info("");

    std::shared_lock<std::shared_timed_mutex> lock(sBroadcasterCallbacksMutex);
    CallbackEnv sCallbackEnv(__func__);

    if (!sCallbackEnv.valid() || sBroadcasterCallbacksObj == nullptr) {
      return;
    }
    sCallbackEnv->CallVoidMethod(sBroadcasterCallbacksObj, method_onBroadcastDestroyed,
                                 (jint)broadcast_id);
  }

  void OnBroadcastStateChanged(uint32_t broadcast_id, BroadcastState state) override {
    log::info("");

    std::shared_lock<std::shared_timed_mutex> lock(sBroadcasterCallbacksMutex);
    CallbackEnv sCallbackEnv(__func__);

    if (!sCallbackEnv.valid() || sBroadcasterCallbacksObj == nullptr) {
      return;
    }
    sCallbackEnv->CallVoidMethod(
            sBroadcasterCallbacksObj, method_onBroadcastStateChanged, (jint)broadcast_id,
            (jint) static_cast<std::underlying_type<BroadcastState>::type>(state));
  }

  void OnBroadcastMetadataChanged(
          uint32_t broadcast_id,
          const bluetooth::le_audio::BroadcastMetadata& broadcast_metadata) override {
    log::info("");

    std::shared_lock<std::shared_timed_mutex> lock(sBroadcasterCallbacksMutex);
    CallbackEnv sCallbackEnv(__func__);

    ScopedLocalRef<jobject> metadata_obj(
            sCallbackEnv.get(),
            prepareBluetoothLeBroadcastMetadataObject(sCallbackEnv.get(), broadcast_metadata));

    if (!sCallbackEnv.valid() || sBroadcasterCallbacksObj == nullptr) {
      return;
    }
    sCallbackEnv->CallVoidMethod(sBroadcasterCallbacksObj, method_onBroadcastMetadataChanged,
                                 (jint)broadcast_id, metadata_obj.get());
  }

  void OnBroadcastAudioSessionCreated(bool success) override {
    log::info("");

    std::shared_lock<std::shared_timed_mutex> lock(sBroadcasterCallbacksMutex);
    CallbackEnv sCallbackEnv(__func__);

    if (!sCallbackEnv.valid() || sBroadcasterCallbacksObj == nullptr) {
      return;
    }
    sCallbackEnv->CallVoidMethod(sBroadcasterCallbacksObj, method_onBroadcastAudioSessionCreated,
                                 success ? JNI_TRUE : JNI_FALSE);
  }

  void OnDbigStatusChanged(uint8_t dbig_handle, uint16_t status,
                            uint16_t dev_id, std::vector<uint8_t> name,
                            uint8_t num_bis, std::vector<uint16_t> bis_dev_ids,
                            uint16_t broadcast_features) override {
    log::info("dbig_handle={}, status=0x{:04x}, dev_id=0x{:03x}, num_bis={}, broadcast_features=0x{:04x}",
              dbig_handle, status, dev_id, num_bis, broadcast_features);

    std::shared_lock<std::shared_timed_mutex> lock(sBroadcasterCallbacksMutex);
    CallbackEnv sCallbackEnv(__func__);

    if (!sCallbackEnv.valid() || sBroadcasterCallbacksObj == nullptr) {
      return;
    }

    ScopedLocalRef<jbyteArray> jname(sCallbackEnv.get(),
                                     sCallbackEnv->NewByteArray(name.size()));
    if (!jname.get()) {
      log::error("Failed to allocate byte array for name");
      return;
    }
    sCallbackEnv->SetByteArrayRegion(jname.get(), 0, name.size(),
                                     reinterpret_cast<const jbyte*>(name.data()));

    ScopedLocalRef<jcharArray> jbis_dev_ids(sCallbackEnv.get(),
                                            sCallbackEnv->NewCharArray(bis_dev_ids.size()));
    if (!jbis_dev_ids.get()) {
      log::error("Failed to allocate char array for bis_dev_ids");
      return;
    }
    sCallbackEnv->SetCharArrayRegion(jbis_dev_ids.get(), 0, bis_dev_ids.size(),
                                     reinterpret_cast<const jchar*>(bis_dev_ids.data()));

    sCallbackEnv->CallVoidMethod(sBroadcasterCallbacksObj, method_onDbigStatusChanged,
                                 (jint)dbig_handle, (jint)status, (jint)dev_id, jname.get(),
                                 (jint)num_bis, jbis_dev_ids.get(), (jint)broadcast_features);
  }
};

static LeAudioBroadcasterCallbacksImpl sLeAudioBroadcasterCallbacks;

static void BroadcasterInitNative(JNIEnv* env, jobject object) {
  std::unique_lock<std::shared_timed_mutex> interface_lock(sBroadcasterInterfaceMutex);
  std::unique_lock<std::shared_timed_mutex> callbacks_lock(sBroadcasterCallbacksMutex);

  const bt_interface_t* btInf = getBluetoothInterface();
  if (btInf == nullptr) {
    log::error("Bluetooth module is not loaded");
    return;
  }

  android_bluetooth_BluetoothDevice.clazz =
          (jclass)env->NewGlobalRef(env->FindClass("android/bluetooth/BluetoothDevice"));
  if (android_bluetooth_BluetoothDevice.clazz == nullptr) {
    log::error("Failed to allocate Global Ref for BluetoothDevice class");
    return;
  }

  java_util_ArrayList.clazz = (jclass)env->NewGlobalRef(env->FindClass("java/util/ArrayList"));
  if (java_util_ArrayList.clazz == nullptr) {
    log::error("Failed to allocate Global Ref for ArrayList class");
    return;
  }

  android_bluetooth_BluetoothLeAudioCodecConfigMetadata.clazz = (jclass)env->NewGlobalRef(
          env->FindClass("android/bluetooth/BluetoothLeAudioCodecConfigMetadata"));
  if (android_bluetooth_BluetoothLeAudioCodecConfigMetadata.clazz == nullptr) {
    log::error(
            "Failed to allocate Global Ref for BluetoothLeAudioCodecConfigMetadata "
            "class");
    return;
  }

  android_bluetooth_BluetoothLeAudioContentMetadata.clazz = (jclass)env->NewGlobalRef(
          env->FindClass("android/bluetooth/BluetoothLeAudioContentMetadata"));
  if (android_bluetooth_BluetoothLeAudioContentMetadata.clazz == nullptr) {
    log::error(
            "Failed to allocate Global Ref for BluetoothLeAudioContentMetadata "
            "class");
    return;
  }

  android_bluetooth_BluetoothLeBroadcastSubgroup.clazz = (jclass)env->NewGlobalRef(
          env->FindClass("android/bluetooth/BluetoothLeBroadcastSubgroup"));
  if (android_bluetooth_BluetoothLeBroadcastSubgroup.clazz == nullptr) {
    log::error("Failed to allocate Global Ref for BluetoothLeBroadcastSubgroup class");
    return;
  }

  android_bluetooth_BluetoothLeBroadcastChannel.clazz = (jclass)env->NewGlobalRef(
          env->FindClass("android/bluetooth/BluetoothLeBroadcastChannel"));
  if (android_bluetooth_BluetoothLeBroadcastChannel.clazz == nullptr) {
    log::error("Failed to allocate Global Ref for BluetoothLeBroadcastChannel class");
    return;
  }

  android_bluetooth_BluetoothLeBroadcastMetadata.clazz = (jclass)env->NewGlobalRef(
          env->FindClass("android/bluetooth/BluetoothLeBroadcastMetadata"));
  if (android_bluetooth_BluetoothLeBroadcastMetadata.clazz == nullptr) {
    log::error("Failed to allocate Global Ref for BluetoothLeBroadcastMetadata class");
    return;
  }

  if (sBroadcasterCallbacksObj != nullptr) {
    log::info("Cleaning up LeAudio Broadcaster callback object");
    env->DeleteGlobalRef(sBroadcasterCallbacksObj);
    sBroadcasterCallbacksObj = nullptr;
  }

  if ((sBroadcasterCallbacksObj = env->NewGlobalRef(object)) == nullptr) {
    log::error("Failed to allocate Global Ref for LeAudio Broadcaster Callbacks");
    return;
  }

  sLeAudioBroadcasterInterface = (LeAudioBroadcasterInterface*)btInf->get_profile_interface(
          BT_PROFILE_LE_AUDIO_BROADCASTER_ID);
  if (sLeAudioBroadcasterInterface == nullptr) {
    log::error("Failed to get Bluetooth LeAudio Broadcaster Interface");
    return;
  }

  sLeAudioBroadcasterInterface->Initialize(&sLeAudioBroadcasterCallbacks);
}

static void BroadcasterStopNative(JNIEnv* /* env */, jobject /* object */) {
  std::unique_lock<std::shared_timed_mutex> interface_lock(sBroadcasterInterfaceMutex);

  const bt_interface_t* btInf = getBluetoothInterface();
  if (btInf == nullptr) {
    log::error("Bluetooth module is not loaded");
    return;
  }

  if (sLeAudioBroadcasterInterface != nullptr) {
    sLeAudioBroadcasterInterface->Stop();
  }
}

static void BroadcasterCleanupNative(JNIEnv* env, jobject /* object */) {
  std::unique_lock<std::shared_timed_mutex> interface_lock(sBroadcasterInterfaceMutex);
  std::unique_lock<std::shared_timed_mutex> callbacks_lock(sBroadcasterCallbacksMutex);

  const bt_interface_t* btInf = getBluetoothInterface();
  if (btInf == nullptr) {
    log::error("Bluetooth module is not loaded");
    return;
  }

  env->DeleteGlobalRef(java_util_ArrayList.clazz);
  java_util_ArrayList.clazz = nullptr;

  env->DeleteGlobalRef(android_bluetooth_BluetoothDevice.clazz);
  android_bluetooth_BluetoothDevice.clazz = nullptr;

  env->DeleteGlobalRef(android_bluetooth_BluetoothLeAudioCodecConfigMetadata.clazz);
  android_bluetooth_BluetoothLeAudioCodecConfigMetadata.clazz = nullptr;

  env->DeleteGlobalRef(android_bluetooth_BluetoothLeAudioContentMetadata.clazz);
  android_bluetooth_BluetoothLeAudioContentMetadata.clazz = nullptr;

  env->DeleteGlobalRef(android_bluetooth_BluetoothLeBroadcastSubgroup.clazz);
  android_bluetooth_BluetoothLeBroadcastSubgroup.clazz = nullptr;

  env->DeleteGlobalRef(android_bluetooth_BluetoothLeBroadcastChannel.clazz);
  android_bluetooth_BluetoothLeBroadcastChannel.clazz = nullptr;

  env->DeleteGlobalRef(android_bluetooth_BluetoothLeBroadcastMetadata.clazz);
  android_bluetooth_BluetoothLeBroadcastMetadata.clazz = nullptr;

  if (sLeAudioBroadcasterInterface != nullptr) {
    sLeAudioBroadcasterInterface->Cleanup();
    sLeAudioBroadcasterInterface = nullptr;
  }

  if (sBroadcasterCallbacksObj != nullptr) {
    env->DeleteGlobalRef(sBroadcasterCallbacksObj);
    sBroadcasterCallbacksObj = nullptr;
  }
}

static std::vector<std::vector<uint8_t>> convertToDataVectors(JNIEnv* env, jobjectArray dataArray) {
  jsize arraySize = env->GetArrayLength(dataArray);
  std::vector<std::vector<uint8_t>> res(arraySize);

  for (int i = 0; i < arraySize; ++i) {
    jbyteArray rowData = (jbyteArray)env->GetObjectArrayElement(dataArray, i);
    jsize dataSize = env->GetArrayLength(rowData);
    std::vector<uint8_t>& rowVector = res[i];
    rowVector.resize(dataSize);
    env->GetByteArrayRegion(rowData, 0, dataSize, reinterpret_cast<jbyte*>(rowVector.data()));
    env->DeleteLocalRef(rowData);
  }
  return res;
}

static void CreateBroadcastNative(JNIEnv* env, jobject /* object */, jboolean isPublic,
                                  jstring broadcastName, jbyteArray broadcast_code,
                                  jbyteArray publicMetadata, jintArray qualityArray,
                                  jobjectArray metadataArray) {
  log::info("");
  std::shared_lock<std::shared_timed_mutex> lock(sBroadcasterInterfaceMutex);
  if (!sLeAudioBroadcasterInterface) {
    return;
  }

  std::array<uint8_t, 16> code_array{0};
  if (broadcast_code) {
    jsize size = env->GetArrayLength(broadcast_code);
    if (size > 16) {
      log::error("broadcast code to long");
      return;
    }

    env->GetByteArrayRegion(broadcast_code, 0, size, (jbyte*)code_array.data());
  }

  const char* broadcast_name = nullptr;
  if (broadcastName) {
    broadcast_name = env->GetStringUTFChars(broadcastName, nullptr);
  }

  jbyte* public_meta = nullptr;
  if (publicMetadata) {
    public_meta = env->GetByteArrayElements(publicMetadata, nullptr);
  }

  jint* quality_array = nullptr;
  if (qualityArray) {
    quality_array = env->GetIntArrayElements(qualityArray, nullptr);
  }

  sLeAudioBroadcasterInterface->CreateBroadcast(
          isPublic, broadcast_name ? broadcast_name : "",
          broadcast_code ? std::optional<std::array<uint8_t, 16>>(code_array) : std::nullopt,
          public_meta ? std::vector<uint8_t>(public_meta,
                                             public_meta + env->GetArrayLength(publicMetadata))
                      : std::vector<uint8_t>(),
          quality_array ? std::vector<uint8_t>(quality_array,
                                               quality_array + env->GetArrayLength(qualityArray))
                        : std::vector<uint8_t>(),
          convertToDataVectors(env, metadataArray));

  if (broadcast_name) {
    env->ReleaseStringUTFChars(broadcastName, broadcast_name);
  }
  if (public_meta) {
    env->ReleaseByteArrayElements(publicMetadata, public_meta, 0);
  }
  if (quality_array) {
    env->ReleaseIntArrayElements(qualityArray, quality_array, 0);
  }
}

static void CreateEnhancedBroadcastNative(JNIEnv* env, jobject /* object */,
                                          jstring broadcastName, jbyteArray broadcast_code,
                                          jintArray qualityArray,
                                          jobjectArray metadataArray,
                                          jfloat iso_interval) {
  log::info("");
  std::shared_lock<std::shared_timed_mutex> lock(sBroadcasterInterfaceMutex);
  if (!sLeAudioBroadcasterInterface) {
    return;
  }

  std::array<uint8_t, 16> code_array{0};
  if (broadcast_code) {
    jsize size = env->GetArrayLength(broadcast_code);
    if (size > 16) {
      log::error("broadcast code too long");
      return;
    }

    env->GetByteArrayRegion(broadcast_code, 0, size, (jbyte*)code_array.data());
  }

  const char* broadcast_name = nullptr;
  if (broadcastName) {
    broadcast_name = env->GetStringUTFChars(broadcastName, nullptr);
  }

  jint* quality_array = nullptr;
  if (qualityArray) {
    quality_array = env->GetIntArrayElements(qualityArray, nullptr);
  }

  sLeAudioBroadcasterInterface->CreateEnhancedBroadcast(
          broadcast_name ? broadcast_name : "",
          broadcast_code ? std::optional<std::array<uint8_t, 16>>(code_array) : std::nullopt,
          quality_array ? std::vector<uint8_t>(quality_array,
                                               quality_array + env->GetArrayLength(qualityArray))
                        : std::vector<uint8_t>(),
          convertToDataVectors(env, metadataArray),
          iso_interval);

  if (broadcast_name) {
    env->ReleaseStringUTFChars(broadcastName, broadcast_name);
  }
  if (quality_array) {
    env->ReleaseIntArrayElements(qualityArray, quality_array, 0);
  }
}

static void ReadSupportedStatesNative(JNIEnv* /* env */, jobject /* object */) {
  log::info("");
  std::shared_lock<std::shared_timed_mutex> lock(sBroadcasterInterfaceMutex);
  if (!sLeAudioBroadcasterInterface) {
    return;
  }
  sLeAudioBroadcasterInterface->readSupportedStates();
}

static jbyteArray GetDbigParamsNative(JNIEnv* env, jobject /* object */) {
  log::info("");
  std::shared_lock<std::shared_timed_mutex> lock(sBroadcasterInterfaceMutex);
  if (!sLeAudioBroadcasterInterface) {
    return nullptr;
  }

  std::vector<uint8_t> params = sLeAudioBroadcasterInterface->getDbigParams();
  ScopedLocalRef<jbyteArray> jparams(env, env->NewByteArray((jsize)params.size()));
  if (!jparams.get()) {
    log::error("Failed to allocate byte array for DBIG params");
    return nullptr;
  }
  env->SetByteArrayRegion(jparams.get(), 0, (jsize)params.size(),
                          reinterpret_cast<const jbyte*>(params.data()));
  return jparams.release();
}

static jint GetEnhancedBroadcastCapNative(JNIEnv* /* env */, jobject /* object */) {
  std::shared_lock<std::shared_timed_mutex> lock(sBroadcasterInterfaceMutex);
  if (!sLeAudioBroadcasterInterface) {
    return -1;
  }
  return (jint)sLeAudioBroadcasterInterface->getEnhancedBroadcastCap();
}

static void UpdateMetadataNative(JNIEnv* env, jobject /* object */, jint broadcast_id,
                                 jstring broadcastName, jbyteArray publicMetadata,
                                 jobjectArray metadataArray) {
  const char* broadcast_name = nullptr;
  if (broadcastName) {
    broadcast_name = env->GetStringUTFChars(broadcastName, nullptr);
  }

  jbyte* public_meta = nullptr;
  if (publicMetadata) {
    public_meta = env->GetByteArrayElements(publicMetadata, nullptr);
  }

  sLeAudioBroadcasterInterface->UpdateMetadata(
          broadcast_id, broadcast_name ? broadcast_name : "",
          public_meta ? std::vector<uint8_t>(public_meta,
                                             public_meta + env->GetArrayLength(publicMetadata))
                      : std::vector<uint8_t>(),
          convertToDataVectors(env, metadataArray));

  if (broadcast_name) {
    env->ReleaseStringUTFChars(broadcastName, broadcast_name);
  }
  if (public_meta) {
    env->ReleaseByteArrayElements(publicMetadata, public_meta, 0);
  }
}

static void StartBroadcastNative(JNIEnv* /* env */, jobject /* object */, jint broadcast_id) {
  log::info("");
  std::shared_lock<std::shared_timed_mutex> lock(sBroadcasterInterfaceMutex);
  if (!sLeAudioBroadcasterInterface) {
    return;
  }
  sLeAudioBroadcasterInterface->StartBroadcast(broadcast_id);
}

static void StopBroadcastNative(JNIEnv* /* env */, jobject /* object */, jint broadcast_id) {
  log::info("");
  std::shared_lock<std::shared_timed_mutex> lock(sBroadcasterInterfaceMutex);
  if (!sLeAudioBroadcasterInterface) {
    return;
  }
  sLeAudioBroadcasterInterface->StopBroadcast(broadcast_id);
}

static void PauseBroadcastNative(JNIEnv* /* env */, jobject /* object */, jint broadcast_id) {
  log::info("");
  std::shared_lock<std::shared_timed_mutex> lock(sBroadcasterInterfaceMutex);
  if (!sLeAudioBroadcasterInterface) {
    return;
  }
  sLeAudioBroadcasterInterface->PauseBroadcast(broadcast_id);
}

static void DestroyBroadcastNative(JNIEnv* /* env */, jobject /* object */, jint broadcast_id) {
  log::info("");
  std::shared_lock<std::shared_timed_mutex> lock(sBroadcasterInterfaceMutex);
  if (!sLeAudioBroadcasterInterface) {
    return;
  }
  sLeAudioBroadcasterInterface->DestroyBroadcast(broadcast_id);
}

static void getBroadcastMetadataNative(JNIEnv* /* env */, jobject /* object */, jint broadcast_id) {
  log::info("");
  std::shared_lock<std::shared_timed_mutex> lock(sBroadcasterInterfaceMutex);
  if (!sLeAudioBroadcasterInterface) {
    return;
  }
  sLeAudioBroadcasterInterface->GetBroadcastMetadata(broadcast_id);
}

static void setAchatAttributesNative(JNIEnv* env, jobject /* object */,
                                     jbyteArray dev_id, jbyteArray name) {
  log::info("setAchatAttributesNative");
  if (dev_id == nullptr || name == nullptr) {
    log::error("setAchatAttributesNative: null parameter");
    return;
  }
  jsize dev_id_len = env->GetArrayLength(dev_id);
  jsize name_len = env->GetArrayLength(name);
  if (dev_id_len < 2 || name_len < 10) {
    log::error("setAchatAttributesNative: invalid parameter lengths");
    return;
  }
  jbyte* dev_id_bytes = env->GetByteArrayElements(dev_id, nullptr);
  jbyte* name_bytes = env->GetByteArrayElements(name, nullptr);
  if (dev_id_bytes == nullptr || name_bytes == nullptr) {
    if (dev_id_bytes) env->ReleaseByteArrayElements(dev_id, dev_id_bytes, JNI_ABORT);
    if (name_bytes) env->ReleaseByteArrayElements(name, name_bytes, JNI_ABORT);
    return;
  }
  std::vector<uint8_t> dev_id_vec(reinterpret_cast<uint8_t*>(dev_id_bytes),
                                   reinterpret_cast<uint8_t*>(dev_id_bytes) + dev_id_len);
  std::vector<uint8_t> name_vec(reinterpret_cast<uint8_t*>(name_bytes),
                                 reinterpret_cast<uint8_t*>(name_bytes) + name_len);
  env->ReleaseByteArrayElements(dev_id, dev_id_bytes, JNI_ABORT);
  env->ReleaseByteArrayElements(name, name_bytes, JNI_ABORT);
  BTM_SetAchatAttributes(dev_id_vec, name_vec);
}

static void setDbigJoinControlNative(JNIEnv* /* env */, jobject /* object */, jboolean mode) {
  log::info("setDbigJoinControlNative: mode={}", (bool)mode);
  BTM_SetDbigJoinControl((bool)mode);
}

static int register_com_android_bluetooth_le_audio_broadcaster(JNIEnv* env) {
  const JNINativeMethod methods[] = {
          {"initNative", "()V", (void*)BroadcasterInitNative},
          {"stopNative", "()V", (void*)BroadcasterStopNative},
          {"cleanupNative", "()V", (void*)BroadcasterCleanupNative},
          {"createBroadcastNative", "(ZLjava/lang/String;[B[B[I[[B)V",
           (void*)CreateBroadcastNative},
           {"createEnhancedBroadcastNative", "(Ljava/lang/String;[B[I[[BF)V",
           (void*)CreateEnhancedBroadcastNative},
          {"readSupportedStatesNative", "()V", (void*)ReadSupportedStatesNative},
          {"getDbigParamsNative", "()[B", (void*)GetDbigParamsNative},
          {"getEnhancedBroadcastCapNative", "()I", (void*)GetEnhancedBroadcastCapNative},
          {"updateMetadataNative", "(ILjava/lang/String;[B[[B)V", (void*)UpdateMetadataNative},
          {"startBroadcastNative", "(I)V", (void*)StartBroadcastNative},
          {"stopBroadcastNative", "(I)V", (void*)StopBroadcastNative},
          {"pauseBroadcastNative", "(I)V", (void*)PauseBroadcastNative},
          {"destroyBroadcastNative", "(I)V", (void*)DestroyBroadcastNative},
          {"getBroadcastMetadataNative", "(I)V", (void*)getBroadcastMetadataNative},
          {"setAchatAttributesNative", "([B[B)V", (void*)setAchatAttributesNative},
          {"setDbigJoinControlNative", "(Z)V", (void*)setDbigJoinControlNative},
  };

  const int result = REGISTER_NATIVE_METHODS(
          env, "com/android/bluetooth/le_audio/LeAudioBroadcasterNativeInterface", methods);
  if (result != 0) {
    return result;
  }

  const JNIJavaMethod javaMethods[] = {
          {"onBroadcastCreated", "(IZ)V", &method_onBroadcastCreated},
          {"onBroadcastDestroyed", "(I)V", &method_onBroadcastDestroyed},
          {"onBroadcastStateChanged", "(II)V", &method_onBroadcastStateChanged},
          {"onBroadcastMetadataChanged", "(ILandroid/bluetooth/BluetoothLeBroadcastMetadata;)V",
           &method_onBroadcastMetadataChanged},
          {"onBroadcastAudioSessionCreated", "(Z)V", &method_onBroadcastAudioSessionCreated},
          {"onDbigStatusChanged", "(III[BI[CI)V", &method_onDbigStatusChanged},
  };
  GET_JAVA_METHODS(env, "com/android/bluetooth/le_audio/LeAudioBroadcasterNativeInterface",
                   javaMethods);

  const JNIJavaMethod javaArrayListMethods[] = {
          {"<init>", "()V", &java_util_ArrayList.constructor},
          {"add", "(Ljava/lang/Object;)Z", &java_util_ArrayList.add},
  };
  GET_JAVA_METHODS(env, "java/util/ArrayList", javaArrayListMethods);

  const JNIJavaMethod javaLeAudioCodecMethods[] = {
          {"<init>", "(JIII[B)V",
           &android_bluetooth_BluetoothLeAudioCodecConfigMetadata.constructor},
  };
  GET_JAVA_METHODS(env, "android/bluetooth/BluetoothLeAudioCodecConfigMetadata",
                   javaLeAudioCodecMethods);

  const JNIJavaMethod javaLeAudioContentMethods[] = {
          {"<init>", "(Ljava/lang/String;Ljava/lang/String;[B)V",
           &android_bluetooth_BluetoothLeAudioContentMetadata.constructor},
  };
  GET_JAVA_METHODS(env, "android/bluetooth/BluetoothLeAudioContentMetadata",
                   javaLeAudioContentMethods);

  const JNIJavaMethod javaLeBroadcastChannelMethods[] = {
          {"<init>", "(ZILandroid/bluetooth/BluetoothLeAudioCodecConfigMetadata;)V",
           &android_bluetooth_BluetoothLeBroadcastChannel.constructor},
  };
  GET_JAVA_METHODS(env, "android/bluetooth/BluetoothLeBroadcastChannel",
                   javaLeBroadcastChannelMethods);

  const JNIJavaMethod javaLeBroadcastSubgroupMethods[] = {
          {"<init>",
           "(JLandroid/bluetooth/BluetoothLeAudioCodecConfigMetadata;"
           "Landroid/bluetooth/BluetoothLeAudioContentMetadata;"
           "Ljava/util/List;)V",
           &android_bluetooth_BluetoothLeBroadcastSubgroup.constructor},
  };
  GET_JAVA_METHODS(env, "android/bluetooth/BluetoothLeBroadcastSubgroup",
                   javaLeBroadcastSubgroupMethods);

  const JNIJavaMethod javaBluetoothDevieceMethods[] = {
          {"<init>", "(Ljava/lang/String;I)V", &android_bluetooth_BluetoothDevice.constructor},
  };
  GET_JAVA_METHODS(env, "android/bluetooth/BluetoothDevice", javaBluetoothDevieceMethods);

  const JNIJavaMethod javaLeBroadcastMetadataMethods[] = {
          {"<init>",
           "(ILandroid/bluetooth/BluetoothDevice;IIIZZLjava/lang/String;"
           "[BIIILandroid/bluetooth/BluetoothLeAudioContentMetadata;"
           "Ljava/util/List;)V",
           &android_bluetooth_BluetoothLeBroadcastMetadata.constructor},
  };
  GET_JAVA_METHODS(env, "android/bluetooth/BluetoothLeBroadcastMetadata",
                   javaLeBroadcastMetadataMethods);

  return 0;
}

/* Le Audio Broadcast Sink */
using bluetooth::le_audio::broadcast_sink::BroadcastSinkInterface;
using bluetooth::le_audio::broadcast_sink::BroadcastSinkCallbacks;

static jmethodID method_onSourceAddFailed;
static jmethodID method_onSinkStartFailed;
static jmethodID method_onSinkStopFailed;
static jmethodID method_onSourceRemoveFailed;
static jmethodID method_onSourceDestroyed;
static jmethodID method_onSourceMetadataChanged;
static jmethodID method_onBroadcastSinkAudioSessionCreated;
static jmethodID method_onBroadcastSinkStateChanged;
static jmethodID method_onEnhancedSourceDetected;
static jmethodID method_onBigSyncCreated;
static jmethodID method_onBigSyncLost;
static jmethodID method_onBigSyncTerminated;
static jmethodID method_onSinkDbigStatusChanged;

static BroadcastSinkInterface* sBroadcastSinkInterface = nullptr;
static std::shared_timed_mutex sBroadcastSinkInterfaceMutex;

static jobject sBroadcastSinkCallbacksObj = nullptr;
static std::shared_timed_mutex sBroadcastSinkCallbacksMutex;


class BroadcastSinkCallbacksImpl : public BroadcastSinkCallbacks {
public:
  ~BroadcastSinkCallbacksImpl() = default;

  /* -----------------------------------------------------------------------
   * Standard broadcast sink callbacks
   * ----------------------------------------------------------------------- */

  void OnSourceAddFailed(bluetooth::le_audio::BroadcastId broadcast_id, uint8_t reason) override {
    log::info("broadcast_id=0x{:08x}, reason={}", broadcast_id, reason);
    std::shared_lock<std::shared_timed_mutex> lock(sBroadcastSinkCallbacksMutex);
    CallbackEnv sCallbackEnv(__func__);
    if (!sCallbackEnv.valid() || sBroadcastSinkCallbacksObj == nullptr) {
      return;
    }
    sCallbackEnv->CallVoidMethod(sBroadcastSinkCallbacksObj, method_onSourceAddFailed,
                                 (jint)broadcast_id, (jint)reason);
  }

  void OnSourceJoinFailed(bluetooth::le_audio::BroadcastId broadcast_id,
                          uint8_t reason) override {
    log::info("");
    std::shared_lock<std::shared_timed_mutex> lock(sBroadcastSinkCallbacksMutex);
    CallbackEnv sCallbackEnv(__func__);
    if (!sCallbackEnv.valid() || sBroadcastSinkCallbacksObj == nullptr) {
      return;
    }

    sCallbackEnv->CallVoidMethod(sBroadcastSinkCallbacksObj, method_onSinkStartFailed,
                                 (jint)broadcast_id, (jint)reason);
  }

  void OnSourceLeaveFailed(bluetooth::le_audio::BroadcastId broadcast_id, uint8_t reason) override {
    log::info("");
    std::shared_lock<std::shared_timed_mutex> lock(sBroadcastSinkCallbacksMutex);
    CallbackEnv sCallbackEnv(__func__);
    if (!sCallbackEnv.valid() || sBroadcastSinkCallbacksObj == nullptr) {
      return;
    }
    sCallbackEnv->CallVoidMethod(sBroadcastSinkCallbacksObj, method_onSinkStopFailed,
                                 (jint)broadcast_id, (jint)reason);
  }

  void OnSourceDestroyed(bluetooth::le_audio::BroadcastId broadcast_id, uint8_t reason) override {
    log::info("broadcast_id=0x{:08x}, reason={}", broadcast_id, reason);
    std::shared_lock<std::shared_timed_mutex> lock(sBroadcastSinkCallbacksMutex);
    CallbackEnv sCallbackEnv(__func__);
    if (!sCallbackEnv.valid() || sBroadcastSinkCallbacksObj == nullptr) {
      return;
    }
    sCallbackEnv->CallVoidMethod(sBroadcastSinkCallbacksObj, method_onSourceDestroyed,
                                 (jint)broadcast_id, (jint)reason);
  }

  void OnSourceRemoveFailed(bluetooth::le_audio::BroadcastId broadcast_id, uint8_t reason) override {
    log::info("");
    std::shared_lock<std::shared_timed_mutex> lock(sBroadcastSinkCallbacksMutex);
    CallbackEnv sCallbackEnv(__func__);
    if (!sCallbackEnv.valid() || sBroadcastSinkCallbacksObj == nullptr) {
      return;
    }
    sCallbackEnv->CallVoidMethod(sBroadcastSinkCallbacksObj, method_onSourceRemoveFailed,
                                 (jint)broadcast_id, (jint)reason);
  }

  void OnSourceMetadataChanged(bluetooth::le_audio::BroadcastId broadcast_id,
                               const bluetooth::le_audio::BroadcastMetadata& broadcast_metadata) override {
    log::info("");
    std::shared_lock<std::shared_timed_mutex> lock(sBroadcastSinkCallbacksMutex);
    CallbackEnv sCallbackEnv(__func__);
    if (!sCallbackEnv.valid() || sBroadcastSinkCallbacksObj == nullptr) {
      return;
    }

    ScopedLocalRef<jobject> metadata(sCallbackEnv.get(),
                                     prepareBluetoothLeBroadcastMetadataObject(sCallbackEnv.get(), broadcast_metadata));
    sCallbackEnv->CallVoidMethod(sBroadcastSinkCallbacksObj, method_onSourceMetadataChanged,
                                 (jint)broadcast_id, metadata.get());
  }

  void OnBroadcastSinkAudioSessionCreated(bool success) override {
    log::info("");
    std::shared_lock<std::shared_timed_mutex> lock(sBroadcastSinkCallbacksMutex);
    CallbackEnv sCallbackEnv(__func__);
    if (!sCallbackEnv.valid() || sBroadcastSinkCallbacksObj == nullptr) {
      return;
    }
    sCallbackEnv->CallVoidMethod(sBroadcastSinkCallbacksObj, method_onBroadcastSinkAudioSessionCreated,
                                 success ? JNI_TRUE : JNI_FALSE);
  }

  void OnBroadcastSinkStateChanged(bluetooth::le_audio::BroadcastId broadcast_id, uint8_t state) override {
    log::info("broadcast_id=0x{:08x}, state={}", broadcast_id, state);
    std::shared_lock<std::shared_timed_mutex> lock(sBroadcastSinkCallbacksMutex);
    CallbackEnv sCallbackEnv(__func__);
    if (!sCallbackEnv.valid() || sBroadcastSinkCallbacksObj == nullptr) {
      return;
    }
    sCallbackEnv->CallVoidMethod(sBroadcastSinkCallbacksObj, method_onBroadcastSinkStateChanged,
                                 (jint)broadcast_id, (jint)state);
  }

  /**
   * Called when BASE data parsing reveals that the broadcast source is an
   * enhanced (enhanced broadcast) source (at least one subgroup has >= 3 BISes).
   * The Java layer should call startEnhancedBroadcastSinkNative() instead of
   *
   * @param broadcast_id  Broadcast ID of the enhanced source
   * @param num_bis       Total number of BISes detected in the subgroup
   */
  void OnEnhancedSourceDetected(bluetooth::le_audio::BroadcastId broadcast_id,
                                uint8_t num_bis) override {
    log::info("broadcast_id=0x{:08x}, num_bis={}", broadcast_id, num_bis);
    std::shared_lock<std::shared_timed_mutex> lock(sBroadcastSinkCallbacksMutex);
    CallbackEnv sCallbackEnv(__func__);
    if (!sCallbackEnv.valid() || sBroadcastSinkCallbacksObj == nullptr) {
      return;
    }
    sCallbackEnv->CallVoidMethod(sBroadcastSinkCallbacksObj, method_onEnhancedSourceDetected,
                                 (jint)broadcast_id, (jint)num_bis);
  }

  /**
   * Called when BIG sync is successfully established (all ISO data paths
   * configured for enhanced sources).  Forwards broadcastId, bigHandle, and
   * the array of BIS connection handles to Java.
   */
  void OnBigSyncCreated(bluetooth::le_audio::BroadcastId broadcast_id,
                        uint8_t big_handle,
                        const std::vector<uint16_t>& bis_handles) override {
    log::info("broadcast_id=0x{:08x}, big_handle={}, num_bis={}", broadcast_id, big_handle,
              bis_handles.size());
    std::shared_lock<std::shared_timed_mutex> lock(sBroadcastSinkCallbacksMutex);
    CallbackEnv sCallbackEnv(__func__);
    if (!sCallbackEnv.valid() || sBroadcastSinkCallbacksObj == nullptr) {
      return;
    }

    // Convert std::vector<uint16_t> → Java int[]
    ScopedLocalRef<jintArray> bis_handles_array(
        sCallbackEnv.get(),
        sCallbackEnv->NewIntArray(static_cast<jsize>(bis_handles.size())));
    if (!bis_handles_array.get()) {
      log::error("Failed to allocate jintArray for BIS handles");
      return;
    }
    for (jsize i = 0; i < static_cast<jsize>(bis_handles.size()); ++i) {
      jint handle = static_cast<jint>(bis_handles[i]);
      sCallbackEnv->SetIntArrayRegion(bis_handles_array.get(), i, 1, &handle);
    }

    sCallbackEnv->CallVoidMethod(sBroadcastSinkCallbacksObj, method_onBigSyncCreated,
                                 (jint)broadcast_id, (jint)big_handle,
                                 bis_handles_array.get());
  }

  /**
   * Called when BIG sync is lost (unexpected disconnection or controller
   * termination).  Forwards broadcastId, bigHandle, and HCI reason to Java.
   */
  void OnBigSyncLost(bluetooth::le_audio::BroadcastId broadcast_id,
                     uint8_t big_handle,
                     uint8_t reason) override {
    log::info("broadcast_id=0x{:08x}, big_handle={}, reason=0x{:02x}", broadcast_id, big_handle,
              reason);
    std::shared_lock<std::shared_timed_mutex> lock(sBroadcastSinkCallbacksMutex);
    CallbackEnv sCallbackEnv(__func__);
    if (!sCallbackEnv.valid() || sBroadcastSinkCallbacksObj == nullptr) {
      return;
    }
    sCallbackEnv->CallVoidMethod(sBroadcastSinkCallbacksObj, method_onBigSyncLost,
                                 (jint)broadcast_id, (jint)big_handle, (jint)reason);
  }

  void OnBigSyncTerminated(bluetooth::le_audio::BroadcastId broadcast_id,
                           uint8_t big_handle,
                           uint8_t status) override {
    log::info("broadcast_id=0x{:08x}, big_handle={}, status=0x{:02x}", broadcast_id, big_handle,
              status);
    std::shared_lock<std::shared_timed_mutex> lock(sBroadcastSinkCallbacksMutex);
    CallbackEnv sCallbackEnv(__func__);
    if (!sCallbackEnv.valid() || sBroadcastSinkCallbacksObj == nullptr) {
      return;
    }
    sCallbackEnv->CallVoidMethod(sBroadcastSinkCallbacksObj, method_onBigSyncTerminated,
                                 (jint)broadcast_id, (jint)big_handle, (jint)status);
  }

  void OnDbigStatusChanged(uint8_t dbig_handle, uint16_t status,
                            uint16_t dev_id, std::vector<uint8_t> name,
                            uint8_t num_bis, std::vector<uint16_t> bis_dev_ids,
                            uint16_t broadcast_features) override {
    log::info("dbig_handle={}, status=0x{:04x}, dev_id=0x{:03x}, num_bis={}, broadcast_features=0x{:04x}",
              dbig_handle, status, dev_id, num_bis, broadcast_features);
    std::shared_lock<std::shared_timed_mutex> lock(sBroadcastSinkCallbacksMutex);
    CallbackEnv sCallbackEnv(__func__);
    if (!sCallbackEnv.valid() || sBroadcastSinkCallbacksObj == nullptr) {
      return;
    }
    ScopedLocalRef<jbyteArray> jname(sCallbackEnv.get(),
                                     sCallbackEnv->NewByteArray(name.size()));
    if (!jname.get()) {
      log::error("Failed to allocate byte array for name");
      return;
    }
    sCallbackEnv->SetByteArrayRegion(jname.get(), 0, name.size(),
                                     reinterpret_cast<const jbyte*>(name.data()));

    ScopedLocalRef<jcharArray> jbis_dev_ids(sCallbackEnv.get(),
                                            sCallbackEnv->NewCharArray(bis_dev_ids.size()));
    if (!jbis_dev_ids.get()) {
      log::error("Failed to allocate char array for bis_dev_ids");
      return;
    }
    sCallbackEnv->SetCharArrayRegion(jbis_dev_ids.get(), 0, bis_dev_ids.size(),
                                     reinterpret_cast<const jchar*>(bis_dev_ids.data()));

    sCallbackEnv->CallVoidMethod(sBroadcastSinkCallbacksObj, method_onSinkDbigStatusChanged,
                                 (jint)dbig_handle, (jint)status, (jint)dev_id, jname.get(),
                                 (jint)num_bis, jbis_dev_ids.get(), (jint)broadcast_features);
  }
};

static BroadcastSinkCallbacksImpl sBroadcastSinkCallbacks;

static void BroadcastSinkInitNative(JNIEnv* env, jobject object, jint maxSourceCapacity) {
  std::unique_lock<std::shared_timed_mutex> interface_lock(sBroadcastSinkInterfaceMutex);
  std::unique_lock<std::shared_timed_mutex> callbacks_lock(sBroadcastSinkCallbacksMutex);

  const bt_interface_t* btInf = getBluetoothInterface();
  if (btInf == nullptr) {
    log::error("Bluetooth module is not loaded");
    return;
  }

  if (sBroadcastSinkCallbacksObj != nullptr) {
    log::info("Cleaning up Broadcast Sink callback object");
    env->DeleteGlobalRef(sBroadcastSinkCallbacksObj);
    sBroadcastSinkCallbacksObj = nullptr;
  }

  if ((sBroadcastSinkCallbacksObj = env->NewGlobalRef(object)) == nullptr) {
    log::error("Failed to allocate Global Ref for Broadcast Sink Callbacks");
    return;
  }

  sBroadcastSinkInterface = (BroadcastSinkInterface*)btInf->get_profile_interface(
      BT_PROFILE_LE_AUDIO_BROADCAST_SINK_ID);
  if (sBroadcastSinkInterface == nullptr) {
    log::error("Failed to get Broadcast Sink interface");
    return;
  }

  log::info("Initializing Broadcast Sink with maxSourceCapacity={}", maxSourceCapacity);
  sBroadcastSinkInterface->Initialize(&sBroadcastSinkCallbacks, maxSourceCapacity);
}

static void BroadcastSinkCleanupNative(JNIEnv* env, jobject /* object */) {
  std::unique_lock<std::shared_timed_mutex> interface_lock(sBroadcastSinkInterfaceMutex);
  std::unique_lock<std::shared_timed_mutex> callbacks_lock(sBroadcastSinkCallbacksMutex);

  if (sBroadcastSinkInterface != nullptr) {
    sBroadcastSinkInterface->Cleanup();
    sBroadcastSinkInterface = nullptr;
  }

  if (sBroadcastSinkCallbacksObj != nullptr) {
    env->DeleteGlobalRef(sBroadcastSinkCallbacksObj);
    sBroadcastSinkCallbacksObj = nullptr;
  }
}

// Add source - initiates PA sync to a broadcast source
static void BroadcastSinkAddSourceNative(JNIEnv* env, jobject /* object */,
                                         jstring address, jint addressType,
                                         jint advSid, jint broadcastId, jint rssi,
                                         jstring broadcastName, jboolean isPublic,
                                         jbyteArray publicMetadata, jint publicFeatures) {
  log::info("");
  std::shared_lock<std::shared_timed_mutex> lock(sBroadcastSinkInterfaceMutex);
  if (!sBroadcastSinkInterface) {
    log::error("Broadcast Sink interface not initialized");
    return;
  }

  const char* c_address = env->GetStringUTFChars(address, NULL);
  if (!c_address) {
    log::error("Failed to get address string");
    return;
  }

  RawAddress addr;
  RawAddress::FromString(std::string(c_address), addr);
  env->ReleaseStringUTFChars(address, c_address);

  const char* broadcast_name = nullptr;
  if (broadcastName) {
    broadcast_name = env->GetStringUTFChars(broadcastName, nullptr);
  }

  bool is_public = (isPublic == JNI_TRUE);

  // Extract public metadata if provided
  std::vector<uint8_t> public_metadata;
  if (publicMetadata != nullptr) {
    jsize metadata_len = env->GetArrayLength(publicMetadata);
    if (metadata_len > 0) {
      public_metadata.resize(metadata_len);
      env->GetByteArrayRegion(publicMetadata, 0, metadata_len,
                             reinterpret_cast<jbyte*>(public_metadata.data()));
    }
  }

  log::info("address={}, addressType={}, advSid={}, broadcastId={}, rssi={}, broadcastName={}, isPublic={}, publicMetadata={} bytes, publicFeatures=0x{:x}",
            addr.ToRedactedStringForLogging(), addressType, advSid, broadcastId, rssi,
            broadcast_name ? broadcast_name : "null", is_public, public_metadata.size(), publicFeatures);

  sBroadcastSinkInterface->AddSource(addr, addressType, advSid, broadcastId, rssi,
                                     broadcast_name ? broadcast_name : "", is_public,
                                     public_metadata, static_cast<uint8_t>(publicFeatures));

  if (broadcast_name) {
    env->ReleaseStringUTFChars(broadcastName, broadcast_name);
  }
}

// Join enhanced source - initiates BIG sync for an enhanced broadcast broadcast source
// with bidirectional (RX + TX) ISO data paths.
// Should be called after receiving the onEnhancedSourceDetected() callback.
static void BroadcastSinkStartEnhancedBroadcastSinkNative(JNIEnv* env, jobject /* object */,
                                                  jint broadcastId, jbyteArray broadcastCode) {
  // Enhanced broadcast always syncs to all BISes — no BIS selection needed.
  log::info("startEnhancedBroadcastSinkNative: broadcastId=0x{:08x}", broadcastId);

  if (!sBroadcastSinkInterface) {
    log::error("sBroadcastSinkInterface is null");
    return;
  }

  std::optional<std::array<uint8_t, 16>> code = std::nullopt;
  if (broadcastCode != nullptr) {
    jsize len = env->GetArrayLength(broadcastCode);
    if (len == 16) {
      std::array<uint8_t, 16> arr{};
      env->GetByteArrayRegion(broadcastCode, 0, 16,
                              reinterpret_cast<jbyte*>(arr.data()));
      code = arr;
    }
  }

  sBroadcastSinkInterface->StartEnhancedBroadcastSink(
      (bluetooth::le_audio::BroadcastId)broadcastId, code);
}

// Leave source - terminates BIG sync but keeps PA sync
static void BroadcastSinkStopEnhancedBroadcastSinkNative(JNIEnv* env, jobject /* object */, jint broadcastId) {
  log::info("");
  std::shared_lock<std::shared_timed_mutex> lock(sBroadcastSinkInterfaceMutex);
  if (!sBroadcastSinkInterface) {
    log::error("Broadcast Sink interface not initialized");
    return;
  }

  sBroadcastSinkInterface->StopEnhancedBroadcastSink((bluetooth::le_audio::BroadcastId)broadcastId);
}

// Remove source - terminates both BIG and PA sync
static void BroadcastSinkRemoveSourceNative(JNIEnv* env, jobject /* object */, jint broadcastId) {
  log::info("");
  std::shared_lock<std::shared_timed_mutex> lock(sBroadcastSinkInterfaceMutex);
  if (!sBroadcastSinkInterface) {
    log::error("Broadcast Sink interface not initialized");
    return;
  }

  sBroadcastSinkInterface->RemoveSource((bluetooth::le_audio::BroadcastId)broadcastId);
}

// Destroy source - cleanup all resources for a broadcast source
static void BroadcastSinkDestroySourceNative(JNIEnv* env, jobject /* object */, jint broadcastId) {
  log::info("");
  std::shared_lock<std::shared_timed_mutex> lock(sBroadcastSinkInterfaceMutex);
  if (!sBroadcastSinkInterface) {
    log::error("Broadcast Sink interface not initialized");
    return;
  }

  sBroadcastSinkInterface->DestroySource((bluetooth::le_audio::BroadcastId)broadcastId);
}

// Notify that source metadata has changed
static void BroadcastSinkReadSupportedStatesForSinkNative(JNIEnv* /* env */, jobject /* object */) {
  log::info("");
  std::shared_lock<std::shared_timed_mutex> lock(sBroadcastSinkInterfaceMutex);
  if (!sBroadcastSinkInterface) {
    log::error("Broadcast Sink interface not initialized");
    return;
  }
  sBroadcastSinkInterface->readSupportedStatesForSink();
}

static jint BroadcastSinkGetEnhancedBroadcastSinkCapNative(JNIEnv* /* env */, jobject /* object */) {
  std::shared_lock<std::shared_timed_mutex> lock(sBroadcastSinkInterfaceMutex);
  if (!sBroadcastSinkInterface) {
    return -1;
  }
  return (jint)sBroadcastSinkInterface->getEnhancedBroadcastSinkCap();
}

static void BroadcastSinkSetEnhancedDbigParamsNative(JNIEnv* env, jobject /* object */,
                                                     jbyteArray dbigParams) {
  log::info("");
  std::shared_lock<std::shared_timed_mutex> lock(sBroadcastSinkInterfaceMutex);
  if (!sBroadcastSinkInterface) {
    log::error("Broadcast Sink interface not initialized");
    return;
  }
  if (dbigParams == nullptr) {
    log::error("dbigParams is null");
    return;
  }

  jsize len = env->GetArrayLength(dbigParams);
  jbyte* params = env->GetByteArrayElements(dbigParams, nullptr);
  if (!params) {
    log::error("Failed to get DBIG params array elements");
    return;
  }

  std::vector<uint8_t> dbig_params_vec(params, params + len);
  env->ReleaseByteArrayElements(dbigParams, params, JNI_ABORT);

  sBroadcastSinkInterface->setEnhancedDbigParams(dbig_params_vec);
}

static void BroadcastSinkSourcePublicMetadataChangedNative(JNIEnv* env, jobject /* object */,
                                                          jint broadcastId, jstring broadcastName,
                                                          jbyteArray publicMetadata) {
  log::info("");
  std::shared_lock<std::shared_timed_mutex> lock(sBroadcastSinkInterfaceMutex);
  if (!sBroadcastSinkInterface) {
    log::error("Broadcast Sink interface not initialized");
    return;
  }

  const char* broadcast_name = nullptr;
  if (broadcastName) {
    broadcast_name = env->GetStringUTFChars(broadcastName, nullptr);
  }

  // Extract public metadata if provided
  std::vector<uint8_t> public_metadata;
  if (publicMetadata != nullptr) {
    jsize metadata_len = env->GetArrayLength(publicMetadata);
    if (metadata_len > 0) {
      public_metadata.resize(metadata_len);
      env->GetByteArrayRegion(publicMetadata, 0, metadata_len,
                             reinterpret_cast<jbyte*>(public_metadata.data()));
    }
  }

  log::info("broadcastId={}, broadcastName={}, publicMetadata={} bytes",
            broadcastId, broadcast_name ? broadcast_name : "null", public_metadata.size());

  sBroadcastSinkInterface->SourcePublicMetadataChanged((bluetooth::le_audio::BroadcastId)broadcastId,
                                                 broadcast_name ? broadcast_name : "",
                                                 public_metadata);

  if (broadcast_name) {
    env->ReleaseStringUTFChars(broadcastName, broadcast_name);
  }
}

static int register_com_android_bluetooth_le_audio_broadcast_sink(JNIEnv* env) {
  const JNINativeMethod methods[] = {
          {"initializeNative", "(I)V", (void*)BroadcastSinkInitNative},
          {"cleanupNative", "()V", (void*)BroadcastSinkCleanupNative},
          {"addSourceNative", "(Ljava/lang/String;IIIILjava/lang/String;Z[BI)V",
           (void*)BroadcastSinkAddSourceNative},
          {"startEnhancedBroadcastSinkNative", "(I[B)V", (void*)BroadcastSinkStartEnhancedBroadcastSinkNative},
          {"stopEnhancedBroadcastSinkNative", "(I)V", (void*)BroadcastSinkStopEnhancedBroadcastSinkNative},
          {"removeSourceNative", "(I)V", (void*)BroadcastSinkRemoveSourceNative},
          {"destroySourceNative", "(I)V", (void*)BroadcastSinkDestroySourceNative},
          {"readSupportedStatesForSinkNative", "()V", (void*)BroadcastSinkReadSupportedStatesForSinkNative},
          {"getEnhancedBroadcastSinkCapNative", "()I", (void*)BroadcastSinkGetEnhancedBroadcastSinkCapNative},
          {"setEnhancedDbigParamsNative", "([B)V", (void*)BroadcastSinkSetEnhancedDbigParamsNative},
          {"sourcePublicMetadataChangedNative", "(ILjava/lang/String;[B)V",
           (void*)BroadcastSinkSourcePublicMetadataChangedNative},
  };

  const int result = REGISTER_NATIVE_METHODS(
          env, "com/android/bluetooth/le_audio/LeAudioBroadcastSinkNativeInterface", methods);
  if (result != 0) {
    return result;
  }

  const JNIJavaMethod javaMethods[] = {
          /* Standard broadcast sink callbacks */
          {"onSourceAddFailed", "(II)V", &method_onSourceAddFailed},
          {"onSinkStartFailed", "(II)V", &method_onSinkStartFailed},
          {"onSinkStopFailed", "(II)V", &method_onSinkStopFailed},
          {"onSourceRemoveFailed", "(II)V", &method_onSourceRemoveFailed},
          {"onSourceDestroyed", "(II)V", &method_onSourceDestroyed},
          {"onSourceMetadataChanged", "(ILandroid/bluetooth/BluetoothLeBroadcastMetadata;)V",
           &method_onSourceMetadataChanged},
          {"onBroadcastSinkAudioSessionCreated", "(Z)V",
           &method_onBroadcastSinkAudioSessionCreated},
          {"onBroadcastSinkStateChanged", "(II)V", &method_onBroadcastSinkStateChanged},
          {"onEnhancedSourceDetected", "(II)V", &method_onEnhancedSourceDetected},
          {"onBigSyncCreated", "(II[I)V", &method_onBigSyncCreated},
          {"onBigSyncLost", "(III)V", &method_onBigSyncLost},
          {"onBigSyncTerminated", "(III)V", &method_onBigSyncTerminated},
          {"onDbigStatusChanged", "(III[BI[CI)V", &method_onSinkDbigStatusChanged},
  };
  GET_JAVA_METHODS(env, "com/android/bluetooth/le_audio/LeAudioBroadcastSinkNativeInterface",
                   javaMethods);

  return 0;
}

int register_com_android_bluetooth_le_audio(JNIEnv* env) {
  const JNINativeMethod methods[] = {
          {"initNative", "([Landroid/bluetooth/BluetoothLeAudioCodecConfig;)V", (void*)initNative},
          {"cleanupNative", "()V", (void*)cleanupNative},
          {"connectLeAudioNative", "([B)Z", (void*)connectLeAudioNative},
          {"disconnectLeAudioNative", "([B)Z", (void*)disconnectLeAudioNative},
          {"setEnableStateNative", "([BZ)Z", (void*)setEnableStateNative},
          {"groupAddNodeNative", "(I[B)Z", (void*)groupAddNodeNative},
          {"groupRemoveNodeNative", "(I[B)Z", (void*)groupRemoveNodeNative},
          {"groupSetActiveNative", "(I)V", (void*)groupSetActiveNative},
          {"setCodecConfigPreferenceNative",
           "(ILandroid/bluetooth/BluetoothLeAudioCodecConfig;"
           "Landroid/bluetooth/BluetoothLeAudioCodecConfig;)V",
           (void*)setCodecConfigPreferenceNative},
          {"setCcidInformationNative", "(II)V", (void*)setCcidInformationNative},
          {"setInCallNative", "(Z)V", (void*)setInCallNative},
          {"setUnicastMonitorModeNative", "(IZ)V", (void*)setUnicastMonitorModeNative},
          {"sendAudioProfilePreferencesNative", "(IZZ)V", (void*)sendAudioProfilePreferencesNative},
          {"setGroupAllowedContextMaskNative", "(III)V", (void*)setGroupAllowedContextMaskNative},
  };

  const int result = REGISTER_NATIVE_METHODS(
          env, "com/android/bluetooth/le_audio/LeAudioNativeInterface", methods);
  if (result != 0) {
    return result;
  }

  const JNIJavaMethod javaMethods[] = {
          {"onGroupStatus", "(II)V", &method_onGroupStatus},
          {"onGroupNodeStatus", "([BII)V", &method_onGroupNodeStatus},
          {"onAudioConf", "(IIIII)V", &method_onAudioConf},
          {"onSinkAudioLocationAvailable", "([BI)V", &method_onSinkAudioLocationAvailable},
          {"onInitialized", "()V", &method_onInitialized, true},
          {"onConnectionStateChanged", "(I[B)V", &method_onConnectionStateChanged},
          {"onAudioLocalCodecCapabilities",
           "([Landroid/bluetooth/BluetoothLeAudioCodecConfig;"
           "[Landroid/bluetooth/BluetoothLeAudioCodecConfig;)V",
           &method_onAudioLocalCodecCapabilities},
          {"onAudioGroupCurrentCodecConf",
           "(ILandroid/bluetooth/BluetoothLeAudioCodecConfig;"
           "Landroid/bluetooth/BluetoothLeAudioCodecConfig;)V",
           &method_onAudioGroupCurrentCodecConf},
          {"onAudioGroupSelectableCodecConf",
           "(I[Landroid/bluetooth/BluetoothLeAudioCodecConfig;"
           "[Landroid/bluetooth/BluetoothLeAudioCodecConfig;)V",
           &method_onAudioGroupSelectableCodecConf},
          {"OnMetadataUpdate", "(I)V", &method_OnMetadataUpdate},
          {"onHealthBasedRecommendationAction", "([BI)V",
           &method_onHealthBasedRecommendationAction},
          {"onHealthBasedGroupRecommendationAction", "(II)V",
           &method_onHealthBasedGroupRecommendationAction},
          {"onUnicastMonitorModeStatus", "(II)V", &method_onUnicastMonitorModeStatus},
          {"onGroupStreamStatus", "(II)V", &method_onGroupStreamStatus},
  };
  GET_JAVA_METHODS(env, "com/android/bluetooth/le_audio/LeAudioNativeInterface", javaMethods);

  const JNIJavaMethod javaLeAudioCodecMethods[] = {
          {"<init>", "(IIIIIIIIIJJJJ)V",
           &android_bluetooth_BluetoothLeAudioCodecConfig.constructor},
          {"getCodecType", "()I", &android_bluetooth_BluetoothLeAudioCodecConfig.getCodecType},
          {"getSampleRate", "()I", &android_bluetooth_BluetoothLeAudioCodecConfig.getSampleRate},
          {"getBitsPerSample", "()I",
           &android_bluetooth_BluetoothLeAudioCodecConfig.getBitsPerSample},
          {"getChannelCount", "()I",
           &android_bluetooth_BluetoothLeAudioCodecConfig.getChannelCount},
          {"getFrameDuration", "()I",
           &android_bluetooth_BluetoothLeAudioCodecConfig.getFrameDuration},
          {"getOctetsPerFrame", "()I",
           &android_bluetooth_BluetoothLeAudioCodecConfig.getOctetsPerFrame},
          {"getCodecPriority", "()I",
           &android_bluetooth_BluetoothLeAudioCodecConfig.getCodecPriority},
  };
  GET_JAVA_METHODS(env, "android/bluetooth/BluetoothLeAudioCodecConfig", javaLeAudioCodecMethods);

  // Register broadcaster
  int broadcaster_result = register_com_android_bluetooth_le_audio_broadcaster(env);
  if (broadcaster_result != 0) {
    return broadcaster_result;
  }

  // Register broadcast sink
  return register_com_android_bluetooth_le_audio_broadcast_sink(env);
}
}  // namespace android
