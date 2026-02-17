#!/usr/bin/env python3
import subprocess
import os
import sys
import datetime
import argparse
import re

# Configuration
PRPFMT_EXECUTABLE = "../../../prpfmt"
FULL_PYROPE_DIR = "../../full_pyrope"

def remove_comments(text):
    # Remove line comments
    text = re.sub(r'//.*', '', text)
    # Strip each line and remove empty lines to compare essential content
    lines = [line.strip() for line in text.splitlines()]
    return "\n".join(line for line in lines if line)

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
        exactly_same_no_comments_count = 0
        matching_no_comments_files = []
        total_files_processed = 0
        
        for file_path in files_to_test:
            total_files_processed += 1
            filename = os.path.basename(file_path)
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
                    print(f"--- prpfmt encountered an error for {filename} (Exit Code: {result.returncode}) ---", file=sys.stderr)
                    if result.stderr:
                        print(result.stderr, file=sys.stderr)
                    # Even with error, compare if content is identical (e.g., prpfmt just outputs error message)
            except FileNotFoundError:
                print(f"Error: prpfmt executable not found at {PRPFMT_EXECUTABLE}", file=sys.stderr)
                sys.exit(1)
            except Exception as e:
                print(f"An unexpected error occurred while running prpfmt on {filename}: {e}", file=sys.stderr)
                continue
            
            # Compare original and formatted content
            if original_content == formatted_content:
                exactly_same_count += 1
            
            if remove_comments(original_content) == remove_comments(formatted_content):
                exactly_same_no_comments_count += 1
                matching_no_comments_files.append(filename)
        
        print(f"\n--- prpfmt Statistics ({start_num}-{end_num}) ---")
        print(f"Total files processed: {total_files_processed}")
        print(f"Files with identical original and formatted content: {exactly_same_count}")
        print(f"Files with identical content (comments removed): {exactly_same_no_comments_count}")
        if matching_no_comments_files:
            print(f"Matches (no comments): {', '.join(matching_no_comments_files)}")
        print(f"Files with differences: {total_files_processed - exactly_same_count}")
        print("---------------------------------------")

    else: # Existing interactive behavior
        # Get last compiled time of prpfmt
        last_compiled_time = "N/A"
        if os.path.exists(PRPFMT_EXECUTABLE):
            last_compiled_timestamp = os.path.getmtime(PRPFMT_EXECUTABLE)
            last_compiled_time = datetime.datetime.fromtimestamp(last_compiled_timestamp).strftime('%Y-%m-%d %H:%M:%S')

        print(f"\nLast compiled: {last_compiled_time}")
        print("Interactive Mode:")
        print("  - Press Enter to start/continue sequentially")
        print("  - Type a number (e.g., '5') to jump to file5.prp")
        print("  - Type 'q' to quit")
        
        current_index = 0
        while current_index < len(files_to_test):
            file_path = files_to_test[current_index]
            
            # Read original file content
            try:
                with open(file_path, 'r') as f:
                    original_content = f.read()
            except Exception as e:
                print(f"Error reading {file_path}: {e}")
                current_index += 1
                continue

            # Run prpfmt
            try:
                result = subprocess.run(
                    [PRPFMT_EXECUTABLE, file_path],
                    capture_output=True,
                    text=True,
                    check=False
                )
                formatted_content = result.stdout
                if result.returncode != 0:
                    print(f"--- prpfmt encountered an error for {file_path} (Exit Code: {result.returncode}) ---")
                    if result.stderr:
                        print(result.stderr)
            except Exception as e:
                print(f"An unexpected error occurred while running prpfmt on {file_path}: {e}")
                current_index += 1
                continue

            display_side_by_side(original_content, formatted_content, file_path)

            while True:
                user_input = input("\nNext (Enter), Jump to # (e.g. 10), or Quit (q): ").strip().lower()
                if user_input == 'q':
                    print("Exiting interactive test.")
                    return
                elif user_input == '':
                    current_index += 1
                    break
                elif user_input.isdigit():
                    target_num = int(user_input)
                    # Find the index of file<target_num>.prp in files_to_test
                    target_name = f"file{target_num}.prp"
                    found = False
                    for idx, path in enumerate(files_to_test):
                        if os.path.basename(path) == target_name:
                            current_index = idx
                            found = True
                            break
                    if found:
                        break
                    else:
                        print(f"File {target_name} not found in current test range.")
                else:
                    print("Invalid input. Press Enter, type a file number, or 'q'.")

if __name__ == "__main__":
    main()
