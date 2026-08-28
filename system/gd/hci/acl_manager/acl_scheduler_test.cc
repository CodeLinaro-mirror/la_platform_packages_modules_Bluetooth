/*
 * Copyright (C) 2022 The Android Open Source Project
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

#include "hci/acl_manager/acl_scheduler.h"

#include <com_android_bluetooth_flags.h>
#include <flag_macros.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <utility>

#include "hci/address.h"
#include "os/thread.h"

#define TEST_BT com::android::bluetooth::flags

namespace bluetooth {
namespace hci {
namespace acl_manager {
namespace {

const auto address1 = Address::FromString("A1:A2:A3:A4:A5:A6").value();
const auto address2 = Address::FromString("B1:B2:B3:B4:B5:B6").value();
const auto address3 = Address::FromString("C1:C2:C3:C4:C5:C6").value();

const auto timeout = std::chrono::milliseconds(100);

MATCHER(IsSet, "Future is set") {
  if (arg.wait_for(timeout) != std::future_status::ready) {
    return false;
  }
  const_cast<std::future<void>&>(arg).get();
  return true;
}

class AclSchedulerTest : public ::testing::Test {
protected:
  void SetUp() override {
    thread_ = new os::Thread("test_thread", os::Thread::Priority::NORMAL);
    client_handler_ = new os::Handler(thread_);

    acl_scheduler_ = std::make_unique<AclScheduler>(client_handler_);

    com_android_bluetooth_flags_reset_flags();

    ::testing::FLAGS_gtest_death_test_style = "threadsafe";
  }

  void TearDown() override {
    acl_scheduler_.reset();

    client_handler_->Clear();
    client_handler_->WaitUntilStopped(bluetooth::kHandlerStopTimeout);

    delete client_handler_;
    delete thread_;
  }

  common::ContextualOnceCallback<void(std::string)> impossibleCallbackTakingString() {
    return client_handler_->BindOnce([](std::string /* _ */) { ADD_FAILURE(); });
  }

  common::ContextualOnceCallback<void(std::string)> emptyCallbackTakingString() {
    return client_handler_->BindOnce([](std::string /* _ */) {});
  }

  common::ContextualOnceCallback<void(std::string)> promiseCallbackTakingString(
          std::promise<void> promise) {
    return client_handler_->BindOnce(
            [](std::promise<void> promise, std::string /* _ */) { promise.set_value(); },
            std::move(promise));
  }

  common::ContextualOnceCallback<void()> impossibleCallback() {
    return client_handler_->BindOnce([] { ADD_FAILURE(); });
  }

  common::ContextualOnceCallback<void()> emptyCallback() {
    return client_handler_->BindOnce([] {});
  }

  common::ContextualOnceCallback<void()> promiseCallback(std::promise<void> promise) {
    return client_handler_->BindOnce([](std::promise<void> promise) { promise.set_value(); },
                                     std::move(promise));
  }

  os::Thread* thread_ = nullptr;
  os::Handler* client_handler_ = nullptr;
  std::unique_ptr<AclScheduler> acl_scheduler_ = nullptr;
};

TEST_F(AclSchedulerTest, SingleConnectionImmediatelyExecuted) {
  auto promise = std::promise<void>{};
  auto future = promise.get_future();

  // start connection, which should immediately execute
  acl_scheduler_->EnqueueOutgoingAclConnection(address1, promiseCallback(std::move(promise)));

  // it has started
  EXPECT_THAT(future, IsSet());
}

