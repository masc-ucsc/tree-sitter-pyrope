#!/usr/bin/env python3
import subprocess
import os
import sys
import difflib
import re

# Configuration
PRPFMT_EXECUTABLE = "../../prpfmt"
TEST_FILES_DIR = "../full_pyrope"

def normalize(text):
    # Remove comments (// style)
    text = re.sub(r'//.*', '', text)
    # Remove semicolons
    text = text.replace(';', '')
    # Remove all whitespace
    text = re.sub(r'\s+', '', text)
    return text

def run_test(file_path):
    print(f"Testing {file_path}... ", end="")
    
    # Read original file content
    try:
        with open(file_path, 'r') as f:
            original_content = f.read()
    except Exception as e:
        print(f"ERROR (could not read file: {e})")
        return False

    # Run prpfmt and capture output
    try:
        result = subprocess.run(
            [PRPFMT_EXECUTABLE, file_path],
            capture_output=True,
            text=True,
            check=True
        )
        formatted_content = result.stdout
    except subprocess.CalledProcessError as e:
        print(f"FAILURE (prpfmt exited with code {e.returncode})")
        if e.stderr:
            print(e.stderr)
        return False
    except FileNotFoundError:
        print(f"ERROR (executable not found at {PRPFMT_EXECUTABLE})")
        sys.exit(1)

    # Normalize for comparison (ignore whitespace, comments, semicolons)
    norm_original = normalize(original_content)
    norm_formatted = normalize(formatted_content)

    if norm_original == norm_formatted:
        print("SUCCESS (equivalent)")
        return True
    else:
        print("FAILURE (content differs even when normalized)")
        print(f"DEBUG: norm_original:  {norm_original}")
        print(f"DEBUG: norm_formatted: {norm_formatted}")
        # Generate diff of the normalized versions to show where it's failing
        # (Though diffing normalized content can be ugly, it shows the missing/extra tokens)
        diff = difflib.unified_diff(
            original_content.splitlines(keepends=True),
            formatted_content.splitlines(keepends=True),
            fromfile='original',
            tofile='formatted'
        )
        print("".join(diff))
        return False

def main():
    if not os.path.exists(PRPFMT_EXECUTABLE):
        print(f"Error: prpfmt executable not found at {PRPFMT_EXECUTABLE}")
        sys.exit(1)

    if not os.path.isdir(TEST_FILES_DIR):
        print(f"Error: Test directory not found at {TEST_FILES_DIR}")
        sys.exit(1)

    test_files = [
        os.path.join(TEST_FILES_DIR, f) 
        for f in os.listdir(TEST_FILES_DIR) 
        if f.endswith(".prp")
    ]
    test_files.sort()

    passed = 0
    failed = 0

    for file in test_files:
        if run_test(file):
            passed += 1
        else:
            failed += 1

    print("\n" + "-"*50)
    print(f"Test Summary: {passed} passed, {failed} failed")
    
    if failed > 0:
        sys.exit(1)

if __name__ == "__main__":
    main()
