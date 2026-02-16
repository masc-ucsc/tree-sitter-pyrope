#!/usr/bin/env python3
import subprocess
import os
import sys
import datetime
import argparse

# Configuration
PRPFMT_EXECUTABLE = "../../../prpfmt"
FULL_PYROPE_DIR = "../../full_pyrope"

def get_file_list(start_num, end_num):
    files = []
    for i in range(start_num, end_num + 1):
        file_name = f"file{i}.prp"
        file_path = os.path.join(FULL_PYROPE_DIR, file_name)
        if os.path.exists(file_path):
            files.append(file_path)
        else:
            print(f"Warning: {file_path} not found. Skipping.")
    return files

def display_side_by_side(original_content, formatted_content, file_path):
    print(f"\n--- Comparing: {file_path} ---")
    original_lines = original_content.splitlines()
    formatted_lines = formatted_content.splitlines()

    max_len_original = max(len(line) for line in original_lines) if original_lines else 0
    max_len_formatted = max(len(line) for line in formatted_lines) if formatted_lines else 0
    
    # Ensure equal length for iteration
    max_lines = max(len(original_lines), len(formatted_lines))

    print(f"{'Original'.ljust(max_len_original)} | {'Formatted'}")
    print(f"{'-' * max_len_original} | {'-' * max_len_formatted}")

    for i in range(max_lines):
        orig_line = original_lines[i] if i < len(original_lines) else ""
        fmt_line = formatted_lines[i] if i < len(formatted_lines) else ""
        print(f"{orig_line.ljust(max_len_original)} | {fmt_line}")
    print("-" * (max_len_original + 3 + max_len_formatted))


def main():
    parser = argparse.ArgumentParser(description="Interactively test prpfmt on full_pyrope files.")
    parser.add_argument("range", nargs="?", help="Range of files to test, e.g., '1-9'")
    parser.add_argument("-s", "--stats", action="store_true", 
                        help="Report statistics: count of files with identical original and formatted content.")
    args = parser.parse_args()

    if not os.path.exists(PRPFMT_EXECUTABLE):
        print(f"Error: prpfmt executable not found at {PRPFMT_EXECUTABLE}")
        sys.exit(1)
    
    if not os.path.isdir(FULL_PYROPE_DIR):
        print(f"Error: full_pyrope directory not found at {FULL_PYROPE_DIR}")
        sys.exit(1)

    start_num = 1
    end_num = 84 # Assuming 84 files in full_pyrope, based on previous glob

    if args.range:
        try:
            range_parts = args.range.split('-')
            if len(range_parts) == 2:
                start_num = int(range_parts[0])
                end_num = int(range_parts[1])
            else:
                raise ValueError
        except ValueError:
            print("Invalid range format. Please use 'start-end' (e.g., '1-9').")
            sys.exit(1)
    
    if start_num > end_num:
        print("Start number cannot be greater than end number.")
        sys.exit(1)

    files_to_test = get_file_list(start_num, end_num)

    if not files_to_test:
        print(f"No .prp files found in the specified range ({start_num}-{end_num}) or directory.")
        sys.exit(0)

    if args.stats:
        exactly_same_count = 0
        total_files_processed = 0
        
        for file_path in files_to_test:
            total_files_processed += 1
            # Read original file content
            try:
                with open(file_path, 'r') as f:
                    original_content = f.read()
            except Exception as e:
                print(f"Error reading {file_path}: {e}")
                continue

            # Run prpfmt
            try:
                result = subprocess.run(
                    [PRPFMT_EXECUTABLE, file_path],
                    capture_output=True,
                    text=True,
                    check=False # Do not raise exception for non-zero exit codes immediately
                )
                formatted_content = result.stdout
                if result.returncode != 0:
                    print(f"--- prpfmt encountered an error for {os.path.basename(file_path)} (Exit Code: {result.returncode}) ---", file=sys.stderr)
                    if result.stderr:
                        print(result.stderr, file=sys.stderr)
                    # Even with error, compare if content is identical (e.g., prpfmt just outputs error message)
            except FileNotFoundError:
                print(f"Error: prpfmt executable not found at {PRPFMT_EXECUTABLE}", file=sys.stderr)
                sys.exit(1)
            except Exception as e:
                print(f"An unexpected error occurred while running prpfmt on {os.path.basename(file_path)}: {e}", file=sys.stderr)
                continue
            
            # Compare original and formatted content
            if original_content == formatted_content:
                exactly_same_count += 1
        
        print(f"\n--- prpfmt Statistics ({start_num}-{end_num}) ---")
        print(f"Total files processed: {total_files_processed}")
        print(f"Files with identical original and formatted content: {exactly_same_count}")
        print(f"Files with differences: {total_files_processed - exactly_same_count}")
        print("---------------------------------------")

    else: # Existing interactive behavior
        # Get last compiled time of prpfmt
        last_compiled_time = "N/A"
        if os.path.exists(PRPFMT_EXECUTABLE):
            last_compiled_timestamp = os.path.getmtime(PRPFMT_EXECUTABLE)
            last_compiled_time = datetime.datetime.fromtimestamp(last_compiled_timestamp).strftime('%Y-%m-%d %H:%M:%S')

        print(f"\nLast compiled: {last_compiled_time}")
        
        while True:
            user_input = input("Press Enter to start, or 'q' to quit: ").strip().lower()
            if user_input == 'q':
                print("Exiting interactive test.")
                sys.exit(0)
            elif user_input == '':
                break # Continue with the test
            else:
                print("Invalid input. Press Enter or 'q'.")

        for file_path in files_to_test:
            # Read original file content
            try:
                with open(file_path, 'r') as f:
                    original_content = f.read()
            except Exception as e:
                print(f"Error reading {file_path}: {e}")
                continue

            # Run prpfmt
            try:
                result = subprocess.run(
                    [PRPFMT_EXECUTABLE, file_path],
                    capture_output=True,
                    text=True,
                    check=False # Do not raise exception for non-zero exit codes immediately
                )
                formatted_content = result.stdout
                if result.returncode != 0:
                    print(f"--- prpfmt encountered an error for {file_path} (Exit Code: {result.returncode}) ---")
                    if result.stderr:
                        print(result.stderr)
                    # Continue to display side-by-side even with error
            except FileNotFoundError:
                print(f"Error: prpfmt executable not found at {PRPFMT_EXECUTABLE}")
                sys.exit(1)
            except Exception as e:
                print(f"An unexpected error occurred while running prpfmt on {file_path}: {e}")
                continue

            display_side_by_side(original_content, formatted_content, file_path)

            while True:
                user_input = input("Press Enter to continue, or 'q' to quit: ").strip().lower()
                if user_input == 'q':
                    print("Exiting interactive test.")
                    return
                elif user_input == '':
                    break # Continue to next file
                else:
                    print("Invalid input. Press Enter or 'q'.")

if __name__ == "__main__":
    main()