TEST_F(AclSchedulerTest, ThreeConnectionsQueue) {
  auto promise1 = std::promise<void>{};
  auto future1 = promise1.get_future();
  auto promise2 = std::promise<void>{};
  auto future2 = promise2.get_future();

  // start first connection, which immediately runs
  acl_scheduler_->EnqueueOutgoingAclConnection(address1, emptyCallback());
  // start second connection
  acl_scheduler_->EnqueueOutgoingAclConnection(address2, (promiseCallback(std::move(promise1))));
  // start third connection
  acl_scheduler_->EnqueueOutgoingAclConnection(address3, (promiseCallback(std::move(promise2))));

  // the second and third connections are currently queued
  EXPECT_THAT(future1.wait_for(timeout), std::future_status::timeout);

  // first connection fails, so next one should start
  acl_scheduler_->ReportOutgoingAclConnectionFailure();

  // the second connection has started, the third one is queued
  EXPECT_THAT(future1, IsSet());
  EXPECT_THAT(future2.wait_for(timeout), std::future_status::timeout);

  // second connection fails, so third one should start
  acl_scheduler_->ReportOutgoingAclConnectionFailure();

  // the third connection has started
  EXPECT_THAT(future2, IsSet());
}

TEST_F(AclSchedulerTest, SingleConnectionCompletionCallback) {
  auto promise = std::promise<void>{};
  auto future = promise.get_future();

  // start connection, which immediately runs
  acl_scheduler_->EnqueueOutgoingAclConnection(address1, emptyCallback());

  // the outgoing connection completes
  acl_scheduler_->ReportAclConnectionCompletion(address1, promiseCallback(std::move(promise)),
                                                impossibleCallback(),
                                                impossibleCallbackTakingString());

  // the outgoing_connection callback should have executed
  EXPECT_THAT(future, IsSet());
}

TEST_F(AclSchedulerTest, SingleConnectionCompletionDequeueNext) {
  auto promise = std::promise<void>{};
  auto future = promise.get_future();

  // start connection, which immediately runs
  acl_scheduler_->EnqueueOutgoingAclConnection(address1, emptyCallback());
  // start second connection which should queue
  acl_scheduler_->EnqueueOutgoingAclConnection(address2, promiseCallback(std::move(promise)));

  // complete the first connection
  acl_scheduler_->ReportAclConnectionCompletion(address1, emptyCallback(), impossibleCallback(),
                                                impossibleCallbackTakingString());

  // the next connection should dequeue now
  EXPECT_THAT(future, IsSet());
}

TEST_F(AclSchedulerTest, IncomingConnectionCallback) {
  auto promise = std::promise<void>{};
  auto future = promise.get_future();

  // an incoming connection arrives
  acl_scheduler_->RegisterPendingIncomingConnection(address1);

  // and completes
  acl_scheduler_->ReportAclConnectionCompletion(address1, impossibleCallback(),
                                                promiseCallback(std::move(promise)),
                                                impossibleCallbackTakingString());

  // the incoming_connection callback should have executed
  EXPECT_THAT(future, IsSet());
}

TEST_F(AclSchedulerTest, UnknownConnectionCallback) {
  auto promise = std::promise<void>{};
  auto future = promise.get_future();

  // start outgoing connection
  acl_scheduler_->EnqueueOutgoingAclConnection(address1, emptyCallback());

  // an incoming connection arrives
  acl_scheduler_->RegisterPendingIncomingConnection(address2);

  // then an unknown connection completes
  acl_scheduler_->ReportAclConnectionCompletion(address3, impossibleCallback(),
                                                impossibleCallback(),
                                                (promiseCallbackTakingString(std::move(promise))));

  // the unknown_connection callback should have executed
  EXPECT_THAT(future, IsSet());
}

TEST_F(AclSchedulerTest, TiebreakForOutgoingConnection) {
  auto promise1 = std::promise<void>{};
  auto future1 = promise1.get_future();
  auto promise2 = std::promise<void>{};
  auto future2 = promise2.get_future();

  // start outgoing connection
  acl_scheduler_->EnqueueOutgoingAclConnection(address1, emptyCallback());

  // an incoming connection arrives *from the same address*
  acl_scheduler_->RegisterPendingIncomingConnection(address1);

  // then the connection to that address completes
  acl_scheduler_->ReportAclConnectionCompletion(address1, promiseCallback(std::move(promise1)),
                                                impossibleCallback(),
                                                impossibleCallbackTakingString());

  // the outgoing_connection callback should have executed, NOT the incoming_connection one
  // this preserves working behavior, it is not based on any principled decision (so if you need to
  // break this test, go for it)
  EXPECT_THAT(future1, IsSet());

  // start an outgoing RNR to another device
  acl_scheduler_->EnqueueRemoteNameRequest(address2, promiseCallback(std::move(promise2)),
                                           emptyCallback());

  // we expect the start callback to be invoked for the RNR and not deadlock
  EXPECT_THAT(future2, IsSet());
}

