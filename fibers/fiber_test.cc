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
// loom/fibers/fiber_test.cc
// -----------------------------------------------------------------------------
//
// Unit test cases for loom::Fiber.
//

#include <cstdlib>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/status/status_matchers.h"
#include <gtest/gtest.h>

#include "fibers/fiber.h"
#include "fibers/stackarena.h"

#include <iostream>

namespace loom {

namespace {

using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;
using ::testing::Not;
using absl::StatusCode;

// A stack arena that just malloc's and free's stacks that it gives out.
class FakeStackArena : public StackArena {
 public:
  explicit FakeStackArena(size_t stack_size) : StackArena(stack_size) {}

  absl::StatusOr<void*> Lease() override {
    // Typical machines rely on an alignment of 16 bytes at most
    void* stack = std::aligned_alloc(16, stack_size());
    if (stack == nullptr) {
      return absl::ResourceExhaustedError("could not allocate test stack");
    }

    ++outstanding_stacks_;
    return stack;
  }

  void Release(void* stack) override {
    --outstanding_stacks_;
    std::free(stack);
  }

  int outstanding_stacks() const { return outstanding_stacks_; }

 private:
  int outstanding_stacks_ = 0;
};

class FiberTest : public ::testing::Test {
 protected:
  void TearDown() override {
    // Catch memory leaks.
    // IF YOUR TEST FAILED AT THIS ASSERT, NOT ALL STACKS ARE BEING RELEASED.
    ASSERT_EQ(arena_.outstanding_stacks(), 0);
  }

  FakeStackArena arena_ = FakeStackArena(64 * 1024);
};

// A Fiber should be creatable and in a suspended state when it is first
// created.
TEST_F(FiberTest, CreatesSuccessfullyAndHasSuspendedState) {
  auto fiber_or = Fiber::Create(&arena_, []() {});
  ASSERT_THAT(fiber_or, IsOk());
  Fiber* fiber = fiber_or.value();

  // There should be one stack allocated
  EXPECT_EQ(arena_.outstanding_stacks(), 1);

  // Fiber should have state "kSuspended" when it is first created and is not
  // being executed.
  EXPECT_EQ(fiber->state(), Fiber::State::kSuspended);

  // Cleanup
  Fiber::Reap(fiber);
}

// A Fiber must be able to accept an entry point function with any number of
// arguments, and accept a variadic set of arguments to forward to that
// function.
TEST_F(FiberTest, PerfectlyForwardsVariadicArguments) {
  bool executed = false;
  int captured_int = 0;
  std::string captured_str = "";

  auto entry = [&](int i, std::string s) {
    executed = true;
    captured_int = i;
    captured_str = s;
    // Returning here causes the fiber to switch back as dead.
  };

  auto fiber_or = Fiber::Create(&arena_, entry, 42, "Fibers are awesome!");
  ASSERT_THAT(fiber_or, IsOk());
  Fiber* fiber = fiber_or.value();

  // Allow the fiber to run.
  fiber->Jump();

  // Confirm that the variadic arguments were passed successfully.
  EXPECT_TRUE(executed);
  EXPECT_EQ(captured_int, 42);
  EXPECT_EQ(captured_str, "Fibers are awesome!");

  // Confirm that the fiber was marked as dead.
  EXPECT_EQ(fiber->state(), Fiber::State::kDead);

  // Cleanup
  Fiber::Reap(fiber);
}

// A Fiber must accurately track its execution state at any given time.
TEST_F(FiberTest, AccuratelyTracksStateOverLifecycle) {
  auto entry = []() {
    EXPECT_EQ(Fiber::GetCurrentFiber()->state(), Fiber::State::kRunning);
    Fiber::GetCurrentFiber()->YieldBack();
    EXPECT_EQ(Fiber::GetCurrentFiber()->state(), Fiber::State::kRunning);
  };

  auto fiber_or = Fiber::Create(&arena_, entry);
  ASSERT_THAT(fiber_or, IsOk());
  Fiber* fiber = fiber_or.value();

  // Should be suspended at creation
  EXPECT_EQ(fiber->state(), Fiber::State::kSuspended);
  
  fiber->Jump();

  // Should be back to suspended
  EXPECT_EQ(fiber->state(), Fiber::State::kSuspended);

  // Jump back in one time to finish the fiber
  fiber->Jump();

  // Confirm that the fiber was marked as dead.
  EXPECT_EQ(fiber->state(), Fiber::State::kDead);

  // Cleanup
  Fiber::Reap(fiber);
}

// GetCurrentFiber() should always return an accurate pointer to the currently
// executing fiber, and should return NULL when not in a fiber.
TEST_F(FiberTest, GetCurrentFiberReturnsTheCorrectPointer) {
  Fiber* captured_ptr = nullptr;

  auto entry = [&]() {
    captured_ptr = Fiber::GetCurrentFiber();
  };

  // Create two fibers
  auto fiber_a_or = Fiber::Create(&arena_, entry);
  ASSERT_THAT(fiber_a_or, IsOk());
  Fiber* fiber_a = fiber_a_or.value();
  
  auto fiber_b_or = Fiber::Create(&arena_, entry);
  ASSERT_THAT(fiber_b_or, IsOk());
  Fiber* fiber_b = fiber_b_or.value();

  // Make sure it is null to begin with
  EXPECT_EQ(Fiber::GetCurrentFiber(), nullptr);

  // Make sure fiber_a is reading the current fiber correctly.
  fiber_a->Jump();
  EXPECT_EQ(captured_ptr, fiber_a);

  // Should now be nullptr again
  EXPECT_EQ(Fiber::GetCurrentFiber(), nullptr);

  // Same check for fiber_b
  fiber_b->Jump();
  EXPECT_EQ(captured_ptr, fiber_b);

  // One last time should be nullptr
  EXPECT_EQ(Fiber::GetCurrentFiber(), nullptr);

  // Cleanup
  Fiber::Reap(fiber_a);
  Fiber::Reap(fiber_b);
}

}

}

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

