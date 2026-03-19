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
// loom/fibers/stackpool_test.cc
// -----------------------------------------------------------------------------
//
// Tests for StackPool to ensure that a pool:
//
// A) Can be allocated
// B) Can lease and release stacks
// C) Leases out stacks in a FIFO pattern
// D) Holds up to multithreaded use.
//

#include "fibers/stackpool.h"

#include <thread>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/status/status_matchers.h"
#include <gtest/gtest.h>

namespace loom {

namespace {

using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;
using ::testing::Not;
using absl::StatusCode;

class StackPoolTest : public ::testing::Test {
 protected:
  const unsigned int kNumStacks = 10;
  const uintptr_t kStackSize = 64 * 1024; // 64KB
};

TEST_F(StackPoolTest, AllocationSucceeds) {
  auto pool_or = StackPool::AllocateStackPool(kNumStacks, kStackSize);
  ASSERT_THAT(pool_or, IsOk());
}

TEST_F(StackPoolTest, LeaseAndReleaseHappyPath) {
  auto pool_or = StackPool::AllocateStackPool(kNumStacks, kStackSize);
  StackPool pool = std::move(pool_or).value();

  // Lease all available stacks
  std::vector<void*> leased_stacks;
  for (unsigned int i = 0; i < kNumStacks; ++i) {
    auto stack_or = pool.Lease();
    ASSERT_THAT(stack_or, IsOk()) << "Failed to lease stack " << i;
    leased_stacks.push_back(stack_or.value());
  }

  // Next lease should fail
  auto failed_lease = pool.Lease();
  EXPECT_THAT(failed_lease, StatusIs(StatusCode::kResourceExhausted));

  // Release them all back
  for (void* ptr : leased_stacks) {
    pool.Release(ptr);
  }

  // Should be able to lease again now
  EXPECT_THAT(pool.Lease(), IsOk());
}

TEST_F(StackPoolTest, LIFOBehaviorForCacheLocality) {
  auto pool_or = StackPool::AllocateStackPool(kNumStacks, kStackSize);
  StackPool pool = std::move(pool_or).value();

  void* first = pool.Lease().value();
  void* second = pool.Lease().value();

  // If we release 'second' then 'first', the next lease 
  // should give us 'first' back (LIFO).
  pool.Release(second);
  pool.Release(first);

  EXPECT_EQ(pool.Lease().value(), first);
  EXPECT_EQ(pool.Lease().value(), second);
}

TEST_F(StackPoolTest, MultithreadedHammerTest) {
  const int kThreads = 8;
  const int kIterations = 1000;
  auto pool_or = StackPool::AllocateStackPool(kThreads * 2, kStackSize);
  StackPool pool = std::move(pool_or).value();

  auto worker = [&pool, kIterations]() {
    for (int i = 0; i < kIterations; ++i) {
      auto stack_or = pool.Lease();
      if (stack_or.ok()) {
        void* ptr = stack_or.value();
        // Mimic some "work" being done on the stack
        std::this_thread::yield(); 
        pool.Release(ptr);
      }
    }
  };

  std::vector<std::thread> threads;
  for (int i = 0; i < kThreads; ++i) {
    threads.emplace_back(worker);
  }

  for (auto& t : threads) {
    t.join();
  }

  // After all that hammering, we should still be able to lease 
  // exactly the number of stacks we started with.
  for (unsigned int i = 0; i < kThreads * 2; ++i) {
    EXPECT_THAT(pool.Lease(), IsOk());
  }
  EXPECT_THAT(pool.Lease(), Not(IsOk()));
}

}  // namespace

}  // namespace loom

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

