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
// loom/fibers/stackpool.h
// -----------------------------------------------------------------------------
//
// Fibers require stacks to run on, and in a performance-minded fiber
// implementation, stacks are expensive to allocate. A stack pool provides a
// single, massive, pre-allocated space in memory from which smaller stacks may
// be leased.
//

#ifndef LOOM_FIBERS_FIBERS_STACKPOOL_H_
#define LOOM_FIBERS_FIBERS_STACKPOOL_H_

#include <atomic>
#include <cstdint>

#include "absl/status/statusor.h"

namespace loom {

// loom::StackPoolNode
//
// A node in a StackPool's free list. The physical memory address at which these
// nodes are placed are the bottom of each referenced stack.
struct StackPoolNode {
  StackPoolNode* next;
};

// loom::TaggedPointer
//
// A reference into the stack pool free list that contains a unique tag,
// allowing for perfect memory order access without locks.
struct TaggedPointer {
  StackPoolNode* ptr;
  uintptr_t tag;
};

// loom::StackPool
//
// A resource pool that manages a huge amount of memory and can divvy it up into
// individual sections of a set size for use as stacks that fibers run on.
class StackPool {
 public:
  // Not default constructible.
  StackPool() = delete;

  // Not copyable.
  StackPool(const StackPool& other) = delete;
  StackPool& operator=(const StackPool& other) = delete;

  // Moveable.
  StackPool(StackPool&& other)
      : free_list_head_(other.free_list_head_.load(std::memory_order_relaxed)),
        stack_size_(other.stack_size_), pool_base_(other.pool_base_),
        pool_size_bytes_(other.pool_size_bytes_) {}
  StackPool& operator=(StackPool&& other) = default;

  // Creates a stack pool with the given metadata and a pre-allocated region of
  // memory to use to back the pool. pool_mem MUST be allocated using the OS's
  // specific way of allocating large blocks of memory. For this reason,
  // StackPool::AllocateStackPool is provided to hide all of the system-specific
  // logic and error handling.
  StackPool(void* pool_mem, unsigned int stacks, uintptr_t stack_size);

  // Releases all stacks allocated by this pool back to the kernel. Please note
  // that the memory released is not overwritten, only freed.
  ~StackPool();

  // Allocates a new stack pool with the given number of stacks, each of
  // stack_size length (in bytes). Use this method instead of the constructor.
  static absl::StatusOr<StackPool> AllocateStackPool(unsigned int stacks,
                                                     uintptr_t stack_size);

  // Lease()
  //
  // Asks the StackPool for a stack to use for a fiber. If the StackPool has one
  // available, it will return a pointer to the bottom of the stack. If it does
  // not, Lease() will fail with absl::StatusCode::kResourceExhausted.
  //
  // To obtain the size of the stacks that this pool provides, stack_size() is
  // available.
  absl::StatusOr<void*> Lease();

  // Release()
  //
  // Informs the StackPool that the given stack is no longer needed and should
  // be freed up for another fiber to use.
  //
  // It results in undefined behavior to pass a pointer that is not the base of
  // a fixed-size stack to this method.
  void Release(void* stack);

  // Obtains the size of stacks returned by Lease().
  uintptr_t stack_size() const {
    return stack_size_;
  }

 private:
  alignas(16) std::atomic<TaggedPointer> free_list_head_;
  uintptr_t stack_size_;

  // Used for munmap, VirtualFree, etc.
  void* pool_base_;
  uintptr_t pool_size_bytes_;
};

}

#endif  // LOOM_FIBERS_FIBERS_STACKPOOL_H_