TEST_F(AclSchedulerTest, QueueWhileIncomingConnectionsPending) {
  auto promise = std::promise<void>{};
  auto future = promise.get_future();

  // start outgoing connection
  acl_scheduler_->EnqueueOutgoingAclConnection(address1, emptyCallback());
  // queue a second outgoing connection
  acl_scheduler_->EnqueueOutgoingAclConnection(address2, promiseCallback(std::move(promise)));

  // an incoming connection arrives
  acl_scheduler_->RegisterPendingIncomingConnection(address3);

  // then the first outgoing connection completes
  acl_scheduler_->ReportAclConnectionCompletion(address1, emptyCallback(), impossibleCallback(),
                                                impossibleCallbackTakingString());

  // the outgoing_connection callback should not have executed yet
  EXPECT_THAT(future.wait_for(timeout), std::future_status::timeout);

  // now the incoming connection completes
  acl_scheduler_->ReportAclConnectionCompletion(address3, impossibleCallback(), emptyCallback(),
                                                impossibleCallbackTakingString());

  // only now does the next outgoing connection start
  EXPECT_THAT(future, IsSet());
}

TEST_F(AclSchedulerTest, DoNothingWhileIncomingConnectionsExist) {
  auto promise = std::promise<void>{};
  auto future = promise.get_future();

  // an incoming connection arrives
  acl_scheduler_->RegisterPendingIncomingConnection(address1);

  // try to start an outgoing connection to another device
  acl_scheduler_->EnqueueOutgoingAclConnection(address2, promiseCallback(std::move(promise)));

  // the outgoing_connection callback should not have executed yet
  EXPECT_THAT(future.wait_for(timeout), std::future_status::timeout);

  // a second incoming connection arrives
  acl_scheduler_->RegisterPendingIncomingConnection(address3);

  // the first incoming connection completes
  acl_scheduler_->ReportAclConnectionCompletion(address1, impossibleCallback(), emptyCallback(),
                                                impossibleCallbackTakingString());

  // the outgoing_connection callback should *still* not have executed yet
  EXPECT_THAT(future.wait_for(timeout), std::future_status::timeout);

  // the second incoming connection completes, so none are left
  acl_scheduler_->ReportAclConnectionCompletion(address3, impossibleCallback(), emptyCallback(),
                                                impossibleCallbackTakingString());

  // only now does the outgoing connection start
  EXPECT_THAT(future, IsSet());
}

TEST_F(AclSchedulerTest, IncomingConnectionPendingWithOutgoingRemoteNameRequest) {
  auto promise = std::promise<void>{};
  auto future = promise.get_future();

  // an incoming connection arrives
  acl_scheduler_->RegisterPendingIncomingConnection(address1);

  // start an outgoing RNR
  acl_scheduler_->EnqueueRemoteNameRequest(address1, promiseCallback(std::move(promise)),
                                           emptyCallback());

  // we expect the outgoing RNR to queue while incoming pending connection from same device
  EXPECT_THAT(future, IsSet());
}

TEST_F(AclSchedulerTest, ConnectionToSameDeviceIncomingConnectionPending) {
  auto promise = std::promise<void>{};
  auto future = promise.get_future();

  // an incoming connection arrives
  acl_scheduler_->RegisterPendingIncomingConnection(address1);

  // try to start an outgoing connection to same device
  acl_scheduler_->EnqueueOutgoingAclConnection(address1, promiseCallback(std::move(promise)));

  // we expect the outgoing connection to wait and then dropped once connection
  // established
  EXPECT_EQ(future.wait_for(timeout), std::future_status::timeout);
}

