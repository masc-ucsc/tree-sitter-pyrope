# Tests Directory

This directory contains Python scripts for verifying, debugging, and benchmarking the formatter.

## Core Verification
- `verify_all.py`: Runs the formatter on a directory and checks for errors, AST validity, and idempotency.
- `prpfmt_debug.py`: Provides a side-by-side diff between original and formatted source code. Includes a `--stats` mode for batch content verification.

## Development & Maintenance
- `check_order.py`: Maintenance script to verify that function order is consistent between `prpfmt.h` and `prpfmt.c`.
- `generate_eval.py`: Generates blind side-by-side comparison data for qualitative evaluation.

## Performance
- `run_benchmarks.py`: Generates synthetic test files of varying sizes (1KB to 1MB) and measures formatting performance.

## Usage
```bash
# Run side-by-side debugger
python3 tests/prpfmt_debug.py <directory_of_prp_files>

# Run full automated verification
python3 tests/verify_all.py <directory_of_prp_files>
```
