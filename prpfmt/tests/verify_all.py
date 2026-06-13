#!/usr/bin/env python3
import subprocess
import os
import sys
import argparse
import re

# Import the interactive loop from the other script
try:
    from prpfmt_debug import run_interactive_loop
except ImportError:
    # Handle the case where the script is run from a different directory
    sys.path.append(os.path.dirname(os.path.abspath(__file__)))
    from prpfmt_debug import run_interactive_loop

# Helper for natural sorting (e.g., file2.prp before file10.prp)
def natural_sort_key(s):
    return [int(text) if text.isdigit() else text.lower()
            for text in re.split('([0-9]+)', s)]

def run_verification_test(target_dir, prpfmt_bin, debug_mode=False):
    # Resolve absolute path to the executable
    prpfmt_path = prpfmt_bin
    if not os.path.isabs(prpfmt_path):
        if not os.path.exists(prpfmt_path):
            script_dir = os.path.dirname(os.path.abspath(__file__))
            candidate = os.path.abspath(os.path.join(script_dir, prpfmt_bin))
            if os.path.exists(candidate):
                prpfmt_path = candidate

    if not os.path.exists(prpfmt_path):
        print(f"Error: prpfmt executable not found at {prpfmt_bin}")
        sys.exit(1)
    
    if not os.path.isdir(target_dir):
        print(f"Error: Directory not found: {target_dir}")
        sys.exit(1)

    all_files = sorted([os.path.join(target_dir, f) for f in os.listdir(target_dir) if f.endswith(".prp")], key=natural_sort_key)
    
    if not all_files:
        print(f"No .prp files found in {target_dir}")
        return

    total_files = len(all_files)
    passed_count = 0
    input_parse_failed_count = 0
    output_parse_failed_count = 0
    other_failed_count = 0
    
    failed_paths = []
    input_parse_failed_files = []
    output_parse_failed_files = []
    other_failed_files = []

    print(f"\n--- Running prpfmt verification on {total_files} files in {target_dir} ---")
    print(f"Using binary: {prpfmt_path}")
    
    for file_path in all_files:
        filename = os.path.basename(file_path)
        try:
            # Run prpfmt with verification flag
            result = subprocess.run(
                [prpfmt_path, file_path, "-v"],
                capture_output=True,
                text=True,
                check=False
            )
            
            if result.returncode == 0:
                passed_count += 1
            else:
                stderr = result.stderr
                failed_paths.append(file_path)
                
                # Use exit codes for precise categorization
                if result.returncode == 2:
                    input_parse_failed_count += 1
                    input_parse_failed_files.append(filename)
                    print(f"[FAIL] {filename} (Input Parse Error)")
                elif result.returncode == 3:
                    output_parse_failed_count += 1
                    output_parse_failed_files.append(filename)
                    print(f"[FAIL] {filename} (Formatting Broken)")
                else:
                    other_failed_count += 1
                    other_failed_files.append(filename)
                    print(f"[FAIL] {filename} (Exit Code: {result.returncode})")
                
                if stderr:
                    # Look for the specific warning block or error message
                    lines = stderr.strip().split('\n')
                    important_lines = []
                    capture = False
                    for line in lines:
                        if "WARNING" in line or "Error:" in line:
                            capture = True
                        if capture:
                            important_lines.append(line)
                    if important_lines:
                        print("       " + "\n       ".join(important_lines[:5])) # Limit output

        except Exception as e:
            print(f"[ERROR] Could not process {filename}: {e}")
            other_failed_count += 1
            other_failed_files.append(filename)
            failed_paths.append(file_path)

    # Report Summary
    print("\n" + "="*40)
    print("VERIFICATION SUMMARY")
    print("="*40)
    print(f"Total Files:          {total_files}")
    print(f"Passed:               {passed_count}")
    print(f"Input Parse Failed:   {input_parse_failed_count}")
    print(f"Formatting Broken:    {output_parse_failed_count}")
    if other_failed_count > 0:
        print(f"Other Failures:       {other_failed_count}")
    
    if input_parse_failed_files:
        print("\nInput Parse Failed Files:")
        for f in input_parse_failed_files:
            print(f" - {f}")
            
    if output_parse_failed_files:
        print("\nFormatting Broken Files (CRITICAL):")
        for f in output_parse_failed_files:
            print(f" - {f}")

    if other_failed_files:
        print("\nOther Failed Files:")
        for f in other_failed_files:
            print(f" - {f}")
    
    print("="*40)
    
    if debug_mode and failed_paths:
        print(f"\nLaunching interactive debug mode for {len(failed_paths)} failed files...")
        run_interactive_loop(failed_paths, prpfmt_path)

    return (input_parse_failed_count + output_parse_failed_count + other_failed_count) == 0

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Run prpfmt -v on all .prp files in a directory.")
    parser.add_argument("directory", help="The directory containing .prp files to verify.")
    parser.add_argument("-b", "--bin", default="../prpfmt", help="Path to prpfmt executable (default: ../prpfmt)")
    parser.add_argument("-d", "--debug", action="store_true", help="Launch interactive debug mode for failed files.")
    args = parser.parse_args()

    success = run_verification_test(args.directory, args.bin, args.debug)
    sys.exit(0 if success else 1)