TEST_F(AclSchedulerTest, CancelOutgoingConnection) {
  auto promise = std::promise<void>{};
  auto future = promise.get_future();

  // start an outgoing connection
  acl_scheduler_->EnqueueOutgoingAclConnection(address1, emptyCallback());
  // enqueue a second connection
  acl_scheduler_->EnqueueOutgoingAclConnection(address2, promiseCallback(std::move(promise)));

  // cancel the outgoing connection
  acl_scheduler_->CancelAclConnection(address1, emptyCallback(), impossibleCallback());

  // we expect the second connection to stay queued until the cancel completes
  EXPECT_THAT(future.wait_for(timeout), std::future_status::timeout);

  // now the cancel completes (with a failed status, in reality, but the scheduler doesn't care)
  acl_scheduler_->ReportAclConnectionCompletion(address1, emptyCallback(), impossibleCallback(),
                                                impossibleCallbackTakingString());

  // so only now do we advance the queue
  EXPECT_THAT(future, IsSet());
}

TEST_F(AclSchedulerTest, CancelOutgoingConnectionCallback) {
  auto promise = std::promise<void>{};
  auto future = promise.get_future();

  // start an outgoing connection
  acl_scheduler_->EnqueueOutgoingAclConnection(address1, emptyCallback());

  // cancel the outgoing connection
  acl_scheduler_->CancelAclConnection(address1, promiseCallback(std::move(promise)),
                                      impossibleCallback());

  // we expect the cancel_connection callback to be invoked since we are cancelling an actually
  // active connection
  EXPECT_THAT(future, IsSet());
}

TEST_F(AclSchedulerTest, CancelQueuedConnectionRemoveFromQueue) {
  auto promise = std::promise<void>{};
  auto future = promise.get_future();

  // start an outgoing connection
  acl_scheduler_->EnqueueOutgoingAclConnection(address1, emptyCallback());
  // start another connection that will queue
  acl_scheduler_->EnqueueOutgoingAclConnection(address2, impossibleCallback());
  // start a third connection that will queue
  acl_scheduler_->EnqueueOutgoingAclConnection(address3, promiseCallback(std::move(promise)));

  // cancel the first queued connection
  acl_scheduler_->CancelAclConnection(address2, impossibleCallback(), emptyCallback());

  // the second queued connection should remain enqueued, since another connection is in progress
  EXPECT_THAT(future.wait_for(timeout), std::future_status::timeout);

  // complete the outgoing connection
  acl_scheduler_->ReportOutgoingAclConnectionFailure();

  // only now can we dequeue the second queued connection
  EXPECT_THAT(future, IsSet());
}

TEST_F(AclSchedulerTest, CancelQueuedConnectionCallback) {
  auto promise = std::promise<void>{};
  auto future = promise.get_future();

  // start an outgoing connection
  acl_scheduler_->EnqueueOutgoingAclConnection(address1, emptyCallback());
  // start another connection that will queue
  acl_scheduler_->EnqueueOutgoingAclConnection(address2, emptyCallback());

  // cancel the queued connection
  acl_scheduler_->CancelAclConnection(address2, impossibleCallback(),
                                      promiseCallback(std::move(promise)));

  // we expect the cancel_connection_completed callback to be invoked since we are cancelling a
  // connection in the queue
  EXPECT_THAT(future, IsSet());
}

TEST_F(AclSchedulerTest, CancelUnknownIncomingConnectionClearsStaleEntry) {
  auto promise = std::promise<void>{};
  auto future = promise.get_future();

  // an incoming connection arrives (its Accept Connection Request will fail)
  acl_scheduler_->RegisterPendingIncomingConnection(address1);

  // an unrelated RNR to a different peer is enqueued and blocked by address1's presence
  acl_scheduler_->EnqueueRemoteNameRequest(address2, promiseCallback(std::move(promise)),
                                           impossibleCallback());
  EXPECT_THAT(future.wait_for(timeout), std::future_status::timeout);

  // Accept Connection Request for address1 failed; CancelAclConnection is invoked for it.
  // It is not an outgoing/queued connection, so the existing matcher finds nothing ...
  acl_scheduler_->CancelAclConnection(address1, impossibleCallback(), impossibleCallback());

  // ... but the fix must still clear the stale incoming registration and unblock the queue.
  EXPECT_THAT(future, IsSet());
}

