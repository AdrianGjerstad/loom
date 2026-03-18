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
// loom/fibers/fibers/stackpool_linux.cc
// -----------------------------------------------------------------------------
//
// Implements the Linux-specific functionality for loom::StackPool.
//

#include "fibers/stackpool.h"

#include <errno.h>
#include <sys/mman.h>

#include <cstdint>

#include "absl/status/status.h"
#include "absl/status/statusor.h"

#include <iostream>

namespace loom {

absl::StatusOr<StackPool> StackPool::AllocateStackPool(unsigned int stacks,
                                                       uintptr_t stack_size) {
  // In Linux, we use mmap(2) to allocate the backing memory for stack pools.
  // This gives us more control over stack alignments and access control.
  void* mem = mmap(NULL, stack_size * stacks, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

  if (mem == MAP_FAILED) {
    switch (errno) {
    case EAGAIN:
    case ENOMEM:
    case EOVERFLOW:
      return absl::ResourceExhaustedError("not enough memory for allocation");
    case EINVAL:
      return absl::InvalidArgumentError("size not aligned to page boundary");
    default:
      return absl::UnknownError("mmap failed unexpectedly");
    }
  }

  // StackPool must be constructed in place because it is move-only.
  return absl::StatusOr<StackPool>(absl::in_place, mem, stacks, stack_size);
}

StackPool::~StackPool() {
  munmap(pool_base_, pool_size_bytes_);
}

}

