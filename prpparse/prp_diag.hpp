// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// A standalone diagnostic surface shape-compatible with livehd::diag
// (../livehd/core/diag.hpp), so the future LiveHD integration maps 1:1. The
// fields mirror livehd::diag::Diagnostic; `Span` mirrors the load-bearing
// subset of hhds::Source_span (file + byte span + line/col).
//
// Policy (plan decision 3): fail-fast. The first syntax error builds one rich
// Diag and aborts the parse via Parse_error. No error recovery, no ERROR nodes.

#include <cstdint>
#include <exception>
#include <format>
#include <string>
#include <string_view>
#include <vector>

namespace prpparse {

enum class Severity { error, warning, note };

inline std::string_view to_string(Severity s) {
  switch (s) {
    case Severity::error:   return "error";
    case Severity::warning: return "warning";
    case Severity::note:    return "note";
  }
  return "error";
}

// livehd::diag category vocabulary (pinned). prpparse mostly emits "syntax".
inline constexpr std::string_view kCategorySyntax      = "syntax";
inline constexpr std::string_view kCategoryInternal    = "internal";
inline constexpr std::string_view kCategoryUnsupported = "unsupported";

struct Span {
  std::string file;
  uint32_t    start_byte = 0;
  uint32_t    end_byte   = 0;  // exclusive
  uint32_t    start_line = 0;  // 1-based
  uint32_t    start_col  = 0;  // 1-based
  uint32_t    end_line   = 0;
  uint32_t    end_col    = 0;  // exclusive
  bool        valid      = false;

  [[nodiscard]] bool is_null() const { return !valid; }
};

struct Note {
  std::string message;
  Span        span;
};

struct Diag {
  Severity                 severity = Severity::error;
  std::string              code     = "error";   // stable, greppable kebab-case id
  std::string              category{kCategorySyntax};
  std::string              pass     = "prpparse";
  std::string              message;               // one line, no location, no period
  Span                     span;
  std::string              hint;
  std::vector<std::string> see;
  std::vector<Note>        notes;

  // clang-style human rendering: `<file>:<line>:<col>: error[code]: message`.
  [[nodiscard]] std::string render() const {
    std::string out;
    if (span.valid)
      out += std::format("{}:{}:{}: ", span.file, span.start_line, span.start_col);
    out += std::format("{}[{}]: {}", to_string(severity), code, message);
    if (!hint.empty())
      out += std::format("\n  help: {}", hint);
    for (const auto& n : notes) {
      out += "\n  note: ";
      if (n.span.valid)
        out += std::format("{}:{}:{}: ", n.span.file, n.span.start_line, n.span.start_col);
      out += n.message;
    }
    for (const auto& s : see)
      out += std::format("\n  see: {}", s);
    return out;
  }
};

// Fail-fast carrier: thrown on the first syntax error.
struct Parse_error : std::exception {
  Diag        diag;
  std::string rendered;
  explicit Parse_error(Diag d) : diag(std::move(d)), rendered(diag.render()) {}
  [[nodiscard]] const char* what() const noexcept override { return rendered.c_str(); }
};

}  // namespace prpparse
