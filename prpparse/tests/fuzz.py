#!/usr/bin/env python3
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# prpparse syntax-error fuzzer.
#
# Takes valid full_pyrope/*.prp files, applies deterministic SYNTAX-level
# mutations, and uses tree-sitter as the *syntax* oracle (it decides whether a
# mutation actually broke the grammar). Each mutation is classified:
#
#   benign          both accept  -> mutation did not break syntax (ignored)
#   caught          both reject  -> prpparse caught the syntax error  (good)
#   missed          ts rejects, prpparse accepts -> a syntax error prpparse let
#                   through. NOT a hard failure: inou/prp catches it later from
#                   the parse tree. Reported as a capture-rate shortfall.
#   false-positive  prpparse rejects, ts accepts -> prpparse rejected VALID
#                   syntax. This is a real bug; the run fails on any of these.
#
# For `caught` mutations we measure localization: the byte/line distance between
# prpparse's reported error span and the injected edit site.
#
#   python3 prpparse/tests/fuzz.py [--per-file N] [--seed S] [--min-capture P]
#
# Run from the repo root (needs node_modules/tree-sitter-cli + a built parser).

import argparse
import os
import random
import re
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CLI = os.path.join(ROOT, "bazel-bin", "prpparse", "prpparse_cli")
TS = os.path.join(ROOT, "node_modules", "tree-sitter-cli", "tree-sitter")
CORPUS = os.path.join(ROOT, "full_pyrope")
TMP = "/tmp/prpparse_fuzz"

# Approximate tokenizer — only needs plausible edit boundaries, not fidelity.
TOKEN_RE = re.compile(
    r'"(?:\\.|[^"\\])*"'          # double-quoted string
    r"|'(?:[^'\n])*'"            # single-quoted string
    r"|//[^\n]*"                 # line comment
    r"|/\*.*?\*/"                # block comment
    r"|[A-Za-z_$][A-Za-z0-9_$]*"  # identifier / keyword
    r"|0[sSuU]?[xXoObBdD]?[0-9a-fA-F_?]+|[0-9][0-9_]*[KMGT]?"  # number
    r"|\.\.[=<+]|<<=|>>=|\.\.\."  # 3-char operators
    r"|::|\.\.|->|<<|>>|==|!=|<=|>=|[-+*/|&^]="  # 2-char operators
    r"|[()\[\]{}.,;:@#?~!+\-*/%<>&^|=]",          # 1-char
    re.DOTALL,
)

REPLACEMENTS = [")", "(", "]", "[", "}", "{", "+", "*", "=", ":", ",", "@", "#", "if", "else"]
BRACKETS = ["(", ")", "[", "]", "{", "}"]
CLOSERS = [")", "]", "}"]
OPENERS = ["(", "[", "{"]


def tokens(text):
    """Return [(start, end, text)] for non-trivia tokens."""
    out = []
    for m in TOKEN_RE.finditer(text):
        s = m.group(0)
        if s.startswith("//") or s.startswith("/*"):
            continue  # skip comments
        out.append((m.start(), m.end(), s))
    return out


