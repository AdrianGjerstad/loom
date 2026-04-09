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
// loom/fibers/globalworkqueue_test.cc
// -----------------------------------------------------------------------------
//
// Unit tests for loom::GlobalWorkQueue.
//

#include <deque>
#include <memory>
#include <vector>

#include "absl/status/status.h"
#include <gtest/gtest.h>

#include "fibers/globalworkqueue.h"
#include "fibers/schedulable.h"
#include "fibers/workstealingdeque.h"

namespace loom {

namespace {

// Fake of a Schedulable object that simply tracks unique identity.
class FakeSchedulable : public Schedulable {
 public:
  explicit FakeSchedulable(int id) : id_(id) {}

  void** Suspend() override { return nullptr; }
  void ContinueFrom(Schedulable* current) override {}
  State state() const override { return State::kSuspended; }

  int id() const { return id_; }

 private:
  int id_;
};

// Fake of a WorkStealingDeque that does not meet the thread safety guarantees
// made by the real thing, but needed to independently test GlobalWorkQueue.
class FakeWorkStealingDeque : public WorkStealingDeque {
 public:
  // Minimal size for the real structure to reduce memory overhead as much as
  // possible.
  FakeWorkStealingDeque() : WorkStealingDeque(2) {}

  size_t size() const override { return work_.size(); }

  // For simplicity's sake, this method never fails.
  absl::Status Push(Work work) override {
    work_.push_back(std::move(work));
    return absl::OkStatus();
  }

  // Maintains stack-like behavior of the real thing.
  Work Pop() override {
    Work work = std::move(work_.back());
    work_.pop_back();

    return work;
  }

  size_t StealBatch(Batch* output) override {
    size_t count = work_.size() / 2;

    for (size_t i = 0; i < count; ++i) {
      output->push_back(std::move(work_.front()));
      work_.pop_front();
    }

    return count;
  }

 private:
  std::deque<Work> work_;
};

// Helper to create an opaque std::unique_ptr<Schedulable> with a unique ID
std::unique_ptr<Schedulable> MakeWork(int id) {
  return std::make_unique<FakeSchedulable>(id);
}

// Helper to check the ID of a FakeSchedulable masquerading as a Schedulable.
int GetId(const std::unique_ptr<Schedulable>& work) {
  return static_cast<FakeSchedulable*>(work.get())->id();
}

// Helper to create a batch of work with sequential IDs starting from the given
// ID.
std::vector<std::unique_ptr<Schedulable>> MakeBatch(size_t size,
                                                    int starting_id = 0) {
  std::vector<std::unique_ptr<Schedulable>> batch;
  for (size_t i = 0; i < size; ++i) {
    batch.push_back(MakeWork(starting_id + i));
  }

  return batch;
}

// GlobalWorkQueue Test Fixture
class GlobalWorkQueueTest : public :: testing::Test {
 protected:
  GlobalWorkQueue queue_;
};

TEST_F(GlobalWorkQueueTest, PushesAndPopsSingleWork) {
  queue_.Push(MakeWork(0));
  EXPECT_EQ(queue_.size(), 1u);

  auto work = queue_.Pop();
  ASSERT_NE(work, nullptr);
  EXPECT_EQ(GetId(work), 0);
  EXPECT_EQ(queue_.size(), 0u);
}

TEST_F(GlobalWorkQueueTest, MaintainsFifoOrder) {
  queue_.Push(MakeWork(1));
  queue_.Push(MakeWork(2));
  queue_.Push(MakeWork(3));

  EXPECT_EQ(GetId(queue_.Pop()), 1);
  EXPECT_EQ(GetId(queue_.Pop()), 2);
  EXPECT_EQ(GetId(queue_.Pop()), 3);
}

TEST_F(GlobalWorkQueueTest, PushBatchClearsInputVector) {
  auto batch = MakeBatch(2, 0);

  queue_.PushBatch(&batch);

  EXPECT_EQ(batch.size(), 0u); // Documentation says batch is empty after call
  EXPECT_EQ(queue_.size(), 2u);
}

TEST_F(GlobalWorkQueueTest, PopBatchRespectsLimitAndExistingVectorContent) {
  for (int i = 0; i < 10; ++i) {
    queue_.Push(MakeWork(i));
  }

  std::vector<std::unique_ptr<Schedulable>> batch;
  // Put existing work in the batch to ensure it isn't cleared
  batch.push_back(MakeWork(999));

  size_t popped = queue_.PopBatch(&batch, 5);

  EXPECT_EQ(popped, 5u);
  ASSERT_EQ(batch.size(), 6u); // 1 original + 5 popped
  EXPECT_EQ(GetId(batch[0]), 999);
  EXPECT_EQ(GetId(batch[1]), 0);
  EXPECT_EQ(GetId(batch[2]), 1);
  EXPECT_EQ(GetId(batch[3]), 2);
  EXPECT_EQ(GetId(batch[4]), 3);
  EXPECT_EQ(GetId(batch[5]), 4);
  EXPECT_EQ(queue_.size(), 5u);  // 5 items still remain to be popped
}

TEST_F(GlobalWorkQueueTest, DrainLocalQueueMovesHalfOfWork) {
  FakeWorkStealingDeque local_deque;
  local_deque.Push(MakeWork(1)).IgnoreError();
  local_deque.Push(MakeWork(2)).IgnoreError();
  local_deque.Push(MakeWork(3)).IgnoreError();
  local_deque.Push(MakeWork(4)).IgnoreError();

  // Should move 2 items (half of 4)
  queue_.DrainLocalQueue(&local_deque);

  EXPECT_EQ(local_deque.size(), 2u);
  EXPECT_EQ(queue_.size(), 2u);

  // Verify IDs to ensure correct order of stealing (oldest work)
  EXPECT_EQ(GetId(queue_.Pop()), 1);
}

TEST_F(GlobalWorkQueueTest, PopEmptyQueueReturnsNullptr) {
  auto work = queue_.Pop();
  EXPECT_EQ(work, nullptr);

  std::vector<std::unique_ptr<Schedulable>> batch;
  size_t popped = queue_.PopBatch(&batch, 10);
  EXPECT_EQ(popped, 0u);
}

}

}

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

