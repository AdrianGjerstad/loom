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
// loom/fibers/fiber.h
// -----------------------------------------------------------------------------
//
// Any good fiber-driven application needs a concept of fibers to operate. That
// is what this file is for. It defines a Fiber object.
//

#ifndef LOOM_FIBERS_FIBER_H_
#define LOOM_FIBERS_FIBER_H_

#include <memory>
#include <utility>

#include "fibers/schedulable.h"
#include "fibers/stackarena.h"

namespace loom {

// loom::Fiber
//
// A single unit of execution within Loom, cooperatively scheduled with many
// thousands of other fibers at any given time. These are good to use for any
// tasks that are primarily I/O bound, such as socket handling.
class Fiber : public Schedulable {
 public:
  // Copying is not allowed. We want to avoid having a fiber running twice
  // simultaneously on the same stack.
  Fiber(const Fiber& other) = delete;
  Fiber& operator=(const Fiber& other) = delete;

  ~Fiber();

  // Creates a new loom::Fiber, getting a stack from the given StackArena and
  // configuring it so that it is ready to run. Takes a variadic argument list
  // for ease of use.
  //
  // Fails if the stack could not be allocated.
  template <typename F, typename... Args>
  static absl::StatusOr<std::unique_ptr<Fiber>> Create(StackArena* arena,
      F&& entry, Args&&... args) {
    // Allocate a stack
    auto stack_or = arena->Lease();
    if (!stack_or.ok()) {
      return stack_or.status();
    }

    void* stack = stack_or.value();

    // Allocate the fiber in a unique_ptr
    auto fiber = std::unique_ptr<Fiber>(new Fiber(arena, stack));

    fiber->task_ = [f = std::forward<F>(entry),
                    tup = std::make_tuple(std::forward<Args>(args)...)]()
                      mutable {
      std::apply(f, tup);
    };

    return fiber;
  }

  void** Suspend() override;

  void ContinueFrom(Schedulable* current) override;

  State state() const override { return state_; }

 private:
  // Initializes a Fiber and configures the given stack for that fiber's needs.
  // Takes ownership of the stack it is given, calling arena->Release() is done
  // in the destructor.
  Fiber(StackArena* arena, void* stack);

  // The entry point that is actually provided to loom::ConfigureStack
  static void EntryPoint(void* fiber_ptr);

  StackArena* arena_;
  void* stack_;
  std::function<void()> task_;

  // Stack pointer. sp_ is the stack pointer of this fiber after a yield.
  void* sp_;

  State state_;
};

}

#endif  // LOOM_FIBERS_FIBER_H_

