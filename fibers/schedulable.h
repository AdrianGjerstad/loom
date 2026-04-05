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
// loom/fibers/schedulable.h
// -----------------------------------------------------------------------------
//
// This file defines an interface for any task that needs to be schedulable in
// the eyes of the fiber runtime. At the time of writing, this consists
// predominantly of `loom::Fiber` and any fakes used for testing purposes. It
// also defines subroutines for executing these Schedulables.
//

#ifndef LOOM_FIBERS_SCHEDULABLE_H_
#define LOOM_FIBERS_SCHEDULABLE_H_

#include <functional>
#include <memory>

namespace loom {

// loom::Schedulable
//
// A pure virtual interface intended to be derived from by classes that need to
// be able to be scheduled by the scheduler, such as fibers.
class Schedulable {
 public:
  // Virtual destructor for inheritable interface
  virtual ~Schedulable() = default;

  // A list of different states that a Schedulable can be in.
  enum class State {
    kUnknown = 0,
    kRunning,  // Actively running
    kSuspended,  // Not actively running but can be ;)
    kDead,  // Has no more code to execute
  };

  // Tells this Schedulable to mark itself as suspended and returns the location
  // in memory where its stack pointer should be placed. For testing purposes,
  // reliant code should be tolerant of this function returning nullptr,
  // indicating that the Schedulable object has no stack of its own to return
  // to at some point in the future.
  virtual void** Suspend() = 0;

  // Launches this Schedulable object by returning execution to its stack so
  // that it can continue to do work. It is given a raw pointer to the current
  // fiber so that execution resources can be transferred. It can be safely
  // assumed that current_schedulable is set to this.
  //
  // Child classes should call current.Suspend() to obtain that stack pointer to
  // pass the second argument to loom::SwitchStack().
  virtual void ContinueFrom(Schedulable* current) = 0;

  // Obtains this schedulable's current state
  virtual State state() const = 0;
};

// A subnamespace for fiber-related subroutines and standin functions like read
// and accept.
namespace this_fiber {

// Represents the currently executing fiber (schedulable) object. Access allows
// for context switches and jumps, along with I/O scheduling.
thread_local std::unique_ptr<Schedulable> current_schedulable;

// Defines the strategy for scheduling the next schedulable to execute. Usually
// filled in by the dispatcher working on a specific thread, but this function
// must do the following:
//
// - Save the current schedulable by moving it into e.g. a ready queue.
// - Using some dispatch strategy, select a new schedulable to jump to and move
//   it into loom::this_fiber::current_schedulable.
// - Return a reference to the schedulable that was just moved into a ready
//   queue.
thread_local std::function<Schedulable*()> pick_new_schedulable_and_swap;

// Simply yields the execution of the current fiber.
void yield();

}

}

#endif  // LOOM_FIBERS_SCHEDULABLE_H_