TEST_F(AclSchedulerTest, RemoteNameRequestImmediatelyExecuted) {
  auto promise = std::promise<void>{};
  auto future = promise.get_future();

  // start an outgoing request
  acl_scheduler_->EnqueueRemoteNameRequest(address1, promiseCallback(std::move(promise)),
                                           emptyCallback());

  // we expect the start callback to be invoked immediately
  EXPECT_THAT(future, IsSet());
}

TEST_F(AclSchedulerTest, RemoteNameRequestQueuing) {
  auto promise = std::promise<void>{};
  auto future = promise.get_future();

  // start an outgoing request
  acl_scheduler_->EnqueueRemoteNameRequest(address1, emptyCallback(), impossibleCallback());
  // enqueue a second one
  acl_scheduler_->EnqueueRemoteNameRequest(address2, promiseCallback(std::move(promise)),
                                           impossibleCallback());

  // we should still be queued
  EXPECT_THAT(future.wait_for(timeout), std::future_status::timeout);

  // the first request completes
  acl_scheduler_->ReportRemoteNameRequestCompletion(address1);

  // so the second request should now have started
  EXPECT_THAT(future, IsSet());
}

TEST_F(AclSchedulerTest, RemoteNameRequestCancellationCallback) {
  auto promise = std::promise<void>{};
  auto future = promise.get_future();

  // start an outgoing request
  acl_scheduler_->EnqueueRemoteNameRequest(address1, emptyCallback(), impossibleCallback());

  // cancel it
  acl_scheduler_->CancelRemoteNameRequest(address1, promiseCallback(std::move(promise)));

  // the cancel callback should be invoked
  EXPECT_THAT(future, IsSet());
}

TEST_F(AclSchedulerTest, RemoteNameRequestCancellationWhileQueuedCallback) {
  auto promise = std::promise<void>{};
  auto future = promise.get_future();

  // start an outgoing request
  acl_scheduler_->EnqueueRemoteNameRequest(address1, emptyCallback(), impossibleCallback());
  // enqueue a second one
  acl_scheduler_->EnqueueRemoteNameRequest(address2, impossibleCallback(),
                                           promiseCallback(std::move(promise)));

  // cancel the second one
  acl_scheduler_->CancelRemoteNameRequest(address2, impossibleCallback());

  // the cancel_request_completed calback should be invoked
  EXPECT_THAT(future, IsSet());

  // the first request completes
  acl_scheduler_->ReportRemoteNameRequestCompletion(address1);
  // ensure request above completes, before we TearDown the test
  client_handler_->Synchronize(std::chrono::milliseconds(20));

  // we don't dequeue the second one, since it was cancelled
  // implicitly assert that its callback was never invoked
}

TEST_F(AclSchedulerTest, CancelQueuedRemoteNameRequestRemoveFromQueue) {
  auto promise = std::promise<void>{};
  auto future = promise.get_future();

  // start an outgoing connection
  acl_scheduler_->EnqueueOutgoingAclConnection(address1, emptyCallback());
  // start another connection that will queue
  acl_scheduler_->EnqueueRemoteNameRequest(address2, impossibleCallback(), emptyCallback());
  // start a third connection that will queue
  acl_scheduler_->EnqueueRemoteNameRequest(address3, promiseCallback(std::move(promise)),
                                           impossibleCallback());

  // cancel the first queued connection
  acl_scheduler_->CancelRemoteNameRequest(address2, impossibleCallback());

  // the second queued connection should remain enqueued, since another connection is in progress
  EXPECT_THAT(future.wait_for(timeout), std::future_status::timeout);

  // complete the outgoing connection
  acl_scheduler_->ReportOutgoingAclConnectionFailure();

  // only now can we dequeue the second queued connection
  EXPECT_THAT(future, IsSet());
}