def mutate(text, rng):
    """Return (mutated_text, edit_offset, strategy) or None if not applicable."""
    toks = tokens(text)
    if len(toks) < 3:
        return None
    strat = rng.choice(
        ["del_tok", "dup_tok", "swap", "replace", "ins_bracket", "del_char",
         "truncate", "del_closer", "del_opener", "ins_block_comment",
         "break_block_close", "ins_line_comment"]
    )
    if strat == "ins_block_comment":
        # Opens a block comment; unterminated unless a later */ closes it.
        s, _, _ = rng.choice(toks)
        return text[:s] + "/* " + text[s:], s, strat
    if strat == "break_block_close":
        i = text.find("*/")
        if i < 0:
            return None
        return text[:i] + text[i + 1:], i, strat  # drop the '*' -> unterminated
    if strat == "ins_line_comment":
        s, _, _ = rng.choice(toks)
        return text[:s] + "// " + text[s:], s, strat  # comments out rest of line
    if strat == "del_tok":
        s, e, _ = rng.choice(toks)
        return text[:s] + text[e:], s, strat
    if strat == "dup_tok":
        s, e, t = rng.choice(toks)
        return text[:e] + t + text[e:], e, strat
    if strat == "swap":
        i = rng.randrange(len(toks) - 1)
        s0, e0, t0 = toks[i]
        s1, e1, t1 = toks[i + 1]
        return text[:s0] + t1 + text[e0:s1] + t0 + text[e1:], s0, strat
    if strat == "replace":
        s, e, _ = rng.choice(toks)
        r = rng.choice(REPLACEMENTS)
        return text[:s] + r + text[e:], s, strat
    if strat == "ins_bracket":
        s, _, _ = rng.choice(toks)
        b = rng.choice(BRACKETS)
        return text[:s] + b + text[s:], s, strat
    if strat == "del_char":
        s, e, _ = rng.choice(toks)
        o = rng.randrange(s, e)
        return text[:o] + text[o + 1:], o, strat
    if strat == "truncate":
        s, _, _ = rng.choice(toks[len(toks) // 2:])
        return text[:s], s, strat
    if strat == "del_closer":
        cands = [(s, e) for (s, e, t) in toks if t in CLOSERS]
        if not cands:
            return None
        s, e = rng.choice(cands)
        return text[:s] + text[e:], s, strat
    if strat == "del_opener":
        cands = [(s, e) for (s, e, t) in toks if t in OPENERS]
        if not cands:
            return None
        s, e = rng.choice(cands)
        return text[:s] + text[e:], s, strat
    return None


def run_batch(argv, files, parse_line):
    """Run `argv files...` in chunks; call parse_line(line) per output line."""
    for i in range(0, len(files), 400):
        chunk = files[i:i + 400]
        res = subprocess.run(argv + chunk, cwd=ROOT, capture_output=True, text=True)
        for line in res.stdout.splitlines():
            parse_line(line)


def line_of(text, off):
    return text.count("\n", 0, max(0, min(off, len(text)))) + 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--per-file", type=int, default=8)
    ap.add_argument("--seed", type=int, default=1234)
    ap.add_argument("--min-capture", type=float, default=0.0,
                    help="fail if syntax-error capture rate < this (0..1)")
    ap.add_argument("--sample", type=int, default=0, help="use only the first N corpus files")
    ap.add_argument("--show", type=int, default=0,
                    help="print N missed + N worst-localized examples (and keep their files)")
    args = ap.parse_args()

    if not os.path.exists(CLI):
        subprocess.run(["bazel", "build", "//prpparse:prpparse_cli"], cwd=ROOT, check=True)

    corpus = sorted(f for f in os.listdir(CORPUS) if f.endswith(".prp"))
    if args.sample:
        corpus = corpus[:args.sample]

    if os.path.exists(TMP):
        shutil.rmtree(TMP)
    os.makedirs(TMP)

    # Generate mutations.
    manifest = {}  # mutfile -> (edit_offset, strategy, mutated_text)
    n_mut = 0
    for fname in corpus:
        with open(os.path.join(CORPUS, fname)) as fh:
            text = fh.read()
        rng = random.Random(f"{args.seed}:{fname}")
        for k in range(args.per_file):
            m = mutate(text, rng)
            if m is None:
                continue
            mtext, off, strat = m
            if mtext == text:
                continue
            mf = os.path.join(TMP, f"m{n_mut:06d}.prp")
            with open(mf, "w") as out:
                out.write(mtext)
            manifest[mf] = (off, strat, mtext, fname)
            n_mut += 1

    mutfiles = sorted(manifest)
    if not mutfiles:
        print("no mutations generated", file=sys.stderr)
        return 1

    # prpparse verdicts (--fuzz: "path\tok|err\tstart_byte\tcode").
    pp = {}
    def pp_line(line):
        parts = line.split("\t")
        if len(parts) == 4:
            pp[parts[0]] = (parts[1] == "err", int(parts[2]), parts[3])
    run_batch([CLI, "--fuzz"], mutfiles, pp_line)

    # tree-sitter syntax oracle: -q prints a line only for erroring files, and
    # the line includes the first error node, e.g. "(ERROR [row, col] - ...)".
    # We use that row as the achievable localization bound for each mutation.
    ts_reject = set()
    ts_line_of = {}  # mf -> 1-based error line tree-sitter reports
    err_re = re.compile(r"\((?:ERROR|MISSING)[^[]*\[(\d+),")

    def ts_parse(line):
        if not line.strip():
            return
        tok = line.split()[0]
        mf = tok if os.path.isabs(tok) else os.path.join(ROOT, tok)
        ts_reject.add(mf)
        m = err_re.search(line)
        if m:
            ts_line_of[mf] = int(m.group(1)) + 1
    run_batch([TS, "parse", "-q"], mutfiles, ts_parse)

    # Classify.
    benign = caught = 0
    missed_ex = []
    false_pos = []
    caught_ex = []  # (line_dist, byte_dist, mf, strat, off, pp_byte, code, orig)
    by_code = {}    # code -> [line_dist, ...]
    dists = []
    same_line = 0
    near8 = 0
    vs_ts = []          # signed (pp_line - ts_line); <=0 means as good as / better than ts
    worse_vs_ts = []    # cases where prpparse localizes worse than tree-sitter
    for mf in mutfiles:
        off, strat, mtext, orig = manifest[mf]
        if mf not in pp:
            continue
        pp_rej, pp_byte, pp_code = pp[mf]
        ts_rej = mf in ts_reject
        if ts_rej and pp_rej:
            caught += 1
            d = abs(pp_byte - off)
            pp_line = line_of(mtext, pp_byte)
            ld = abs(pp_line - line_of(mtext, off))
            dists.append(d)
            by_code.setdefault(pp_code, []).append(ld)
            caught_ex.append((ld, d, mf, strat, off, pp_byte, pp_code, orig))
            if d <= 8:
                near8 += 1
            if ld == 0:
                same_line += 1
            if mf in ts_line_of:
                dts = pp_line - ts_line_of[mf]  # >0 => prpparse reports later (worse)
                vs_ts.append(dts)
                if dts > 1:
                    worse_vs_ts.append((dts, mf, strat, pp_code, off, pp_line,
                                        ts_line_of[mf], orig))
        elif ts_rej and not pp_rej:
            missed_ex.append((mf, strat, orig, off))
        elif (not ts_rej) and pp_rej:
            false_pos.append((mf, strat, pp_code, off, pp_byte))
        else:
            benign += 1
    missed = len(missed_ex)

    syntax_errs = caught + missed
    capture = (caught / syntax_errs) if syntax_errs else 1.0
    dists.sort()

    def pct(p):
        return dists[min(len(dists) - 1, int(p * len(dists)))] if dists else 0

    print("=" * 56)
    print(f"prpparse fuzz: {len(mutfiles)} mutations / {len(corpus)} files (seed {args.seed})")
    print(f"  benign (mutation kept valid syntax):   {benign}")
    print(f"  caught (both reject):                  {caught}")
    print(f"  missed (ts rejects, prpparse accepts): {missed}")
    print(f"  false positive (prpparse over-reject): {len(false_pos)}")
    print("-" * 56)
    print(f"syntax-error capture rate: {caught}/{syntax_errs} = {capture * 100:.1f}%")
    if dists:
        print(f"localization (caught): median={pct(0.5)}B p90={pct(0.9)}B "
              f"max={dists[-1]}B  within-8B={near8 * 100 // len(dists)}%  "
              f"same-line={same_line * 100 // len(dists)}%")
        print("-" * 56)
        print("by error code (count / same-line% / median line-dist):")
        for code, lds in sorted(by_code.items(), key=lambda kv: (-len(kv[1]))):
            sl = sum(1 for x in lds if x == 0) * 100 // len(lds)
            med = sorted(lds)[len(lds) // 2]
            print(f"  {code:<22} n={len(lds):<5} same-line={sl:>3}%  med-line-dist={med}")
    if vs_ts:
        print("-" * 56)
        eq = sum(1 for x in vs_ts if x == 0)
        le = sum(1 for x in vs_ts if x <= 0)
        print("localization vs tree-sitter (the achievable bound):")
        print(f"  same line as tree-sitter:       {eq}/{len(vs_ts)} = {eq * 100 // len(vs_ts)}%")
        print(f"  as good or better (<= ts line): {le}/{len(vs_ts)} = {le * 100 // len(vs_ts)}%")
        print(f"  WORSE than ts by >1 line:       {len(worse_vs_ts)}")
    print("=" * 56)

    if false_pos:
        print("FALSE POSITIVES (prpparse rejected tree-sitter-valid syntax):")
        for mf, strat, code, off, pb in false_pos[:20]:
            print(f"  {os.path.basename(mf)} strat={strat} code={code} edit@{off} err@{pb}")
        print("  -> these are real bugs (run `--sexp` on the file to inspect).")

    if args.show:
        print(f"\n--- {min(args.show, missed)} MISSED (syntax error prpparse let through) ---")
        for mf, strat, orig, off in missed_ex[:args.show]:
            print(f"  {mf} (from {orig}, strat={strat}, edit@{off})")
        # Cases where prpparse localizes WORSE than tree-sitter (the real
        # improvement targets — distance-to-edit alone over-counts the inherently
        # non-local mutations that tree-sitter also can't localize).
        worst = sorted(worse_vs_ts, reverse=True)[:args.show]
        print(f"--- {len(worst)} WORSE-THAN-TREE-SITTER (pp line > ts line) ---")
        for dts, mf, strat, code, off, pl, tl, orig in worst:
            print(f"  +{dts} lines  {os.path.basename(mf)} (from {orig}) strat={strat} "
                  f"code={code} pp@L{pl} ts@L{tl}")

    ok = (not false_pos) and (capture >= args.min_capture)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
