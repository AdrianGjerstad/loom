/* SPDX-License-Identifier: Apache-2.0 */

/* Copyright 2026 Adrian Gjerstad.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/* -----------------------------------------------------------------------------
 * loom/fibers/fibers/stackswitch_linux_x86_64.S
 * -----------------------------------------------------------------------------
 *
 * Fast stack-switching implementation for x86_64 systems running Linux (SysV/
 * compatible idioms assumed).
 */

.file "stackswitch_linux_x86_64.S"
.globl loom__configure_stack
.type loom__configure_stack,@function
.globl loom__switch_to_stack
.type loom__switch_to_stack,@function
.align 16

/* Used as a trampoline to prevent jumping to unspecified memory. (SEE #3) */
.extern abort

.text

/* In order of lower memory to higher memory (last pushed to first pushed, first
 * popped to last popped), the Linux x86_64 saved context layout is as follows:
 *
 *      +-------+-------+-------+-------+-------+-------+-------+-------+
 *      |  0x0  |  0x8  |  0x10 |  0x18 |  0x20 |  0x28 |  0x30 |  0x38 |
 *      |=======|=======|=======|=======|=======|=======|=======|=======|
 *      |  RDI  |  R15  |  R14  |  R13  |  R12  |  RBX  |  RBP  |  RIP  |
 *      +-------+-------+-------+-------+-------+-------+-------+-------+
 *
 * which consists of 64 bytes of memory.
 * 
 * Special Notes:
 * - RDI is included because in the System V ABI, RDI is the register that holds
 *   the first argument if it is a pointer. In order to support launching new
 *   fibers, this field must be here. (This space would be wasted anyways due to
 *   alignment requirements)
 */

/* loom__configure_stack() - see stackswitch.h
 *
 * %rdi - stack_base
 * %rsi - stack_size
 * %rdx - start
 * %rcx - arg
 */
loom__configure_stack:
  pushq %rbp
  movq %rsp, %rbp

  /* Calculate new stack's pointer and store in rax */
  leaq -1(%rdi,%rsi), %rax
  andq $~0xF, %rax  /* Align to 16 bytes per ABI */

  /* Insert trampoline return address */
  leaq trampoline(%rip), %r8
  movq %r8, (%rax)

  /* Total memory used to save this initial state: 64 bytes (0x40) */
  subq $0x40, %rax

  movq %rdx, 0x38(%rax)  /* rip */
  movq $0, 0x30(%rax)    /* rbp */
  movq %rcx, (%rax)      /* rdi (first argument) */
  
  /* return stack pointer as rax */
  popq %rbp
  ret

trampoline:
  /* This code runs if the entry point of a fiber returns for some reason. The
   * current stack is the fiber that has had an underrun. The stack is in the
   * middle of a 16-byte alignment, leaving room for %rbp to be pushed.
   */

  /* See issue #3 on GitHub. For now we'll just abort. */
  jmp abort

/* loom__switch_to_stack() - see stackswitch.h
 * %rdi - destination_sp
 * %rsi - save_sp
 */
loom__switch_to_stack:
  /* Save context for this stack (return address (rip) is already saved by call)
   */
  pushq %rbp
  pushq %rbx
  pushq %r12
  pushq %r13
  pushq %r14
  pushq %r15
  pushq %rdi

  /* Save this stack pointer and switch to the other stack */
  movq %rsp, (%rsi)
  movq %rdi, %rsp

  /* Load this stack's context */
  popq %rdi
  popq %r15
  popq %r14
  popq %r13
  popq %r12
  popq %rbx
  popq %rbp

  /* Drop into new fiber by popping rip (ret) */
  ret

