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
// loom/fibers/globalworkqueue.h
// -----------------------------------------------------------------------------
//
// A global work queue is a queue that functions as a traditional FIFO queue and
// is surrounded by synchronization primitives and additional functionality that
// suits it well to being able to serve up work on demand.
//

#ifndef LOOM_FIBERS_GLOBALWORKQUEUE_H_
#define LOOM_FIBERS_GLOBALWORKQUEUE_H_

#include <memory>
#include <queue>
#include <vector>

#include "absl/synchronization/mutex.h"

#include "fibers/schedulable.h"
#include "fibers/workstealingdeque.h"

namespace loom {

// loom::GlobalWorkQueue
//
// A thread-safe queue of work that any thread can access at any time, though
// shouldn't do often if they can at all avoid it.
class GlobalWorkQueue {
 public:
  // A unit of work that the user of this queue sees.
  typedef std::unique_ptr<Schedulable> Work;

  // A collection of work that is useful when working with batch operations.
  typedef std::vector<Work> Batch;

  // Creates a new global work queue that is protected and thread-safe.
  GlobalWorkQueue() {}

  // Destroys the work that is still in the queue.
  ~GlobalWorkQueue();

  // Gets the size of the queue
  size_t size() const { return work_.size(); }

  // Pushes a single Schedulable onto the queue. It is better to call PushBatch
  // than to call Push multiple times in a row, so try to prefer that where
  // possible.
  void Push(Work work);

  // Pushes a batch of work onto the queue, typically called when a thread wants
  // to offload older work onto the global queue. Batch is empty after calls to
  // this method, as the unique_ptrs will have been moved into this queue.
  void PushBatch(Batch* batch);

  // Pops a single Schedulable from the queue, if one is available. Returns
  // nullptr if there is no work available. The recommended variant of this is
  // PopBatch().
  Work Pop();

  // Pops a batch of Schedulables from the queue, up to a maximum batch size.
  // This is recommended because it results in fewer round trips being made to
  // this queue, and thus less lock contention overall. Returns the number of
  // Schedulables that were popped. The vector passed in is not cleared before
  // popping, and so the return value may be less than batch->size().
  size_t PopBatch(Batch* batch, size_t max);

  // Offloads half of the given local queue's work onto the global queue.
  // Typically only for when the local thread wants to offload work to try to
  // push more onto its local queue.
  void DrainLocalQueue(WorkStealingDeque* local_queue);

 private:
  // In order to save computation time in a similar manner to WorkStealingDeque,
  // we store raw pointers. These aren't atomic, however, because this queue is
  // protected by locks because it should be accessed infrequently.
  std::queue<Schedulable*> work_;
  
  // The mutex that protects the underlying work queue.
  absl::Mutex mutex_;
};

}

#endif  // LOOM_FIBERS_GLOBALWORKQUEUE_H_

