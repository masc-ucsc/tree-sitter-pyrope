import os
import subprocess
import glob

SNIPPET_DIR = "../../docs/tmp/"
BENCH_DIR = "benchmarks"
TARGETS = {
    "1KB": 1024,
    "10KB": 10 * 1024,
    "100KB": 100 * 1024,
    "1MB": 1024 * 1024
}

def get_valid_snippets():
    print("Finding valid snippets...")
    all_files = glob.glob(os.path.join(SNIPPET_DIR, "file*.prp"))
    valid = []
    for f in all_files:
        try:
            # Quick check if it parses with tree-sitter
            res = subprocess.run(["tree-sitter", "parse", f], capture_output=True, text=True)
            if res.returncode == 0 and "ERROR" not in res.stdout and "MISSING" not in res.stdout:
                with open(f, 'r') as content:
                    valid.append(content.read())
        except Exception:
            continue
    print(f"Found {len(valid)} valid snippets.")
    return valid

def create_bench_file(name, target_bytes, snippets):
    path = os.path.join(BENCH_DIR, f"bench_{name}.prp")
    current_size = 0
    with open(path, 'w') as out:
        while current_size < target_bytes:
            for s in snippets:
                chunk = s + "\n\n"
                out.write(chunk)
                current_size += len(chunk)
                if current_size >= target_bytes:
                    break
    return path

def run_bench(path):
    print(f"\nBenchmarking {path}...")
    # Run prpfmt with --bench and capture stderr
    res = subprocess.run(["../../prpfmt", path, "--bench"], capture_output=True, text=True)
    print(res.stderr)

def main():
    if not os.path.exists(BENCH_DIR):
        os.makedirs(BENCH_DIR)
        
    snippets = get_valid_snippets()
    if not snippets:
        print("No valid snippets found!")
        return

    for name, size in TARGETS.items():
        path = create_bench_file(name, size, snippets)
        run_bench(path)

if __name__ == "__main__":
    main()
