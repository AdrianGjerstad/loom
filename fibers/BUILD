# SPDX-License-Identifier: Apache-2.0

# Copyright 2026 Adrian Gjerstad.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# -----------------------------------------------------------------------------
# loom/fibers/BUILD
# -----------------------------------------------------------------------------
#
# Bazel build declarations for fiber runtime code.

load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

cc_library(
    name = "stackarena",
    srcs = select({
        "//platforms:linux_x86_64": ["stackarena_linux_x86_64.cc"],
    }),
    hdrs = ["stackarena.h"],
    deps = [
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/synchronization",
    ],
    linkopts = ["-latomic"],
    copts = ["-mcx16"],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "stackpool",
    srcs = [
        "stackpool.cc",
    ] + select({
        "@platforms//os:linux": ["stackpool_linux.cc"],
    }),
    hdrs = ["stackpool.h"],
    deps = [
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
    ],
    linkopts = ["-latomic"],
    copts = ["-mcx16"],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "stackswitch",
    srcs = select({
        "//platforms:linux_x86_64": ["stackswitch_linux_x86_64.S"],
    }),
    hdrs = ["stackswitch.h"],
    visibility = ["//visibility:public"],
)

cc_test(
    name = "stackpool_test",
    srcs = ["stackpool_test.cc"],
    deps = [
        ":stackpool",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/status:status_matchers",
        "@googletest//:gtest",
    ],
)

cc_test(
    name = "stackswitch_test",
    srcs = [
        "stackswitch_test.cc",
    ] + select({
        "//platforms:linux_x86_64": ["stackswitch_linux_x86_64_test.S"],
    }),
    deps = [
        ":stackswitch",
        "@googletest//:gtest",
    ],
)

