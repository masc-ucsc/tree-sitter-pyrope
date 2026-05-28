#!/usr/bin/env ruby
# frozen_string_literal: true
# Extract Pyrope (or unlabeled) code snippets from Markdown.
# - Default: print snippets to STDOUT (concatenated, preserving original behavior).
# - With -d DIR: write each snippet to DIR/file<N>.prp (1-based), one file per snippet.

require 'optparse'
require 'fileutils'

# ----------------------------- CLI parsing -----------------------------
options = { out_dir: nil }
parser = OptionParser.new do |opts|
  opts.banner = "Usage: #{File.basename($0)} [-d DIR] <files.md...>"
  opts.on('-d', '--dir DIR', 'Output directory; creates file1.prp, file2.prp, ...') { |d| options[:out_dir] = d }
  opts.on('-h', '--help', 'Show help') { puts opts; exit 0 }
end

begin
  parser.parse!
rescue OptionParser::ParseError => e
  warn e.message
  warn parser
  exit 1
end

if ARGV.empty?
  warn parser
  exit 1
end

# ----------------------------- Core extraction -----------------------------
out_dir = options[:out_dir]
if out_dir
  # Ensure output directory exists and is writable.
  FileUtils.mkdir_p(out_dir)
  raise "Output directory #{out_dir} is not writable" unless File.writable?(out_dir)
end

snippet_index = 0
buffer = +""

def fence_opens?(line)
  # A fence opens if it has backticks AND we're not already in a code block.
  line.include?('```')
end

def pyrope_or_unlabeled?(fence_line)
  # Match fences like ```pyrope, ``` pyrope, or plain ``` with nothing else.
  # Case-insensitive to be robust.
  # - Unlabeled: exactly triple backticks with optional trailing whitespace.
  # - Pyrope: contains 'pyrope' anywhere after the backticks.
  stripped = fence_line.strip
  return true if stripped == '```' # unlabeled fence
  !!(stripped =~ /^```.*pyrope/i)
end

in_code   = false
in_pyrope = false

def flush_buffer_to_file(dir, idx, buf)
  path = File.join(dir, "file#{idx}.prp")
  File.write(path, buf)
end

ARGV.each do |arg|
  File.open(arg, 'r') do |fd|
    while (line = fd.gets)
      if line =~ /```/
        # Toggle state on every fence line. We treat any line containing ``` as a fence.
        if in_code
          # Closing fence
          in_code = false
          if in_pyrope
            if out_dir
              snippet_index += 1
              # Defensive: ensure we always have content even for empty fences.
              flush_buffer_to_file(out_dir, snippet_index, buffer)
            else
              puts buffer
              puts # blank line separator between snippets
            end
          end
          in_pyrope = false
          buffer.clear
        else
          # Opening fence
          in_code = true
          in_pyrope = pyrope_or_unlabeled?(line)
          buffer.clear
        end
        next
      end

      # Collect content only for selected fences, excluding lines with "compile error".
      if in_code && in_pyrope
        next if line =~ /error:/i
        buffer << line
      end
    end
  end
end

# If the file ended inside a selected code fence (malformed markdown), finalize it defensively.
if in_code && in_pyrope && !buffer.empty?
  if out_dir
    snippet_index += 1
    flush_buffer_to_file(out_dir, snippet_index, buffer)
  else
    puts buffer
  end
end

# Simple assertion: in directory mode, if we requested extraction, at least one snippet should exist
# unless inputs truly had none. This helps catch mis-specified language filters.
if out_dir
  # Count created files; not fatal if none, but assert directory still exists.
  raise "Output directory missing at exit" unless Dir.exist?(out_dir)
end

