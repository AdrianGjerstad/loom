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
// loom/fibers/stackpool.cc
// -----------------------------------------------------------------------------
//
// Cross-platform components of a StackPool.
//

#include "fibers/stackpool.h"

#include <atomic>
#include <cstdint>

#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace loom {

StackPool::StackPool(void* pool_mem, unsigned int stacks, uintptr_t stack_size)
    : free_list_head_({static_cast<StackPoolNode*>(pool_mem), 0}),
      stack_size_(stack_size), pool_base_(pool_mem),
      pool_size_bytes_(stack_size * stacks) {
  // Set up free list
  StackPoolNode* node = free_list_head_.load(std::memory_order_relaxed).ptr;
  for (unsigned int i = 0; i < stacks - 1; ++i) {
    node->next = reinterpret_cast<StackPoolNode*>((uintptr_t)node + stack_size);
    node = node->next;
  }

  node->next = NULL;
}

absl::StatusOr<void*> StackPool::Lease() {
  TaggedPointer old_head = free_list_head_.load(std::memory_order_acquire);

  while (old_head.ptr != nullptr) {
    // We are safe to read .next because old_head.ptr is 'acquired'
    StackPoolNode* next_node = old_head.ptr->next;

    TaggedPointer new_head = {next_node, old_head.tag + 1};

    if (free_list_head_.compare_exchange_weak(old_head, new_head,
                                              std::memory_order_acq_rel,
                                              std::memory_order_acquire)) {
      return static_cast<void*>(old_head.ptr);
    }
    // If it fails, old_head is updated with the current (newer) tag/ptr
  }

  return absl::ResourceExhaustedError("no available stack");
}

void StackPool::Release(void* stack) {
  auto* node = static_cast<StackPoolNode*>(stack);
  TaggedPointer old_head = free_list_head_.load(std::memory_order_relaxed);
  TaggedPointer new_head;

  do {
    node->next = old_head.ptr;
    new_head = {node, old_head.tag + 1};
    
    // We use release to ensure node->next is visible before head_ changes
  } while (!free_list_head_.compare_exchange_weak(old_head, new_head,
                                                  std::memory_order_release,
                                                  std::memory_order_relaxed));
}

}

