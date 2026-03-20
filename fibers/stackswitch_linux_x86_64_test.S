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
 * loom/fibers/stackswitch_linux_x86_64_test.S
 * -----------------------------------------------------------------------------
 *
 * Assembly-specific stack switching testing helper functions.
 */

.file "stackswitch_linux_x86_64_test.S"
.globl write_simd
.type write_simd,@function
.globl read_simd
.type read_simd,@function
.align 16

.text

/* write_simd() - see stackswitch_test.cc
 *
 * %rdi - value
 */
write_simd:
  /* The SIMD-specific register that we will use is XMM5. */
  movq %rdi, %xmm5

  ret

/* read_simd() - see stackswitch_test.cc
 */
read_simd:
  /* Return contents of SIMD-specific register XMM5. */
  movq %xmm5, %rax

  ret

