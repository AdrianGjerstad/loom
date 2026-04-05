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
// loom/fibers/schedulable.cc
// -----------------------------------------------------------------------------
//
// Implementations of the several helper functions found in loom::this_fiber,
// such as yield() and sleep().
//

#include "fibers/schedulable.h"

namespace loom {

namespace this_fiber {

void yield() {
  auto* old_schedulable = ::loom::this_fiber::pick_new_schedulable_and_swap();

  ::loom::this_fiber::current_schedulable->ContinueFrom(old_schedulable);
}

}

}

