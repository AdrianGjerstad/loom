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
// loom/fibers/globalworkqueue.cc
// -----------------------------------------------------------------------------
//
// Implementation of a global work queue data structure that specializes in
// batch assignment of work to specific runner threads.
//

#include "fibers/globalworkqueue.h"

#include "absl/synchronization/mutex.h"

#include "fibers/schedulable.h"
#include "fibers/workstealingdeque.h"

namespace loom {

namespace {

typedef GlobalWorkQueue::Work Work;
typedef GlobalWorkQueue::Batch Batch;

}

GlobalWorkQueue::~GlobalWorkQueue() {
  // Since all pointers to Schedulable objects in work_ are raw, we have to call
  // their destructors and free them manually by delete'ing them.
  absl::MutexLock lock(mutex_);

  while (!work_.empty()) {
    Schedulable* work = work_.front();
    work_.pop();

    delete work;
  }
}

void GlobalWorkQueue::Push(Work work) {
  absl::MutexLock lock(mutex_);

  work_.push(work.release());
}

void GlobalWorkQueue::PushBatch(Batch* batch) {
  absl::MutexLock lock(mutex_);

  for (auto& work : *batch) {
    work_.push(work.release());
  }

  // All unique_ptrs released their owned objects.
  batch->clear();
}

Work GlobalWorkQueue::Pop() {
  absl::MutexLock lock(mutex_);

  if (work_.empty()) {
    return nullptr;
  }
  
  Schedulable* work = work_.front();
  work_.pop();

  return Work(work);
}

size_t GlobalWorkQueue::PopBatch(Batch* batch, size_t max) {
  absl::MutexLock lock(mutex_);

  if (max > work_.size()) {
    max = work_.size();
  }

  for (size_t i = 0; i < max; ++i) {
    Schedulable* work = work_.front();
    work_.pop();

    batch->emplace_back(work);
  }

  return max;
}

void GlobalWorkQueue::DrainLocalQueue(WorkStealingDeque* local_queue) {
  Batch batch;
  local_queue->StealBatch(&batch);

  PushBatch(&batch);
}

}

