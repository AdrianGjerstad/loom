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
#include "fibers/schedulable.h"
#include "fibers/stackarena.h"
#include "fibers/stackswitch.h"

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

// A fake schedulable that just refers to the main stack of execution.
class MainSchedulable : public Schedulable {
 public:
  void** Suspend() override {
    state_ = State::kSuspended;
    return &main_sp_;
  }

  void ContinueFrom(Schedulable* current) override {
    void** old_sp = current->Suspend();

    state_ = State::kRunning;
    loom::SwitchStack(main_sp_, old_sp);
  }

  State state() const { return state_; }

 private:
  void* main_sp_;
  State state_ = State::kRunning;
};

class FiberTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Configure the "dispatch strategy" (return to the "main" stack)
    loom::this_fiber::pick_new_schedulable_and_swap = [&]() {
      *fiber_restore_ = std::move(loom::this_fiber::current_schedulable);

      loom::this_fiber::current_schedulable = std::move(main_sch_);
      return fiber_restore_->get();
    };

    loom::this_fiber::current_schedulable = std::move(main_sch_);
  }

  // Performs the effect of a dispatcher selecting the given fiber to run next
  // on the thread, dispatching it, and dropping back out to the main thread for
  // asserts after the fact (courtesy of SetUp() configuring
  // pick_new_schedulable_and_swap).
  void DispatchFiber(std::unique_ptr<Schedulable>* fiber) {
    fiber_restore_ = fiber;
    main_sch_ = std::move(loom::this_fiber::current_schedulable);
    loom::this_fiber::current_schedulable = std::move(*fiber);
    loom::this_fiber::current_schedulable->ContinueFrom(main_sch_.get());

    // Because fiber_restore_ was set correctly, fiber should still reference
    // not only a valid fiber, but specifically the exact fiber that was passed
    // in.
  }

  void TearDown() override {
    // Catch memory leaks.
    // IF YOUR TEST FAILED AT THIS ASSERT, NOT ALL STACKS ARE BEING RELEASED.
    ASSERT_EQ(arena_.outstanding_stacks(), 0);
  }

  std::unique_ptr<Schedulable>* fiber_restore_ = nullptr;
  std::unique_ptr<Schedulable> main_sch_ = std::make_unique<MainSchedulable>();
  FakeStackArena arena_ = FakeStackArena(64 * 1024);
};

// A Fiber should be creatable and in a suspended state when it is first
// created.
TEST_F(FiberTest, CreatesSuccessfullyAndHasSuspendedState) {
  auto fiber_or = Fiber::Create(&arena_, []() {});
  ASSERT_THAT(fiber_or, IsOk());
  std::unique_ptr<Fiber> fiber = std::move(fiber_or).value();

  // There should be one stack allocated
  EXPECT_EQ(arena_.outstanding_stacks(), 1);

  // Fiber should have state "kSuspended" when it is first created and is not
  // being executed.
  EXPECT_EQ(fiber->state(), Fiber::State::kSuspended);
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
  std::unique_ptr<Fiber> fiber = std::move(fiber_or).value();

  // Allow the fiber to run.
  DispatchFiber(reinterpret_cast<std::unique_ptr<Schedulable>*>(&fiber));

  // Confirm that the variadic arguments were passed successfully.
  EXPECT_TRUE(executed);
  EXPECT_EQ(captured_int, 42);
  EXPECT_EQ(captured_str, "Fibers are awesome!");

  // Confirm that the fiber was marked as dead.
  EXPECT_EQ(fiber->state(), Fiber::State::kDead);
}

// A Fiber must accurately track its execution state at any given time.
TEST_F(FiberTest, AccuratelyTracksStateOverLifecycle) {
  auto entry = []() {
    EXPECT_EQ(this_fiber::current_schedulable->state(), Fiber::State::kRunning);
    this_fiber::yield();
    EXPECT_EQ(this_fiber::current_schedulable->state(), Fiber::State::kRunning);
  };

  auto fiber_or = Fiber::Create(&arena_, entry);
  ASSERT_THAT(fiber_or, IsOk());
  std::unique_ptr<Fiber> fiber = std::move(fiber_or).value();

  // Should be suspended at creation
  EXPECT_EQ(fiber->state(), Fiber::State::kSuspended);
  
  DispatchFiber(reinterpret_cast<std::unique_ptr<Schedulable>*>(&fiber));

  // Should be back to suspended
  EXPECT_EQ(fiber->state(), Fiber::State::kSuspended);

  // Jump back in one time to finish the fiber
  DispatchFiber(reinterpret_cast<std::unique_ptr<Schedulable>*>(&fiber));

  // Confirm that the fiber was marked as dead.
  EXPECT_EQ(fiber->state(), Fiber::State::kDead);
}

// When running as a schedulable, loom::this_fiber::current_schedulable should
// be an accurate, owning unique_ptr to the currently executing fiber
TEST_F(FiberTest, CurrentSchedulableIsAccurate) {
  Schedulable* captured_ptr = nullptr;
  Schedulable* main_ptr = nullptr;

  auto entry = [&]() {
    captured_ptr = this_fiber::current_schedulable.get();
    main_ptr = main_sch_.get();
  };

  // Create two fibers
  auto fiber_a_or = Fiber::Create(&arena_, entry);
  ASSERT_THAT(fiber_a_or, IsOk());
  std::unique_ptr<Fiber> fiber_a = std::move(fiber_a_or).value();
  
  auto fiber_b_or = Fiber::Create(&arena_, entry);
  ASSERT_THAT(fiber_b_or, IsOk());
  std::unique_ptr<Fiber> fiber_b = std::move(fiber_b_or).value();

  // Make sure fiber_a is reading the current fiber correctly.
  DispatchFiber(reinterpret_cast<std::unique_ptr<Schedulable>*>(&fiber_a));
  EXPECT_EQ(captured_ptr, reinterpret_cast<Schedulable*>(fiber_a.get()));

  // Should also be accurate, because we are technically under a schedulable
  // right now (main_sch_).
  EXPECT_EQ(this_fiber::current_schedulable.get(), main_ptr);

  // Same check for fiber_b
  DispatchFiber(reinterpret_cast<std::unique_ptr<Schedulable>*>(&fiber_b));
  EXPECT_EQ(captured_ptr, reinterpret_cast<Schedulable*>(fiber_b.get()));
}

}

}

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

