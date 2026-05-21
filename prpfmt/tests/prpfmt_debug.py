#!/usr/bin/env python3
import subprocess
import os
import sys
import datetime
import argparse
import re

# Helper for natural sorting (e.g., file2.prp before file10.prp)
def natural_sort_key(s):
    return [int(text) if text.isdigit() else text.lower()
            for text in re.split('([0-9]+)', s)]

def remove_comments(text):
    # Remove block comments
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.DOTALL)
    # Remove line comments
    text = re.sub(r'//.*', '', text)
    # Strip each line and remove empty lines to compare essential content
    lines = [line.strip() for line in text.splitlines()]
    return "\n".join(line for line in lines if line)

def remove_whitespace(text):
    # Remove all whitespace characters including newlines
    return re.sub(r'\s+', '', text)

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

def run_interactive_loop(files_to_test, prpfmt_executable):
    last_compiled_time = "N/A"
    if os.path.exists(prpfmt_executable):
        last_compiled_timestamp = os.path.getmtime(prpfmt_executable)
        last_compiled_time = datetime.datetime.fromtimestamp(last_compiled_timestamp).strftime('%Y-%m-%d %H:%M:%S')

    print(f"\nExecutable: {prpfmt_executable} (Last compiled: {last_compiled_time})")
    print("Interactive Mode:")
    print("  - [Enter]: Next file")
    print("  - [Index]: Type 0-{0} to jump to that index".format(len(files_to_test)-1))
    print("  - [Name]: Type part of a filename to jump")
    print("  - [q]: Quit")
    
    current_index = 0
    while current_index < len(files_to_test):
        file_path = files_to_test[current_index]
        filename = os.path.basename(file_path)
        
        try:
            with open(file_path, 'r') as f:
                original_content = f.read()
        except Exception as e:
            print(f"Error reading {file_path}: {e}")
            current_index += 1
            continue

        try:
            result = subprocess.run(
                [prpfmt_executable, file_path, "-v"],
                capture_output=True,
                text=True,
                check=False
            )
            formatted_content = result.stdout
            if result.returncode != 0:
                print(f"--- prpfmt error for {filename} (Exit: {result.returncode}) ---")
                if result.stderr:
                    print(result.stderr)
        except Exception as e:
            print(f"Error running prpfmt on {filename}: {e}")
            current_index += 1
            continue

        display_side_by_side(original_content, formatted_content, file_path)

        while True:
            prompt = f"({current_index}/{len(files_to_test)-1}) {filename} > Next (Enter), Jump, or Quit (q): "
            user_input = input(prompt).strip()
            if user_input.lower() == 'q':
                return
            elif user_input == '':
                current_index += 1
                break
            elif user_input.isdigit():
                target_idx = int(user_input)
                if 0 <= target_idx < len(files_to_test):
                    current_index = target_idx
                    break
                else:
                    # Try to find fileN.prp where N is the input
                    found = False
                    for idx, path in enumerate(files_to_test):
                        fname = os.path.basename(path)
                        if fname == f"file{user_input}.prp" or fname == f"{user_input}.prp":
                            current_index = idx
                            found = True
                            break
                    if found: break
                    print(f"Invalid index or file number.")
            else:
                # Search by filename
                matches = [idx for idx, path in enumerate(files_to_test) if user_input in os.path.basename(path)]
                if matches:
                    current_index = matches[0]
                    if len(matches) > 1:
                        print(f"Multiple matches found, jumping to first: {os.path.basename(files_to_test[matches[0]])}")
                    break
                print("No matches for that name.")

