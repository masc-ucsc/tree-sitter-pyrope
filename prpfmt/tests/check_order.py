import re

def get_order(filename, pattern):
    with open(filename, 'r') as f:
        content = f.read()
    # Handle multiline declarations and strip parameters
    matches = re.finditer(pattern, content, re.MULTILINE)
    funcs = []
    for match in matches:
        func_name = match.group(1).strip()
        funcs.append(func_name)
    return funcs

header_funcs = get_order('prpfmt.h', r'^void\s+(print_[a-zA-Z0-9_]+|preserve_whitespace|check_format_directives|emit_node_text)\s*\(')
source_funcs = get_order('prpfmt.c', r'^void\s+(print_[a-zA-Z0-9_]+|preserve_whitespace|check_format_directives|emit_node_text)\s*\(')

# Compare
mismatches = []
for h_func in header_funcs:
    if h_func not in source_funcs:
        mismatches.append(f"In header but not in source: {h_func}")

for s_func in source_funcs:
    if s_func not in header_funcs:
        mismatches.append(f"In source but not in header: {s_func}")

print(f"Header count: {len(header_funcs)}")
print(f"Source count: {len(source_funcs)}")

# Check order
print("\nOrder Differences:")
import difflib
import pprint
diff = list(difflib.context_diff(header_funcs, source_funcs))
if not diff:
    print("Exact match!")
else:
    for line in diff:
        print(line)

for m in mismatches:
    print(m)

