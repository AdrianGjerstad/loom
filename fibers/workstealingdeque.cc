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
// loom/fibers/workstealingdeque.cc
// -----------------------------------------------------------------------------
//
// Implementation of loom::WorkStealingDeque as a queue of work to be performed
// that can be stolen from.
//

#include "fibers/workstealingdeque.h"

#include <stdint.h>

#include <atomic>
#include <thread>

#include "absl/status/status.h"

namespace loom {

namespace {

// Takes a size_t-sized integer and rounds it up to the next power of two.
size_t bit_ceil(size_t x) {
  --x;
  
  x |= x >> 1;
  x |= x >> 2;
  x |= x >> 4;
  x |= x >> 8;
#if (SIZE_MAX >= 0xFFFFFFFF)
  // size_t is at least 32-bit
  x |= x >> 16;
#endif
#if (SIZE_MAX >= 0xFFFFFFFFFFFFFFFF)
  // size_t is at least 64-bit
  x |= x >> 32;
#endif

  ++x;
  return x;
}

typedef WorkStealingDeque::Work Work;
typedef WorkStealingDeque::Batch Batch;

}

WorkStealingDeque::WorkStealingDeque(size_t capacity)
    : mask_(bit_ceil(capacity) - 1), capacity_(bit_ceil(capacity)),
      work_(bit_ceil(capacity)) {
  // Head points to the next "cell" in the vector that can hold data. Currently
  // unpopulated. Tail points to the next "cell" to be popped when stealing
  // work. Currently populated. Head - tail calculates the size of the queue.
  head_.store(0, std::memory_order_relaxed);
  tail_.store(0, std::memory_order_relaxed);
}

WorkStealingDeque::~WorkStealingDeque() {
  while (Work w = Pop()) {
    // Allow w to fall out of scope and be deleted.
  }
}

size_t WorkStealingDeque::size() const {
  size_t h = head_.load(std::memory_order_relaxed);
  size_t t = tail_.load(std::memory_order_relaxed);

  return h - t;
}

absl::Status WorkStealingDeque::Push(Work work) {
  size_t h = head_.load(std::memory_order_relaxed);
  size_t t = tail_.load(std::memory_order_acquire);

  if (h - t >= capacity_) {
    // Queue is full
    return absl::ResourceExhaustedError(
      "WorkStealingDeque full, please drain!"
    );
  }

  work_[h & mask_].store(work.release(), std::memory_order_relaxed);
  std::atomic_thread_fence(std::memory_order_release);
  head_.store(h + 1, std::memory_order_relaxed);

  return absl::OkStatus();
}

Work WorkStealingDeque::Pop() {
  size_t h = head_.load(std::memory_order_relaxed);
  if (h == 0) return nullptr;

  h -= 1;
  head_.store(h, std::memory_order_relaxed);
  std::atomic_thread_fence(std::memory_order_seq_cst);

  size_t t = tail_.load(std::memory_order_relaxed);
  if (t > h) {
    // Queue was empty; restore head.
    head_.store(h + 1, std::memory_order_relaxed);
    return nullptr;
  }

  // Extract the work.
  Schedulable* s = work_[h & mask_].load(std::memory_order_relaxed);

  if (t == h) {
    // Competing for the last item with potential stealers.
    if (!tail_.compare_exchange_strong(t, t + 1, 
                                       std::memory_order_seq_cst,
                                       std::memory_order_relaxed)) {
      // Stealer won.
      s = nullptr;
    }
    head_.store(h + 1, std::memory_order_relaxed);
  } else {
    // We are safely away from the tail; clear the slot.
    work_[h & mask_].store(nullptr, std::memory_order_relaxed);
  }

  return Work(s);
}

Work WorkStealingDeque::Steal() {
  size_t t = tail_.load(std::memory_order_acquire);
  std::atomic_thread_fence(std::memory_order_seq_cst);
  size_t h = head_.load(std::memory_order_acquire);

  if (t >= h) return nullptr;

  // We load the work pointer.
  Schedulable* s = work_[t & mask_].load(std::memory_order_relaxed);

  // Attempt to claim this index by moving the tail forward.
  if (!tail_.compare_exchange_strong(t, t + 1, 
                                     std::memory_order_seq_cst,
                                     std::memory_order_relaxed)) {
    return nullptr; // Lost the race to another stealer or Pop().
  }

  return Work(s);
}

size_t WorkStealingDeque::StealBatch(Batch* output) {
  size_t count = 0;
  size_t max_to_steal = size() / 2;
  while (count < max_to_steal) {
    Work work = Steal();
    if (work == nullptr) break;
    
    output->push_back(std::move(work));
    count++;
  }
  return count;
}

}