TEST_F(AclSchedulerTest, RemoteNameRequestCancellationShouldDequeueNext) {
  auto promise = std::promise<void>{};
  auto future = promise.get_future();

  // start an outgoing request
  acl_scheduler_->EnqueueRemoteNameRequest(address1, emptyCallback(), impossibleCallback());
  // enqueue a second one
  acl_scheduler_->EnqueueRemoteNameRequest(address2, promiseCallback(std::move(promise)),
                                           impossibleCallback());

  // we should still be queued
  EXPECT_THAT(future.wait_for(timeout), std::future_status::timeout);

  // the first request is cancelled
  acl_scheduler_->CancelRemoteNameRequest(address1, emptyCallback());

  // we should still remain queued while we wait for the cancel to complete
  EXPECT_THAT(future.wait_for(timeout), std::future_status::timeout);

  // the cancel completes
  acl_scheduler_->ReportRemoteNameRequestCompletion(address1);

  // so the second request should now have started
  EXPECT_THAT(future, IsSet());
}

// Tests for RNR promotion on incoming ACL connection.
// Negative test: the promotion scan (Block 3 in try_dequeue_next_operation) must
// NOT promote an RNR whose address is NOT in the incoming set, even when another
// address IS incoming and the outgoing slot is free.
//
// Queue state when try_dequeue fires after ACL(address1) completes:
//   pending: [ ACL(address2), RNR(address3) ]
//   incoming_set: { address2 }   <- address2, NOT address3
//   outgoing_entry: empty
//
// Block 3 runs (no outgoing entry, incoming_set not empty), scans the queue,
// finds RNR(address3) but address3 is NOT in incoming_set -> no promotion.
// ACL(address2) is also blocked by its own incoming connection.
TEST_F(AclSchedulerTest, DoesNotPromoteRnrIfAddressNotInIncomingSet) {
  auto rnr_promise = std::promise<void>{};
  auto rnr_future = rnr_promise.get_future();
  auto acl2_promise = std::promise<void>{};
  auto acl2_future = acl2_promise.get_future();

  // ACL(address1) dispatched immediately.
  acl_scheduler_->EnqueueOutgoingAclConnection(address1, emptyCallback());
  // ACL(address2) queued behind address1.
  acl_scheduler_->EnqueueOutgoingAclConnection(address2,
                                               promiseCallback(std::move(acl2_promise)));
  // RNR(address3) queued behind ACL(address2).
  acl_scheduler_->EnqueueRemoteNameRequest(address3, promiseCallback(std::move(rnr_promise)),
                                           emptyCallback());

  // Incoming connection for address2, NOT address3 -- RNR(address3) is ineligible.
  acl_scheduler_->RegisterPendingIncomingConnection(address2);

  // ACL(address1) completes -> outgoing_entry_ becomes empty -> Block 3 runs.
  // Block 3 scans: ACL(address2) skip, RNR(address3): address3 NOT in incoming_set -> no promotion.
  acl_scheduler_->ReportAclConnectionCompletion(address1, emptyCallback(), impossibleCallback(),
                                                impossibleCallbackTakingString());

  // RNR(address3) must NOT be promoted.
  EXPECT_THAT(rnr_future.wait_for(timeout), std::future_status::timeout)
      << "RNR(address3) must not be promoted when address3 is not in the incoming set";

  // ACL(address2) must also be blocked by its own incoming connection.
  EXPECT_THAT(acl2_future.wait_for(timeout), std::future_status::timeout)
      << "ACL(address2) must be blocked by its own incoming connection";
}

