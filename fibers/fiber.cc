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

#include "fibers/stackarena.h"
#include "fibers/stackswitch.h"

#include <iostream>

namespace loom {

namespace {

thread_local Fiber* current_fiber = nullptr;

}

Fiber::~Fiber() {
  if (arena_ == nullptr) {
    return;
  }

  arena_ = nullptr;
  stack_ = nullptr;
  state_ = Fiber::State::kDead;
  task_ = nullptr;
  sp_ = nullptr;
  yield_sp_ = nullptr;
}

void Fiber::Reap(Fiber* fiber) {
  if (fiber->state_ != Fiber::State::kSuspended &&
      fiber->state_ != Fiber::State::kDead) {
    // Do not reap a fiber that is already running.
    return;
  }

  auto* arena = fiber->arena_;
  auto* stack = fiber->stack_;

  std::cerr << "vptr for fiber = " << *(void**)fiber << std::endl;

  fiber->~Fiber();
  
  arena->Release(stack);
}

Fiber* Fiber::GetCurrentFiber() {
  return current_fiber;
}

void Fiber::Jump() {
  // Configure this thread's environment for running this fiber
  current_fiber = this;
  state_ = Fiber::State::kRunning;

  // Perform the context switch
  loom::SwitchStack(sp_, &yield_sp_);

  // Fiber has yielded back, reset the environment
  if (state_ == Fiber::State::kRunning) {
    // We only want to update this state if the fiber still has work to do. If
    // the fiber returns, state will already be marked kDead by the time we get
    // here.
    state_ = Fiber::State::kSuspended;
  }
  current_fiber = nullptr;
}

void Fiber::YieldBack() {
  loom::SwitchStack(yield_sp_, &sp_);
}

Fiber::Fiber(StackArena* arena, void* stack) : arena_(arena), stack_(stack),
    state_(Fiber::State::kSuspended) {
  sp_ = loom::ConfigureStack(stack_, arena_->stack_size() - sizeof(Fiber),
                             Fiber::EntryPoint, this);
  yield_sp_ = nullptr;
}

void Fiber::EntryPoint(void* fiber_ptr) {
  auto* fiber = static_cast<Fiber*>(fiber_ptr);

  // Run task
  fiber->task_();

  // Prevent returning from this function at all costs.
  while (true) {
    fiber->state_ = Fiber::State::kDead;
    fiber->YieldBack();
  }
}

}

