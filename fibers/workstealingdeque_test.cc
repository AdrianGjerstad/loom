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
// loom/fibers/workstealingdeque_test.cc
// -----------------------------------------------------------------------------
//
// Unit tests for loom::WorkStealingDeque.
//

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include <gtest/gtest.h>

#include "fibers/schedulable.h"
#include "fibers/workstealingdeque.h"

#include <iostream>

namespace loom {

namespace {

using ::absl_testing::IsOk;
using ::testing::Not;

// A fake "schedulable" that doesn't actually do any work but simply acts as a
// unit of storage within the WorkStealingDeque that stores a unique ID for
// testing purposes.
class FakeSchedulable : public Schedulable {
 public:
  // Creates a fake schedulable object with a unique ID
  explicit FakeSchedulable(int id) : Schedulable(), id_(id) {}

  // Gets the ID of this schedulable
  int id() const { return id_; }

  // No-ops to make FakeSchedulable a concrete class. Should never be called.
  void** Suspend() override { return nullptr; }
  void ContinueFrom(Schedulable*) override {}
  State state() const override { return State::kUnknown; }

 private:
  int id_;
};

// Helper to create an opaque std::unique_ptr<Schedulable> with a unique ID
std::unique_ptr<Schedulable> MakeWork(int id) {
  return std::make_unique<FakeSchedulable>(id);
}

// Helper to check the ID of a FakeSchedulable masquerading as a Schedulable.
int GetId(const std::unique_ptr<Schedulable>& work) {
  return static_cast<FakeSchedulable*>(work.get())->id();
}

// WorkStealingDeque Test Fixure
class WorkStealingDequeTest : public ::testing::Test {
 protected:
  WorkStealingDeque deque_{8};
};

TEST_F(WorkStealingDequeTest, LocalPushPopIsLIFO) {
  ASSERT_THAT(deque_.Push(MakeWork(1)), IsOk());
  ASSERT_THAT(deque_.Push(MakeWork(2)), IsOk());
  ASSERT_THAT(deque_.Push(MakeWork(3)), IsOk());

  EXPECT_EQ(deque_.size(), 3u);

  // Pop should return items in reverse order
  std::unique_ptr<Schedulable> w3 = deque_.Pop();
  ASSERT_NE(w3, nullptr);
  EXPECT_EQ(GetId(w3), 3);
  
  std::unique_ptr<Schedulable> w2 = deque_.Pop();
  ASSERT_NE(w2, nullptr);
  EXPECT_EQ(GetId(w2), 2);

  std::unique_ptr<Schedulable> w1 = deque_.Pop();
  ASSERT_NE(w1, nullptr);
  EXPECT_EQ(GetId(w1), 1);

  // Next Pop should fail
  EXPECT_EQ(deque_.Pop(), nullptr);
  EXPECT_EQ(deque_.size(), 0u);
}

TEST_F(WorkStealingDequeTest, PushFailsWhenFull) {
  // Fill the deque (capacity of 8)
  for (int i = 0; i < 8; ++i) {
    ASSERT_THAT(deque_.Push(MakeWork(i)), IsOk());
  }

  // Trying to push the ninth piece should fail due to being full
  EXPECT_THAT(deque_.Push(MakeWork(9)), Not(IsOk()));
}

TEST_F(WorkStealingDequeTest, StealBatchIsFIFO) {
  // Push 1, 2, 3, 4
  for (int i = 1; i <= 4; ++i) {
    ASSERT_THAT(deque_.Push(MakeWork(i)), IsOk());
  }

  std::vector<std::unique_ptr<Schedulable>> stolen;
  size_t count = deque_.StealBatch(&stolen);

  // StealBatch steals half (4 / 2 = 2)
  EXPECT_EQ(count, 2u);
  ASSERT_EQ(stolen.size(), 2u);

  // StealBatch should take the OLDEST items (1 and 2)
  EXPECT_EQ(GetId(stolen[0]), 1);
  EXPECT_EQ(GetId(stolen[1]), 2);

  // Remaining items in deque should still be 4 and 3 (LIFO order for Pop)
  std::unique_ptr<Schedulable> w4 = deque_.Pop();
  EXPECT_EQ(GetId(w4), 4);
  std::unique_ptr<Schedulable> w3 = deque_.Pop();
  EXPECT_EQ(GetId(w3), 3);
}

TEST_F(WorkStealingDequeTest, ConcurrencyStressTest) {
  const int kNumItems = 65536;
  const int kNumStealers = 4;
  WorkStealingDeque large_deque(2048);

  // Counts the number of processed work items to ensure no duplications or
  // dropped work occurs.
  std::atomic<size_t> items_processed(0);
  std::atomic<bool> done(false);

  auto process_work = [&](std::unique_ptr<Schedulable> work) {
    if (work) {
      items_processed.fetch_add(1);
    }

    // Schedulable is destructed by falling out of scope here
  };

  std::cerr << "Setup complete" << std::endl;

  // Start stealing threads
  std::vector<std::thread> stealers;
  for (int i = 0; i < kNumStealers; ++i) {
    stealers.emplace_back([&]() {
      while (!done.load() || large_deque.size() > 2) {
        std::vector<std::unique_ptr<Schedulable>> batch;
        if (large_deque.StealBatch(&batch) > 0) {
          for (auto& w : batch) {
            process_work(std::move(w));
          }
        }

        // Wait for other threads to do their thing first.
        std::this_thread::yield();
      }
    });
  }

  std::cerr << "Threads started" << std::endl;

  // Owner of the deque pushes and occasionally pops data to "run" itself
  for (int i = 0; i < kNumItems; ++i) {
    while (!large_deque.Push(MakeWork(i)).ok()) {
      // Wait for stealers to make room
      std::this_thread::yield();
    }

    if (i % 10 == 0) {
      process_work(large_deque.Pop());
    }
  }

  std::cerr << "All elements pushed" << std::endl;

  // All items have been "scheduled"
  done.store(true);

  // Join all stealer threads
  for (auto& t : stealers) {
    t.join();
  }

  std::cerr << "All threads joined" << std::endl;

  // Drain all remaining data
  while (std::unique_ptr<Schedulable> w = large_deque.Pop()) {
    process_work(std::move(w));
  }

  // Verify: No items lost, no duplicates (set size), no double-frees (ASAN would catch)
  EXPECT_EQ(items_processed.load(), (size_t)kNumItems);
}

}

}

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

