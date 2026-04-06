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
#include <vector>

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

  work_[h & mask_].store(work.release(), std::memory_order_seq_cst);
  std::atomic_thread_fence(std::memory_order_release);
  head_.store(h + 1, std::memory_order_relaxed);

  return absl::OkStatus();
}

Work WorkStealingDeque::Pop() {
  // Greedily take from the head, and only give back if necessary.
  size_t h = head_.load(std::memory_order_relaxed) - 1;
  head_.store(h, std::memory_order_relaxed);
  std::atomic_thread_fence(std::memory_order_seq_cst);

  size_t t = tail_.load(std::memory_order_relaxed);
  if (t > h) {
    // There was nothing in the queue to begin with
    head_.store(h + 1, std::memory_order_relaxed);
    return nullptr;
  }

  if (t == h) {
    // Potential data race: another thread could attempt to steal right now and
    // it would affect this exact same item we're trying to access.
    if (!tail_.compare_exchange_strong(t, t + 1, std::memory_order_seq_cst,
                                       std::memory_order_relaxed)) {
      // Stealing thread won.
      head_.store(h + 1, std::memory_order_relaxed);
      return nullptr;
    }

    // We may have won the race, but tail still ended up incremented by 1, which
    // would mean that without this line, tail would be ahead by 1 and result in
    // a "size" of -1.
    head_.store(h + 1, std::memory_order_relaxed);
  }

  return Work(work_[h & mask_].load(std::memory_order_seq_cst));
}

size_t WorkStealingDeque::StealBatch(std::vector<Work>* output) {
  size_t t = tail_.load(std::memory_order_acquire);
  std::atomic_thread_fence(std::memory_order_seq_cst);
  size_t h = head_.load(std::memory_order_acquire);

  if (t >= h) {
    // No work to steal
    return 0;
  }

  // We want to steal half of the work here without being greedy, so its going
  // to be (h - t) / 2. For example, if there are 5 entries, we would end up
  // stealing 2 entries.
  size_t steal_count = (h - t) / 2;

  if (!tail_.compare_exchange_strong(t, t + steal_count, 
                                     std::memory_order_seq_cst, 
                                     std::memory_order_relaxed)) {
    // Steal unsuccessful :(
    return 0;
  }
  
  // Steal successful, stash the goodies and make off like a bandit!
  for (size_t i = 0; i < steal_count; ++i) {
    output->emplace_back(work_[(t + i) & mask_].load(
      std::memory_order_seq_cst));
  }

  return steal_count;
}

}

