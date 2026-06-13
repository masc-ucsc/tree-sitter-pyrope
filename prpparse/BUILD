# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_test")
load("//tools:copt_default.bzl", "COPTS")

cc_library(
    name = "prpparse",
    srcs = glob(
        ["*.cpp"],
        exclude = ["*_test.cpp"],
        allow_empty = True,
    ),
    hdrs = glob(["*.hpp"]),
    copts = ["-std=c++23"] + COPTS,
    includes = ["."],
    textual_hdrs = glob(
        ["*.def"],
        allow_empty = True,
    ),
    visibility = ["//visibility:public"],
    deps = [
        "@hhds//hhds:core",
    ],
)

cc_binary(
    name = "prpparse_cli",
    srcs = glob(
        ["tests/cli.cpp"],
        allow_empty = True,
    ),
    copts = ["-std=c++23"] + COPTS,
    visibility = ["//visibility:public"],
    deps = [":prpparse"],
)

cc_test(
    name = "prpparse_test",
    srcs = glob(["tests/*_test.cpp"]),
    copts = ["-std=c++23"] + COPTS,
    deps = [
        ":prpparse",
        "@hhds//hhds:core",
        "@googletest//:gtest_main",
    ],
)
