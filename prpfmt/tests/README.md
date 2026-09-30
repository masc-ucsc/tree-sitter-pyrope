# Tests Directory

This directory contains the regression tests (Python, plus one C++ API test) and the helper scripts for verifying, debugging, and benchmarking the formatter.

## Core Verification
- `cli_test.py`: Checks CLI behavior, both layouts, width boundaries, comment attachment, shorthand, sorting (and the lists that keep source order), grouping-parenthesis removal, backticks (dropped only where they change nothing; kept on keywords, on the reserved type words such as `` `U4` `` or `` `Bool` ``, on the banned old spellings such as `` `u8` `` and on reserved placeholders such as `` `_0` ``), alignment, comparison spacing (comparisons are always spaced), one-element tuple commas and idempotency.
- `api_test.cc`: Checks the default and mode-aware embeddable APIs, byte-count input, failure behavior, and comparison spacing (always spaced) / one-element tuple commas in both layouts.
- `verify_all.py`: Checks parseability and byte-for-byte idempotency in both modes (`--mode ai|human|both`, default `both`).
- `prpfmt_debug.py`: Provides a side-by-side diff between original and formatted source code. Includes a `--stats` mode for batch content verification.

## Development & Maintenance
- `check_order.py`: Maintenance script to verify that function order is consistent between `prpfmt.h` and `prpfmt.cc`.
- `generate_eval.py`: Generates blind side-by-side comparison data (original vs formatted, from the docs corpus `full_pyrope/`) for qualitative evaluation; writes `evaluation_data.txt` and `evaluation_key.txt` in the cwd.
- `remove_indent.py`: Strips the leading whitespace of every line of a file (written to `test.prp`), to check that formatting restores the indentation.

## Performance
- `run_benchmarks.py`: Concatenates the docs-corpus files (`full_pyrope/`, built by `make corpus` at the repo root) into files of about 1KB, 10KB, 100KB and 1MB under `tests/benchmarks/`, and prints `prpfmt --bench` timings for each.

## Usage
The scripts find the formatter built in `prpfmt/` and the corpus from their own
location, so they run from any directory (`-b <path>` picks another binary).
```bash
# Run side-by-side debugger
python3 tests/prpfmt_debug.py <directory_of_prp_files>

# Run full automated verification
python3 tests/verify_all.py <directory_of_prp_files>

# Formatting speed on 1KB..1MB inputs
python3 tests/run_benchmarks.py
```
