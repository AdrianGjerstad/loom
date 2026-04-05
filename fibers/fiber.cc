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
// loom/fibers/fiber.cc
// -----------------------------------------------------------------------------
//
// Implementation details of loom::Fiber.
//

#include "fibers/fiber.h"

#include <memory>

#include "fibers/schedulable.h"
#include "fibers/stackarena.h"
#include "fibers/stackswitch.h"

namespace loom {

Fiber::~Fiber() {
  if (arena_ == nullptr) {
    return;
  }

  auto* arena = arena_;
  auto* stack = stack_;

  arena_ = nullptr;
  stack_ = nullptr;
  state_ = Fiber::State::kDead;
  task_ = nullptr;
  sp_ = nullptr;
  
  arena->Release(stack);
}

void** Fiber::Suspend() {
  if (state_ == Fiber::State::kRunning) {
    state_ = Fiber::State::kSuspended;
  }

  return &sp_;
}

void Fiber::ContinueFrom(Schedulable* current) {
  // Suspend the current schedulable and get its old_sp pointer.
  void** old_sp = current->Suspend();

  // Mark this fiber as running and run it.
  state_ = Fiber::State::kRunning;
  loom::SwitchStack(sp_, old_sp);
}

Fiber::Fiber(StackArena* arena, void* stack) : arena_(arena), stack_(stack),
    state_(Fiber::State::kSuspended) {
  sp_ = loom::ConfigureStack(stack_, arena_->stack_size(), Fiber::EntryPoint,
                             this);
}

void Fiber::EntryPoint(void* fiber_ptr) {
  auto* fiber = static_cast<Fiber*>(fiber_ptr);

  // Run task
  fiber->task_();

  // Prevent returning from this function at all costs.
  while (true) {
    fiber->state_ = Fiber::State::kDead;
    loom::this_fiber::yield();
  }
}

}

