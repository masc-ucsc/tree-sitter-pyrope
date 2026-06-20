# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_test")
load("//tools:copt_default.bzl", "COPTS")

# Buffer/stack hardening for the first-party parser. Applied only to prpparse's
# own targets (NOT to @hhds or other deps). Mirrors the prpfmt C++ build:
#   _GLIBCXX_ASSERTIONS       bounds-checks std::vector / std::string_view /
#                             std::array. The lexer cursor and token vector use
#                             hand-rolled `< n` guards; this traps an off-by-one
#                             instead of silently corrupting.
#   -fstack-protector-strong  catches classic stack overruns.
# Both are cheap and valid at any optimization level. prpfmt's third flag,
# -D_FORTIFY_SOURCE=2, needs an optimized build (bazel's default fastbuild is
# -O0, where it only warns), so it is left to `-c opt` / .bazelrc.
# To exercise the sanitizers the repo .bazelrc already defines, run e.g.:
#   bazel test --config=asan  //prpparse:prpparse_test
#   bazel test --config=ubsan //prpparse:prpparse_test
PRPPARSE_COPTS = ["-std=c++23"] + COPTS + [
    "-D_GLIBCXX_ASSERTIONS",
    "-fstack-protector-strong",
]

cc_library(
    name = "prpparse",
    srcs = glob(
        ["*.cpp"],
        exclude = ["*_test.cpp"],
        allow_empty = True,
    ),
    hdrs = glob(["*.hpp"]),
    copts = PRPPARSE_COPTS,
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
    copts = PRPPARSE_COPTS,
    visibility = ["//visibility:public"],
    deps = [":prpparse"],
)

cc_test(
    name = "prpparse_test",
    srcs = glob(["tests/*_test.cpp"]),
    copts = PRPPARSE_COPTS,
    deps = [
        ":prpparse",
        "@hhds//hhds:core",
        "@googletest//:gtest_main",
    ],
)
