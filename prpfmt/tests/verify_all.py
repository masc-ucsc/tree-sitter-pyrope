#!/usr/bin/env python3
import subprocess
import os
import sys
import argparse

# Configuration
# Path from the tests/ directory to the prpfmt executable
PRPFMT_EXECUTABLE = "../../../prpfmt"

def run_verification_test(target_dir):
    # Get the absolute path to the executable relative to this script
    script_dir = os.path.dirname(os.path.abspath(__file__))
    prpfmt_path = os.path.abspath(os.path.join(script_dir, PRPFMT_EXECUTABLE))

    if not os.path.exists(prpfmt_path):
        print(f"Error: prpfmt executable not found at {prpfmt_path}")
        sys.exit(1)
    
    if not os.path.isdir(target_dir):
        print(f"Error: Directory not found: {target_dir}")
        sys.exit(1)

    all_files = sorted([os.path.join(target_dir, f) for f in os.listdir(target_dir) if f.endswith(".prp")])
    
    if not all_files:
        print(f"No .prp files found in {target_dir}")
        return

    total_files = len(all_files)
    passed_count = 0
    failed_count = 0
    failed_files = []

    print(f"\n--- Running prpfmt verification on {total_files} files ---")
    
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
                print(f"[PASS] {filename}")
            else:
                failed_count += 1
                failed_files.append(filename)
                print(f"[FAIL] {filename} (Exit Code: {result.returncode})")
                if result.stderr:
                    # Look for the specific warning block
                    lines = result.stderr.strip().split('\n')
                    warning_lines = []
                    capture = False
                    for line in lines:
                        if "WARNING" in line:
                            capture = True
                        if capture:
                            warning_lines.append(line)
                    if warning_lines:
                        print("       " + "\n       ".join(warning_lines))

        except Exception as e:
            print(f"[ERROR] Could not process {filename}: {e}")
            failed_count += 1
            failed_files.append(filename)

    # Report Summary
    print("\n" + "="*40)
    print("VERIFICATION SUMMARY")
    print("="*40)
    print(f"Total Files: {total_files}")
    print(f"Passed:      {passed_count}")
    print(f"Failed:      {failed_count}")
    
    if failed_files:
        print("\nFailed Files:")
        for f in failed_files:
            print(f" - {f}")
    
    print("="*40)
    
    # Return non-zero if any files failed
    sys.exit(1 if failed_count > 0 else 0)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Run prpfmt -v on all .prp files in a directory.")
    parser.add_argument("directory", help="The directory containing .prp files to verify.")
    args = parser.parse_args()

    run_verification_test(args.directory)
