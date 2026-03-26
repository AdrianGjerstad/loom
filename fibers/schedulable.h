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
// predominantly of `loom::Fiber` and any fakes used for testing purposes.
//

#ifndef LOOM_FIBERS_SCHEDULABLE_H_
#define LOOM_FIBERS_SCHEDULABLE_H_

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

  // Dispatching logic calls this method to run a step of this schedulable
  // object's task. The dispatching logic should not call this method after the
  // state becomes kDead, but the implementation logic should handle this case
  // anyways.
  virtual void Step() = 0;

  // Obtains this schedulable's current state
  virtual State state() const = 0;
};

}

#endif  // LOOM_FIBERS_SCHEDULABLE_H_

