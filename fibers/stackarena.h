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
// loom/fibers/stackarena.h
// -----------------------------------------------------------------------------
//
// Fibers require stacks to run on, and in a performance-minded fiber
// implementation, stacks are expensive to allocate. A stack arena provides a
// single, massive, pre-allocated space in memory from which smaller stacks may
// be leased.
//
// Stacks returned as a result of `loom::StackArena::Lease()` all have the
// following properties:
//
// - Unique (system is battle-tested against race conditions involving multiple
//   threads).
// - Aligned to a multiple of the system's memory page size.
// - *Never* executable.
// - Likely to be provided at an address that is advantageous for cache
//   locality.
//

#ifndef LOOM_FIBERS_STACKARENA_H_
#define LOOM_FIBERS_STACKARENA_H_

#include <atomic>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"

namespace loom {

class StackArena {
 public:
  // Copying is disallowed, as doing so would result in double-free upon
  // destruction.
  StackArena(const StackArena& other) = delete;
  StackArena& operator=(const StackArena& other) = delete;

  // Moving is allowed.
  StackArena(StackArena&& other) noexcept;
  StackArena& operator=(StackArena&& other) noexcept;

  // Virtual to allow for mocks and fakes.
  virtual ~StackArena();

  // Creates a new loom::StackArena where each stack is of the given size
  // (aligned to system page boundaries, in favor of more memory).
  //
  // Fails if the allocator could not allocate enough memory to support
  // allocation of the initial chunk of memory.
  static absl::StatusOr<StackArena> Create(size_t stack_size);

  // Attempts to obtain a new stack lease from the arena. Returns the pointer to
  // the address of the lowest byte in the stack. May fail if the allocator
  // could not allocate enough memory to support the operation.
  virtual absl::StatusOr<void*> Lease();

  // Marks a given stack lease as unneeded. Summarily equivalent to UNIX
  // `free()`. It is undefined behavior to pass a pointer to this function that
  // was not previously returned by `Lease()` or has already been passed to
  // `Release()`.
  virtual void Release(void* stack_base);

  // Getters
  size_t stack_size() const { return stack_size_; }

 protected:
  // Invoked by `Create()` to create the initial `StackArena`. No alignment
  // checks are performed on stack_size.
  explicit StackArena(size_t stack_size);

 private:
  // Contains metadata about a chunk acquired by the allocator.
  struct Chunk {
    void* memory;
    size_t size;
  };

  // A single stack in a free list is referenced by this Node struct.
  struct Node {
    Node* next;
  };

  // The free list is accessed like this to prevent ABA races.
  struct TaggedNode {
    Node* node;
    uintptr_t tag;
  };

  // Obtains a new chunk of memory for use for stacks. Will fail if the
  // allocator cannot obtain enough memory.
  absl::Status CreateNewChunk();

  size_t stack_size_;

  // This list is kept for clerical purposes, mostly cleanup at the destructor.
  absl::Mutex chunk_mutex_;
  std::vector<Chunk> chunks_;

  alignas(16) std::atomic<TaggedNode> free_list_head_{TaggedNode{nullptr, 0}};
};

}

#endif  // LOOM_FIBERS_STACKARENA_H_

