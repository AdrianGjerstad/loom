// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 Adrian Gjerstad.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// -----------------------------------------------------------------------------
// loom/fibers/stackswitch_test.cc
// -----------------------------------------------------------------------------
//
// Tests for loom::ConfigureStack and loom::SwitchStack to ensure that support
// for stack switching:
//
// A) Works
// B) Can properly preserve registers
// C) Allows fibers to switch between each other
// D) Works to migrate fibers from one thread to another
// E) Aborts when an entry point returns (see #3)
// F) Includes SIMD saving when necessary.
//
// This test statically allocates its stacks to make it separate from StackPool.
//

#include "fibers/stackswitch.h"

#include <future>
#include <thread>

#include <gtest/gtest.h>
#include <gtest/gtest-death-test.h>

extern "C" {

// SIMD functions that are implemented in assembly allow us to test the
// SIMD-specific registers that aren't normally saved.
extern void write_simd(uintptr_t value);  // Writes a value to a SIMD register.
extern uintptr_t read_simd(void);  // Reads a value from a SIMD register.

}

namespace loom {

namespace {

// Minimal stack size to avoid the real stack overflowing
constexpr uintptr_t kStackSize = 8 * 1024;  // 8KB

// Shared state between the test and the "fiber" to communicate success and
// failure.
struct FiberContext {
  void* main_sp;
  void* fiber_a_sp;
  void* fiber_b_sp;
  int shared_int;
};

class StackSwitchTest : public ::testing::Test {
 protected:
  // Alternate stacks for "fibers"
  uint8_t fiber_a_stack[kStackSize];
  uint8_t fiber_b_stack[kStackSize];
};

using StackSwitchDeathTest = StackSwitchTest;

TEST_F(StackSwitchTest, ConfiguresAndSwitchesSuccessfully) {
  FiberContext ctx;

  // Fiber test code
  auto FiberEntry = [](void* arg) {
    auto* ctx = static_cast<FiberContext*>(arg);

    // Prove that this entry ran.
    ctx->shared_int = 42;

    // Switch back to the main stack
    loom::SwitchStack(ctx->main_sp, &ctx->fiber_a_sp);
  };

  // Configure the stack. ConfigureStack returns the initial SP.
  ctx.fiber_a_sp = loom::ConfigureStack(fiber_a_stack, kStackSize, FiberEntry,
                                        &ctx);
  ctx.shared_int = 0;  // The fiber must change this value to indicate success.

  // Perform the switching test.
  loom::SwitchStack(ctx.fiber_a_sp, &ctx.main_sp);

  ASSERT_EQ(ctx.shared_int, 42);
}

TEST_F(StackSwitchTest, PreservesRegistersAcrossSwitches) {
  FiberContext ctx;

  // Fiber test code
  auto PingPongEntry = [](void* arg) {
    auto* ctx = static_cast<FiberContext*>(arg);

    ctx->shared_int += 1;  // Should now be 1
    loom::SwitchStack(ctx->main_sp, &ctx->fiber_a_sp);

    ctx->shared_int += 1;  // Should now be 3
    loom::SwitchStack(ctx->main_sp, &ctx->fiber_a_sp);
  };

  ctx.fiber_a_sp = loom::ConfigureStack(fiber_a_stack, kStackSize,
                                        PingPongEntry, &ctx);
  ctx.shared_int = 0;

  // Switch the first time
  loom::SwitchStack(ctx.fiber_a_sp, &ctx.main_sp);

  ASSERT_EQ(ctx.shared_int, 1);

  ctx.shared_int += 1;  // Should now be 2
  loom::SwitchStack(ctx.fiber_a_sp, &ctx.main_sp);

  ASSERT_EQ(ctx.shared_int, 3);
}

TEST_F(StackSwitchTest, SwitchesBetweenMultipleStacks) {
  FiberContext ctx;

  // This test uses order of operations and a lack of the commutative property
  // between addition and multiplication to ensure the exact order of fibers is
  // consistent.

  auto FiberAEntry = [](void* arg) {
    auto* ctx = static_cast<FiberContext*>(arg);

    ctx->shared_int += 4;  // Should now be 9
    loom::SwitchStack(ctx->fiber_b_sp, &ctx->fiber_a_sp);

    ctx->shared_int -= 1;  // Should now be 26
    loom::SwitchStack(ctx->fiber_b_sp, &ctx->fiber_a_sp);
  };

  auto FiberBEntry = [](void* arg) {
    auto* ctx = static_cast<FiberContext*>(arg);

    ctx->shared_int *= 3;  // Should now be 27
    loom::SwitchStack(ctx->fiber_a_sp, &ctx->fiber_b_sp);

    ctx->shared_int /= 2;  // Should now be 13
    loom::SwitchStack(ctx->main_sp, &ctx->fiber_b_sp);
  };

  ctx.fiber_a_sp = loom::ConfigureStack(fiber_a_stack, kStackSize, FiberAEntry,
                                        &ctx);
  ctx.fiber_b_sp = loom::ConfigureStack(fiber_b_stack, kStackSize, FiberBEntry,
                                        &ctx);
  ctx.shared_int = 5;

  // Initiate test
  loom::SwitchStack(ctx.fiber_a_sp, &ctx.main_sp);

  ASSERT_EQ(ctx.shared_int, 13);
}

TEST_F(StackSwitchTest, MigratesThreadsSuccessfully) {
  FiberContext ctx;

  // Fiber test code
  auto MigrationEntry = [](void* arg) {
    auto* ctx = static_cast<FiberContext*>(arg);

    // Currently executing in Thread B.
    ctx->shared_int += 1;  // Should be 1
    loom::SwitchStack(ctx->main_sp, &ctx->fiber_a_sp);

    // Currently executing in Thread A.
    ctx->shared_int += 1;  // Should be 3
    loom::SwitchStack(ctx->main_sp, &ctx->fiber_a_sp);
  };

  ctx.fiber_a_sp = loom::ConfigureStack(fiber_a_stack, kStackSize,
                                        MigrationEntry, &ctx);
  ctx.shared_int = 0;

  // We use promise/future here because we're not actually worried about
  // multi-threaded support, we're worried about keeping contexts self-contained
  // and ensuring that a fiber can migrate threads in the first place. In this
  // test, only one thread will ever actually be executing at a time.
  std::promise<void> promise;
  std::future<void> future = promise.get_future();

  // Second thread code
  auto ThreadBEntry = [&]() {
    // Fiber will be launched in this thread and migrate to the test thread to
    // make assertions easier to handle.
    loom::SwitchStack(ctx.fiber_a_sp, &ctx.main_sp);

    // Fiber has switched back
    promise.set_value();
  };

  std::thread t(ThreadBEntry);
  future.wait();

  t.join();

  // Fiber has switched back the first time.
  EXPECT_EQ(ctx.shared_int, 1);

  ctx.shared_int += 1;
  loom::SwitchStack(ctx.fiber_a_sp, &ctx.main_sp);

  // Fiber has switched back the second time.
  ASSERT_EQ(ctx.shared_int, 3);
}

TEST_F(StackSwitchDeathTest, EntryPointReturns) {
  auto EntryPointAbort = [](void* arg) {
    // This fiber returns from the entry point to test what happens when
    // execution falls off the end of an entry point.
  };

  EXPECT_EXIT({
    FiberContext ctx;

    ctx.fiber_a_sp = loom::ConfigureStack(fiber_a_stack, kStackSize,
                                          EntryPointAbort, &ctx);

    loom::SwitchStack(ctx.fiber_a_sp, &ctx.main_sp);
  }, testing::KilledBySignal(SIGABRT), ".*");
}

TEST_F(StackSwitchTest, PreservesSIMDData) {
  FiberContext ctx;
  
  auto SIMDEntry = [](void* arg) {
    auto* ctx = static_cast<FiberContext*>(arg);

    // Write SIMD data and the yield. SIMDGuard should protect the data.
    write_simd(42);

    {
      loom::SIMDGuard guard;
      loom::SwitchStack(ctx->main_sp, &ctx->fiber_a_sp);
    }

    // The SIMD data should be restored and can be read from the main stack.
    loom::SwitchStack(ctx->main_sp, &ctx->fiber_a_sp);
  };

  ctx.fiber_a_sp = loom::ConfigureStack(fiber_a_stack, kStackSize, SIMDEntry,
                                        &ctx);

  loom::SwitchStack(ctx.fiber_a_sp, &ctx.main_sp);

  ASSERT_EQ(read_simd(), 42u);
  write_simd(143);  // Clobber the old data
  ASSERT_EQ(read_simd(), 143u);

  loom::SwitchStack(ctx.fiber_a_sp, &ctx.main_sp);

  // Verify that the data was restored.
  ASSERT_EQ(read_simd(), 42u);
}

}

}

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

