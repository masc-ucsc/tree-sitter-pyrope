import os
import random
import subprocess
import glob

SNIPPET_DIR = "../../docs/tmp/"
OUTPUT_FILE = "evaluation_data.txt"
SAMPLE_SIZE = 50

def get_valid_files():
    all_files = glob.glob(os.path.join(SNIPPET_DIR, "file*.prp"))
    valid = []
    for f in all_files:
        res = subprocess.run(["tree-sitter", "parse", f], capture_output=True, text=True)
        if res.returncode == 0 and "ERROR" not in res.stdout:
            valid.append(f)
    return valid

def format_snippet(path):
    res = subprocess.run(["../../prpfmt", path, "-i", "2"], capture_output=True, text=True)
    return res.stdout

def main():
    valid_files = get_valid_files()
    if len(valid_files) < SAMPLE_SIZE:
        print(f"Warning: Only found {len(valid_files)} valid files.")
        sample = valid_files
    else:
        sample = random.sample(valid_files, SAMPLE_SIZE)

    results = []
    with open(OUTPUT_FILE, 'w') as out:
        out.write("PYROPE FORMATTING BLIND EVALUATION\n")
        out.write("==================================\n")
        out.write("Instruction: Evaluate which version is more readable, maintainable, and professionally formatted.\n\n")
        
        for i, path in enumerate(sample, 1):
            with open(path, 'r') as f:
                original = f.read()
            formatted = format_snippet(path)
            
            # Randomize which is A and which is B to prevent bias
            order = ["original", "formatted"]
            random.shuffle(order)
            
            version_a = original if order[0] == "original" else formatted
            version_b = original if order[1] == "original" else formatted
            
            out.write(f"--- SNIPPET {i} ---\n")
            out.write("VERSION A:\n")
            out.write(version_a.strip() + "\n\n")
            out.write("VERSION B:\n")
            out.write(version_b.strip() + "\n")
            out.write("-" * 20 + "\n\n")
            
            # Keep a key for yourself so you know which was which!
            results.append(f"Snippet {i}: A={order[0]}, B={order[1]}")

    with open("evaluation_key.txt", "w") as key:
        key.write("\n".join(results))

    print(f"Done! {len(sample)} pairs written to {OUTPUT_FILE}.")
    print("The randomization key is saved in evaluation_key.txt.")

if __name__ == "__main__":
    main()
