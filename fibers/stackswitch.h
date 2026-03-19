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
// loom/fibers/stackswitch.h
// -----------------------------------------------------------------------------
//
// Fibers are just like threads. They require a stack to be able to store
// return addresses, local variables, and other data. The interface declared in
// this file is implemented in assembly for any number of specific
// architectures, and is designed to perform userspace context switching by
// switching between different execution stacks as fast as possible.
//
// Implementor's Note: This family of functions is designed to operate as fast
// as possible, which means that minimal information is saved. For chipsets that
// support SIMD, please use a secondary means of saving SIMD registers, or
// refrain from using SIMD.
//

#ifndef LOOM_FIBERS_FIBERS_STACKSWITCH_H_
#define LOOM_FIBERS_FIBERS_STACKSWITCH_H_

#include <stdint.h>

extern "C" {

// Configures a new stack with the right structure necessary to jump into it
// when ready. Takes the address of the lowest byte in the stack, its size,
// the entry point for the new stack, and its pointer argument. Returns a
// pointer valid as the first argument to loom__switch_to_stack.
//
// For example:
//   auto stack_or = pool.Lease();
//   if (!stack_or.ok()) {
//     // Failure logic
//   }
//   void* stack = stack_or.value();
//
//   // Configure stack
//   void* sp = loom::ConfigreStack(stack, pool.stack_size(), FiberMain, NULL);
//
//   // Start fiber
//   loom::SwitchStack(sp, &this_sp);
void* loom__configure_stack(void* stack_base, uintptr_t stack_size,
                            void(*start)(void*), void* arg);

// Switches to a secondary stack to begin/continue execution on that other
// stack. Takes a stack pointer to jump to after state for this stack has been
// saved, and an address at which to save the stack pointer immediately after
// state has been saved. Please use loom::SwitchStack().
//
// For example:
// with stack pointer B initialized to the stack pointer for B's stack
//  In A:
//   // Normal execution flow
//   loom__switch_to_stack(stack_pointer_B, &stack_pointer_A);
//   // Continues after B switches back.
//  In B:
//   // Top of function in B starts after A switches
//   loom__switch_to_stack(stack_pointer_A, &stack_pointer_B);
//   // Would not continue until other fiber switches back to this stack.
void loom__switch_to_stack(void* destination_sp, void** save_sp);

// Discovers the exact SIMD buffer size necessary to save all SIMD state.
uintptr_t loom__discover_simd_buffer_size(void);

// Saves the system dependent SIMD state in the given buffer. The buffer must be
// the exact size returned by loom__discover_simd_buffer_size().
void loom__save_simd_state(void* state);

// Restores the system dependent SIMD state in the given buffer. The buffer must
// be the exact size returned by loom__discover_simd_buffer_size().
void loom__restore_simd_state(void* state);

}  // extern "C"

namespace loom {

const uintptr_t kSIMDBufferSize = loom__discover_simd_buffer_size();

// Performs the exact same duties as loom__configure_stack, but is wrapped in
// C++-familiar usage.
inline void* ConfigureStack(void* stack_base, uintptr_t stack_size,
                            void(*start)(void*), void* arg) {
  return loom__configure_stack(stack_base, stack_size, start, arg);
}

// Performs the exact same duties as loom__switch_to_stack, but is wrapped in
// C++-familiar usage. The void* pointers are to be viewed from C++ code as
// opaque.
inline void SwitchStack(void* new_sp, void** old_sp) {
  loom__switch_to_stack(new_sp, old_sp);
}

// loom::SIMDGuard
//
// Acts as an RAII interface for saving SIMD data (like x86_64's XMM registers)
// across stack switches. SIMD data is not saved by default because it is too
// expensive to do so for 99% of use cases.
//
// Example:
//
// void Foo() {
//   // Do some work
//
//   {
//     loom::SIMDGuard guard;
//
//     Yield();
//   }
//
//   // Continue SIMD work
// }
class SIMDGuard {
 public:
  // Saves the current SIMD state
  SIMDGuard() : simd_buffer_(new uint8_t[kSIMDBufferSize]) {
    loom__save_simd_state(simd_buffer_);
  }

  // Restores the saved SIMD state
  ~SIMDGuard() {
    loom__restore_simd_state(simd_buffer_);
    delete[] simd_buffer_;
  }

  // Not copyable
  SIMDGuard(const SIMDGuard& other) = delete;
  SIMDGuard& operator=(const SIMDGuard& other) = delete;

 private:
  uint8_t* simd_buffer_;
};

}

#endif  // LOOM_FIBERS_FIBERS_STACKSWITCH_H_

