// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace prpparse {

// Owns the bytes of one source file for the lifetime of the parse. All token
// and node text are std::string_view into `bytes_` (no per-token allocation).
// `bytes_` is a std::string, so `data()[size()]` is guaranteed to be '\0'.
class Source_buffer {
public:
  Source_buffer(std::string path, std::string bytes)
      : path_(std::move(path)), bytes_(std::move(bytes)) {
    build_line_table();
  }

  // Loads a file from disk. The stored path is used for srcid/diagnostics; for
  // hhds::Source_locator it must be workspace-relative (no leading '/'), so
  // callers typically pass a relative path here.
  static Source_buffer from_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return Source_buffer(path, ss.str());
  }

  [[nodiscard]] const std::string& path() const { return path_; }
  [[nodiscard]] std::string_view   view() const { return bytes_; }
  [[nodiscard]] const char*        data() const { return bytes_.data(); }
  [[nodiscard]] size_t             size() const { return bytes_.size(); }

  // 1-based line for a byte offset.
  [[nodiscard]] uint32_t line_of(uint32_t offset) const {
    // line_starts_[k] = byte offset of the first char of line k+1.
    // Find the greatest k with line_starts_[k] <= offset.
    size_t lo = 0, hi = line_starts_.size();
    while (lo + 1 < hi) {
      size_t mid = (lo + hi) / 2;
      if (line_starts_[mid] <= offset)
        lo = mid;
      else
        hi = mid;
    }
    return static_cast<uint32_t>(lo + 1);
  }

  // 1-based column for a byte offset.
  [[nodiscard]] uint32_t col_of(uint32_t offset) const {
    uint32_t line = line_of(offset);
    return offset - line_starts_[line - 1] + 1;
  }

private:
  void build_line_table() {
    line_starts_.clear();
    line_starts_.push_back(0);
    for (uint32_t i = 0; i < bytes_.size(); ++i) {
      if (bytes_[i] == '\n')
        line_starts_.push_back(i + 1);
    }
  }

  std::string           path_;
  std::string           bytes_;
  std::vector<uint32_t> line_starts_;
};

}  // namespace prpparse
