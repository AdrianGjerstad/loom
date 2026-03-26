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
// loom/fibers/stackarena_linux_x86_64.cc
// -----------------------------------------------------------------------------
//
// Linux on x86_64 implementation of `loom::StackArena`.
//

#include "fibers/stackarena.h"

#include <errno.h>
#include <sys/mman.h>
#include <unistd.h>

#include <atomic>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"

namespace loom {

namespace {

// Each chunk can only be so big, and we should avoid allocating a bajillion of
// them. This variable dictates how many stacks are supposed to fit into each
// chunk of contiguous memory.
constexpr size_t kStacksPerChunk = 16;

// Syscalls like `mmap` have arguments that are required to be aligned to a
// multiple of the system's page size.
const size_t kPageSize = sysconf(_SC_PAGESIZE);

}

StackArena::StackArena(StackArena&& other) noexcept
    : stack_size_(other.stack_size_), chunks_(other.chunks_),
      free_list_head_(other.free_list_head_.load(std::memory_order_relaxed)) {
  absl::MutexLock lock(other.chunk_mutex_);
  other.chunks_.clear();
  other.free_list_head_.store(StackArena::TaggedNode{nullptr, 0});
}

StackArena& StackArena::operator=(StackArena&& other) noexcept {
  // Make `this` safe to move into first (release chunks)
  absl::MutexLock lock(chunk_mutex_);
  absl::MutexLock otherlock(other.chunk_mutex_);

  free_list_head_.store(StackArena::TaggedNode{nullptr, 0});
  for (const auto& chunk : chunks_) {
    munmap(chunk.memory, chunk.size);
  }
  chunks_.clear();

  // Now we can perform the destructive move.
  stack_size_ = other.stack_size_;
  chunks_ = other.chunks_;
  free_list_head_.store(other.free_list_head_.load(std::memory_order_relaxed));
  other.chunks_.clear();
  other.free_list_head_.store(StackArena::TaggedNode{nullptr, 0});

  return *this;
}

StackArena::~StackArena() {
  absl::MutexLock lock(chunk_mutex_);

  free_list_head_.store(StackArena::TaggedNode{nullptr, 0});

  for (const auto& chunk : chunks_) {
    munmap(chunk.memory, chunk.size);
  }
}

absl::StatusOr<StackArena> StackArena::Create(size_t stack_size) {
  // Align stack_size to page boundary.
  stack_size = (stack_size + kPageSize - 1) & ~(kPageSize - 1);

  // We have to have at least one page of usable memory.
  if (stack_size <= kPageSize) {
    stack_size = 2 * kPageSize;
  }

  StackArena arena(stack_size);
  auto status = arena.CreateNewChunk();
  if (!status.ok()) {
    return status;
  }

  return std::move(arena);
}

absl::StatusOr<void*> StackArena::Lease() {
  StackArena::TaggedNode old_head =
      free_list_head_.load(std::memory_order_acquire);

  while (old_head.node != nullptr) {
    // Access the 'next' pointer stored in the stack itself
    // Note: Node is stored at the beginning of the usable stack memory
    StackArena::Node* next_ptr =
        static_cast<StackArena::Node*>(old_head.node)->next;
    StackArena::TaggedNode new_head = {next_ptr, old_head.tag + 1};

    if (free_list_head_.compare_exchange_weak(old_head, new_head,
                                              std::memory_order_release,
                                              std::memory_order_acquire)) {
      // Tell the kernel that we will need this memory soon.
      madvise(old_head.node, stack_size_, MADV_WILLNEED);
      return static_cast<void*>(old_head.node);
    }
    // on failure, old_head is updated with the current value
  }

  // No free stacks in any current chunk
  auto status = CreateNewChunk();
  if (!status.ok()) {
    return status;
  }

  return Lease();
}

void StackArena::Release(void* stack_base) {
  // Allow kernel to reclaim physical memory but keep virtual reservation
  madvise(stack_base, stack_size_, MADV_DONTNEED);

  StackArena::Node* new_node = static_cast<StackArena::Node*>(stack_base);
  StackArena::TaggedNode old_head =
      free_list_head_.load(std::memory_order_acquire);
  StackArena::TaggedNode new_head;

  do {
    new_node->next = old_head.node;
    new_head = {new_node, old_head.tag + 1};
  } while (!free_list_head_.compare_exchange_weak(old_head, new_head,
                                                  std::memory_order_release,
                                                  std::memory_order_acquire));
}

absl::Status StackArena::CreateNewChunk() {
  absl::MutexLock lock(chunk_mutex_);

  size_t chunk_size = stack_size_ * kStacksPerChunk;

  void* base = mmap(nullptr, chunk_size, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

  if (base == MAP_FAILED) {
    switch (errno) {
    case EAGAIN:
    case ENOMEM:
    case EOVERFLOW:
      return absl::ResourceExhaustedError("not enough memory for allocation");
    default:
      return absl::UnknownError("mmap failed unexpectedly");
    }
  }

  // Push the new stacks into the free list
  for (size_t i = 0; i < kStacksPerChunk; ++i) {
    void* stack = static_cast<char*>(base) + (i * stack_size_);
    Release(stack);
  }

  chunks_.push_back(StackArena::Chunk{base, chunk_size});
  return absl::OkStatus();
}

}

