/******************************************************************************
 * Copyright (C) 2013,2016-2017 The Linux Foundation. All rights reserved
 * Not a Contribution.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted (subject to the limitations in the
 * disclaimer below) provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *
 * * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *
 * * Neither the name of The Linux Foundation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE
 * GRANTED BY THIS LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT
 * HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Copyright (C) 2012 The Android Open Source Project
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
 ******************************************************************************/

/******************************************************************************
 * Changes from Qualcomm Technologies, Inc. are provided under the following license:
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 *
 ******************************************************************************/

#define LOG_TAG "BluetoothVendorJni"

#include <cutils/properties.h>
#include <hardware/bt_vendor.h>

#include <shared_mutex>
#include <vector>

#include "com_android_bluetooth.h"
#include "utils/Log.h"

namespace android {

static jmethodID method_adapterPropertyChangedCallback;
static jmethodID method_ssrCleanupCallback;

static btvendor_interface_t* sBluetoothVendorInterface = NULL;
static jobject mCallbacksObj = NULL;

static std::shared_timed_mutex interface_mutex;

static int get_properties(int num_properties, bt_vendor_property_t* properties, jintArray* types,
                          jobjectArray* props) {
  CallbackEnv sCallbackEnv(__func__);
  if (!sCallbackEnv.valid()) {
    return -1;
  }
  for (int i = 0; i < num_properties; i++) {
    ScopedLocalRef<jbyteArray> propVal(sCallbackEnv.get(),
                                       sCallbackEnv->NewByteArray(properties[i].len));
    if (!propVal.get()) {
      log::error("Error while allocation of array in {}", __func__);
      return -1;
    }

    sCallbackEnv->SetByteArrayRegion(propVal.get(), 0, properties[i].len,
                                     (jbyte*)properties[i].val);
    sCallbackEnv->SetObjectArrayElement(*props, i, propVal.get());
    sCallbackEnv->SetIntArrayRegion(*types, i, 1, (jint*)&properties[i].type);
  }
  return 0;
}

static void ssr_cleanup_callback(void) {
  log::info("{}", __FUNCTION__);
  CallbackEnv sCallbackEnv(__func__);

  if (!sCallbackEnv.valid()) {
    return;
  }

  sCallbackEnv->CallVoidMethod(mCallbacksObj, method_ssrCleanupCallback);
}

static void adapter_vendor_properties_callback(bt_status_t status, int num_properties,
                                               bt_vendor_property_t* properties) {
  CallbackEnv sCallbackEnv(__func__);
  if (!sCallbackEnv.valid()) {
    return;
  }
  log::error("{}: Status is: {}, Properties: {}", __func__, status, num_properties);
  if (status != BT_STATUS_SUCCESS) {
    log::error("{}: Status {} is incorrect", __func__, status);
    return;
  }
  ScopedLocalRef<jbyteArray> val(sCallbackEnv.get(),
                                 (jbyteArray)sCallbackEnv->NewByteArray(num_properties));
  if (!val.get()) {
    log::error("{}: Error allocating byteArray", __func__);
    return;
  }
  ScopedLocalRef<jclass> mclass(sCallbackEnv.get(), sCallbackEnv->GetObjectClass(val.get()));
  ScopedLocalRef<jobjectArray> props(
          sCallbackEnv.get(), sCallbackEnv->NewObjectArray(num_properties, mclass.get(), NULL));
  if (!props.get()) {
    log::error("{}: Error allocating object Array for properties", __func__);
    return;
  }
  ScopedLocalRef<jintArray> types(sCallbackEnv.get(),
                                  (jintArray)sCallbackEnv->NewIntArray(num_properties));
  if (!types.get()) {
    log::error("{}: Error allocating int Array for values", __func__);
    return;
  }
  jintArray typesPtr = types.get();
  jobjectArray propsPtr = props.get();
  if (get_properties(num_properties, properties, &typesPtr, &propsPtr) < 0) {
    return;
  }
  sCallbackEnv->CallVoidMethod(mCallbacksObj, method_adapterPropertyChangedCallback, types.get(),
                               props.get());
}

static btvendor_callbacks_t sBluetoothVendorCallbacks = {
        sizeof(sBluetoothVendorCallbacks),
        adapter_vendor_properties_callback,
        ssr_cleanup_callback,
};

static void classInitNative(JNIEnv* env, jclass clazz) {
  method_adapterPropertyChangedCallback =
          env->GetMethodID(clazz, "adapterPropertyChangedCallback", "([I[[B)V");
  method_ssrCleanupCallback = env->GetMethodID(clazz, "ssr_cleanup_callback", "()V");
}

static void initNative(JNIEnv* env, jobject object) {
  const bt_interface_t* btInf;
  bt_status_t status;

  std::unique_lock<std::shared_timed_mutex> interface_lock(interface_mutex);

  if ((btInf = getBluetoothInterface()) == NULL) {
    log::error("Bluetooth module is not loaded");
    return;
  }

  if (mCallbacksObj != NULL) {
    log::warn("Cleaning up Bluetooth Vendor callback object");
    env->DeleteGlobalRef(mCallbacksObj);
    mCallbacksObj = NULL;
  }

  if ((sBluetoothVendorInterface =
               (btvendor_interface_t*)btInf->get_profile_interface(BT_PROFILE_VENDOR_ID)) == NULL) {
    log::error("Failed to get Bluetooth Vendor Interface");
    return;
  }

  if ((status = sBluetoothVendorInterface->init(&sBluetoothVendorCallbacks)) != BT_STATUS_SUCCESS) {
    log::error("Failed to initialize Bluetooth Vendor, status: {}", status);
    sBluetoothVendorInterface = NULL;
    return;
  }
  mCallbacksObj = env->NewGlobalRef(object);
}

static void cleanupNative(JNIEnv* env, jobject object) {
  const bt_interface_t* btInf;

  std::unique_lock<std::shared_timed_mutex> interface_lock(interface_mutex);

  if ((btInf = getBluetoothInterface()) == NULL) {
    log::error("Bluetooth module is not loaded");
    return;
  }

  if (sBluetoothVendorInterface != NULL) {
    log::warn("Cleaning up Bluetooth Vendor Interface...");
    sBluetoothVendorInterface->cleanup();
    sBluetoothVendorInterface = NULL;
  }

  if (mCallbacksObj != NULL) {
    log::warn("Cleaning up Bluetooth Vendor callback object");
    env->DeleteGlobalRef(mCallbacksObj);
    mCallbacksObj = NULL;
  }
}

static bool informTimeoutToHidlNative(JNIEnv* env, jobject obj) {
  log::info("{}", __FUNCTION__);

  if (1) {
    // todo need to inform hidl
    log::error("{}: Failed to inform to HIDL about timeout", __FUNCTION__);
  }

  return JNI_TRUE;
}

static bool setWifiStateNative(JNIEnv* env, jobject obj, jboolean status) {
  log::info("{}", __FUNCTION__);

  jboolean result = JNI_FALSE;
  if (!sBluetoothVendorInterface) {
    return result;
  }

  sBluetoothVendorInterface->set_wifi_state(status);
  return JNI_TRUE;
}

static bool setPowerBackoffNative(JNIEnv* env, jobject obj, jboolean status) {
  log::info("{}", __FUNCTION__);

  jboolean result = JNI_FALSE;
  if (!sBluetoothVendorInterface) {
    return result;
  }

  sBluetoothVendorInterface->set_Power_back_off_state(status);
  return JNI_TRUE;
}

#ifdef TARGET_QCOM_IOT_BT_EXT
static bool addBleKeyNative(JNIEnv* env, jobject obj, jbyteArray address, jbyteArray keyArray, jint keyType, jboolean add) {
  log::info("{}", __FUNCTION__);

  jboolean result = JNI_FALSE;
  if (!sBluetoothVendorInterface) {
    return result;
  }

  jbyte* addr = env->GetByteArrayElements(address, NULL);
  if (addr == NULL) {
    jniThrowIOException(env, EINVAL);
    return JNI_FALSE;
  }

  jbyte* keyPtr = env->GetByteArrayElements(keyArray, NULL);
  if (keyPtr == NULL) {
    jniThrowIOException(env, EINVAL);
    env->ReleaseByteArrayElements(address, addr, 0);
    return JNI_FALSE;
  }

  jint key_len = env->GetArrayLength(keyArray);

  int ret = sBluetoothVendorInterface->add_ble_key(reinterpret_cast<RawAddress*>(addr), key_len, reinterpret_cast<uint8_t*>(keyPtr), keyType, add);

  env->ReleaseByteArrayElements(address, addr, 0);
  env->ReleaseByteArrayElements(keyArray, keyPtr, 0);

  return (ret == BT_STATUS_SUCCESS) ? JNI_TRUE : JNI_FALSE;
}

static bool setEncryptionNative(JNIEnv* env, jobject obj, jbyteArray address, jint transport, jint sec_act) {
  log::info("{}", __FUNCTION__);

  jboolean result = JNI_FALSE;
  if (!sBluetoothVendorInterface) {
    return result;
  }

  jbyte* addr = env->GetByteArrayElements(address, nullptr);
  if (addr == nullptr) {
    jniThrowIOException(env, EINVAL);
    return JNI_FALSE;
  }
  RawAddress addr_obj = {};
  addr_obj.FromOctets(reinterpret_cast<uint8_t*>(addr));
  env->ReleaseByteArrayElements(address, addr, 0);

  sBluetoothVendorInterface->set_encryption(addr_obj, (tBT_TRANSPORT)transport, sec_act);
  return JNI_TRUE;
}
#endif

static JNINativeMethod sMethods[] = {
        {"classInitNative", "()V", (void*)classInitNative},
        {"initNative", "()V", (void*)initNative},
        {"cleanupNative", "()V", (void*)cleanupNative},
        {"setWifiStateNative", "(Z)V", (void*)setWifiStateNative},
        {"setPowerBackoffNative", "(Z)V", (void*)setPowerBackoffNative},
        {"informTimeoutToHidlNative", "()V", (void*)informTimeoutToHidlNative},
#ifdef TARGET_QCOM_IOT_BT_EXT
        {"addBleKeyNative", "([B[BIZ)Z", (void*)addBleKeyNative},
        {"setEncryptionNative", "([BII)Z", (void*)setEncryptionNative},
#endif
};

int register_com_android_bluetooth_btservice_vendor(JNIEnv* env) {
  log::error("{}:", __FUNCTION__);
  return jniRegisterNativeMethods(env, "com/android/bluetooth/btservice/Vendor", sMethods,
                                  NELEM(sMethods));
}

} /* namespace android */