def main():
    parser = argparse.ArgumentParser(description="Interactively test prpfmt on .prp files.")
    parser.add_argument("directory", help="Directory containing .prp files.")
    parser.add_argument("range", nargs="?", help="Range of files (e.g., '1-9'). Filters by fileN.prp if present, else uses 0-based indices.")
    parser.add_argument("-s", "--stats", action="store_true", 
                        help="Report statistics instead of interactive mode.")
    parser.add_argument("-b", "--bin", default="../prpfmt", help="Path to prpfmt executable (default: ../prpfmt)")
    args = parser.parse_args()

    # Resolve binary path
    prpfmt_executable = args.bin
    if not os.path.isabs(prpfmt_executable):
        # Try relative to CWD first, then relative to script if that fails
        if not os.path.exists(prpfmt_executable):
            script_dir = os.path.dirname(os.path.abspath(__file__))
            candidate = os.path.abspath(os.path.join(script_dir, args.bin))
            if os.path.exists(candidate):
                prpfmt_executable = candidate

    if not os.path.exists(prpfmt_executable):
        print(f"Error: prpfmt executable not found at {args.bin}")
        sys.exit(1)
    
    if not os.path.isdir(args.directory):
        print(f"Error: Directory not found: {args.directory}")
        sys.exit(1)

    # Discovery and Natural Sort
    all_files = sorted([f for f in os.listdir(args.directory) if f.endswith(".prp")], key=natural_sort_key)
    
    if not all_files:
        print(f"No .prp files found in {args.directory}")
        sys.exit(1)

    files_to_test_names = all_files

    # Range Handling
    if args.range:
        try:
            range_parts = args.range.split('-')
            if len(range_parts) == 2:
                start_val = int(range_parts[0])
                end_val = int(range_parts[1])
                
                # Check if we should use numeric filenames or indices
                # Prioritize numeric filtering if many files follow fileN.prp pattern
                file_n_pattern = re.compile(r'file(\d+)\.prp')
                numeric_files = {int(file_n_pattern.search(f).group(1)): f 
                                 for f in all_files if file_n_pattern.search(f)}
                
                if len(numeric_files) > len(all_files) / 2:
                    files_to_test_names = [numeric_files[n] for n in sorted(numeric_files.keys()) 
                                          if start_val <= n <= end_val]
                else:
                    # Index Mode
                    start_idx = max(0, start_val)
                    end_idx = min(len(all_files) - 1, end_val)
                    files_to_test_names = all_files[start_idx : end_idx + 1]
            else:
                raise ValueError
        except ValueError:
            print("Invalid range format. Please use 'start-end' (e.g., '1-9').")
            sys.exit(1)

    files_to_test = [os.path.join(args.directory, f) for f in files_to_test_names]

    if not files_to_test:
        print(f"No .prp files found matching criteria in {args.directory}.")
        sys.exit(0)

    if args.stats:
        exactly_same_count = 0
        exactly_same_no_comments_count = 0
        exactly_same_no_whitespace_count = 0
        input_parse_failed_count = 0
        formatting_broken_count = 0
        wrong_content_files = []
        total_files_processed = 0
        
        for file_path in files_to_test:
            total_files_processed += 1
            filename = os.path.basename(file_path)
            try:
                with open(file_path, 'r') as f:
                    original_content = f.read()
            except Exception as e:
                print(f"Error reading {file_path}: {e}")
                continue

            try:
                result = subprocess.run(
                    [prpfmt_executable, file_path, "-v"],
                    capture_output=True,
                    text=True,
                    check=False
                )
                formatted_content = result.stdout
                stderr = result.stderr
                
                if result.returncode != 0:
                    # Use exit codes for precise categorization
                    if result.returncode == 2:
                        input_parse_failed_count += 1
                    elif result.returncode == 3:
                        formatting_broken_count += 1
                    else:
                        print(f"--- prpfmt error for {filename} (Exit: {result.returncode}) ---", file=sys.stderr)
                        if stderr:
                            print(stderr, file=sys.stderr)
            except Exception as e:
                print(f"Error running prpfmt on {filename}: {e}", file=sys.stderr)
                continue
            
            if original_content == formatted_content:
                exactly_same_count += 1
            
            orig_no_comments = remove_comments(original_content)
            fmt_no_comments = remove_comments(formatted_content)
            if orig_no_comments == fmt_no_comments:
                exactly_same_no_comments_count += 1

            if remove_whitespace(orig_no_comments) == remove_whitespace(fmt_no_comments):
                exactly_same_no_whitespace_count += 1
            else:
                wrong_content_files.append(filename)
        
        print(f"\n--- prpfmt Statistics for {args.directory} ---")
        print(f"Total files:          {total_files_processed}")
        print(f"Identical:            {exactly_same_count}")
        print(f"Identical (no comments): {exactly_same_no_comments_count}")
        print(f"Identical (no whitespace): {exactly_same_no_whitespace_count}")
        print(f"Input Parse Failed:   {input_parse_failed_count}")
        print(f"Formatting Broken:    {formatting_broken_count}")
        if wrong_content_files:
            print(f"Files with content differences: {len(wrong_content_files)}")
            if len(wrong_content_files) < 20:
                print(f"Differing: {', '.join(wrong_content_files)}")
        print("---------------------------------------")

    else: # Interactive behavior
        run_interactive_loop(files_to_test, prpfmt_executable)

if __name__ == "__main__":
    main()