// Positive test: an RNR sitting behind an unrelated ACL in the queue must be
// promoted and dispatched as soon as an incoming connection arrives for its
// address and the head ACL completes.
//
// Queue state when try_dequeue_next_operation() fires after ACL(address1) completes:
//   pending: [ ACL(address2), RNR(address3) ]
//   incoming_set: { address3 }
//   outgoing_entry: empty
//
// Expected: RNR(address3) is promoted past ACL(address2) and dispatched.
//           ACL(address2) remains blocked while address3 is still incoming.
TEST_F(AclSchedulerTest, PromotesRnrFromQueueWhenBehindAcl) {
  auto rnr_promise = std::promise<void>{};
  auto rnr_future = rnr_promise.get_future();
  auto acl2_promise = std::promise<void>{};
  auto acl2_future = acl2_promise.get_future();

  // 1. Start an outgoing ACL to address1 -- dispatched immediately.
  acl_scheduler_->EnqueueOutgoingAclConnection(address1, emptyCallback());

  // 2. Enqueue a second ACL to address2 -- queued behind address1.
  acl_scheduler_->EnqueueOutgoingAclConnection(address2,
                                               promiseCallback(std::move(acl2_promise)));

  // 3. Enqueue an RNR for address3 -- queued behind address2.
  acl_scheduler_->EnqueueRemoteNameRequest(address3, promiseCallback(std::move(rnr_promise)),
                                           emptyCallback());

  // 4. An incoming connection arrives from address3, making the RNR eligible.
  acl_scheduler_->RegisterPendingIncomingConnection(address3);

  // 5. The first ACL (address1) completes -- this triggers try_dequeue_next_operation().
  //    address1 is NOT in the incoming set, so it is not erased from there.
  acl_scheduler_->ReportAclConnectionCompletion(address1, emptyCallback(), impossibleCallback(),
                                                impossibleCallbackTakingString());

  // 6. The RNR for address3 must be promoted past ACL(address2) and dispatched.
  EXPECT_THAT(rnr_future, IsSet()) << "RNR should be promoted and dispatched from queue";

  // 7. ACL(address2) must still be blocked: incoming_set still contains address3.
  EXPECT_THAT(acl2_future.wait_for(timeout), std::future_status::timeout)
      << "ACL for address2 should remain blocked while address3 incoming is pending";
}

// Verify that after the promoted RNR completes, the previously blocked ACL
// is eventually dispatched once the incoming connection for address3 resolves.
TEST_F(AclSchedulerTest, AclUnblockedAfterPromotedRnrCompletes) {
  auto rnr_promise = std::promise<void>{};
  auto rnr_future = rnr_promise.get_future();
  auto acl2_promise = std::promise<void>{};
  auto acl2_future = acl2_promise.get_future();

  // Build the same queue as PromotesRnrFromQueueWhenBehindAcl.
  acl_scheduler_->EnqueueOutgoingAclConnection(address1, emptyCallback());
  acl_scheduler_->EnqueueOutgoingAclConnection(address2,
                                               promiseCallback(std::move(acl2_promise)));
  acl_scheduler_->EnqueueRemoteNameRequest(address3, promiseCallback(std::move(rnr_promise)),
                                           emptyCallback());
  acl_scheduler_->RegisterPendingIncomingConnection(address3);
  acl_scheduler_->ReportAclConnectionCompletion(address1, emptyCallback(), impossibleCallback(),
                                                impossibleCallbackTakingString());

  // RNR is now dispatched (outgoing_entry = RNR(address3)).
  EXPECT_THAT(rnr_future, IsSet());

  // RNR completes -- outgoing_entry is cleared.
  acl_scheduler_->ReportRemoteNameRequestCompletion(address3);

  // ACL(address2) must STILL be blocked: address3 is still in the incoming set.
  EXPECT_THAT(acl2_future.wait_for(timeout), std::future_status::timeout)
      << "ACL for address2 must remain blocked after RNR completes (address3 still incoming)";

  // The incoming connection for address3 also resolves.
  acl_scheduler_->ReportAclConnectionCompletion(address3, impossibleCallback(), emptyCallback(),
                                                impossibleCallbackTakingString());

  // Now the incoming set is empty and outgoing_entry is empty, so ACL(address2) can proceed.
  EXPECT_THAT(acl2_future, IsSet())
      << "ACL for address2 should be dispatched after RNR completes and incoming resolves";
}

}  // namespace
}  // namespace acl_manager
}  // namespace hci
}  // namespace bluetooth
