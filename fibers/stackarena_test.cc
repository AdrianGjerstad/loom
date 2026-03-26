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
// loom/fibers/stackarena_test.cc
// -----------------------------------------------------------------------------
//
// Tests for StackArena to ensure that an arena:
//
// A) Can be allocated
// B) Can lease and release stacks
// C) Leases out stacks in a FIFO pattern
// D) Holds up to multithreaded use.
//

#include "fibers/stackarena.h"

#include <atomic>
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

class StackArenaTest : public ::testing::Test {
 protected:
  const uintptr_t kStackSize = 64 * 1024; // 64KB
};

TEST_F(StackArenaTest, AllocationSucceeds) {
  auto arena_or = StackArena::Create(kStackSize);
  ASSERT_THAT(arena_or, IsOk());
}

TEST_F(StackArenaTest, LeaseAndReleaseHappyPath) {
  auto arena_or = StackArena::Create(kStackSize);
  ASSERT_THAT(arena_or, IsOk());
  StackArena arena = std::move(arena_or).value();

  // Should be able to lease
  EXPECT_THAT(arena.Lease(), IsOk());
}

TEST_F(StackArenaTest, LIFOBehaviorForCacheLocality) {
  auto arena_or = StackArena::Create(kStackSize);
  ASSERT_THAT(arena_or, IsOk());
  StackArena arena = std::move(arena_or).value();

  void* first = arena.Lease().value();
  void* second = arena.Lease().value();

  // If we release 'second' then 'first', the next lease 
  // should give us 'first' back (LIFO).
  arena.Release(second);
  arena.Release(first);

  EXPECT_EQ(arena.Lease().value(), first);
  EXPECT_EQ(arena.Lease().value(), second);
}

TEST_F(StackArenaTest, MultithreadedHammerTest) {
  auto arena_or = StackArena::Create(kStackSize);
  ASSERT_THAT(arena_or, IsOk());
  StackArena arena = std::move(arena_or).value();

  std::atomic<bool> running{true};
  auto initial_stack_or = arena.Lease();
  ASSERT_THAT(initial_stack_or, IsOk());
  arena.Release(initial_stack_or.value());

  // One thread constantly leases/releases to rotate the TaggedNode tag
  std::thread hammer([&]() {
    while (running) {
      auto s_or = arena.Lease();
      ASSERT_THAT(s_or, IsOk());
      arena.Release(s_or.value());
    }
  });

  // Main thread checks that we never get a duplicate pointer in the same cycle
  for (int i = 0; i < 100000; ++i) {
    auto s1_or = arena.Lease();
    ASSERT_THAT(s1_or, IsOk());
    auto s2_or = arena.Lease();
    ASSERT_THAT(s2_or, IsOk());
    EXPECT_NE(s1_or.value(), s2_or.value());
    arena.Release(s1_or.value());
    arena.Release(s2_or.value());
  }

  running = false;
  hammer.join();
}

}  // namespace

}  // namespace loom

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

