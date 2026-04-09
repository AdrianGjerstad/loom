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
// loom/fibers/workstealingdeque.h
// -----------------------------------------------------------------------------
//
// A work-stealing deque, which is a specialization of a Chase-Lev deque, is the
// data structure used as the local queue for dispatchers. From the perspective
// of the dispatcher, it actually behaves like a stack (LIFO), but other
// dispatchers that wish to steal work grab it off of the bottom of the stack,
// or the tail of the queue.
//
// This approach minimizes contention between threads such that the only time
// that there *is* contention is with a queue size of 1 and both the local
// dispatcher and another thread want to use the work on the queue. Keeping this
// type of contention in mind, loom::WorkStealingDeque is thread-safe, not
// requiring any locks to maintain that safety.
//

#ifndef LOOM_FIBERS_WORKSTEALINGDEQUE_H_
#define LOOM_FIBERS_WORKSTEALINGDEQUE_H_

#include <atomic>
#include <memory>
#include <vector>

#include "absl/status/status.h"

#include "fibers/schedulable.h"

namespace loom {

// loom::WorkStealingDeque
//
// A specialization of a Chase-Lev deque that is built specifically for the
// purpose of storing std::unique_ptr<Schedulable> units until they are ready to
// be run. Moveable, not copyable.
class WorkStealingDeque {
 public:
  // The units that we are returning to the user. We are actually storing raw
  // pointers and that is done to alleviate the need for std::move, which is not
  // by any means an atomic operation.
  typedef std::unique_ptr<Schedulable> Work;

  // A "batch" of work is just a collection of Schedulables that behaves like a
  // vector (and below is defined as a vector).
  typedef std::vector<Work> Batch;

  // Creates a deque with a default capacity of 32.
  WorkStealingDeque() : WorkStealingDeque(32) {}

  // Creates a deque with a given capacity. Capacity is rounded up to a power of
  // 2.
  explicit WorkStealingDeque(size_t capacity);

  // Deletes the work that was scheduled.
  virtual ~WorkStealingDeque();

  // Obtains the current number of Work objects in the deque.
  virtual size_t size() const;

  // From the perspective of the local-thread ONLY, pushes some work onto the
  // queue. Fails if there is no more room on the queue for work to go.
  virtual absl::Status Push(Work work);

  // From the perspective of the local-thread ONLY, pops some work from the
  // queue. Returns a nullptr if there is no data to pop.
  virtual Work Pop();

  // Steals half of the work contained in this work-stealing queue and places it
  // with no particular guarantee as to order in the given vector. Output vector
  // is only appended to, so work that was already in the vector is preserved.
  // Returns the number of entries that were stolen.
  //
  // This method has two uses:
  // - For a local dispatcher that wants to push more work but can't because
  //   there isn't enough space, and so offloads the work to a global queue.
  // - For a dispatcher running on a different thread that wants to steal a
  //   batch of work to start crunching through.
  //
  // The work stolen is the oldest half of the work in the queue.
  virtual size_t StealBatch(Batch* output);

 private:
  // A bitmask that allows for efficient
  size_t mask_;

  // The total capacity of the vector.
  size_t capacity_;

  // This deque is implemented as a fixed-size ring buffer that "drains" out to
  // a global, lock-protected queue if it becomes too full.
  std::vector<std::atomic<Schedulable*>> work_;
  std::atomic<size_t> head_;
  std::atomic<size_t> tail_;
};

}

#endif  // LOOM_FIBERS_WORKSTEALINGDEQUE_H_

