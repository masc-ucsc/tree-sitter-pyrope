#!/usr/bin/env python3
import sys

def remove_leading_whitespace(input_file_path, output_file_path="test.prp"):
    try:
        with open(input_file_path, 'r') as f_in, open(output_file_path, 'w') as f_out:
            for line in f_in:
                f_out.write(line.lstrip())
    except FileNotFoundError:
        print(f"Error: File '{input_file_path}' not found.")
        sys.exit(1)
    except Exception as e:
        print(f"An error occurred: {e}")
        sys.exit(1)

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: python remove_indent.py <input_file_path>")
        sys.exit(1)
    
    input_file_path = sys.argv[1]
    remove_leading_whitespace(input_file_path)
