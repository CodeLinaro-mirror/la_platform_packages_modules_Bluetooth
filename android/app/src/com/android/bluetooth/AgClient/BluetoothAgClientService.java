/*
 * Copyright (c) 2025 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

package com.android.bluetooth.agClient;

import android.app.Service;


import static android.content.pm.PackageManager.FEATURE_WATCH;

import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothHeadsetClientCall;
import android.bluetooth.BluetoothProfile;
import android.bluetooth.BluetoothSinkAudioPolicy;
import android.bluetooth.BluetoothStatusCodes;
import android.bluetooth.BluetoothHeadsetClient;
import com.android.bluetooth.hfp.HeadsetService;
import com.android.bluetooth.hfpclient.HeadsetClientService;
import com.android.bluetooth.hfpclient.HfpClientCall;


import android.annotation.NonNull;
import android.annotation.Nullable;
import android.annotation.RequiresPermission;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothHeadset;


import android.os.Handler;
import android.os.Message;
import android.os.Bundle;
import android.os.HandlerThread;
import android.os.Looper;
import android.util.Log;
import java.util.Objects;
import java.util.List;
import java.util.ArrayList;


import androidx.annotation.VisibleForTesting;

import android.bluetooth.BluetoothProfile;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.net.Uri;
import android.os.Binder;
import android.os.IBinder;



public class BluetoothAgClientService extends Service {
   private static final String TAG = "BluetoothAgClientService";

   private static BluetoothAgClientService sBluetoothAgClientService;
   private boolean mHeadsetAGSco = false;
   private boolean mHeadsetClientSco = false;
   private boolean mHeadsetAGConnection = false;
   private boolean mHeadsetClientConnection = false;
   private boolean mPendingAgSco = false;
   private AgClientHandler mHandler = null;
   private HandlerThread mHandlerThread;
   private boolean mHeadsetConnectFlag = false;
   private BluetoothDevice mActiveAGDevice = null;
   private BluetoothDevice mActiveHFDevice = null;
   //List to maintain HF calls
   private List<BluetoothHeadsetClientCall> mUpdatecurrentHFCalls = new ArrayList<>();

   private static final Object LOCK = new Object();

   private static final int CONNECT_AG_AUDIO = 1;
   private static final int CONNECT_CLIENT_AUDIO= 2;
   private static final int FETCH_CALL_DETAILS = 3;

   private static final int INCOMING_INDICATORS = 1;
   private static final int OUTGOING_INDICATORS = 2;
   private static final int CALL_ACTIVE_INDICATORS = 3;
   private static final int CALL_HELD_INDICATORS = 4;
   private static final int CALL_END_INDICATORS = 5;


   private static final int CALL_STATE_ACTIVE = 0;
   private static final int CALL_STATE_HELD = 1;
   private static final int CALL_STATE_DIALING = 2;
   private static final int CALL_STATE_ALERTING = 3;
   private static final int CALL_STATE_INCOMING = 4;
   private static final int CALL_STATE_IDLE = 6;

   private int mNumActiveCalls = 0;
   private int mNumHeldCalls = 0;
   private int mCallState = CALL_STATE_IDLE;
   private String mRingingAddress = "";
   private String mRingingName = null;
   private boolean mSendFakeIndicatorsDuringSlc = false;
   private int mSendFakeEvent;

   private static final int DEFAULT_RINGING_ADDRESS_TYPE = 128;
   private static final int mRingingAddressType = DEFAULT_RINGING_ADDRESS_TYPE;


   /**
      * Listens to connections and disconnections of bluetooth headsets. We need to save the current
      * bluetooth headset so that we know where to send BluetoothCall updates.
      */
     @VisibleForTesting
     public BluetoothProfile.ServiceListener mProfileListener =
             new BluetoothProfile.ServiceListener() {
                 @Override
                 public void onServiceConnected(int profile, BluetoothProfile proxy) {
                     Log.d(TAG, "onServiceConnected: profile: " + profile);
                     synchronized (LOCK) {
                         if (profile == BluetoothProfile.HEADSET) {
                             /*setBluetoothHeadset(
                                     new BluetoothHeadsetProxy((BluetoothHeadset) proxy));*/
                         }
                     }
                 }

                 @Override
                 public void onServiceDisconnected(int profile) {
                     Log.d(TAG, "onServiceDisconnected: profile: " + profile);
                     synchronized (LOCK) {
                         if (profile == BluetoothProfile.HEADSET) {
                             //setBluetoothHeadset(null);
                         }
                     }
                 }
    };

   public static synchronized BluetoothAgClientService getBluetoothAgClientService() {
      return sBluetoothAgClientService;
   }

   @VisibleForTesting
   public static synchronized void setBluetoothAgClientService(BluetoothAgClientService instance) {
      if (instance == null) {
          Log.e(TAG, "setBluetoothAgClientService() - instance is null");
             return;
         }
         Log.d(TAG, "setBluetoothAgClientService() - set service to " + instance);
         sBluetoothAgClientService = instance;
    }

   @Override
   public void onCreate() {
     Log.d(TAG, "BluetoothAgClientService's on create() is called");
      setBluetoothAgClientService(this);
      if (mHandler == null) {
         Log.w(TAG, "start AgClientHandler thread");
         mHandlerThread = new HandlerThread("AgClientHandler");
         mHandlerThread.start();
         mHandler = new AgClientHandler(mHandlerThread.getLooper());
      }
      if (!mHeadsetConnectFlag) {
           Log.d(TAG, "onStartCommand(): getProfileProxy");
           BluetoothAdapter.getDefaultAdapter()
                   .getProfileProxy(this, mProfileListener, BluetoothProfile.HEADSET);
           mHeadsetConnectFlag = true;
      }
   }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        Log.d(TAG, "BluetoothAgClientService's on onStartCommand() is called");
        sBluetoothAgClientService = this;
        if (!mHeadsetConnectFlag) {
           Log.d(TAG, "onStartCommand(): getProfileProxy");
           BluetoothAdapter.getDefaultAdapter()
                   .getProfileProxy(this, mProfileListener, BluetoothProfile.HEADSET);
           mHeadsetConnectFlag = true;
      }
        return START_NOT_STICKY;
    }

   @Override
   public IBinder onBind(Intent intent) {
       Log.d(TAG, "onBind()");
       //IBinder binder = super.onBind(intent);
       return null;
   }

   @Override
   public boolean onUnbind(Intent intent) {
       Log.d(TAG, "onUnbind() - calling cleanup");
       //cleanup();
       return super.onUnbind(intent);
   }

   @Override
   public void onDestroy() {
     Log.d(TAG, "BluetoothAgClientService's on Destroy() is called");
     if (mHandler != null) {
        // Shut down the thread
        Log.d(TAG, "cleanup AgClientHandler");
        mHandler.removeCallbacksAndMessages(null);
        Looper looper = mHandler.getLooper();
        if (looper != null) {
           looper.quit();
        }
           mHandler = null;
     }
     if (mHandlerThread != null) {
        mHandlerThread.quit();
        mHandlerThread = null;
     }
     sBluetoothAgClientService = null;
     mCallState = CALL_STATE_IDLE;
     mRingingName = null;
     mNumActiveCalls = 0;
     mNumHeldCalls = 0;
     mActiveAGDevice =  null;
     mActiveHFDevice = null;
     mPendingAgSco = false;
     mHeadsetAGConnection = false;
     mHeadsetAGSco = false;
     mHeadsetClientSco = false;
     mUpdatecurrentHFCalls.clear();
   }

   class AgClientHandler extends Handler {
       AgClientHandler(Looper looper) {
             super(looper);
         }
       @Override
        public void handleMessage(Message msg) {
           Log.d(TAG, "handleMessage(): msg.what: " + msg.what);
           BluetoothDevice device = (BluetoothDevice) msg.obj;
           switch(msg.what) {
             case CONNECT_AG_AUDIO: 
                 if (mHeadsetClientSco) {
                    //Processing AG connect audio as CLient SCO is already connected.
                    Log.d(TAG, "handleMessage():  connect Audio for AG " + msg.what);
                    if (HeadsetService.getHeadsetService() != null) {
                       Log.d(TAG, "Bluetooth headset is not null ");
                       HeadsetService.getHeadsetService().connectAudio();
                    } else {
                       Log.d(TAG, "Bluetooth headset is null " + msg.what);
                    }
                    mPendingAgSco = false;
                 } else {
                    List<BluetoothHeadsetClientCall> mcurrentHFCalls = getCurrentHfCalls(mActiveHFDevice);
                    if (mHeadsetClientConnection && mcurrentHFCalls != null) {
                     Log.d(TAG, "Client sco needs to be created before ");
                     //need Client SCO to connect first. Postponing the AG sco connection.
                     mPendingAgSco = true;
                   } else  {
                     Log.d(TAG, "No need of Headset client sco. since no calls are present in HF");
                     mPendingAgSco = false;
                     if (HeadsetService.getHeadsetService() != null) {
                        HeadsetService.getHeadsetService().connectAudio();
                     }
                   }
                 }
                 break;
             case CONNECT_CLIENT_AUDIO:
                if (mHeadsetAGSco) {
                    if (!mHeadsetClientSco) {
                      //need to disconnect the AG Sco if its connected
                      Log.d(TAG, "need to disconnect the AG Sco if its connected");
                    } else {
                       Log.d(TAG, "Client Sco is already connected. Redundant SCO connect request");
                     //Client Sco is already connected. Redundant SCO connect request
                    }
                } else {
                   //Process SCO connection for client normally.
                   HeadsetClientService mHeadsetClientService = HeadsetClientService.getHeadsetClientService();
                   mHeadsetClientService.connectAudioFromAgClient(device);
                }
               break;
             case FETCH_CALL_DETAILS: 
                if (mSendFakeIndicatorsDuringSlc) {
                   Log.d(TAG, "FETCH_CALL_DETAILS event" + (int) msg.arg1);
                   int event = msg.arg1;
                   Log.d(TAG, "FETCH_CALL_DETAILS event" + event);
                   dispatchFakeCallIndicators(device, event);
                   mSendFakeIndicatorsDuringSlc = false;
                   mSendFakeEvent = 0;
                }
               break;
           }
        }
   };

   public void UpdateProfileConnectionStatus(
                  BluetoothDevice device,  int profile, int fromState, int toState) {
       Log.d(TAG, "UpdateProfileConnectionStatus");
       if (profile == BluetoothProfile.HEADSET) {
          Log.d(TAG, "UpdateProfileConnectionStatus for headset Profile: fromstate:" + fromState + "tostate:" + toState);
          if (fromState != BluetoothProfile.STATE_CONNECTED
                  && toState == BluetoothProfile.STATE_CONNECTED) {
             Log.d(TAG, "Headset is connected");
             mHeadsetAGConnection = true;
          } else {
             mActiveAGDevice = null;
             mHeadsetAGConnection = false;
          }
       } else if (profile == BluetoothProfile.HEADSET_CLIENT) {
          Log.d(TAG, "UpdateProfileConnectionStatus for headset Client Profile");
          if (fromState != BluetoothProfile.STATE_CONNECTED
                  && toState == BluetoothProfile.STATE_CONNECTED) {
             Log.d(TAG, "HEADSET_CLIENT is connected");
             mHeadsetClientConnection = true;
             mActiveHFDevice = device;
          } else if ((fromState == BluetoothProfile.STATE_CONNECTED
                     && toState == BluetoothProfile.STATE_DISCONNECTED) || 
                     (fromState == BluetoothProfile.STATE_CONNECTED
                     && toState == BluetoothProfile.STATE_DISCONNECTING)) {
                  Log.d(TAG, "Headset CLient sco disconnection");
                  if (mHeadsetAGSco || mHeadsetClientSco) {
                      Log.d(TAG, "Ongoing Sco during disconnection. Send fake call indicators");
                      dispatchFakeCallIndicators(mActiveHFDevice, CALL_END_INDICATORS);
                      if (HeadsetService.getHeadsetService() != null) {
                        Log.d(TAG, "send disconnect audio for AG if HF client device is disconnected");
                         HeadsetService.getHeadsetService().disconnectAudio();
                        //printUnused(disconnectResult, null);
                      }
                  }
                   mActiveHFDevice = null;
                   mHeadsetClientConnection = false;
          }
       }
       HeadsetService mHsService = HeadsetService.getHeadsetService();
       if (mHeadsetAGConnection && mHeadsetClientConnection) {
          if (mHsService != null) {
              mHsService.SetAGClientConnectionStatus(true);
          }
       }
   }

   public void UpdateProfileAudioConnectionStatus(
                  BluetoothDevice device,  int profile, int fromState, int toState) {
       Log.d(TAG, "UpdateProfileAudioConnectionStatus");

       if (profile == BluetoothProfile.HEADSET) {
          Log.d(TAG, "UpdateProfileAudioConnectionStatus for headset Profile: fromstate:"
                                                 + fromState + "tostate:" + toState);

          if (fromState != BluetoothHeadset.STATE_AUDIO_CONNECTED
                  && toState == BluetoothHeadset.STATE_AUDIO_CONNECTED) {
             Log.d(TAG, "Headset SCO is connected");
             mHeadsetAGSco = true;
             mActiveAGDevice = device;
          } else {
             Log.d(TAG, "Headset SCO is disconnected");
             mHeadsetAGSco = false;
          }
       } else if (profile == BluetoothProfile.HEADSET_CLIENT) {
          Log.d(TAG, "UpdateProfileAudioConnectionStatus for headset Client Profile: fromstate:"
                                                 + fromState + "tostate:" + toState);

          if (fromState != BluetoothHeadsetClient.STATE_AUDIO_CONNECTED
                  && toState == BluetoothHeadsetClient.STATE_AUDIO_CONNECTED) {
             Log.d(TAG, "HEADSET_CLIENT SCO is connected");
             mHeadsetClientSco = true;
             mActiveHFDevice = device;
             HeadsetService mHsService = HeadsetService.getHeadsetService();
             if (mHsService != null) {
                 mHsService.SetHfClientScoConnectionStatus(true);
             }
             if (mPendingAgSco) {
                Message msg = mHandler.obtainMessage();
                msg.what = CONNECT_AG_AUDIO;
                mHandler.sendMessage(msg);
             }
             if (mSendFakeIndicatorsDuringSlc) {
                Message msg = mHandler.obtainMessage();
                msg.what = FETCH_CALL_DETAILS;
                msg.obj = mActiveHFDevice;
                msg.arg1 = mSendFakeEvent;
                mHandler.sendMessage(msg);
             }
          } else {
            mHeadsetClientSco = false;
          }
       }
   }

   public boolean getAGClientConnectionStatus() {
      if (mHeadsetAGSco && mHeadsetClientSco) {
         return true;
      } else {
         return false;
      }
   }

   public void dialOutgoingCall(String number) {
       Log.e(TAG, "dial outgoing call ");
       HeadsetClientService mHeadsetClientService = HeadsetClientService.getHeadsetClientService();
       /*BluetoothHeadsetClientCall call = 
                  mHeadsetClientService.toLegacyCall(mHeadsetClientService.dial(mActiveHFDevice, number));*/
       HfpClientCall call = 
                   mHeadsetClientService.dial(mActiveHFDevice, number); //todo get the device and return type of call
       Log.e(TAG, "HfpClientCall is: " + call);
   }

   public void connectAgAudio(BluetoothDevice device) {
       Log.e(TAG, "handle AG SCO events in separate thread for serialization");
       Message msg = mHandler.obtainMessage();
       msg.what = CONNECT_AG_AUDIO;
       msg.obj = device;
       mActiveAGDevice = device;
       mHandler.sendMessage(msg);
   }

   public void connectClientAudio(BluetoothDevice device) {
       Log.e(TAG, "handle SCO events in separate thread for serialization");
       Message msg = mHandler.obtainMessage();
       msg.what = CONNECT_CLIENT_AUDIO;
       msg.obj = device;
       mActiveHFDevice = device;
       mHandler.sendMessage(msg);
   }

   public List<BluetoothHeadsetClientCall>  getCurrentHfCalls(BluetoothDevice device) {
       Log.e(TAG, "getCurrentHfCalls()");
       HeadsetClientService mHeadsetClientService = HeadsetClientService.getHeadsetClientService();
       mUpdatecurrentHFCalls = mHeadsetClientService.getCurrentHfCalls(device);
       return mUpdatecurrentHFCalls;
   }

   public void dispatchFakeCallIndicators(BluetoothDevice device, int event) {
       Log.e(TAG, "dispatchFakeCallIndicators event is:" + event);
       List<BluetoothHeadsetClientCall> mCurrentCallsDuringSLC = getCurrentHfCalls(device);
       if (mCurrentCallsDuringSLC == null)  {
          /*Message msg = mHandler.obtainMessage();
          msg.what = FETCH_CALL_DETAILS;
          msg.obj = device;
          msg.arg1 = event;
          mHandler.sendMessage(msg);*/
          mSendFakeIndicatorsDuringSlc = true;
          mSendFakeEvent = event;
       } else {
         Log.e(TAG, "current calls is updated:");
         boolean status = fetchcallDetailsToUpdate(mCurrentCallsDuringSLC);
         if (status) {
           switch(event) {
             case INCOMING_INDICATORS:
             case OUTGOING_INDICATORS:
               break;
             case CALL_ACTIVE_INDICATORS:
                if (HeadsetService.getHeadsetService() != null) {
                   HeadsetService.getHeadsetService().phoneStateChanged(mNumActiveCalls = 0, mNumHeldCalls = 0,
                                                       mCallState = CALL_STATE_DIALING,
                                                       mRingingAddress = "", mRingingAddressType, mRingingName, false);
                   HeadsetService.getHeadsetService().phoneStateChanged(mNumActiveCalls = 0, mNumHeldCalls = 0,
                                                       mCallState = CALL_STATE_ALERTING,
                                                       mRingingAddress = "", mRingingAddressType, mRingingName, false);
                }
              break;
             case CALL_END_INDICATORS:
                if (HeadsetService.getHeadsetService() != null) {
                   HeadsetService.getHeadsetService().phoneStateChanged(mNumActiveCalls = 0, mNumHeldCalls = 0, 
                                                        mCallState = CALL_STATE_IDLE, 
                                                        mRingingAddress = "", mRingingAddressType, mRingingName = null, false);
                }
                break;
             case CALL_HELD_INDICATORS:
               if (HeadsetService.getHeadsetService() != null) {
                   HeadsetService.getHeadsetService().phoneStateChanged(mNumActiveCalls = 0, mNumHeldCalls = 0,
                                                       mCallState = CALL_STATE_DIALING, 
                                                       mRingingAddress = "", mRingingAddressType, mRingingName, false);
                   HeadsetService.getHeadsetService().phoneStateChanged(mNumActiveCalls = 0, mNumHeldCalls = 0,
                                                       mCallState = CALL_STATE_ALERTING, 
                                                       mRingingAddress = "", mRingingAddressType, mRingingName, false);
               }
              break;
           }
        }
      }
    }

     public boolean fetchcallDetailsToUpdate(List<BluetoothHeadsetClientCall> CallsFromSlc) {
       Log.e(TAG, "fetchcallDetailsToUpdate:");
       if (CallsFromSlc != null) {
           for (BluetoothHeadsetClientCall call : CallsFromSlc) {
               Log.e(TAG, "call iteration in for loop:");
               switch(call.getState()) {
                  case CALL_STATE_ACTIVE:
                  case CALL_STATE_HELD:
                    Log.e(TAG, "Received Active/held call in SLC:");
                    if (call.isOutgoing()) {
                        Log.e(TAG, "call direction is outgoing");
                        mRingingName = call.getNumber();
                        return true;
                    } else {
                        Log.e(TAG, "call direction is incoming");
                    }
                    break;
                  case CALL_STATE_DIALING:
                  case CALL_STATE_ALERTING:
                  case CALL_STATE_INCOMING:
                   Log.e(TAG, "Received dailing/incoming in SLC:");
                   //no need to fake in these scenarios as the call details comes from telephony to incall Service
                    break;
               }
           }
       }
       return true;
     }

     BluetoothDevice getAgActiveDevice() {
       Log.e(TAG, "getAgActiveDevice");
       return mActiveAGDevice;
     }
}






