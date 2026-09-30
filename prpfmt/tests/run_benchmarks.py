import os
import subprocess
import glob

# Paths resolve from this script, whatever the cwd: the docs corpus
# (`make corpus` at the repo root builds full_pyrope/) and the prpfmt built in
# prpfmt/.
TESTS_DIR = os.path.dirname(os.path.abspath(__file__))
PRPFMT = os.path.join(os.path.dirname(TESTS_DIR), "prpfmt")
SNIPPET_DIR = os.path.join(os.path.dirname(os.path.dirname(TESTS_DIR)), "full_pyrope")
BENCH_DIR = os.path.join(TESTS_DIR, "benchmarks")
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
    for f in sorted(all_files):
        try:
            # A snippet is valid when prpfmt formats it (it exits 2 on a parse error).
            res = subprocess.run([PRPFMT, f], capture_output=True, text=True)
            if res.returncode == 0:
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
    res = subprocess.run([PRPFMT, path, "--bench"], capture_output=True, text=True)
    print(res.stderr)

def main():
    if not os.path.exists(BENCH_DIR):
        os.makedirs(BENCH_DIR)
        
    snippets = get_valid_snippets()
    if not snippets:
        print(f"No valid snippets found in {SNIPPET_DIR} (run `make corpus` at the repo root)")
        return

    for name, size in TARGETS.items():
        path = create_bench_file(name, size, snippets)
        run_bench(path)

if __name__ == "__main__":
    main()
