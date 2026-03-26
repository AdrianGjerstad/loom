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

#include <utility>

#include "fibers/stackarena.h"

namespace loom {

// loom::Fiber
//
// A single unit of execution within Loom, cooperatively scheduled with many
// thousands of other fibers at any given time. These are good to use for any
// tasks that are primarily I/O bound, such as socket handling.
class Fiber {
 public:
  // Copying is not allowed. We want to avoid having a fiber running twice
  // simultaneously on the same stack.
  Fiber(const Fiber& other) = delete;
  Fiber& operator=(const Fiber& other) = delete;

  // Not moveable. Fibers must only be allocated at the top of the stack they
  // control.
  Fiber(Fiber&& other) = delete;
  Fiber& operator=(Fiber&& other) = delete;

  // A list of different states that a Fiber can be in.
  enum class State {
    kUnknown = 0,
    kRunning,  // Fiber is actively running
    kSuspended,  // Fiber is not actively running but can be ;)
    kDead,  // Fiber has no more code to execute
  };

  // Virtual to allow for mocks and fakes.
  virtual ~Fiber();

  // Creates a new loom::Fiber, getting a stack from the given StackArena and
  // configuring it so that it is ready to run. Takes a variadic argument list
  // for ease of use. Fiber is allocated on the stack itself, so you must
  // remember to `Reap()` it.
  //
  // Fails if the stack could not be allocated.
  template <typename F, typename... Args>
  static absl::StatusOr<Fiber*> Create(StackArena* arena, F&& entry,
      Args&&... args) {
    // Allocate a stack
    auto stack_or = arena->Lease();
    if (!stack_or.ok()) {
      return stack_or.status();
    }

    // Place the Fiber object itself at the top of the stack.
    void* stack = stack_or.value();

    void* fiber_ptr = reinterpret_cast<void*>(
        (reinterpret_cast<uintptr_t>(stack) + arena->stack_size()
         - sizeof(Fiber)) & ~(0xF));
    Fiber* fiber = new(fiber_ptr) Fiber(arena, stack);

    fiber->task_ = [f = std::forward<F>(entry),
                    tup = std::make_tuple(std::forward<Args>(args)...)]()
                      mutable {
      std::apply(f, tup);
    };

    return fiber;
  }

  // Cleans up a finished loom::Fiber. Destructor is not called automatically
  // because of where the Fiber is allocated. Use of the fiber after a call to
  // this function is equivalent to use-after-free.
  static void Reap(Fiber* fiber);

  // Obtains the pointer to the currently running Fiber. Returns nullptr if
  // called outside of a fiber.
  static Fiber* GetCurrentFiber();

  // Suspends execution of the currently-executing thread/fiber and jumps into
  // this fiber for execution. Execution returns to the caller once the fiber
  // yields. MUST NOT be called from within a fiber.
  virtual void Jump();

  // Yields execution of the current Fiber, back to whoever called into it last
  // via Jump(). MUST ONLY be called from within this fiber.
  //
  // The proper call to this function within a fiber looks like this:
  // loom::Fiber::GetCurrentFiber()->YieldBack();
  virtual void YieldBack();

  // Gets the current state of the fiber
  State state() const { return state_; }

 private:
  // Initializes a Fiber and configures the given stack for that fiber's needs.
  Fiber(StackArena* arena, void* stack);

  // The entry point that is actually provided to loom::ConfigureStack
  static void EntryPoint(void* fiber_ptr);

  StackArena* arena_;
  void* stack_;
  std::function<void()> task_;

  // Stack pointers. sp_ is the stack pointer of this fiber after a yield.
  // yield_sp_ is the stack pointer in the stack that we need to jump back into
  // upon this fiber yielding.
  void* sp_;
  void* yield_sp_;

  State state_;
};

}

#endif  // LOOM_FIBERS_FIBER_H_

