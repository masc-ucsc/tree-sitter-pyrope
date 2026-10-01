#!/usr/bin/env python3
"""Standalone formatter CLI behavior, including preservation on failure."""
import pathlib
import re
import subprocess
import tempfile
import time
import unittest

FMT = pathlib.Path(__file__).resolve().parents[1] / 'prpfmt'

class CliTest(unittest.TestCase):
    def test_cli(self):
        with tempfile.TemporaryDirectory() as wd:
            root = pathlib.Path(wd)
            source = root / 'input.prp'
            original = 'mod foo(a:U8,b:U8)->(x:U8@[0],y:U8@[0]){\nx=a+b\ny=a-b\n}\n'
            source.write_text(original)
            def run(*args, ok=True):
                result = subprocess.run([str(FMT), *map(str, args)], cwd=wd, capture_output=True, text=True)
                self.assertEqual(result.returncode == 0, ok, result.stderr)
                return result
            expected = run(source).stdout
            self.assertIn('\n  x = a + b', expected)
            self.assertEqual(run('--mode=ai', source).stdout, expected)
            self.assertEqual(run('--indent', 2, '--width', 132, source).stdout, expected)
            self.assertEqual(run(source, '--indent=2', '--width=132').stdout, expected)
            self.assertEqual(run('--indent', 4, source).stdout, run(source, '--indent', 4).stdout)
            self.assertIn('\n    x = a + b', run('--indent', 4, source).stdout)
            # A long signature fits at the new default but wraps at 80.
            wide = root / 'wide.prp'
            wide.write_text('mod foo(first_input:U8,second_input:U8,third_input:U8,fourth_input:U8)->(result:U8@[0]) {\nresult=first_input\n}\n')
            self.assertEqual(run(wide).stdout, run('--width', 132, wide).stdout)
            self.assertNotEqual(run(wide).stdout, run('--mode', 'human', '--width', 80, wide).stdout)
            self.assertEqual(run('-i', source).stdout, '')
            self.assertEqual(source.read_text(), expected)
            run(source, '-i')
            self.assertEqual(source.read_text(), expected)
            run('-o', source, source)  # same output path is read before opening
            self.assertEqual(source.read_text(), expected)
            output = root / 'out.prp'
            run('--output', output, source, '-v')
            self.assertEqual(output.read_text(), expected)
            dashed = root / '-file.prp'
            dashed.write_text(original)
            self.assertEqual(run('--', '-file.prp').stdout, expected)
            for args in [('-w', 80), ('-i2',), ('--indent', '2x'), ('--width', 0),
                         ('--indent', -1), ('--width', '999999999999999999999'),
                         ('--indent',), ('--width=',), ('--mode',), ('--mode=',), ('--mode', 'bad'), ('--unknown',)]:
                run(source, *args, ok=False)
            run('-i', '-o', output, source, ok=False)
            run(ok=False)
            run('--indent', 2, '--help')
            malformed = 'mod broken(a:U8 ->\n'
            source.write_text(malformed)
            run('-i', source, ok=False)
            self.assertEqual(source.read_text(), malformed)
            run(source, '-o', output, ok=False)
            self.assertEqual(output.read_text(), expected)

    def test_parse_errors_skip_all_formatting(self):
        # ERROR and MISSING nodes both reject the entire input, even without -v.
        for text in ('const `` = 1\n', 'const x = @\n', 'const x = (1\n'):
            for mode in ('ai', 'human'):
                with self.subTest(text=text, mode=mode), tempfile.TemporaryDirectory() as wd:
                    source = pathlib.Path(wd) / 'bad.prp'
                    output = pathlib.Path(wd) / 'out.prp'
                    source.write_text(text)
                    output.write_text('SENTINEL')
                    for flags in ([], ['-i'], ['-o', str(output)]):
                        result = subprocess.run([str(FMT), str(source), '--mode', mode, *flags],
                                                capture_output=True, text=True)
                        self.assertEqual(result.returncode, 2)
                        self.assertEqual(result.stdout, '')
                        self.assertIn('input did not parse; formatting skipped', result.stderr)
                        self.assertIn(str(source), result.stderr)
                        self.assertEqual(source.read_text(), text)
                        self.assertEqual(output.read_text(), 'SENTINEL')

    def test_declaration_keywords_survive(self):
        # `pub` and `wire` are grammar-ALIASED, which makes their nodes NAMED --
        # they used to fall through the printers' unnamed-only default arm and
        # vanish with exit 0, silently changing a module's visibility. Every one of
        # the 325 pub-declaring files in livehd's Pyrope corpus was affected, and
        # `-i` wrote the loss straight back over the source.
        with tempfile.TemporaryDirectory() as wd:
            source = pathlib.Path(wd) / 'keep.prp'
            source.write_text('pub comb f(a:U4) -> (r:U8) {\n  wire w:U8\n  w = a\n  r = w + 1\n}\n')
            out = subprocess.run([str(FMT), str(source), '-v'], capture_output=True, text=True)
            self.assertEqual(out.returncode, 0, out.stderr)
            self.assertRegex(out.stdout, r'\bpub\s+comb\s+f\b')
            self.assertRegex(out.stdout, r'\bwire\s+w\b')

    def test_verify_failure_still_prints_on_stdout(self):
        # A failed --verify must protect -o/-i, but stdout destroys nothing and the
        # broken text is what prpfmt_debug.py reads to diagnose the formatter.
        with tempfile.TemporaryDirectory() as wd:
            root = pathlib.Path(wd)
            source = root / 'v.prp'
            source.write_text('pub comb f(a:U4) -> (r:U8) {\n  r = a\n}\n')
            out = root / 'v.out'
            out.write_text('SENTINEL')
            # width 1 forces breaks the formatter cannot always re-parse; when it
            # does trip, stdout must carry the buffer and -o must be untouched.
            to_stdout = subprocess.run([str(FMT), str(source), '-v', '--width', '1'], capture_output=True, text=True)
            if to_stdout.returncode == 3:
                self.assertNotEqual(to_stdout.stdout, '', 'verify failure suppressed the stdout buffer')
                to_file = subprocess.run([str(FMT), str(source), '-v', '--width', '1', '-o', str(out)],
                                         capture_output=True, text=True)
                self.assertEqual(to_file.returncode, 3)
                self.assertEqual(out.read_text(), 'SENTINEL', '-o was clobbered by output that failed to re-parse')

class PreservationTest(unittest.TestCase):
    """Formatting must not change what the program MEANS.

    Every case here is a token that the printers silently dropped or glued,
    at exit 0, because a grammar-ALIASED token node is NAMED and fell through a
    printer's unnamed-only `default:` arm. With `-i` that rewrites the user's
    source. Keep one assertion per construct so a regression names itself.
    """

    def fmt(self, text, *args):
        with tempfile.TemporaryDirectory() as wd:
            src = pathlib.Path(wd) / 'x.prp'
            src.write_text(text)
            r = subprocess.run([str(FMT), str(src), '--mode', 'human', *args], capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, f'exit {r.returncode}: {r.stderr}')
            return r.stdout

    def test_if_chain_wraps_between_branches(self):
        branches = [
            'alu_out_0 = unique if instr.beq { alu_eq }',
            'elif instr.bne { not alu_eq }',
            'elif instr.bge { not alu_lts }',
            'elif instr.bgeu { not alu_ltu }',
            'elif instr.is_slti_blt_slt { alu_lts }',
            'elif instr.is_sltiu_bltu_sltu { alu_ltu }',
            'else { 0ub? != 0 }',
        ]
        for indent in (2, 4):
            expected = ('\n' + ' ' * indent).join(branches) + '\n'
            for source in (' '.join(branches), expected):
                with self.subTest(indent=indent, source=source):
                    out = self.fmt(source, '--indent', str(indent), '-v')
                    self.assertEqual(out, expected)
                    self.assertEqual(self.fmt(out, '--indent', str(indent), '-v'), out)

    def test_if_chain_width_boundary(self):
        for prefix in ('', 'unique '):
            source = f'const x = {prefix}if a {{ 1 }} elif b {{ 2 }} else {{ 3 }}'
            self.assertEqual(self.fmt(source, '--width', str(len(source)), '-v'), source + '\n')
            self.assertEqual(self.fmt(source, '--width', str(len(source) - 1), '-v'), source + '\n')
            expected = source.replace(' elif', '\n  elif').replace(' else', '\n  else') + '\n'
            self.assertEqual(self.fmt(source, '--width', str(len(source) - 12), '-v'), expected)

    def test_if_chain_nested_in_scope(self):
        source = ('comb f() -> () {\n'
                  '  const x = if first_condition { 1 } elif second_condition { 2 } else { 3 }\n'
                  '  const y = if a { 1 } else { 2 }\n'
                  '}\n')
        expected = source.replace(' elif second', '\n    elif second').replace(' else { 3 }', '\n    else { 3 }')
        out = self.fmt(source, '--width', '60', '-v')
        self.assertEqual(out, expected)
        self.assertEqual(self.fmt(out, '--width', '60', '-v'), out)

    def test_if_chain_with_comment(self):
        source = ('const x = if a {\n  // keep this value\n  1\n} '
                  'elif b { 2 } else { 3 }\n')
        out = self.fmt(source, '-v')
        self.assertIn('// keep this value\n', out)
        # One branch must break (its comment), so every branch breaks.
        self.assertEqual(out, 'const x = if a {\n  // keep this value\n  1\n} elif b {\n  2\n} else {\n  3\n}\n')
        self.assertEqual(self.fmt(out, '-v'), out)

    def test_long_identifiers_are_indivisible(self):
        sources = (
            'const x = instr.slli',
            'const x = instr.very_long_member_name.another_very_long_member_name',
            'const x = very_long_identifier_without_dots',
            'const x:package_name.very_long_type_name = nil',
        )
        for source in sources:
            for width in (16, 32, 60):
                with self.subTest(source=source, width=width):
                    out = self.fmt(source, '--width', str(width), '-v')
                    self.assertEqual(out, source + '\n')
                    self.assertEqual(self.fmt(out, '--width', str(width), '-v'), out)

    def test_long_condition_wraps_at_operators(self):
        source = ('const x = if instr.first_long_condition or instr.second_long_condition '
                  'or instr.slli { 1 } else { 2 }')
        out = self.fmt(source, '--width', '40', '-v')
        for name in ('instr.first_long_condition', 'instr.second_long_condition', 'instr.slli'):
            self.assertIn(name, out)
        self.assertRegex(out, r'\n +or instr\.')
        self.assertEqual(self.fmt(out, '--width', '40', '-v'), out)

    def test_repeated_operands_align_with_assignment(self):
        first = 'const compressed_load_offset = (mem_rdata_latched#[5] << 6)'
        second = '| (mem_rdata_latched#[10..=12] << 3)'
        third = '| (mem_rdata_latched#[6] << 2)'
        for indent in (2, 4):
            for depth in (1, 6):
                with self.subTest(indent=indent, depth=depth):
                    prefix = 'comb f() -> () {\n'
                    prefix += ''.join(' ' * (indent * n) + 'if enable {\n' for n in range(1, depth))
                    margin = ' ' * (indent * depth)
                    suffix = margin + 'cputs("done")\n'
                    suffix += ''.join(' ' * (indent * n) + '}\n' for n in reversed(range(depth)))
                    source = prefix + margin + ' '.join((first, second, third)) + '\n' + suffix
                    expected = (prefix + margin + first.replace('#[5]', '#[      5]') + '\n'
                                + margin + ' ' * first.index('=') + second + '\n'
                                + margin + ' ' * first.index('=') + third.replace('#[6]', '#[      6]')
                                + '\n' + suffix)
                    out = self.fmt(source, '--indent', str(indent), '--width', '100', '-v')
                    self.assertEqual(out, expected)
                    self.assertEqual(self.fmt(out, '--indent', str(indent), '--width', '100', '-v'), out)

    def test_binary_operator_families_use_leading_continuations(self):
        expressions = (
            ('first_long_operand * second_long_operand * third_long_operand', '*'),
            ('first_long_operand + second_long_operand + third_long_operand', '+'),
            ('first_long_operand < second_long_operand < third_long_operand', '<'),
            ('first_long_operand or second_long_operand or third_long_operand', 'or'),
        )
        for expression, op in expressions:
            for indent in (2, 4):
                with self.subTest(op=op, indent=indent):
                    source = f'const result = {expression}\n'
                    out = self.fmt(source, '--width', '40', '--indent', str(indent), '-v')
                    lines = out.splitlines()
                    self.assertGreater(len(lines), 1)
                    for line in lines[1:]:
                        self.assertTrue(line.startswith(' ' * indent + op + ' '), repr(line))
                    self.assertEqual(self.fmt(out, '--width', '40', '--indent', str(indent), '-v'), out)

    def test_range_step_does_not_become_a_statement(self):
        # A newline before `step` parses as a separate simulation statement.
        source = 'const result = (0..<first_long_operand) step second_long_operand\n'
        out = self.fmt(source, '--width', '40', '-v')
        self.assertIn(') step second_long_operand', out)
        self.assertNotRegex(out, r'\n\s*step\b')
        self.assertEqual(self.fmt(out, '--width', '40', '-v'), out)

    def test_repeated_operands_stay_compact_when_they_fit(self):
        source = ('const x = (data#[5] << 6) | (data#[10..=12] << 3) | (data#[6] << 2)\n')
        self.assertEqual(self.fmt(source, '--width', '200', '-v'), source)
        aligned = self.fmt(source, '--width', '60', '-v')
        self.assertIn('#[      5]', aligned)
        self.assertEqual(self.fmt(aligned, '--width', '200', '-v'), source)

    def test_different_operands_use_simple_continuation_indent(self):
        source = ('comb f() -> () {\n'
                  '  const compressed_load_offset = (mem_rdata_latched#[5] << 6) '
                  '| (mem_rdata_latched#[10..=12] << 3) | (other_data#[6] << 2)\n'
                  '  cputs("done")\n}\n')
        expected = source.replace(' | ', '\n    | ')
        out = self.fmt(source, '--width', '100', '-v')
        self.assertEqual(out, expected)
        self.assertEqual(self.fmt(out, '--width', '100', '-v'), out)

    def test_repeated_operands_align_multiple_fields(self):
        source = 'const x = (data[1] << 10) | (data[123] << 2) | (data[45] << 3)\n'
        expected = ('const x = (data[  1] << 10)\n'
                    '        | (data[123] <<  2)\n'
                    '        | (data[ 45] <<  3)\n')
        out = self.fmt(source, '--width', '40', '-v')
        self.assertEqual(out, expected)
        self.assertEqual(self.fmt(out, '--width', '40', '-v'), out)

    def test_pub_and_wire_survive(self):
        out = self.fmt('pub comb f(a:U4) -> (r:U8) {\n  wire w:U8\n  w = a\n  r = w + 1\n}\n', '-v')
        self.assertRegex(out, r'\bpub\s+comb\s+f\b')   # dropping `pub` changes visibility
        self.assertRegex(out, r'\bwire\s+w\b')

    def test_fluid_declaration_survives(self):
        out = self.fmt('mod m() -> () {\n  fluid mut req:U8\n  req = 1\n}\n', '-v')
        self.assertRegex(out, r'\bfluid\s+mut\s+req\b')  # a fluid handshake is a different interface

    def test_enum_type_annotation_survives(self):
        # Per the Pyrope rule an integer-typed enum switches OFF one-hot numbering,
        # so `enum V:signed` and `enum V` encode their variants differently.
        out = self.fmt('enum V6:Signed = (a, b, c)\n', '-v')
        self.assertIn('enum V6:Signed', out)

    def test_ref_argument_keeps_its_space(self):
        out = self.fmt('mod m(a:U8) -> (y:U8) {\n  y = g(clock_pin = ref clk, din = a)\n}\n', '-v')
        self.assertRegex(out, r'ref\s+clk')      # `refclk` is a DIFFERENT identifier
        self.assertNotIn('refclk', out)

    def test_step_keeps_its_space(self):
        out = self.fmt('mod m() -> () {\n  step 3\n}\n', '-v')
        self.assertRegex(out, r'step\s+3')       # `step3` is a DIFFERENT identifier
        self.assertNotIn('step3', out)

    def test_call_site_instance_attribute_parses(self):
        out = self.fmt('mod m(a:U8) -> (y:U8) {\n  mut u = g::[name=ux](a = a)\n  y = u\n}\n', '-v')
        self.assertIn('::[', out)
        self.assertIn('name', out)

    def test_generic_postfix_argument_round_trips(self):
        # Ruling 2026-09-27: a generic argument / default may be a bare postfix
        # attribute read. The formatter must keep `<N=x.[bits]>` glued (a space
        # before `>` or around `.` still parses, but the output must be stable),
        # at any width, and must still refuse the unparenthesized operator form.
        source = ('comptime const cfg = (const w = 5)\n'
                  'comb addn<N=Z.[bits]>(x:U8) -> (y:U8) { y = x }\n'
                  'mod low<N=cfg.w.[max], T=U8>(x:U8) -> (y:U8) { y = x }\n'
                  'type Row<N=Z.[bits]> = Unsigned(bits=N)\n'
                  'r = low<N=a.[bits]>(x=b)\n'
                  'm = addn<N=cfg.w.[max]>(x=b)\n'
                  'p = addn<a.[bits]>(x=b)\n'
                  'const v = addn<N=b.[bits].[max]>(x=b)\n')
        for mode in ('ai', 'human'):
            for width in ('132', '20'):
                with self.subTest(mode=mode, width=width):
                    out = self.fmt(source, '--mode', mode, '--width', width, '-v')
                    # Human wrapping may add line breaks at list boundaries;
                    # all postfix tokens and their ordering must survive.
                    # A one-item-per-line list ends with `,` (dropped here).
                    compact = ''.join(out.split()).replace(',>', '>').replace(',)', ')')
                    for frag in ('addn<N=Z.[bits]>(', 'low<N=cfg.w.[max], T=U8>(', 'Row<N=Z.[bits]>',
                                 'low<N=a.[bits]>(x=b)', 'addn<N=cfg.w.[max]>(x=b)', 'addn<a.[bits]>(x=b)',
                                 'addn<N=b.[bits].[max]>(x=b)'):
                        self.assertIn(''.join(frag.split()), compact)
                    self.assertEqual(self.fmt(out, '--mode', mode, '--width', width, '-v'), out)
        self.assertIn('f<N=x.[bits]>(a)', self.fmt('r = f<N=x . [bits] >(a)\n', '-v'))
        with tempfile.TemporaryDirectory() as wd:
            for bad in ('r = f<N=W*2>(x=b)\n', 'comb f<N=Z.[bits]+1>(x:U8) -> (y:U8) { y = x }\n'):
                src = pathlib.Path(wd) / 'bad.prp'
                src.write_text(bad)
                r = subprocess.run([str(FMT), str(src), '-v'], capture_output=True, text=True)
                self.assertNotEqual(r.returncode, 0, f'accepted {bad!r}')

    def test_declaration_attribute_still_parses(self):
        # Must keep working alongside the call form above: both spell `::[`, and
        # they compete at the LEXER, so a fix for one can silently break the other.
        out = self.fmt('const a::[note = 1] = 7\ncassert(a.[note] == 1)\n', '-v')
        self.assertIn('::[', out)

    def test_leading_word_operator_continuation(self):
        # `false \n or true` must stay ONE expression. Terminating the statement at
        # the newline kept only the first term.
        for op, rhs in (('or', 'true'), ('and', 'true'), ('implies', 'true'),
                        ('equals', '5'), ('does', 'U4'), ('in', '(1, 2, 3)')):
            lhs = '5' if op in ('equals',) else ('U8' if op == 'does' else ('5' if op == 'in' else 'false'))
            out = self.fmt(f'const r = {lhs}\n        {op} {rhs}\n', '-v')
            self.assertIn(op, out, f'{op} continuation lost')
            self.assertIn(rhs.strip('()').split(',')[0], out)

    def test_identifiers_beginning_with_an_operator_word_still_terminate(self):
        # The continuation lookahead is WHOLE-WORD: these are names, not operators.
        out = self.fmt('mod m() -> () {\n  mut ordinal = 1\n  mut andy = 2\n  mut into = 3\n'
                       '  mut doesx = 4\n  mut hasy = 5\n  mut equalsz = 6\n}\n', '-v')
        for name in ('ordinal', 'andy', 'into', 'doesx', 'hasy', 'equalsz'):
            self.assertIn(name, out)

    def test_underscore_digit_identifier(self):
        # Imported `_0`/`_1` field names need backticks (reserved placeholders).
        out = self.fmt('mod m() -> (y:U1) {\n  wire g:(`_0`:U1, `_1`:U1) = nil\n  y = g.`_0`\n}\n', '-v')
        self.assertIn('_0', out)
        self.assertIn('_1', out)

    def test_line_comment_never_swallows_the_next_item(self):
        # Nothing may follow a `//` comment on the same output line. This needed TWO
        # passes to show: pass 1 moved a trailing comment onto its own line, pass 2
        # glued the next item back onto it -- commenting that code OUT, at exit 0,
        # invisibly to --verify (the result still parses, with fewer statements).
        src = ('enum C = (\n  A = 1,\n  B = 2,   // keep me\n  D = 3\n)\n')
        p1 = self.fmt(src, '-v')
        p2 = self.fmt(p1, '-v')
        for out, which in ((p1, 'pass1'), (p2, 'pass2')):
            for line in out.split('\n'):
                idx = line.find('//')
                if idx >= 0:
                    self.assertEqual(line[idx:].count('='), 0,
                                     f'{which}: code was swallowed by a line comment: {line!r}')
            self.assertIn('D', out, f'{which} lost D')
        self.assertEqual(p1, p2, 'comment placement is not idempotent')


class ModeTest(unittest.TestCase):
    def fmt(self, source, mode='ai', width=132):
        with tempfile.TemporaryDirectory() as wd:
            path = pathlib.Path(wd) / 'mode.prp'
            args = [str(FMT), str(path), '--mode', mode, '--width', str(width), '-v']
            path.write_text(source)
            first = subprocess.run(args, capture_output=True, text=True)
            self.assertEqual(first.returncode, 0, first.stderr + first.stdout)
            path.write_text(first.stdout)
            second = subprocess.run(args, capture_output=True, text=True)
            self.assertEqual(second.returncode, 0, second.stderr)
            self.assertEqual(second.stdout, first.stdout, 'formatting must be idempotent')
            self.assertTrue(all(line == line.rstrip() for line in first.stdout.splitlines()))
            return first.stdout

    def test_ai_ignores_width_and_joins_lists(self):
        source = 'const value = f(\n' + ',\n'.join(f'argument_{i}=input_{i}_with_a_long_name' for i in range(8)) + '\n)\n'
        out = self.fmt(source, width=20)
        self.assertEqual(len(out.splitlines()), 1)
        self.assertGreater(len(out.rstrip()), 132)
        self.assertEqual(self.fmt(source, width=132), out)

    def test_statement_blocks_always_multiline(self):
        source = 'comb f(a:U8) -> (out:U8) { if a == 1 { out = 2 } elif a == 3 { out = 4 } else { out = 0 } }\n'
        expected = ('comb f(a:U8) -> (out:U8) {\n'
                    '  if a == 1 {\n    out = 2\n  } elif a == 3 {\n    out = 4\n  } else {\n    out = 0\n  }\n}\n')
        for mode in ('ai', 'human'):
            self.assertEqual(self.fmt(source, mode), expected)
            self.assertEqual(self.fmt('comb empty() -> () {}\n', mode), 'comb empty() -> () {\n}\n')

    def test_expression_blocks_can_be_inline(self):
        source = 'const out = if a {\n  1\n} else {\n  2\n}\n'
        self.assertEqual(self.fmt(source), 'const out = if a { 1 } else { 2 }\n')
        self.assertIn('if a {\n', self.fmt(source, 'human'))
        source = 'const out = {\n  mut a = 1\n  a + 1\n}\n'
        self.assertIn('mut a = 1\n', self.fmt(source))

    def test_nested_lambda_bodies_stay_attached(self):
        source = ('comb outer() -> () {\n'
                  '  comb first(a, b) -> (r) { r = a + b }\n'
                  '  comb second(a, b) -> (r) { r = a - b }\n}\n')
        for mode in ('ai', 'human'):
            out = self.fmt(source, mode)
            self.assertEqual(out.count('-> (r) {\n'), 2)

    def test_ai_has_no_vertical_alignment(self):
        source = 'const x = a\nconst much_longer = b\ntype A = U8\ntype VeryLong = U16\n'
        self.assertEqual(self.fmt(source), source)
        out = self.fmt('const x = (data#[5] << 6) | (data#[10..=12] << 3) | (data#[6] << 2)\n', width=20)
        self.assertEqual(len(out.splitlines()), 1)
        self.assertNotIn('#[ ', out)

    def test_human_calls_break_between_whole_arguments(self):
        source = 'const out = fifo(\n' + ',\n'.join(f'port_{i}=a_long_input_name_{i}#[f] == 1' for i in range(5)) + '\n)\n'
        out = self.fmt(source, 'human')
        self.assertIn('fifo(\n', out)
        for i in range(5):
            self.assertIn(f'  port_{i}=a_long_input_name_{i}#[f] == 1', out)
        self.assertTrue(all(len(line) <= 132 for line in out.splitlines()))

    def test_human_width_boundaries_include_closing_delimiters(self):
        for width in (60, 132):
            for extra in range(-2, 5):
                source = 'const x = f(a=' + 'v' * (width - 26 + extra) + ', z=2)\n'
                out = self.fmt(source, 'human', width)
                self.assertTrue(all(len(line) <= width for line in out.splitlines()), out)
                if len(source.rstrip()) > width:
                    self.assertIn('f(\n', out)

    def test_selectors_never_split(self):
        for expr in ('wr_addr#[0..<TILE_ADDR_BITS]', 'data#[(col*TILE_WORDS)..+TILE_WORDS]'):
            for mode in ('human', 'ai'):
                out = self.fmt('const result = ' + expr + '\n', mode, 20)
                self.assertIn(expr, out)
        out = self.fmt('const out = f(port=really_long_input#[0..<TILE_ADDR_BITS] == 1)\n', 'human', 20)
        self.assertIn('port=really_long_input#[0..<TILE_ADDR_BITS] == 1', out)

    def test_nested_tuples_split_between_fields(self):
        source = 'const t = (aw=(addr=long_address_name, id=long_identifier_name), data=(word=long_data_word, valid=long_valid_signal))\n'
        out = self.fmt(source, 'human', 80)
        self.assertTrue(all(len(line) <= 80 for line in out.splitlines()), out)
        self.assertIn('aw=(addr=long_address_name, id=long_identifier_name)', out)

    def test_trailing_comments_stay_with_ports_and_arguments(self):
        source = ('comb f(first:U16, // first port\n second:U16) -> (out:U16 // output port\n) {\n'
                  'out = g(z=first, // z input\n a=second)\n}\n')
        for mode in ('ai', 'human'):
            out = self.fmt(source, mode)
            for expected in ('first:U16, // first port', 'out:U16 // output port', 'z=first, // z input'):
                self.assertIn(expected, out)
            self.assertIn(') -> (\n', out)

    def test_header_explodes_inputs_first(self):
        source = 'comb f(' + ', '.join(f'input_{i}_with_a_long_name:U16' for i in range(5)) + ') -> (out:U16) { out = 0 }\n'
        out = self.fmt(source, 'human')
        self.assertTrue(out.startswith('comb f(\n'))
        self.assertIn(') -> (out:U16) {', out)
        self.assertTrue(all(len(line) <= 132 for line in out.splitlines()))

    def test_generic_lists_spacing_and_wrapping(self):
        source = 'const out = f<Z=(WIDTH*(N - 1)), A=cfg.width>(z=3, a=2)\n'
        # Call-site generic bindings are comptime and sort like named arguments.
        self.assertIn('<A=cfg.width, Z=(WIDTH*(N - 1))>', self.fmt(source))
        source = 'const out = f<' + ', '.join(f'Param{i}=very_long_generic_value_{i}' for i in range(5)) + '>(x=1)\n'
        out = self.fmt(source, 'human')
        self.assertTrue(all(len(line) <= 132 for line in out.splitlines()), out)
        self.assertIn('f<\n', out)

    def test_consistent_binding_spacing(self):
        source = ('reg x:U8:[reset_pin = ref rst, async = true] = 0\n'
                  'const t = (aw = (addr = 2, id = 3), bits = N)\n'
                  'const out = f::[name = inst](rst = rst, bits = N)\n')
        for mode in ('ai', 'human'):
            out = self.fmt(source, mode)
            self.assertIn('[reset_pin=ref rst, async=true]', out)
            self.assertIn('aw=(addr=2, id=3)', out)
            # `f` is not a same-file lambda: the same-name binding stays
            # explicit, and its arguments keep source order (a `...` gather
            # in an unseen signature would see the reordering).
            self.assertIn('f::[name=inst](rst=rst, bits=N)', out)

    def test_named_call_and_tuple_sorting(self):
        decl = 'comb f(a, m, z) -> (o) {\n  o = a\n}\n'
        for mode in ('ai', 'human'):
            self.assertEqual(self.fmt(decl + 'const x = f(z=z, a=1, m=2)\n', mode), decl + 'const x = f(a=1, m=2, z)\n')
            # An unresolved callee may gather its arguments: source order.
            self.assertEqual(self.fmt('const x = g(z=z, a=1, m=2)\n', mode), 'const x = g(z=z, a=1, m=2)\n')
            self.assertEqual(self.fmt('const t = (z=3, a=1)\n', mode), 'const t = (a=1, z=3)\n')
            # A tuple that declares fields sorts too (owner ruling 105).
            self.assertEqual(self.fmt('const t = (const z=3, const a=1)\n', mode), 'const t = (const a=1, const z=3)\n')
            self.assertIn('(const x=x)', self.fmt('const t = (const x=x)\n', mode))
            self.assertIn('<N=N>', self.fmt('const x = f<N=N>(x=x)\n', mode))

    def test_sorting_keeps_order_sensitive_lists(self):
        for source, fragment in (
            ('const x = f(z=side_effect(), a=2)\n', 'f(z=side_effect(), a=2)'),
            ('const x = f(z=ref value, a=2)\n', 'f(z=ref value, a=2)'),
            ('const x = f(3, z=1, a=2)\n', 'f(3, z=1, a=2)'),
            ('const t = (const z=1, const a=z)\n', '(const z=1, const a=z)'),
        ):
            self.assertIn(fragment, self.fmt(source))
        out = self.fmt('const x = f(z=1, // z comment\n a=2)\n')
        self.assertLess(out.index('z=1'), out.index('a=2'))

    def test_variadic_named_leftovers_are_preserved(self):
        # A `...` gather makes the callee unresolved: no shorthand at all, since
        # any name may land in the gathered tuple, and no sorting, since the
        # gathered tuple keeps call order (`f(b=x, a=y)` into `f(...args)`
        # folds x first; sorted, it folds y first).
        for mode in ('ai', 'human'):
            out = self.fmt('comb f(fixed, ...rest) -> () {}\nf(z=z, fixed=fixed)\n', mode)
            self.assertIn('f(z=z, fixed=fixed)', out)
            out = self.fmt('comb f(...args) -> (o:U8) {\n  o = 0\n}\nconst o = f(b=x, a=y)\n', mode)
            self.assertIn('f(b=x, a=y)', out)
            out = self.fmt('comb f(first:U4, ...rest) -> (o:U8) {\n  o = first\n}\nconst o = f(first=x, c=y, b=z)\n', mode)
            self.assertIn('f(first=x, c=y, b=z)', out)
            # UFCS and imported callees: the signature is not visible.
            self.assertIn('x.f(b=x, a=y)', self.fmt('const o = x.f(b=x, a=y)\n', mode))
            self.assertIn('lib.f(b=x, a=y)', self.fmt('const o = lib.f(b=x, a=y)\n', mode))

    def test_shorthand_only_for_resolved_same_file_lambdas(self):
        decls = ('comb f(a:U4) -> (o:U4) { o = a }\n'
                 'comb g(rst:U1, a:U4) -> (o:U4) { o = a }\n'
                 'comb s(self, x:U4) -> (o:U4) { o = x }\n'
                 'comb h1(a:U4) -> (o:U4) { o = a }\n'
                 'comb h2(a:U4) -> (o:U4) { o = a }\n'
                 'comb k(a:U4) -> (o:U4) { o = a }\n'
                 'comb v(a:U4, ...rest) -> (o:U4) { o = a }\n'
                 'const hh = [h1, h2]\n'
                 'const alias = k\n')
        cases = {
            'f(a=a)': 'f(a)',
            'f(`a`=a)': 'f(a)',
            # `f` has no parameter `q`: shorthand would turn the error into a
            # positional binding of the single parameter.
            'f(q=q)': 'f(q=q)',
            # One canonical spelling: a bare same-name argument of a resolved
            # callee sorts under its name like `rst=rst`.
            'g(rst=rst, a=1)': 'g(a=1, rst)',
            'g(rst, a=1)': 'g(a=1, rst)',
            'g(a=1, rst)': 'g(a=1, rst)',
            'x.s(x=x)': 'x.s(x=x)',      # UFCS
            'hh(a=a)': 'hh(a=a)',        # gathered overload set
            'h1(a=a)': 'h1(a=a)',        # also used as a value in the set
            'alias(a=a)': 'alias(a=a)',  # alias
            'k(a=a)': 'k(a=a)',          # name used elsewhere (the alias)
            'lib.m(a=a)': 'lib.m(a=a)',  # namespace
            'v(a=a, x=x)': 'v(a=a, x=x)',  # `...` gather
            'ext(rst=rst, a=1)': 'ext(rst=rst, a=1)',  # not declared here: source order
            'ext(a=1, rst)': 'ext(a=1, rst)',  # unresolved bare item: source order
            'ext(rst, a=1)': 'ext(rst, a=1)',
        }
        for mode in ('ai', 'human'):
            for call, expected in cases.items():
                with self.subTest(mode=mode, call=call):
                    out = self.fmt(decls + '\nconst o = ' + call + '\n', mode)
                    self.assertTrue(out.endswith('const o = ' + expected + '\n'), out)
        # A lambda declared in another block does not resolve outside it.
        nested = 'mod m() { comb f(a) -> (o) { o = a }\n}\nconst o = f(a=a)\n'
        self.assertIn('f(a=a)', self.fmt(nested))

    def test_declarations_sort(self):
        # A named tuple VALUE is unordered (owner ruling 105): declaring
        # tuples and parameter lists sort like any all-named list, a `self`
        # parameter staying first. A `type` body is a LAYOUT, like an `enum`
        # member list: typed-tuple construction binds positionally by type
        # (qa.md, 2026-09-29), so a layout keeps its order. So does a
        # declaration whose initializer reads a sibling field (declare before
        # use).
        for mode in ('ai', 'human'):
            for source, expected in (
                ('type Foo = (mut zz:U4 = 0, mut aa:U4 = 0)\n', 'type Foo = (mut zz:U4=0, mut aa:U4=0)\n'),
                ('type Bar = (zz = 1, aa = 2)\n', 'type Bar = (zz=1, aa=2)\n'),
                ('type Baz = (zz:U4 = 3, aa:U4 = 1)\n', 'type Baz = (zz:U4=3, aa:U4=1)\n'),
                ('type Qux = (zz:U4, aa:U4)\n', 'type Qux = (zz:U4, aa:U4)\n'),
                ('enum E = (Zz=2, Aa=1)\n', 'enum E = (Zz=2, Aa=1)\n'),
                ('const t = (zz:U4 = 3, aa = 1)\n', 'const t = (aa=1, zz:U4=3)\n'),
                ('const t = (mut zz = 3, aa = 1)\n', 'const t = (aa=1, mut zz=3)\n'),
                ('const t = (zz = 3, aa = 1)\n', 'const t = (aa=1, zz=3)\n'),
                ('const t = (mut zz = 3, aa = zz)\n', 'const t = (mut zz=3, aa=zz)\n'),
                ('comb f(cfg=(b=1, a=2)) -> (o) {\n  o = cfg.a\n}\n', 'comb f(cfg=(a=2, b=1)) -> (o) {\n  o = cfg.a\n}\n'),
                ('comb f(ref self, zz:U8, aa:U8 = 1) -> (yy:U8, bb:U8) {\n  yy = aa\n}\n',
                 'comb f(ref self, aa:U8=1, zz:U8) -> (bb:U8, yy:U8) {\n  yy = aa\n}\n'),
                ('comb f(zz:U8, aa:U8 = zz) -> (o) {\n  o = aa\n}\n', 'comb f(zz:U8, aa:U8=zz) -> (o) {\n  o = aa\n}\n'),
                ('comb f<Zz, Aa>(p) -> (o) {\n  o = p\n}\n', 'comb f<Zz, Aa>(p) -> (o) {\n  o = p\n}\n'),
            ):
                with self.subTest(mode=mode, source=source):
                    self.assertEqual(self.fmt(source, mode), expected)

    def test_any_comment_blocks_sorting(self):
        for mode in ('ai', 'human'):
            for source in (
                'const o = f(b=y, a=x /* ay */ )\n',
                'const o = f(b=y, /* inline */ a=x)\n',
                'const t = (b=y, /* why */ a=x)\n',
                'const o = f(b=y, a=x /* ay */ , c=z)\n',
                'const t = (x /* one */ ,)\n',
            ):
                with self.subTest(mode=mode, source=source):
                    self.assertEqual(self.fmt(source, mode), source)

    def test_generic_bindings_and_attributes_sort(self):
        for mode in ('ai', 'human'):
            for source, expected in (
                ('const o = h<W=M, A=K>(x=1)\n', 'const o = h<A=K, W=M>(x=1)\n'),
                ('const o = h<U8, A=K>(x=1)\n', 'const o = h<U8, A=K>(x=1)\n'),
                ('reg r:U8:[posclk=false, async=true] = 3\n', 'reg r:U8:[async=true, posclk=false] = 3\n'),
                ('reg r:U8:[reset_pin=ref rst, async=true] = 3\n', 'reg r:U8:[reset_pin=ref rst, async=true] = 3\n'),
                ('reg r:U8:[reset_pin=pick(rst), async=true] = 3\n', 'reg r:U8:[reset_pin=pick(rst), async=true] = 3\n'),
            ):
                with self.subTest(mode=mode, source=source):
                    self.assertEqual(self.fmt(source, mode), expected)
        # Generic PARAMETER declarations keep their order.
        self.assertIn('comb c<W:Signed, A:Signed>(x)', self.fmt('comb c<W:Signed, A:Signed>(x) -> (o) { o = x }\n'))

    def test_pure_casts_and_conditionals_sort(self):
        decl = 'comb h(a, rst, z) -> (o) {\n  o = a\n}\n'
        for mode in ('ai', 'human'):
            for source, expected in (
                ('const o = h(rst=U1(x), a=b)\n', 'const o = h(a=b, rst=U1(x))\n'),
                ('const o = h(rst=S8(x), a=b)\n', 'const o = h(a=b, rst=S8(x))\n'),
                ('const o = h(rst=Unsigned(x), a=b)\n', 'const o = h(a=b, rst=Unsigned(x))\n'),
                ('const o = h(rst=Bool(x), a=b)\n', 'const o = h(a=b, rst=Bool(x))\n'),
                ('const o = h(z=if c { a } else { b }, a=b)\n', 'const o = h(a=b, z=if c { a } else { b })\n'),
                ('const o = h(z=g(x), a=b)\n', 'const o = h(z=g(x), a=b)\n'),
                ('const o = h(z=U1(g(x)), a=b)\n', 'const o = h(z=U1(g(x)), a=b)\n'),
            ):
                with self.subTest(mode=mode, source=source):
                    self.assertEqual(self.fmt(decl + source, mode), decl + expected)
        # A same-file lambda named like a conversion (it must be backticked:
        # `U9` is a reserved type word) is an ordinary call; so is a lambda
        # named like an OLD conversion (`u9`: an ordinary name now).
        self.assertIn('h(z=`U9`(x), a=b)',
                      self.fmt(decl + 'comb `U9`(x) -> (o) { o = x }\nconst o = h(z=`U9`(x), a=b)\n'))
        self.assertIn('h(z=u9(x), a=b)', self.fmt(decl + 'const o = h(z=`u9`(x), a=b)\n'))
        self.assertIn('h(z=u9(x), a=b)', self.fmt(decl + 'const o = h(z=u9(x), a=b)\n'))
        # Builtin conversions have no gather: their arguments sort.
        self.assertIn('U8(bits=4, max=3)', self.fmt('const q = U8(max=3, bits=4)\n'))

    def test_reserved_placeholder_escapes(self):
        for mode in ('ai', 'human'):
            for name in ('_0', '_1a', '_23abc', '_1é', '_١x'):
                with self.subTest(mode=mode, name=name):
                    source = f'const `{name}` = 1\n'
                    out = self.fmt(source, mode)
                    self.assertEqual(out, source)
                    self.assertEqual(self.fmt(out, mode), out)
            for name in ('_1_a', '_1_', '__0'):
                self.assertEqual(self.fmt(f'const `{name}` = 1\n', mode),
                                 f'const {name} = 1\n')

    def test_backticks_in_sort_keys_and_kept_escapes(self):
        for mode in ('ai', 'human'):
            decl = 'comb f(clk, `in`, zed) -> (o) {\n  o = zed\n}\n'
            self.assertEqual(self.fmt(decl + 'const o = f(zed=1, `in`=2, clk=3)\n', mode),
                             decl + 'const o = f(clk=3, `in`=2, zed=1)\n')
            # `name` and name are the same identifier when name is a plain
            # word: the backticks drop.
            # Also nonreserved underscore names, a non-ASCII letter, a word that only starts
            # like a type word (`U8x`, `S_4`, `U`, `Booleans`), a
            # old lowercase type spellings (`u8`, `s2`, `i32`, `bool`, `unsigned`:
            # ordinary names since the 2026-09-30 ruling), and an `els`/`eli` word (the scanner
            # and prpparse match `else`/`elif` as whole words).
            for name in ('plain', 'x_1', 'self', 'Upper', '_x', '__y', 'bits', 'assume', 'U8x', 'Sx', 'i', 'elx',
                         'S_4', 'U', 'S', 'Booleans', 'Stringy', 'U8é', '_1_a', '__0', 'é', 'café2',
                         'Ωmega', 'els', 'eli', 'elsev', 'elsewhere', 'u8x', 's_4', 'u', 's', 'booleans', 'strings',
                         'int', 'integer', 'uint', 'x0', 'a_0',
                         'u8', 's2', 's4', 'i32', 'u0', 's1', 'i0', 'bool', 'boolean', 'unsigned', 'signed', 'string'):
                with self.subTest(mode=mode, name=name):
                    self.assertEqual(self.fmt(f'const `{name}` = 1\n', mode), f'const {name} = 1\n')
            for src, expected in (('reg r:Unsigned(`bits`=2) = 0\n', 'reg r:Unsigned(bits=2) = 0\n'),
                                  ('`assume`(a != 3)\n', 'assume(a != 3)\n'),
                                  ('`print`("x")\n', 'print("x")\n'),
                                  ('const t = x.`e0`.`in`\n', 'const t = x.e0.`in`\n')):
                with self.subTest(mode=mode, src=src):
                    self.assertEqual(self.fmt(src, mode), expected)
            # They stay when dropping them could change how the name lexes: a
            # keyword, `_` alone, a leading digit, or any character an
            # identifier does not allow (`foo[bar]` is not `foo[bar]`; `$` and
            # a non-letter such as `·` or a non-breaking space are no name
            # characters). `nil` is no keyword, but lhd reads a bare `nil` as
            # the literal.
            # A type word (`U<N>`, `S<N>`, `Unsigned`, `Signed`, `Bool`,
            # `String`, `Clock`, `Reset`) is reserved: `U4` is the type,
            # `` `U4` `` a name, so its backticks stay too. So do a reserved
            # placeholder (`_0`, `_12`, `_1a`). The old lowercase spellings
            # (`u8`, `s2`, `i32`, `bool`, ...) are NOT reserved: see above.
            for name in ('`nil`', '`true`', '`false`', '`in`', '`stage`', '`elif`', '`else`',
                         '`U8`', '`S4`', '`U0`', '`S1`', '`U1333`', '`Unsigned`', '`Signed`', '`Bool`', '`String`',
                         '`Clock`', '`Reset`', '`S2`', '`_0`', '`_1`', '`_12`', '`_1a`',
                         '`_`', '`with space`', '`foo[bar]`', '`a.b`', '`x$`', '`foo$bar`', '`$x`', '`3a`',
                         '`a·b`', '`a\u00a0b`', '`é!`'):
                with self.subTest(mode=mode, name=name):
                    self.assertEqual(self.fmt(f'const {name} = 1\n', mode), f'const {name} = 1\n')

    def test_type_words_are_reserved(self):
        # `U<N>`/`S<N>`/`Unsigned`/`Signed`/`Bool`/`String`/`Clock`/`Reset`
        # are reserved type words: a bare `U4` is the type, `` `U4` `` an
        # ordinary name. The backticks stay in every position, and a name never
        # compares equal to the type: `f(`U4`=U4)` binds the parameter `U4` to
        # the TYPE, so it is no `x=x` shorthand, and the value `U4` is no
        # reference to the sibling field `` `U4` ``.
        # (`A` sorts before `U4`: the sort order is plain byte order.)
        decl = 'comb f(A, `U4`) -> (o) {\n  o = A\n}\n'
        for mode in ('ai', 'human'):
            for src, expected in (
                (decl + 'const p = f(`U4`=U4, A=1)\n', decl + 'const p = f(A=1, `U4`=U4)\n'),
                (decl + 'const q = f(`U4`=`U4`, A=1)\n', decl + 'const q = f(A=1, `U4`)\n'),
                ('const t = (`U4`=1, A=U4)\n', 'const t = (A=U4, `U4`=1)\n'),
                ('const s = x.`S8`.`Bool`\n', 'const s = x.`S8`.`Bool`\n'),
                ('mut y:`U4` = `U4`\n', 'mut y:`U4` = `U4`\n'),
                ('const c = U8(x) + S4(`S4`)\n', 'const c = U8(x) + S4(`S4`)\n'),
                ('const b = Bool(x) and `Clock` == Reset\n', 'const b = Bool(x) and `Clock` == Reset\n'),
                ('mod m(c:Clock, r:Reset) -> (o:U8) {\n  o = 0\n}\n', 'mod m(c:Clock, r:Reset) -> (o:U8) {\n  o = 0\n}\n'),
                ('const k = U8.[max] + U8(x)#[0]\n', 'const k = U8.[max] + U8(x)#[0]\n'),
                # the old lowercase spellings are ordinary names: backticks drop
                ('const c = `u8`(x) + `s4`\n', 'const c = u8(x) + s4\n'),
                ('const c = u8(x) + s4\n', 'const c = u8(x) + s4\n'),
            ):
                with self.subTest(mode=mode, src=src):
                    self.assertEqual(self.fmt(src, mode), expected)
        # A bare type word can not be a name: the input does not parse.
        for src in ('const U4 = 1\n', 'const t = (U4=1)\n', 'const s = x.U4\n', 'comb f(U4) -> (o) { o = 1 }\n',
                    'const Bool = 1\n', 'mut Clock = 0\n', 'const q = U8.x\n'):
            with self.subTest(src=src), tempfile.TemporaryDirectory() as wd:
                path = pathlib.Path(wd) / 'bad.prp'
                path.write_text(src)
                result = subprocess.run([str(FMT), str(path)], capture_output=True, text=True)
                self.assertNotEqual(result.returncode, 0, result.stdout)

    def test_backticked_keywords_match_the_grammar_and_lexer(self):
        # Every keyword of grammar.js and of prpparse's lexer keeps its
        # backticks (a bare keyword would not lex as a name).
        import json
        root = pathlib.Path(__file__).resolve().parents[2]
        words = set()
        def walk(x):
            if isinstance(x, dict):
                if x.get('type') == 'STRING' and re.fullmatch(r'[A-Za-z_][A-Za-z0-9_]*', x['value']):
                    words.add(x['value'])
                for v in x.values():
                    walk(v)
            elif isinstance(x, list):
                for v in x:
                    walk(v)
        walk(json.loads((root / 'src' / 'grammar.json').read_text())['rules'])
        words |= set(re.findall(r'^PRP_KEYWORD\((\w+)\)', (root / 'prpparse' / 'prp_keywords.def').read_text(), re.M))
        self.assertIn('in', words)
        words |= {'nil'}
        # Reserved-word matching is CASE-SENSITIVE: only the exact spelling
        # keeps its backticks; every other case variant is an ordinary name.
        type_word = re.compile(r'(?:[US][0-9]+|Unsigned|Signed|Bool|String|Clock|Reset)')
        def reserved(v):
            return v in words or type_word.fullmatch(v) is not None
        variants = {v for w in words for v in (w, w.lower(), w.upper(), w.title(), w.swapcase())}
        variants |= {'eLsE', 'cLoCk', 'bOoLeAn', 'U999999999999999999999999999999',
                     'clock', 'reset', 'Clock', 'Reset', 'IF', 'If', 'U8', 'u8', 'I32', 'BOOL', 'Unsigned', 'UNSIGNED',
                     's2', 'S2', 'i32', 'bool', 'unsigned', 'string'}
        variants = sorted(variants)
        source = ''.join(f'const `{w}` = 1\n' for w in variants)
        expected = ''.join((f'const `{w}` = 1\n' if reserved(w) else f'const {w} = 1\n') for w in variants)
        for mode in ('ai', 'human'):
            out = self.fmt(source, mode)
            # Human mode aligns these declarations; compare their token text.
            self.assertEqual([line.split() for line in out.splitlines()],
                             [line.split() for line in expected.splitlines()])

    def test_backticks_are_case_sensitive(self):
        # `clock`/`reset` are ordinary names (only `Clock`/`Reset` are type
        # words), as are other-case variants of a keyword (`IF`, `If`).
        for mode in ('ai', 'human'):
            for name in ('clock', 'reset', 'IF', 'If', 'Else', 'NIL', 'True', 'I32', 'BOOL', 'UNSIGNED', 'cLoCk', 'u8x',
                         'u8', 's2', 's4', 'i32', 'bool', 'boolean', 'unsigned', 'signed', 'string'):
                with self.subTest(mode=mode, name=name):
                    self.assertEqual(self.fmt(f'const `{name}` = 1\n', mode), f'const {name} = 1\n')
            for name in ('Clock', 'Reset', 'U8', 'S2', 'S4', 'Bool', 'String', 'Unsigned', 'Signed', 'if', 'nil'):
                with self.subTest(mode=mode, name=name):
                    self.assertEqual(self.fmt(f'const `{name}` = 1\n', mode), f'const `{name}` = 1\n')
            # In the positions the compiler mints: a port, a field access and a
            # tick block all use the bare `clock` / `reset`.
            self.assertEqual(self.fmt('comb f(`clock`:Clock, `reset`:Reset) -> (o) {\n  o = 1\n}\n', mode),
                             'comb f(clock:Clock, reset:Reset) -> (o) {\n  o = 1\n}\n')
            self.assertEqual(self.fmt('acc.`reset` = `clock` < 2\n', mode), 'acc.reset = clock < 2\n')
            self.assertEqual(self.fmt('const c = x.`Clock` + `Reset`\n', mode), 'const c = x.`Clock` + `Reset`\n')

    def test_alignment_only_consecutive_same_kind(self):
        source = ('const a = 1\nconst much_longer = 2\ncputs("barrier")\nconst b = 3\n'
                  'wrap value = 1\nconst c = 4\nmut x = 1\nmut longer = 2\n'
                  'type A = U8\ntype Long = U16\n')
        out = self.fmt(source, 'human')
        self.assertIn('const a           = 1\nconst much_longer = 2', out)
        self.assertIn('const b = 3\nwrap value = 1\nconst c = 4', out)
        self.assertIn('mut x      = 1\nmut longer = 2', out)
        self.assertIn('type A    = U8\ntype Long = U16', out)
        self.assertIn('wrap a      = 1\nwrap longer = 2', self.fmt('wrap a = 1\nwrap longer = 2\n', 'human'))

    def test_alignment_allows_a_small_overflow(self):
        source = 'const a = ' + 'v' * 121 + '\nconst longer = 1\n'
        out = self.fmt(source, 'human')
        self.assertIn('const a      = ', out)
        self.assertGreater(len(out.splitlines()[0]), 132)

    def test_alignment_does_not_cause_a_large_overflow(self):
        source = 'const a = ' + 'v' * 121 + '\nconst a_much_longer_declaration_name = 1\n'
        out = self.fmt(source, 'human')
        self.assertEqual(out, source)
        self.assertTrue(all(len(line) <= 132 for line in out.splitlines()))

    def test_soft_width_keeps_a_slightly_long_expression(self):
        source = 'const result = ' + 'a' * 58 + ' + ' + 'b' * 58 + '\n'
        self.assertGreater(len(source.rstrip()), 132)
        self.assertEqual(self.fmt(source, 'human'), source)
        longer = source.replace('b' * 58, 'b' * 80)
        self.assertIn('\n  + ', self.fmt(longer, 'human'))

    def test_trailing_comment_does_not_force_code_to_wrap(self):
        comment = ' // ' + 'explanation ' * 20 + 'ends here\n'
        for expression in ('first_operand + second_operand', 'f(first=one, second=two)'):
            for suffix in ('', 'const next = 2\n'):
                source = 'const result = ' + expression + comment + suffix
                # The trailing comment does not end the alignment group.
                expected = source.replace('const next =', 'const next   =')
                self.assertEqual(self.fmt(source, 'human'), expected)

    def test_alignment_kind_ignores_source_whitespace(self):
        out = self.fmt('comptime const a = 1\ncomptime   const longer = 2\n', 'human')
        self.assertEqual(out, 'comptime const a      = 1\ncomptime const longer = 2\n')

    def test_header_width_includes_opening_brace(self):
        source = 'comb f(' + 'a' * 100 + ':U8) -> (out:U8) { out = 1 }\n'
        for width in (120, 122, 123, 124, 125, 126, 127, 128, 132):
            out = self.fmt(source, 'human', width)
            self.assertTrue(all(len(line) <= width for line in out.splitlines()), out)

    def test_comparisons_always_spaced(self):
        # `==`, `!=`, `<`, `<=`, `>`, `>=` always keep their spaces, also as
        # operands of and/or/implies (a tight `a<b or c>d` reads like generics).
        spaced = {
            'const d = foo!=bar or bar==foo\n': 'const d = foo != bar or bar == foo\n',
            'if foo!=bar or bar==foo {\n  x = 1\n}\n': 'if foo != bar or bar == foo {\n  x = 1\n}\n',
            'const c = a<b and b<c\n': 'const c = a < b and b < c\n',
            'const i = a>=1 implies b<=2\n': 'const i = a >= 1 implies b <= 2\n',
            'const m = i*32==x and y#[0..<3]==0ub101\n': 'const m = i*32 == x and y#[0..<3] == 0ub101\n',
            'const g = n==0 or (n>0 and (z==0))\n': 'const g = n == 0 or (n > 0 and z == 0)\n',
            'cassert(x==1 and y!=2)\n': 'cassert(x == 1 and y != 2)\n',
            'const q = x==-1 and y>-2\n': 'const q = x == -1 and y > -2\n',
            'const w = a in b or c==d\n': 'const w = a in b or c == d\n',
            'const l = a<=b and not (b<=a) or !(c==d)\n': 'const l = a <= b and not (b <= a) or !(c == d)\n',
            'const s = staged_items<BUFFER_DEPTH or pop_beat\n': 'const s = staged_items < BUFFER_DEPTH or pop_beat\n',
            'const u = U1(a<b or c>d)\n': 'const u = U1(a < b or c > d)\n',
            'const s = a==b\n': 'const s = a == b\n',
        }
        for mode in ('ai', 'human'):
            for source, expected in spaced.items():
                with self.subTest(mode=mode, source=source):
                    self.assertEqual(self.fmt(source, mode), expected)
                    self.assertEqual(self.fmt(expected, mode), expected)

    def test_comparisons_wrap_at_the_logical_operator(self):
        source = ('const y = if instr.first_long_condition == 1 or instr.second_long_condition != 2 '
                  'or instr.slli { 1 } else { 2 }\n')
        out = self.fmt(source, 'human', 50)
        self.assertIn('instr.first_long_condition == 1\n', out)
        self.assertIn('\n  or instr.second_long_condition != 2\n', out)

    def test_type_call_arguments_format_like_value_calls(self):
        # `unsigned(...)`, `signed(...)`, `uN(...)`/`sN(...)` and generic type
        # arguments get precedence spacing and `name=value`, join onto one line,
        # and drop the trailing comma after one named argument.
        cases = {
            'mut t:Unsigned( bits = N * N ) = 0\n': 'mut t:Unsigned(bits=N*N) = 0\n',
            'mut t:Unsigned(bits=N+1) = 0\n': 'mut t:Unsigned(bits=N + 1) = 0\n',
            'mut t:U8(bits=N * 2) = 0\n': 'mut t:U8(bits=N*2) = 0\n',
            'mut t:Signed(bits=N  +  1,max=3) = 0\n': 'mut t:Signed(bits=N + 1, max=3) = 0\n',
            'mut t:Signed(bits=W + 1,) = 0\n': 'mut t:Signed(bits=W + 1) = 0\n',
            'mut t:Signed(\n  bits = W+1,\n) = 0\n': 'mut t:Signed(bits=W + 1) = 0\n',
            'mut t:sint(bits=N * N) = 0\n': 'mut t:sint(bits=N*N) = 0\n',
            'type X = Unsigned(bits = W*2 )\n': 'type X = Unsigned(bits=W*2)\n',
            'const z = g<U8, Unsigned( bits = N+1 )>(x=1)\n': 'const z = g<U8, Unsigned(bits=N + 1)>(x=1)\n',
            'comb f(a:Unsigned(max=D-1), b:Signed(bits=W+1,)) -> (o:Unsigned(bits=(W+1)*2)) {\n  o = a\n}\n':
                'comb f(a:Unsigned(max=D - 1), b:Signed(bits=W + 1)) -> (o:Unsigned(bits=(W + 1)*2)) {\n  o = a\n}\n',
            # A single positional argument keeps its comma; `u8` alone is untouched.
            'mut t:Unsigned(8,) = 0\n': 'mut t:Unsigned(8,) = 0\n',
            'mut t:U8 = 0\n': 'mut t:U8 = 0\n',
        }
        for mode in ('ai', 'human'):
            for source, expected in cases.items():
                with self.subTest(mode=mode, source=source):
                    self.assertEqual(self.fmt(source, mode), expected)

    def test_line_ending_semicolons_drop(self):
        cases = {
            'const x = 1;\nconst y = 2\n': 'const x = 1\nconst y = 2\n',
            'const a = 1 ; const b = 2 ;\n': 'const a = 1\nconst b = 2\n',
            'const x = 1;; const q = 2\n': 'const x = 1\nconst q = 2\n',
            'type T = U8;\n': 'type T = U8\n',
            'comb f(a:U8, c:Bool) -> (y:U8, z:U8) {\n  if c { y = a; z = a } else { y = 0; z = 1; }\n}\n':
                'comb f(a:U8, c:Bool) -> (y:U8, z:U8) {\n  if c {\n    y = a\n    z = a\n  } else {\n'
                '    y = 0\n    z = 1\n  }\n}\n',
            'comb f(a:U8) -> (y:U8) {\n  y = a; // c\n  return;\n}\n':
                'comb f(a:U8) -> (y:U8) {\n  y = a // c\n  return\n}\n',
            'const w = { a; }\n': 'const w = { a }\n',
            'while c { break; }\n': 'while c {\n  break\n}\n',
            # A block comment right after the `;` keeps it; so do clause separators.
            'comb f(a:U8) -> (y:U8) {\n  y = a; /* c */\n}\n': 'comb f(a:U8) -> (y:U8) {\n  y = a; /* c */\n}\n',
            'match const t=(a=1); t {\n  case (a=1) {\n    cassert(true)\n  }\n}\n':
                'match const t=(a=1); t {\n  case (a=1) {\n    cassert(true)\n  }\n}\n',
        }
        for mode in ('ai', 'human'):
            for source, expected in cases.items():
                with self.subTest(mode=mode, source=source):
                    self.assertEqual(self.fmt(source, mode), expected)
        # A dropped `;` does not split a Human alignment group (idempotence).
        self.assertEqual(self.fmt('comb d(a) -> (code, p) { p = a; code = a + 10 }\n', 'human'),
                         'comb d(a) -> (code, p) {\n  p    = a\n  code = a + 10\n}\n')

    def test_lambda_header_brace_joins_the_header(self):
        cases = {
            'comb f(a:U8) -> (r:U8)\n{\n  r = a\n}\n': 'comb f(a:U8) -> (r:U8) {\n  r = a\n}\n',
            'pub mod m(a:U8) -> (r:U8@[0])\n{\n  r = a\n}\n': 'pub mod m(a:U8) -> (r:U8@[0]) {\n  r = a\n}\n',
            'pipe p(a:U8) -> (r:U8)\n{\n  r = a\n}\n': 'pipe p(a:U8) -> (r:U8) {\n  r = a\n}\n',
            'comb n(a:U8)\n{\n  a\n}\n': 'comb n(a:U8) {\n  a\n}\n',
            'comb x() {\n  comb k(a:U8) -> (r:U8)\n  {\n    r = a\n  }\n}\n':
                'comb x() {\n  comb k(a:U8) -> (r:U8) {\n    r = a\n  }\n}\n',
        }
        for mode in ('ai', 'human'):
            for source, expected in cases.items():
                with self.subTest(mode=mode, source=source):
                    self.assertEqual(self.fmt(source, mode), expected)

    def test_numeric_literal_case(self):
        cases = {
            'const a = 0XFF\n': 'const a = 0xff\n',
            'const b = -0X0A_bC\n': 'const b = -0x0a_bc\n',
            'const c = 0x00FF\n': 'const c = 0x00ff\n',
            'const d = 0UB1?0_1 + 0SB10 + 0D12 + 0O17\n': 'const d = 0ub1?0_1 + 0sb10 + 0d12 + 0o17\n',
            'const e = 3T + 1_000K + 007\n': 'const e = 3T + 1_000K + 007\n',
            'const f = y#[0XA..=0XB]\n': 'const f = y#[0xa..=0xb]\n',
            'reg r:U8:[reset=0XF] = 0XA\n': 'reg r:U8:[reset=0xf] = 0xa\n',
        }
        for mode in ('ai', 'human'):
            for source, expected in cases.items():
                with self.subTest(mode=mode, source=source):
                    self.assertEqual(self.fmt(source, mode), expected)

    def test_line_comment_trailing_whitespace_strips(self):
        source = '// top\t \t\nconst a = 1 // trail\t\t\ncomb f(a:U8) -> (r:U8) {\n  // inner \t\n  r = a\n}\n'
        expected = '// top\nconst a = 1 // trail\ncomb f(a:U8) -> (r:U8) {\n  // inner\n  r = a\n}\n'
        for mode in ('ai', 'human'):
            self.assertEqual(self.fmt(source, mode), expected)

    def test_one_element_tuple_keeps_its_comma(self):
        # `(x,)` is a one-element tuple, `(x)` is just `x`: the parse tree is the
        # same single-item tuple, so dropping the comma silently turned tuples
        # into scalars (livehd comptime tests adv_tuple_concat_op and
        # function_introspection).
        kept = (
            'mut acc = (0,)\n',
            "cassert(fu.[out] == ('c',))\n",
            'const a = 1 << (0,)\n',
            'const h = f(a, (1,))\n',
            'const k = ((1,),)\n',
            'const t = (mut a=5,)\n',
            'for (idx, i) in (123,) {\n  x = 1\n}\n',
            'const s = (a, b)\n',
        )
        for mode in ('ai', 'human'):
            for source in kept:
                with self.subTest(mode=mode, source=source):
                    self.assertEqual(self.fmt(source, mode), source)
            # Without the comma the parentheses only group: they drop.
            self.assertEqual(self.fmt('const g = (0)\n', mode), 'const g = 0\n')
            self.assertEqual(self.fmt('mut d = (,,1,,)\n', mode), 'mut d = (1,)\n')
            self.assertEqual(self.fmt('const s = (a, b,)\n', mode), 'const s = (a, b)\n')
            self.assertEqual(self.fmt('const e = (0, // c\n)\n', mode), 'const e = (\n  0, // c\n)\n')
            # Comments before a stray leading comma lead the item; they are no
            # empty slot that would steal the one-element tuple's comma.
            self.assertEqual(self.fmt('const x = (\n  // c\n  , 1\n  ,\n)\n', mode), 'const x = (\n  // c\n  1,\n)\n')
            self.assertTrue(self.fmt('const x = (/* c */, 1,)\n', mode).endswith(' 1,)\n'))
            # A destructuring one-element lvalue keeps its comma, with no stray space.
            self.assertEqual(self.fmt('(a,) = f()\n', mode), '(a,) = f()\n')

    def test_redundant_grouping_parens(self):
        # Parentheses around a whole condition/subject, right-hand side or
        # named value drop, and so do those around a comparison or an atom
        # directly under and/or/implies (an atom only under not).
        removed = {
            'if (rst == 1) {\n  x = 1\n} elif (en) {\n  x = 2\n}\n':
                'if rst == 1 {\n  x = 1\n} elif en {\n  x = 2\n}\n',
            'while (i < 3) {\n  i += 1\n}\n': 'while i < 3 {\n  i += 1\n}\n',
            # A chained comparison's operands drop them too (owner ruling 107:
            # a spaced `<` never opens a generic list).
            'z = (a) < (b) < (c)\n': 'z = a < b < c\n',
            'match (s) {\n  == 1 {\n    x = 1\n  }\n}\n': 'match s {\n  == 1 {\n    x = 1\n  }\n}\n',
            'const y = if (c) { 1 } else { 2 }\n': 'const y = if c { 1 } else { 2 }\n',
            'wrap v = (v + 1)\n': 'wrap v = v + 1\n',
            'mut acc:U5 = ((a + b))\n': 'mut acc:U5 = a + b\n',
            'acc |= (b & c)\n': 'acc |= b & c\n',
            'z = (-a)\n': 'z = -a\n',
            'x = (f(a))\n': 'x = f(a)\n',
            'x = (a\n  + b)\n': 'x = a + b\n',
            'f(update=(en and (request!=0)))\n': 'f(update=en and request != 0)\n',
            'const t = (const a=(c), const b=(b & 3))\n': 'const t = (const a=c, const b=b & 3)\n',
            'y = (a == 0) or (b.c) or (d#[0]) or (g(x))\n': 'y = a == 0 or b.c or d#[0] or g(x)\n',
            'y = (a in 1..=3) implies (en)\n': 'y = a in 1..=3 implies en\n',
            'y = not (en) and !(v[1])\n': 'y = not en and !v[1]\n',
            # `((e))` collapses to one layer wherever the inner one is kept.
            'const b = ((x,))\n': 'const b = (x,)\n',
            'y = not ((a == b))\n': 'y = not (a == b)\n',
            'y = a and ((b or c))\n': 'y = a and (b or c)\n',
            'y = ((a + b))*c\n': 'y = (a + b)*c\n',
            'o = a * ((b + c))\n': 'o = a*(b + c)\n',
            'y = f(x=((a, b)))\n': 'y = f(x=(a, b))\n',
            'y = ((a, b))\n': 'y = (a, b)\n',
            'r = a#[((b + 1))..+2]\n': 'r = a#[(b + 1)..+2]\n',
            'foo(((a)))\n': 'foo((a))\n',
            # An atom drops them as any operand or selector index.
            'q = ((a<<1)) | (b)\n': 'q = (a << 1) | b\n',
            'w = (a#[0..<4]) + (b#[4..<8])\n': 'w = a#[0..<4] + b#[4..<8]\n',
            's = a#[((b))]\n': 's = a#[b]\n',
            'z = a - (1)\n': 'z = a - 1\n',
            'z = a < (b)\n': 'z = a < b\n',
            # A lone if/match branch value is a whole value.
            'x = if s == 1 { (a) } else { (0) }\n': 'x = if s == 1 { a } else { 0 }\n',
            'x = if s == 1 { (a + b) } else { (a, b) }\n': 'x = if s == 1 { a + b } else { (a, b) }\n',
        }
        kept = (
            'const a = (x,)\n',
            'const s = (a, b)\n',
            'const l = (a=1)\n',
            'const m = (...t)\n',
            'const n = ({ 3 })\n',
            'const o = (if c { 1 } else { 2 })\n',
            'const q = (a:U8)\n',
            'const p = f(x=(a, b))\n',
            'const d = 3 & (4*4)\n',
            'const e = (a + b)*c\n',
            'const f = (a << 1) | b\n',
            'const h = (a + b) < c\n',
            'const g = (a == b) == c\n',
            'const i = not (a == b)\n',
            'const j = a and (b or c)\n',
            'const r = x or (not y)\n',
            'const u = -(a + b)\n',
            'foo((a))\n',
            'const w = [(a)]\n',
            'if a {\n  b = (1,)\n}\n',
            'match s {\n  == (1) {\n    t = 3\n  }\n}\n',
            # A negative literal would glue to the operator.
            'z = a*(-1)\n',
        )
        for mode in ('ai', 'human'):
            for source, expected in removed.items():
                with self.subTest(mode=mode, source=source):
                    self.assertEqual(self.fmt(source, mode), expected)
                    self.assertEqual(self.fmt(expected, mode), expected)
            for source in kept:
                with self.subTest(mode=mode, source=source):
                    self.assertEqual(self.fmt(source, mode), source)
            # Parentheses that hold a comment stay.
            out = self.fmt('const k = (a /* c */ + b)\n', mode)
            self.assertTrue(out.startswith('const k = (a /* c */') and out.endswith('+ b)\n'), out)
        # A grouped value that names its parameter becomes the shorthand in
        # one pass, like `f(a=a)`.
        self.assertEqual(self.fmt('comb f(a:U4) -> (r:U4) {\n  r = a\n}\nconst k = f(a=(a))\n', 'ai'),
                         'comb f(a:U4) -> (r:U4) {\n  r = a\n}\nconst k = f(a)\n')

    def test_comment_after_open_brace_stays_on_the_header(self):
        cases = {
            'pub mod m(a:U4) -> (o:U4@[]) {  // registered\n  o = a\n}\n':
                'pub mod m(a:U4) -> (o:U4@[]) { // registered\n  o = a\n}\n',
            'comb f(a:U4) -> (o:U4)\n{ // allman\n  o = a\n}\n':
                'comb f(a:U4) -> (o:U4) { // allman\n  o = a\n}\n',
            'if a == 1 { // when one\n  o = 1\n} else { // otherwise\n  o = 2\n}\n':
                'if a == 1 { // when one\n  o = 1\n} else { // otherwise\n  o = 2\n}\n',
            'for i in 0..<4 { // loop\n  x = i\n}\n': 'for i in 0..<4 { // loop\n  x = i\n}\n',
            'if a { // only a comment\n}\n': 'if a { // only a comment\n}\n',
        }
        for mode in ('ai', 'human'):
            for source, expected in cases.items():
                with self.subTest(mode=mode, source=source):
                    self.assertEqual(self.fmt(source, mode), expected)
            # A comment on its own line after `{` still opens the body.
            own = 'if a {\n  // body note\n  o = 1\n}\n'
            self.assertEqual(self.fmt(own, mode), own)

    def test_comment_before_elif_else_moves_to_that_header(self):
        cases = {
            'if p == 0 {\n  r = 1\n} // after if\nelif q == 1 {\n  r = 2\n} else {\n  r = 3\n}\n':
                'if p == 0 {\n  r = 1\n} elif q == 1 { // after if\n  r = 2\n} else {\n  r = 3\n}\n',
            'if a {\n  y = 2\n}\n// about else\nelse {\n  y = 0\n}\n':
                'if a {\n  y = 2\n} else { // about else\n  y = 0\n}\n',
            'if a {\n  y = 2\n} /* blk */ else {\n  y = 0\n}\n':
                'if a {\n  y = 2\n} else { /* blk */\n  y = 0\n}\n',
            # Source order is kept: the moved comments come first (the first
            # trails the `{`), then the branch's own `{` comment.
            'if a {\n  y = 2\n} // m1\n// m2\nelse { // own\n  y = 0\n}\n':
                'if a {\n  y = 2\n} else { // m1\n  // m2\n  // own\n  y = 0\n}\n',
            # A comment after the last `}` stays where it is.
            'if a {\n  y = 2\n} else {\n  y = 0\n} // end\n':
                'if a {\n  y = 2\n} else {\n  y = 0\n} // end\n',
        }
        for mode in ('ai', 'human'):
            for source, expected in cases.items():
                with self.subTest(mode=mode, source=source):
                    self.assertEqual(self.fmt(source, mode), expected)

    def test_if_and_match_expressions_break_all_branches_or_none(self):
        broken = ('const v = if sel == 1 {\n  a + b // sum\n} elif sel == 2 {\n  a\n} else {\n  a - b\n}\n')
        for mode in ('ai', 'human'):
            for source in (broken,
                           'const v = if sel == 1 {\n  a + b // sum\n} elif sel == 2 { a } else { a - b }\n',
                           'const v = if sel == 1 { a + b } elif sel == 2 { a } else {\n  a - b // diff\n}\n'):
                with self.subTest(mode=mode, source=source):
                    out = self.fmt(source, mode)
                    self.assertNotIn('} elif sel == 2 { a }', out)
                    self.assertNotIn('} else { a - b }', out)
            self.assertEqual(self.fmt(broken, mode), broken)
            multi = 'const v = if c { a } else {\n  const t = a + 1\n  t\n}\n'
            self.assertEqual(self.fmt(multi, mode), 'const v = if c {\n  a\n} else {\n  const t = a + 1\n  t\n}\n')
            # match: arms one per line; inline arm blocks unless one must break.
            self.assertEqual(self.fmt('res = match sel { == 0 { a } == 1 { b } else { c } }\n', mode),
                             'res = match sel {\n  == 0 { a }\n  == 1 { b }\n  else { c }\n}\n')
            self.assertEqual(self.fmt('res = match sel {\n  == 0 { a // zero\n  }\n  else { c }\n}\n', mode),
                             'res = match sel {\n  == 0 {\n    a // zero\n  }\n  else {\n    c\n  }\n}\n')
            # A statement `match` keeps its arm blocks vertical, one arm per line.
            self.assertEqual(self.fmt('match sel { == 1 { o = a } else { o = b } }\n', mode),
                             'match sel {\n  == 1 {\n    o = a\n  }\n  else {\n    o = b\n  }\n}\n')
        self.assertEqual(self.fmt('const v = if sel == 1 { a + b } elif sel == 2 { a } else { a - b }\n'),
                         'const v = if sel == 1 { a + b } elif sel == 2 { a } else { a - b }\n')

    def test_one_item_per_line_lists_end_with_a_comma(self):
        cases = {
            'const r = f(x=a, // first\n  y=b)\n': 'const r = f(\n  x=a, // first\n  y=b,\n)\n',
            'const r = f(x=a,\n  y=b // last\n)\n': 'const r = f(\n  x=a,\n  y=b, // last\n)\n',
            'comb m(a:U8, // first\n  b:U8) -> (y:U8 // out\n) {\n  y = a\n}\n':
                'comb m(\n  a:U8, // first\n  b:U8,\n) -> (\n  y:U8 // out\n) {\n  y = a\n}\n',
            'const t = [1, // one\n  2]\n': 'const t = [\n  1, // one\n  2,\n]\n',
            # Inline lists drop the trailing comma; `(x,)` keeps its one.
            'const r = f(\n  x=a,\n  y=b,\n)\n': 'const r = f(x=a, y=b)\n',
            'const k = (a,)\n': 'const k = (a,)\n',
            # A single item gets no comma: `(x)` is grouping, not a tuple.
            'const k = (a // c\n)\n': 'const k = (\n  a // c\n)\n',
        }
        for mode in ('ai', 'human'):
            for source, expected in cases.items():
                with self.subTest(mode=mode, source=source):
                    self.assertEqual(self.fmt(source, mode), expected)
        long = 'const result = f(first=some_long_value, second=another_long_value)\n'
        self.assertEqual(self.fmt(long, 'human', 40),
                         'const result = f(\n  first=some_long_value,\n  second=another_long_value,\n)\n')
        self.assertEqual(self.fmt(long, 'ai', 40), long)

    def test_tuple_with_method_blocks_is_one_item_per_line(self):
        source = ('const iface = (mut value:U8 = 0, comb read(self) -> (v:U8) { v = self.value }, '
                  'comb inc(ref self) -> () { wrap self.value += 1 })\n')
        expected = ('const iface = (\n  mut value:U8=0,\n  comb read(self) -> (v:U8) {\n    v = self.value\n  },\n'
                    '  comb inc(ref self) -> () {\n    wrap self.value += 1\n  },\n)\n')
        for mode in ('ai', 'human'):
            with self.subTest(mode=mode):
                self.assertEqual(self.fmt(source, mode), expected)
                # A multi-statement block argument lays out the same way.
                self.assertEqual(self.fmt('const x = foo(1, {\n  const a = 1\n  a + 1\n}, 3)\n', mode),
                                 'const x = foo(\n  1,\n  {\n    const a = 1\n    a + 1\n  },\n  3,\n)\n')
        self.assertEqual(self.fmt('const x = foo(1, { 3 }, 2)\n'), 'const x = foo(1, { 3 }, 2)\n')


    def test_if_expression_never_staircases(self):
        # Human: an inline chain that does not fit puts each branch on its own
        # continuation line; when a branch still does not fit, every branch
        # takes the statement layout `} elif c {` / `} else {` at the opening
        # line's indentation, never a `}` newline `elif` staircase.
        long_a, long_b = 'a' * 82, 'b' * 61
        source = (f'comb f(a:U8, sel:U2) -> (o:U8) {{\n  const v = if sel == 1 {{ a }} '
                  f'elif sel == 2 {{ {long_a} + {long_b} }} else {{ a - 1 }}\n  o = v\n}}\n')
        out = self.fmt(source, 'human')
        self.assertEqual(out, ('comb f(a:U8, sel:U2) -> (o:U8) {\n  const v = if sel == 1 {\n    a\n'
                               f'  }} elif sel == 2 {{\n    {long_a}\n      + {long_b}\n'
                               '  } else {\n    a - 1\n  }\n  o = v\n}\n'))
        self.assertNotRegex(out, r'\}\n *(elif|else)')
        # Branches that fit their continuation lines stay compact.
        source = ('const vvvvvvvvvvvvvvvvvvvv = if sel == 1 { ' + 'a' * 40 + ' } elif sel == 2 { ' + 'b' * 40 +
                  ' } else { c }\n')
        self.assertEqual(self.fmt(source, 'human', 90),
                         'const vvvvvvvvvvvvvvvvvvvv = if sel == 1 { ' + 'a' * 40 + ' }\n  elif sel == 2 { ' +
                         'b' * 40 + ' }\n  else { c }\n')
        # AI mode keeps the chain on one line.
        self.assertEqual(self.fmt(source, 'ai', 20), source)

    def test_generic_list_stays_inline_when_arguments_split(self):
        # Human: the argument list / header inputs break first; `<...>` stays
        # on the callee line when it fits there.
        args = ', '.join(f'argument_{i}=input_{i}' for i in range(6))
        source = f'const flop = br_flow_reg_fwd<T=U1>({args})\n'
        out = self.fmt(source, 'human', 60)
        self.assertTrue(out.startswith('const flop = br_flow_reg_fwd<T=U1>(\n  argument_0=input_0,\n'), out)
        header = ('pub mod br_flow_reg_fwd<T=U1>(pop_ready:Bool, push_data:T, push_valid:Bool, rst:U1) '
                  '-> (pop_data:T@[0], pop_valid:Bool@[0], push_ready:Bool@[0]) {\n  push_ready = pop_ready\n}\n')
        self.assertEqual(self.fmt(header, 'human'),
                         'pub mod br_flow_reg_fwd<T=U1>(\n  pop_ready:Bool,\n  push_data:T,\n  push_valid:Bool,\n'
                         '  rst:U1,\n) -> (pop_data:T@[0], pop_valid:Bool@[0], push_ready:Bool@[0]) {\n'
                         '  push_ready = pop_ready\n}\n')
        # A generic list that does not fit on its own still breaks.
        source = 'const out = f<' + ', '.join(f'Param{i}=very_long_generic_value_{i}' for i in range(5)) + '>(x=1)\n'
        self.assertIn('f<\n', self.fmt(source, 'human'))
        self.assertEqual(self.fmt(header, 'ai', 20), header)


    def test_attribute_set_keeps_expression_spacing(self):
        # `(e)::[attr]` printed its operand leaf by leaf: `(a or b)` became the
        # identifier `(aorb)`, silently changing the program.
        for mode in ('ai', 'human'):
            for source in ('o = (a or b)::[debug]\n', 'y = (a + b)::[x]\n', 'z = (not a)::[z]\n',
                           'w = (a implies b)::[foo=1]\n', 'f(y=(a or b)::[z])\n',
                           'v = (if c { a } else { b })::[z]\n', 'u = ram.port[0][addr]::[rdport=0]\n'):
                with self.subTest(mode=mode, source=source):
                    self.assertEqual(self.fmt(source, mode), source)

    def test_string_hole_expression_is_formatted(self):
        # A hole's expression prints like any expression (Q2 spacing, no
        # padding blanks); the text, the braces and a format spec stay as
        # written, and a hole holding a comment stays verbatim.
        cases = {
            'const t = "{ a  +  b } {a==b} {x:b} { y :x}"\n': 'const t = "{a + b} {a == b} {x:b} {y:x}"\n',
            'const t = "x{{y}} {a /* c */} {a\n  + b}"\n': 'const t = "x{{y}} {a /* c */} {a + b}"\n',
            'const t = "{ "in{ a==b }" }"\n': 'const t = "{"in{a == b}"}"\n',
        }
        for mode in ('ai', 'human'):
            for source, expected in cases.items():
                with self.subTest(mode=mode, source=source):
                    self.assertEqual(self.fmt(source, mode), expected)

    def test_header_semicolon_runs_collapse(self):
        # A `;` means a newline: in a header a run of them is one init-clause
        # separator, and a run before the block prints nothing (`if c; {` is
        # `if c {`), in `if`/`elif`, `while`, `match` and `loop` alike.
        cases = {
            'if a = 1;; b = 2; c { }\n': 'if a=1; b=2; c {\n}\n',
            'if x; { }\n': 'if x {\n}\n',
            'if a { } elif c = 1;; c; { } else { }\n': 'if a {\n} elif c=1; c {\n} else {\n}\n',
            'while mut i = 0;; i < 3;; { i += 1 }\n': 'while mut i=0; i < 3 {\n  i += 1\n}\n',
            'x = match a;; { == 1 { 2 } }\n': 'x = match a {\n  == 1 { 2 }\n}\n',
            'loop a = 1;; b = 2;; { }\n': 'loop a=1; b=2 {\n}\n',
        }
        for mode in ('ai', 'human'):
            for source, expected in cases.items():
                with self.subTest(mode=mode, source=source):
                    self.assertEqual(self.fmt(source, mode), expected)

    def test_tick_takes_only_a_count(self):
        cases = {
            'test t {\n  tick N {\n    step\n  }\n}\n': 'test t {\n  tick N {\n    step\n  }\n}\n',
            'test t {\n  tick 0xA {\n    step\n  }\n}\n': 'test t {\n  tick 0xa {\n    step\n  }\n}\n',
        }
        for mode in ('ai', 'human'):
            for source, expected in cases.items():
                with self.subTest(mode=mode, source=source):
                    self.assertEqual(self.fmt(source, mode), expected)
        # The count is mandatory, and the `clocks=`/`resets=` clauses are gone.
        for src in ('test t {\n  tick {\n    step\n  }\n}\n',
                    'test t {\n  tick N clocks=(clk=1) {\n    step\n  }\n}\n',
                    'test t {\n  tick 4 resets=(rst=1) {\n    step\n  }\n}\n'):
            with self.subTest(src=src), tempfile.TemporaryDirectory() as wd:
                path = pathlib.Path(wd) / 'bad.prp'
                path.write_text(src)
                result = subprocess.run([str(FMT), str(path)], capture_output=True, text=True)
                self.assertNotEqual(result.returncode, 0, result.stdout)

    def test_human_breaks_before_slash_percent_and_case(self):
        # A line starting with `/`, `%` or `case` continues the statement (the
        # spec: a binary operator, and `case` ALWAYS), so a long chain breaks
        # before them like before `*` or `==`; the result reparses (fmt checks).
        source = ('const q = numerator_value_long_name / denominator_value_long_name % modulus_long_name '
                  '* factor_long_name\n'
                  'const hit = selector_value_long_name case pattern_value_long_name\n')
        for width in (10, 30, 60):
            with self.subTest(width=width):
                out = self.fmt(source, 'human', width)
                self.assertIn('\n  * factor_long_name', out)
                self.assertIn('\n  case pattern_value_long_name', out)
        self.assertIn('\n  % modulus_long_name', self.fmt(source, 'human', 30))
        # A `case` chain of 30-character names fits the 132 target.
        n = 'x' * 30
        chain = 'const c4 = ' + ' case '.join(f'{n}{i}' for i in range(1, 7)) + '\n'
        out = self.fmt(chain, 'human', 132)
        self.assertTrue(all(len(line) <= 132 for line in out.splitlines()), out)

    def test_if_chain_breaks_before_a_type_annotation(self):
        source = ('comb f(`in`:U8) -> (out:U8) {\n'
                  '  mut remaining:Unsigned(bits=N)          = if MSB_HIGHEST_PRIORITY '
                  '{ reverse_bits(value=`in`, width=N) } else { `in` }\n'
                  '  mut rows:Unsigned(bits=NUM_RESULTS * N) = 0\n  out = rows\n}\n')
        self.assertEqual(self.fmt(source, 'human', 60),
                         'comb f(`in`:U8) -> (out:U8) {\n'
                         '  mut remaining:Unsigned(bits=N) = if MSB_HIGHEST_PRIORITY {\n'
                         '    reverse_bits(value=`in`, width=N)\n  } else {\n    `in`\n  }\n'
                         '  mut rows:Unsigned(bits=NUM_RESULTS*N) = 0\n  out = rows\n}\n')

    def test_comment_before_allman_brace_moves_after_it(self):
        cases = {
            'if c == 1\n// lead\n{\n  o = 1\n}\n': 'if c == 1 { // lead\n  o = 1\n}\n',
            'if c {\n  o = 1\n} elif d // lead\n{\n  o = 2\n}\n': 'if c {\n  o = 1\n} elif d { // lead\n  o = 2\n}\n',
            'if c {\n  o = 1\n} else\n// lead\n{\n  o = 0\n}\n': 'if c {\n  o = 1\n} else { // lead\n  o = 0\n}\n',
            'while c == 1 // w\n{\n  o = 0\n}\n': 'while c == 1 { // w\n  o = 0\n}\n',
            'for i in 0..<2 // f\n{\n  o = i\n}\n': 'for i in 0..<2 { // f\n  o = i\n}\n',
            # Source order is kept: the moved comment trails the `{`, the
            # block's own `{` comment follows it.
            'for i in 0..<2 // f\n{ // own\n  o = i\n}\n': 'for i in 0..<2 { // f\n  // own\n  o = i\n}\n',
            'loop // l\n{\n  break\n}\n': 'loop { // l\n  break\n}\n',
        }
        for mode in ('ai', 'human'):
            for source, expected in cases.items():
                with self.subTest(mode=mode, source=source):
                    self.assertEqual(self.fmt(source, mode), expected)

    def test_generic_list_comments_stay_inside_the_brackets(self):
        cases = {
            'o = g<\n  // lead\n  W=1,\n>(a)\n': 'o = g<\n  // lead\n  W=1\n>(a)\n',
            'o = g<\n  W=1,\n  // tail\n>(a)\n': 'o = g<\n  W=1\n  // tail\n>(a)\n',
            'o = g<W=3, // trail\n>(a)\n': 'o = g<\n  W=3 // trail\n>(a)\n',
            'o = g<\n  // lead\n  W=1,\n  A=2, // two\n>(a)\n': 'o = g<\n  // lead\n  W=1,\n  A=2, // two\n>(a)\n',
            'o = g<W=1 /* b */>(a)\n': 'o = g<W=1 /* b */ >(a)\n',
            'o = g</* b */ W=1>(a)\n': 'o = g< /* b */ W=1>(a)\n',
            'comb f<\n  // lead\n  W=1,\n>(a:U8) -> (o:U8) {\n  o = a\n}\n':
                'comb f<\n  // lead\n  W=1\n>(a:U8) -> (o:U8) {\n  o = a\n}\n',
            'type T<\n  // lead\n  W:U8,\n> = (a:U8)\n': 'type T<\n  // lead\n  W:U8\n> = (a:U8)\n',
        }
        for mode in ('ai', 'human'):
            for source, expected in cases.items():
                with self.subTest(mode=mode, source=source):
                    self.assertEqual(self.fmt(source, mode), expected)

    def test_deep_nesting_is_not_cubic(self):
        # ts_node_parent() is O(depth); per-node ancestor walks made 400
        # nested resolved calls take seconds.
        depth = 400
        source = ('comb g(x:U8) -> (y:U8) {\n  y = x\n}\ncomb f(a:U8) -> (o:U8) {\n  o = '
                  + 'g(x=' * depth + 'a' + ')' * depth + '\n}\n')
        start = time.monotonic()
        self.fmt(source)
        self.assertLess(time.monotonic() - start, 5.0)

    def test_destructuring_and_for_index_lists_follow_list_layout(self):
        cases = {
            'const (a, // first\n  b) = t\n': 'const (\n  a, // first\n  b,\n) = t\n',
            'for (i, // idx\n  v) in t {\n  o = v\n}\n': 'for (\n  i, // idx\n  v,\n) in t {\n  o = v\n}\n',
            'mut (p:U8, // pp\n  q:U8)\n': 'mut (\n  p:U8, // pp\n  q:U8,\n)\n',
            'const (\n  // lead\n  m, n) = t\n': 'const (\n  // lead\n  m,\n  n,\n) = t\n',
            # Inline lists drop the trailing comma; a one-item list keeps it.
            'const (a, b,) = t\n': 'const (a, b) = t\n',
            '(a, b,) = t\n': '(a, b) = t\n',
            'for (k, v,) in t {\n  o = k\n}\n': 'for (k, v) in t {\n  o = k\n}\n',
            '(g,) = t\n': '(g,) = t\n',
            'const (h,) = t\n': 'const (h,) = t\n',
            'for (k,) in t {\n  o = k\n}\n': 'for (k,) in t {\n  o = k\n}\n',
            # A one-item binding tuple keeps its comma as written.
            'y = (a=1,)\n': 'y = (a=1,)\n',
        }
        for mode in ('ai', 'human'):
            for source, expected in cases.items():
                with self.subTest(mode=mode, source=source):
                    self.assertEqual(self.fmt(source, mode), expected)

    def test_block_comment_only_list_stays_inline(self):
        for mode in ('ai', 'human'):
            for source in ('y = f( /* c */ )\n', 'z = ( /* c */ )\n', 'v = [ /* c */ ]\n', 'w = f( /* a */ /* b */ )\n'):
                with self.subTest(mode=mode, source=source):
                    self.assertEqual(self.fmt(source, mode), source)
                    self.assertEqual(self.fmt(source.replace('( ', '(').replace(' )', ')').replace('[ ', '[')
                                              .replace(' ]', ']'), mode), source)
            self.assertEqual(self.fmt('u = f(\n  /* c */\n)\n', mode), 'u = f( /* c */ )\n')

    def test_human_if_expression_argument_breaks(self):
        source = ('const zz = f(a=1, b=if selector_signal_name == 1 { first_option_value_name } '
                  'elif selector_signal_name == 2 { second_option_value + another_long_value_name } else { third })\n')
        self.assertEqual(self.fmt(source, 'human'),
                         'const zz = f(\n  a=1,\n  b=if selector_signal_name == 1 { first_option_value_name }\n'
                         '    elif selector_signal_name == 2 { second_option_value + another_long_value_name }\n'
                         '    else { third },\n)\n')
        self.assertEqual(self.fmt(source, 'human', 60),
                         'const zz = f(\n  a=1,\n  b=if selector_signal_name == 1 {\n    first_option_value_name\n'
                         '  } elif selector_signal_name == 2 {\n    second_option_value + another_long_value_name\n'
                         '  } else {\n    third\n  },\n)\n')
        self.assertEqual(self.fmt(source, 'ai'), source)
        # A branch block never breaks without its chain (no `{ a } else {`
        # newline staircase), at any width near the boundary.
        source = ('comb f() -> () {\n  const out = g<WIDTH=WIDTH>(clk=clk, pop_ready=pop_ready, '
                  'push_data=if skid_valid { skid_data } else { ram_rd_data }, rst=rst)\n}\n')
        for width in range(54, 68, 2):
            with self.subTest(width=width):
                out = self.fmt(source, 'human', width)
                self.assertRegex(out, r'push_data=if skid_valid \{ skid_data \}( else|\n +else) \{ ram_rd_data \},')

    def test_multi_line_statement_leaves_the_alignment_group(self):
        source = ('mut e:Unsigned(bits=N, max=3) = 0\nmut f:Unsigned(\n  bits=N // c\n) = 0\n'
                  'mut g:U8 = 1\nmut hhhh:U8 = 1\n')
        self.assertEqual(self.fmt(source, 'human'),
                         'mut e:Unsigned(bits=N, max=3) = 0\nmut f:Unsigned(\n  bits=N // c\n) = 0\n'
                         'mut g:U8    = 1\nmut hhhh:U8 = 1\n')

    def test_generic_values_with_calls_keep_order(self):
        for mode in ('ai', 'human'):
            with self.subTest(mode=mode):
                self.assertEqual(self.fmt('y1 = f<W=2, A=g(x)>(a=1)\n', mode), 'y1 = f<W=2, A=g(x)>(a=1)\n')
                self.assertEqual(self.fmt('y3 = f<W=2, A=U8(x)>(a=1)\n', mode), 'y3 = f<A=U8(x), W=2>(a=1)\n')
                self.assertEqual(self.fmt('y4 = f<W=2, A=Unsigned(x)>(a=1)\n', mode),
                                 'y4 = f<A=Unsigned(x), W=2>(a=1)\n')

    def test_generic_call_has_one_opening_bracket(self):
        # The call printer and generic-list printer must not both emit '<'.
        for mode in ('ai', 'human'):
            for source, expected in (
                ('const x=generic<W=4>(1,2)\n', 'const x = generic<W=4>(1, 2)\n'),
                ('const x=ns.generic<W=4>(a=1)\n', 'const x = ns.generic<W=4>(a=1)\n'),
                ('cassert(generic<W=4>(1,15)==16)\n', 'cassert(generic<W=4>(1, 15) == 16)\n'),
            ):
                with self.subTest(mode=mode, source=source):
                    self.assertEqual(self.fmt(source, mode), expected)
                    self.assertEqual(self.fmt(expected, mode), expected)

    def test_comparisons_never_reparse_as_a_generic_call(self):
        # A `<` after a blank is always a comparison (owner ruling 107) and
        # every comparison prints spaced, so a list with both a `<` and a `>`
        # comparison drops its parentheses and sorts like any other: the
        # output never reads as the generic call `a<b, y=c>(d + 1)`.
        decl = 'comb h(x:Bool, y:Bool) -> (o:Bool) {\n  o = x and y\n}\n'
        for mode in ('ai', 'human'):
            for source, expected in (
                ('o = h(x=(a < b), y=(c > (d + 1)))\n', 'o = h(x=a < b, y=c > (d + 1))\n'),
                ('o = h(y=c > (d + 1), x=a < b)\n', 'o = h(x=a < b, y=c > (d + 1))\n'),
                ('(o, p) = ((a) < (b), (c) > (d + 1))\n', '(o, p) = (a < b, c > (d + 1))\n'),
                ('o = [(a < b), (c > (d))]\n', 'o = [(a < b), (c > d)]\n'),
                ('o = h(y=(c < (d)), x=(a < b))\n', 'o = h(x=a < b, y=c < d)\n'),
                ('z = (a) < (b) > (c)\n', 'z = a < b > c\n'),
            ):
                with self.subTest(mode=mode, source=source):
                    self.assertEqual(self.fmt(decl + source, mode), decl + expected)
                    self.assertEqual(self.fmt(decl + expected, mode), decl + expected)
            # A generic list holding a comparison keeps its parentheses: a bare
            # `>` would close the list.
            self.assertEqual(self.fmt('u = f<N=(a > b)>(x=1)\n', mode), 'u = f<N=(a > b)>(x=1)\n')
            # The generic call needs its `<` glued to the callee; glued input
            # stays a generic call.
            self.assertEqual(self.fmt('o = h(x=a<b, y=c>(d + 1))\n', mode), 'o = h(x=a<b, y=c>(d + 1))\n')

    def test_parameters_bound_by_position_keep_their_order(self):
        # lhd still binds a leftover unnamed argument by position
        # (`addby(ref m, by=2)` binds `m` to the first parameter), so a lambda
        # that a call in the file passes an unnamed non-pun argument keeps its
        # parameter order; a same-name pun (`ref x`, bare `x`) binds by name.
        for mode in ('ai', 'human'):
            kept = 'comb addby(ref x:U8, by:U8) -> () {\n  wrap x = x + by\n}\naddby(ref m, by=2)\n'
            self.assertEqual(self.fmt(kept, mode), kept)
            pun = kept.replace('addby(ref m, by=2)', 'addby(ref x, by=2)')
            self.assertEqual(self.fmt(pun, mode), pun.replace('addby(ref x:U8, by:U8)', 'addby(by:U8, ref x:U8)'))

    def test_parameter_defaults_and_tuple_types_sort(self):
        # A named tuple VALUE in a parameter default sorts (owner ruling 105).
        # A tuple TYPE (`mut t:(b=U4, a=U4)`, also as a generic default or
        # binding, which take types) is a layout and keeps its order: typed
        # tuples are constructed positionally by type (qa.md, 2026-09-29).
        for mode in ('ai', 'human'):
            source = 'comb g(p=(b=1, a=2)) -> (o) {\n  o = p\n}\n'
            with self.subTest(mode=mode, source=source):
                self.assertEqual(self.fmt(source, mode), source.replace('b=1, a=2', 'a=2, b=1'))
            for source in (
                'comb f<T=(b=1, a=2)>(p) -> (o) {\n  o = p\n}\n',
                'mut t:(b=U4, a=U4) = 0\n',
                'const x = f<T=(b=1, a=2)>(q)\n',
            ):
                with self.subTest(mode=mode, source=source):
                    self.assertEqual(self.fmt(source, mode), source)
            # A type call's arguments are no tuple type: they sort.
            self.assertEqual(self.fmt('mut b:Signed(min=0, max=300) = 0\n', mode), 'mut b:Signed(max=300, min=0) = 0\n')

    def test_nested_branch_values_are_idempotent(self):
        # A branch block that broke (pass 1) keeps a source break after `{`
        # on pass 2; its `if`/`match` value must print the same either way.
        nested_match = 'const x = match a { == 1 { match a { == 0 { 0 } else { 0 } } } else { 1 } }\n'
        expected = ('const x = match a {\n  == 1 {\n    match a {\n      == 0 { 0 }\n      else { 0 }\n    }\n  }\n'
                    '  else {\n    1\n  }\n}\n')
        nested_if = ('const x = if a == 5 { if a == 4 { if a == 3 { if a == 2 { if a == 1 { if a == 0 { 0 } else { 0 } }'
                     ' else { 1 } } else { 2 } } else { 3 } } else { 4 } } else { 5 }\n')
        for mode in ('ai', 'human'):
            with self.subTest(mode=mode):
                self.assertEqual(self.fmt(nested_match, mode), expected)
                self.fmt('const x = if c { match a { == 1 { match b { == 0 { 0 } else { 0 } } } == 2 { 5 } else { 1 } } }'
                         ' else { 2 }\n', mode)
        out = self.fmt(nested_if, 'human')
        self.assertEqual(len(out.splitlines()), 5)
        self.assertEqual(self.fmt(nested_if, 'ai'), nested_if)

    def test_block_comment_body_trails_the_header(self):
        # `{ /* c */ }` counts like a trailing comment in both passes, so an
        # over-width comment does not split a header that fits.
        comment = '/* ' + 'c' * 40 + ' */'
        source = ('comb some_long_operation_name(self, d=3, data_input_a:U32, data_input_b:U32) -> (result_value:U32) { '
                  + comment + ' }\n')
        self.assertEqual(self.fmt(source, 'human'), source[:-3] + '\n}\n')
        for width in (20, 40):
            with self.subTest(width=width):
                self.fmt('comb some_op(self, d=3) -> (r) { /* a block comment body */ }\n', 'human', width)

    def test_value_block_or_match_in_expression_breaks_the_layout(self):
        # A `match` value or a block that must break is a must-break item: an
        # `if` expression takes the statement layout (no `} } else {` tail)
        # and a list goes one item per line, the block closing at the item.
        cases = {
            'const x = if c { match a { == 0 { 1 } else { 2 } } } else { 3 }\n':
                'const x = if c {\n  match a {\n    == 0 { 1 }\n    else { 2 }\n  }\n} else {\n  3\n}\n',
            'const y = (p=match a { == 0 { 1 } else { 2 } }, q=1)\n':
                'const y = (\n  p=match a {\n    == 0 { 1 }\n    else { 2 }\n  },\n  q=1,\n)\n',
            'const z = f(match a { == 0 { 1 } else { 2 } })\n':
                'const z = f(\n  match a {\n    == 0 { 1 }\n    else { 2 }\n  }\n)\n',
            'const w = (p={ const k = 1; k }, q=1)\n':
                'const w = (\n  p={\n    const k = 1\n    k\n  },\n  q=1,\n)\n',
            'o = f(b=2, a=if c { const q = x; q + 1 } else { 0 })\n':
                'o = f(\n  b=2,\n  a=if c {\n    const q = x\n    q + 1\n  } else {\n    0\n  },\n)\n',
        }
        for mode in ('ai', 'human'):
            for source, expected in cases.items():
                with self.subTest(mode=mode, source=source):
                    self.assertEqual(self.fmt(source, mode), expected)

    def test_comment_after_one_line_branch_stays_with_it(self):
        cases = {
            'if en { o = a } // take a\nelse { o = 0 }\n':
                'if en {\n  o = a // take a\n} else {\n  o = 0\n}\n',
            'o = if en { a } // c1\nelif b == 1 { c } else { d }\n':
                'o = if en {\n  a // c1\n} elif b == 1 {\n  c\n} else {\n  d\n}\n',
            # After a multi-line branch the comment still moves to the header.
            'if en {\n  o = a\n} // after\nelse {\n  o = 0\n}\n':
                'if en {\n  o = a\n} else { // after\n  o = 0\n}\n',
        }
        for mode in ('ai', 'human'):
            for source, expected in cases.items():
                with self.subTest(mode=mode, source=source):
                    self.assertEqual(self.fmt(source, mode), expected)

    def test_generic_attribute_and_default_values_drop_grouping(self):
        cases = {
            # A generic value is a type: only an atom drops its parentheses.
            'o = g<W=(N + 1), A=(M)>(x)\n': 'o = g<A=M, W=(N + 1)>(x)\n',
            # A call keeps them (a bare call reads as a type); a negative
            # literal is an atom (owner ruling 102: `<N=-3>` is legal).
            'o = g<B=(f(x)), C=(3), D=(a.b)>(x)\n': 'o = g<B=(f(x)), C=3, D=a.b>(x)\n',
            'o = f<N=(-3)>(a)\n': 'o = f<N=-3>(a)\n',
            'o = g<N=(k(a=2))>(x)\n': 'o = g<N=(k(a=2))>(x)\n',
            'comb g<N=(-3)>(x) -> (o) {\n  o = x\n}\n': 'comb g<N=-3>(x) -> (o) {\n  o = x\n}\n',
            'comb g<N=(k(a=2))>(x) -> (o) {\n  o = x\n}\n': 'comb g<N=(k(a=2))>(x) -> (o) {\n  o = x\n}\n',
            'o = g<A=(a > b)>(x)\n': 'o = g<A=(a > b)>(x)\n',
            'reg r:U8:[initial=(N + 1), async=(false)] = 0\n': 'reg r:U8:[async=false, initial=N + 1] = 0\n',
            'comb f(a:U8=(3), b:U8=(3 + 1)) -> (o:U8) {\n  o = a\n}\n':
                'comb f(a:U8=3, b:U8=3 + 1) -> (o:U8) {\n  o = a\n}\n',
            'comb h<A=(M), B=(3)>(a) -> (o) {\n  o = a\n}\n': 'comb h<A=M, B=3>(a) -> (o) {\n  o = a\n}\n',
        }
        for mode in ('ai', 'human'):
            for source, expected in cases.items():
                with self.subTest(mode=mode, source=source):
                    self.assertEqual(self.fmt(source, mode), expected)


    def test_own_line_comment_inside_expression_keeps_its_line(self):
        # An own-line comment before a continuation operator, a selector range
        # operator or a lambda's `->` stays on its own line, at the
        # continuation's indentation (it used to glue as `a// c`, then move).
        cases = {
            'const r = a\n  // own\n  or b\n': 'const r = a\n  // own\n  or b\n',
            'const r = a // tr\n  or b\n': 'const r = a // tr\n  or b\n',
            'e = 1\n  // own\n  + 2\n': 'e = 1\n  // own\n  + 2\n',
            'fluid f(a:U8)\n  // own\n  -> (r:U8) {\n  r = a\n}\n': 'fluid f(a:U8)\n  // own\n  -> (r:U8) {\n  r = a\n}\n',
            'o = v#[(f*4)\n    // own\n    ..+4]\n': 'o = v#[(f*4)\n  // own\n  ..+4]\n',
            'o = v#[f\n  // own\n  ..<4]\n': 'o = v#[f\n  // own\n  ..<4]\n',
            'o = v#[f // tr\n  ..=4]\n': 'o = v#[f // tr\n  ..=4]\n',
        }
        for mode in ('ai', 'human'):
            for source, expected in cases.items():
                with self.subTest(mode=mode, source=source):
                    self.assertEqual(self.fmt(source, mode), expected)

    def test_block_comment_before_operator_single_space(self):
        for mode in ('ai', 'human'):
            for source in ('o = a /* x */ + b\n', 'o = a /* x */ == b\n', 'o = a /* x */ and b\n',
                           'o = a /* x */ << b\n', 'o = f(a /* x */ + b)\n'):
                with self.subTest(mode=mode, source=source):
                    self.assertEqual(self.fmt(source, mode), source)

    def test_nested_list_forced_vertical_by_comment_or_block(self):
        # A nested argument or tuple-field list that must go one item per
        # line does so like the outer list: trailing commas, own-line
        # comments kept, `)` at the item's indentation.
        cases = {
            'const d = g(\n x=1,\n req=(\n a=p,\n // own\n c=r,\n ),\n)\n':
                'const d = g(\n  x=1,\n  req=(\n    a=p,\n    // own\n    c=r,\n  ),\n)\n',
            'const d = f(\n x=1,\n g(\n p,\n // own\n r,\n ),\n)\n':
                'const d = f(\n  x=1,\n  g(\n    p,\n    // own\n    r,\n  ),\n)\n',
            'const t = (\n x=1,\n y=(\n // own\n p=1,\n q=2,\n ),\n)\n':
                'const t = (\n  x=1,\n  y=(\n    // own\n    p=1,\n    q=2,\n  ),\n)\n',
            'const d = g(\n x=1,\n req=(\n a = p // c1\n ,b = q\n // own\n ,c = r\n )\n)\n':
                'const d = g(\n  x=1,\n  req=(\n    a=p, // c1\n    b=q,\n    // own\n    c=r,\n  ),\n)\n',
            'const o = (\n a2 = (\n v = 1,\n comb init(ref self, x) { self.v = x + 1 },\n'
            ' comb val(self) -> (r) { r = self.v }\n ),\n)\n':
                'const o = (\n  a2=(\n    v=1,\n    comb init(ref self, x) {\n      self.v = x + 1\n    },\n'
                '    comb val(self) -> (r) {\n      r = self.v\n    },\n  ),\n)\n',
            'const w = foo(q=1, r=(s=2, t=match a { == 1 { 3 } else { 4 } }))\n':
                'const w = foo(\n  q=1,\n  r=(\n    s=2,\n    t=match a {\n      == 1 { 3 }\n      else { 4 }\n    },\n  ),\n)\n',
        }
        for mode in ('ai', 'human'):
            for source, expected in cases.items():
                with self.subTest(mode=mode, source=source):
                    self.assertEqual(self.fmt(source, mode), expected)

    def test_div_and_mod_stay_tight_and_break_like_mul(self):
        # `/` and `%` are tight like `*` (`a/b`), and, like `*`, a line may start
        # with them (the statement continues), so a Human-mode chain that does
        # not fit breaks before them too (`% b`).
        a, b = 'a' * 24, 'b' * 108
        for op in ('/', '%'):
            source = f'const q = {a}{op}{b}\n'
            with self.subTest(op=op, mode='ai'):
                self.assertEqual(self.fmt(source, 'ai'), source)
            with self.subTest(op=op, mode='human'):
                self.assertEqual(self.fmt(source, 'human'), f'const q = {a}\n  {op} {b}\n')
        self.assertEqual(self.fmt('const q = a / b + c % d\n', 'human', 12), 'const q = a/b\n  + c%d\n')
        # A 12-term `%` chain no longer stays one long line (it used to be
        # about 220 columns at the 132 target).
        chain = ' % '.join(f'operand_value_{i:02d}' for i in range(12))
        out = self.fmt(f'const m = {chain}\n', 'human')
        self.assertTrue(all(len(line) <= 132 for line in out.splitlines()), out)
        self.assertIn('\n  % operand_value_', out)

    def test_trailing_comment_keeps_alignment_group(self):
        source = 'mut p = 1 // c\nmut arr = 2 // d\n\nconst b = 1\nconst test1 = 3 // OK\nconst something = 4 // OK\n'
        self.assertEqual(self.fmt(source, 'human'),
                         'mut p   = 1 // c\nmut arr = 2 // d\n\n'
                         'const b         = 1\nconst test1     = 3 // OK\nconst something = 4 // OK\n')

    def test_old_type_spellings_are_plain_names(self):
        # The old lowercase type spellings (`u8`, `s2`, `i32`, `bool`,
        # `unsigned`, ...) are ordinary names since the 2026-09-30 ruling:
        # bare they format as written, backticked they LOSE their backticks.
        # The exact new type words (`U8`, `S2`, `Bool`, ...) keep theirs.
        bare = ('const u8 = 1\n', 'const s2 = 1\n', 'const i32 = 1\n', 'const bool = 1\n', 'const unsigned = 1\n',
                'const x:u8 = 1\n', 'const x = bool(y)\n', 'y.string = 1\n', 'comb s2(a) -> (o) {\n  o = a\n}\n',
                'mod m(i32:U8) -> (y:U8) {\n  y = i32\n}\n', 'const q = (boolean=2, i32=1)\n')
        for mode in ('ai', 'human'):
            for src in bare:
                with self.subTest(mode=mode, src=src):
                    self.assertEqual(self.fmt(src, mode), src)
                    self.assertEqual(self.fmt(src.replace('u8', '`u8`').replace('i32', '`i32`'), mode), src)
            for src, expected in (('const `u8` = 2\n', 'const u8 = 2\n'), ('const `s2` = 2\n', 'const s2 = 2\n'),
                                  ('const t = x.`bool`.`_3` + `string`\n', 'const t = x.bool.`_3` + string\n'),
                                  ('comb `s1`(a) -> (o) {\n  o = a\n}\n', 'comb s1(a) -> (o) {\n  o = a\n}\n'),
                                  ('const q = (`boolean`=2, `i32`=1)\n', 'const q = (boolean=2, i32=1)\n'),
                                  ('const `U8` = 1\nconst `S2` = 2\n', 'const `U8` = 1\nconst `S2` = 2\n')):
                with self.subTest(mode=mode, src=src):
                    self.assertEqual(self.fmt(src, mode), expected)
        # The reserved placeholders keep their backticks (bare they are errors),
        # and so does the bare `_`.
        for mode in ('ai', 'human'):
            for src in ('const `_0` = 8\n', 'for `_1` in 0..<2 {\n  puts("{`_1`}")\n}\n',
                        'mod m(`_2`:U8) -> (y:U8) {\n  y = `_2`\n}\n'):
                with self.subTest(mode=mode, src=src):
                    self.assertEqual(self.fmt(src, mode), src)
        for src in ('const U8 = 1\n', 'const S2 = 1\n', 'const _0 = 1\n', 'const x = _ + 1\n'):
            with self.subTest(src=src), tempfile.TemporaryDirectory() as wd:
                path = pathlib.Path(wd) / 'bad.prp'
                path.write_text(src)
                result = subprocess.run([str(FMT), str(path)], capture_output=True, text=True)
                self.assertNotEqual(result.returncode, 0, result.stdout)

    def test_type_annotation_after_trailing_comment_is_a_continuation(self):
        # `mut d // c` newline `:U8 = 3`: the `:` line continues the
        # statement one indent level deeper, like `+ b` or `#[0]`.
        src = ('mut d // decl\n:U8 = 3\nmod m() {\n  mut e // decl\n  :U8 = 3\n'
               '  reg f // c\n        :U8\n}\nconst t = (a // c\n:U8=1, b=2)\n')
        expected = ('mut d // decl\n  :U8 = 3\nmod m() {\n  mut e // decl\n    :U8 = 3\n'
                    '  reg f // c\n    :U8\n}\nconst t = (\n  a // c\n    :U8=1,\n  b=2,\n)\n')
        for mode in ('ai', 'human'):
            with self.subTest(mode=mode):
                self.assertEqual(self.fmt(src, mode), expected)

    def test_strings_are_byte_exact(self):
        # `{{`/`}}` literal braces, every escape, format specs and comments
        # inside holes print exactly as written (a hole's expression is
        # formatted, see test_string_hole_expression_is_formatted).
        src = ('const a = "I have {{num}} and }} and {{"\n'
               'const b = "\\u{2287}\\x41\\n\\t\\r\\\\\\"\\\'\\0\\`"\n'
               'const c = "v={x:b} w={y + 1:x} z={f(a=1)}"\n'
               'const d = "{x /* } { : */ } and {y}"\n'
               'const e = "/* no */ // nor"\n')
        for mode in ('ai', 'human'):
            with self.subTest(mode=mode):
                out = self.fmt(src, mode)
                for line in src.splitlines():
                    self.assertIn(line.split('= ', 1)[1], out)

    def test_unresolved_names_keep_call_order(self):
        # A lambda with a `self` first parameter keeps source order
        # (documented, not sorted). A conversion on a type word always sorts:
        # the word is reserved, so no binding can shadow it, even where it is
        # also used as a value (`zz=U8`); a lowercase name (`u8`, bare or backticked)
        # is an ordinary, unresolved call and keeps its order.
        cases = {
            'const t4 = U8(max=3, bits=4)\n': 'const t4 = U8(bits=4, max=3)\n',
            'const t4 = U8(max=3, bits=4)\n\nconst x = (zz=U8, aa=1)\n':
                'const t4 = U8(bits=4, max=3)\n\nconst x = (aa=1, zz=U8)\n',
            'const t4 = `u8`(max=3, bits=4)\n': 'const t4 = u8(max=3, bits=4)\n',
            'const t4 = u8(max=3, bits=4)\n': 'const t4 = u8(max=3, bits=4)\n',
            'comb f(self, a, b) -> (r) {\n  r = a\n}\nconst q = f(b=1, a=2)\n':
                'comb f(self, a, b) -> (r) {\n  r = a\n}\nconst q = f(b=1, a=2)\n',
        }
        for mode in ('ai', 'human'):
            for source, expected in cases.items():
                with self.subTest(mode=mode, source=source):
                    self.assertEqual(self.fmt(source, mode), expected)

    def test_comment_between_match_arms_keeps_arms_inline(self):
        cases = {
            'y = match z { // c\n  == 1 { 4 }\n  else { 0 }\n}\n': 'y = match z { // c\n  == 1 { 4 }\n  else { 0 }\n}\n',
            'y = match z {\n  == 1 { 4 } // d\n  else { 0 }\n}\n': 'y = match z {\n  == 1 { 4 } // d\n  else { 0 }\n}\n',
            'y = match z {\n  // own\n  == 1 { 4 }\n  // own2\n  == 2 { 5 } /* b */\n  else { 0 }\n  // end\n}\n':
                'y = match z {\n  // own\n  == 1 { 4 }\n  // own2\n  == 2 { 5 } /* b */\n  else { 0 }\n  // end\n}\n',
            'y = match z { == 1 { 4 } /* b */ else { 0 } }\n': 'y = match z {\n  == 1 { 4 } /* b */\n  else { 0 }\n}\n',
            # A comment inside an arm block still breaks every arm.
            'y = match z {\n  == 1 { 4 // in\n  }\n  else { 0 }\n}\n':
                'y = match z {\n  == 1 {\n    4 // in\n  }\n  else {\n    0\n  }\n}\n',
        }
        for mode in ('ai', 'human'):
            for source, expected in cases.items():
                with self.subTest(mode=mode, source=source):
                    self.assertEqual(self.fmt(source, mode), expected)

    def test_nested_if_expressions_scale(self):
        # Subtree scans are memoized: depth 400 used to take over 10 s.
        depth = 400
        source = 'const a = ' + ''.join(f'if c{i} {{ ' for i in range(depth)) + '0' + \
                 ''.join(f' }} else {{ {i} }}' for i in range(depth)) + '\n'
        start = time.monotonic()
        self.fmt(source)
        self.assertLess(time.monotonic() - start, 5.0)

    # --- residual defects (each keeps a regression case) ---

    def test_semicolon_before_else_prefixed_word_drops(self):
        # The scanner and prpparse (which lhd builds from this tree) read
        # `else`/`elif` as WHOLE words: `els`, `eli`, `elsev`, `elifz`,
        # `elsewhere` at line start are names that start a new statement, so
        # the `;` before one is redundant and drops like any other.
        cases = {
            'comb top(x:U8) -> (o:U8) {\n  mut elsev:U8 = 0\n  mut y:U8 = x; elsev = 3\n  o = y | elsev\n}\n':
                'comb top(x:U8) -> (o:U8) {\n  mut elsev:U8 = 0\n  mut y:U8 = x\n  elsev = 3\n  o = y | elsev\n}\n',
            'mut els = 0\nmut y = 1; els = 3\n': 'mut els = 0\nmut y = 1\nels = 3\n',
            'mut els = 0\nmut y = 1; els=3\n': 'mut els = 0\nmut y = 1\nels = 3\n',
            'mut eli = 0\nmut y = 1; eli = 3\n': 'mut eli = 0\nmut y = 1\neli = 3\n',
            'mut elifz = 0\nmut y = 1; elifz = 3\n': 'mut elifz = 0\nmut y = 1\nelifz = 3\n',
            'mut elsev = 0\nmut y = 1; `elsev` = 3\n': 'mut elsev = 0\nmut y = 1\nelsev = 3\n',
            'comb top(elsewhere:U8) -> (o:U8) {\n  o = { const k = 1; elsewhere }\n}\n':
                'comb top(elsewhere:U8) -> (o:U8) {\n  o = {\n    const k = 1\n    elsewhere\n  }\n}\n',
            'mut elsa = 0\nmut y = 1; elsa = 3\n': 'mut elsa = 0\nmut y = 1\nelsa = 3\n',
        }
        for mode in ('ai', 'human'):
            for source, expected in cases.items():
                with self.subTest(mode=mode, source=source):
                    # Human mode aligns the `mut` group's `=`; compare
                    # without that padding.
                    self.assertEqual(re.sub('(?<=[^ \\n]) +', ' ', self.fmt(source, mode)), expected)

    def test_line_comment_inside_generic_attribute_and_timing_lists(self):
        # A `//` comment inside a generic binding, an attribute item or a
        # timing slot used to be glued to the next token, commenting out
        # `=3` (a silent change of meaning: `addk<K>` instead of K=3) or
        # breaking the parse.
        cases = {
            'o = addk<K // three\n  =3>(a)\n': 'o = addk<\n  K // three\n  =3\n>(a)\n',
            'const x = g<W= // c\n  2>(b)\n': 'const x = g<\n  W= // c\n  2\n>(b)\n',
            'mut x:U8:[async // c\n  =true] = 1\n': 'mut x:U8:[\n  async // c\n  =true\n] = 1\n',
            'mut e:U8:[async= // c\n  true, posclk=false] = 1\n':
                'mut e:U8:[\n  async= // c\n  true,\n  posclk=false,\n] = 1\n',
            'reg cnt:U8:[reset_pin // c\n  =ref rst, posclk=true] = 0\n':
                'reg cnt:U8:[\n  reset_pin // c\n  =ref rst,\n  posclk=true,\n] = 0\n',
            'mod top(a:U8) -> (o:U9@[ // c\n  1]) {\n  o = a\n}\n':
                'mod top(a:U8) -> (\n  o:U9@[ // c\n  1]\n) {\n  o = a\n}\n',
            'mut x:U8@[1 // c\n] = 1\n': 'mut x:U8@[1 // c\n] = 1\n',
        }
        for mode in ('ai', 'human'):
            for source, expected in cases.items():
                with self.subTest(mode=mode, source=source):
                    self.assertEqual(self.fmt(source, mode), expected)

    def test_deep_nesting_does_not_overflow_the_stack(self):
        # 8000 nested `if` expressions or calls overflowed the 8 MB main-thread
        # stack (exit 139); formatting now runs on a large-stack thread, and a
        # tree deeper than it can hold is refused cleanly, the file untouched.
        with tempfile.TemporaryDirectory() as wd:
            path = pathlib.Path(wd) / 'deep.prp'
            for source in ('const x = ' + 'if c { ' * 12000 + '1' + ' } else { 2 }' * 12000 + '\n',
                           'const x = ' + 'f(a, ' * 8000 + 'b' + ')' * 8000 + '\n'):
                path.write_text(source)
                r = subprocess.run([str(FMT), str(path)], capture_output=True, text=True)
                self.assertEqual(r.returncode, 0, r.stderr)
                self.assertTrue(r.stdout.startswith('const x = '))
            deep = 'const x = ' + '-' * 250000 + 'a\n'
            path.write_text(deep)
            r = subprocess.run([str(FMT), '-i', str(path)], capture_output=True, text=True)
            self.assertEqual(r.returncode, 1)
            self.assertIn('nests too deeply', r.stderr)
            self.assertEqual(path.read_text(), deep)

    def test_if_with_header_block_comment_breaks_its_list_and_arm(self):
        # A block comment in an `if` expression's header gives the chain the
        # statement layout, so its list goes one item per line and its match
        # arm breaks: no mixed `f(a=if c {` or `} }` tail, and idempotent.
        cases = {
            'x = f(a=if c { 1 } /* b */ else { 2 }, b=3)\n':
                'x = f(\n  a=if c {\n    1\n  } else { /* b */\n    2\n  },\n  b=3,\n)\n',
            'const y = match a {\n  == 1 { 4 }\n  else { if b == 0 { 1 } /* Z */ else { 2 } }\n}\n':
                'const y = match a {\n  == 1 {\n    4\n  }\n  else {\n    if b == 0 {\n      1\n    } else { /* Z */\n'
                '      2\n    }\n  }\n}\n',
            'const y = match a {\n  == 1 { 4 }\n  else { if /* Z */ b == 0 { 1 } else { 2 } }\n}\n':
                'const y = match a {\n  == 1 {\n    4\n  }\n  else {\n    if /* Z */ b == 0 {\n      1\n    } else {\n'
                '      2\n    }\n  }\n}\n',
            'x = (a=if c { 1 } else /* b */ { 2 }, b=3)\n':
                'x = (\n  a=if c {\n    1\n  } else { /* b */\n    2\n  },\n  b=3,\n)\n',
        }
        for mode in ('ai', 'human'):
            for source, expected in cases.items():
                with self.subTest(mode=mode, source=source):
                    self.assertEqual(self.fmt(source, mode), expected)

    def test_step_semicolon_drops_and_parenthesized_count_glues(self):
        source = 'tick 10 {\n  step;\n  x = 1\n  step 2;\n  x = 2\n  step(3)\n  step (4)\n}\n'
        expected = 'tick 10 {\n  step\n  x = 1\n  step 2\n  x = 2\n  step(3)\n  step(4)\n}\n'
        for mode in ('ai', 'human'):
            self.assertEqual(self.fmt(source, mode), expected)

    def test_continuation_after_a_line_comment_is_indented(self):
        source = ('mod m(a:U8) -> (o:U8@[0], p:U8@[0]) {\n  const y = // b\n    a + 1\n  const z = // b\n    a\n'
                  '  o = // c\n    y ^ z\n  p = a#[ // d\n    0..<4]\n}\n')
        for mode in ('ai', 'human'):
            self.assertEqual(self.fmt(source, mode), source)
        cases = {
            'x = // b\n2 + a\n': 'x = // b\n  2 + a\n',
            'comb f(a:U8) // x\n-> (r:U8) {\n  r = a\n}\n': 'comb f(a:U8) // x\n  -> (r:U8) {\n  r = a\n}\n',
            'const q = v#[\n// c\n0..<4]\n': 'const q = v#[\n  // c\n  0..<4]\n',
            # Dotted chains continue one level deeper too, never under the `=`.
            'mut longer_name = f(a)\n  // c\n  .g()\n': 'mut longer_name = f(a)\n  // c\n  .g()\n',
            'mut w = a.b\n// c\n.c(1)\n// d\n.e\n': 'mut w = a.b\n  // c\n  .c(1)\n  // d\n  .e\n',
        }
        for mode in ('ai', 'human'):
            for source, expected in cases.items():
                with self.subTest(mode=mode, source=source):
                    self.assertEqual(self.fmt(source, mode), expected)

    def test_human_splits_nested_lists_that_do_not_fit(self):
        source = ('const t = f(aw=g(addr=long_address_name, id=long_identifier_name, x=another_long_name_here, '
                  'y=yet_another_long_one), data=1)\n')
        out = self.fmt(source, 'human', 80)
        self.assertEqual(out, 'const t = f(\n  aw=g(\n    addr=long_address_name,\n    id=long_identifier_name,\n'
                              '    x=another_long_name_here,\n    y=yet_another_long_one,\n  ),\n  data=1,\n)\n')
        self.assertTrue(all(len(line) <= 80 for line in out.splitlines()), out)
        # A nested list that fits stays whole.
        self.assertIn('aw=(addr=a, id=b),', self.fmt('const t = (aw=(addr=a, id=b), ' + 'z' * 90 + '=1)\n', 'human', 80))

    def test_alignment_counts_display_columns(self):
        # `ééé` is a plain name (its backticks drop); `é·é` keeps them (`·`
        # is no letter). Either way a non-ASCII letter counts one column.
        self.assertEqual(self.fmt('const `ééé` = 1\nconst abcd = 2\n', 'human'),
                         'const ééé  = 1\nconst abcd = 2\n')
        self.assertEqual(self.fmt('const `é·é` = 1\nconst abcd = 2\n', 'human'),
                         'const `é·é` = 1\nconst abcd  = 2\n')

    def test_leading_comma_tuple_keeps_a_comma(self):
        cases = {
            'const t = (,(1, 2))\n': 'const t = ((1, 2),)\n',
            'const u = (,a)*c\n': 'const u = (a,)*c\n',
            'if (,a) {\n  x = 1\n}\n': 'if (a,) {\n  x = 1\n}\n',
            'const g = (,,a,,)\n': 'const g = (a,)\n',
            'mut x = (\n  ,ff = 1\n)\n': 'mut x = (ff=1,)\n',
            'x = f(x=(,(1, 2)))\n': 'x = f(x=((1, 2),))\n',
        }
        for mode in ('ai', 'human'):
            for source, expected in cases.items():
                with self.subTest(mode=mode, source=source):
                    self.assertEqual(self.fmt(source, mode), expected)

    def test_annotation_never_splits_for_an_if_chain(self):
        source = 'comb f(c:U1, x:U8, y:U8) -> (o:U8) {\n  mut r:Unsigned(bits=8) = if c { x } else { y }\n  o = r\n}\n'
        out = self.fmt(source, 'human', 30)
        self.assertIn('  mut r:Unsigned(bits=8) = if c {\n', out)

    def test_multiply_continuation_spacing_matches_across_modes(self):
        source = 'const x = b\n  // c\n  * c\n'
        for mode in ('ai', 'human'):
            self.assertEqual(self.fmt(source, mode), source)
        self.assertEqual(self.fmt('const x = aaaaaaaa*bbbbbbbbbb*cccccccc\n', 'human', 20),
                         'const x = aaaaaaaa\n  * bbbbbbbbbb\n  * cccccccc\n')

    def test_match_clause_separator_spaced_like_if(self):
        source = 'if const x = 1; x == 1 {\n  a = 1\n}\nmatch const y = 2; y {\n  == 2 { a = 2 }\n}\n'
        expected = 'if const x=1; x == 1 {\n  a = 1\n}\nmatch const y=2; y {\n  == 2 {\n    a = 2\n  }\n}\n'
        for mode in ('ai', 'human'):
            self.assertEqual(self.fmt(source, mode), expected)

    def test_comment_after_empty_branch_stays_with_it(self):
        cases = {
            'if en { } // nothing to do when enabled\nelse { o = b }\n':
                'if en { // nothing to do when enabled\n} else {\n  o = b\n}\n',
            'if en { o = a } elif q { } // elif note\nelse { o = b }\n':
                'if en {\n  o = a\n} elif q { // elif note\n} else {\n  o = b\n}\n',
        }
        for mode in ('ai', 'human'):
            for source, expected in cases.items():
                with self.subTest(mode=mode, source=source):
                    self.assertEqual(self.fmt(source, mode), expected)

    def test_match_arm_range_stays_whole(self):
        source = 'match x {\n  in 4..<6 { z = 2 }\n  == a + b { z = 3 }\n}\n'
        self.assertEqual(self.fmt(source, 'human'),
                         'match x {\n  in 4..<6 {\n    z = 2\n  }\n  == a + b {\n    z = 3\n  }\n}\n')

    def test_operator_stays_after_a_closing_block(self):
        cases = {
            'mut yy2 = {const x=3 ; 33/3} + 1\n': 'mut yy2 = {\n  const x = 3\n  33/3\n} + 1\n',
            'const z = {\n  a\n} + 100 // OK\n': 'const z = {\n  a\n} + 100 // OK\n',
        }
        for source, expected in cases.items():
            with self.subTest(source=source):
                self.assertEqual(self.fmt(source, 'human'), expected)

    # --- Round of 16 confirmed defects (backticks, comments, layout, speed) ---

    def test_else_prefixed_names_drop_their_backticks(self):
        # The scanner and prpparse match `else`/`elif` as whole words, so a
        # line starting `elsev`, `els =` or `elsewhere` starts a statement:
        # the backticks are redundant and drop, and the output still parses
        # (fmt verifies with -v) with the same statements.
        for name in ('elsev', 'elsewhere', 'elifz', 'els', 'eli', 'else_x', 'elif_ok', 'elseif', 'elsez'):
            for source in (f'mut a = 1\nmut `{name}` = a\n`{name}` = a ^ 1\n',
                           f'mut `{name}` = 0\nif a {{\n  x = 1\n}}\n`{name}` = 2\n',
                           f'x = a\n`{name}`.q = 2\n'):
                for mode in ('ai', 'human'):
                    with self.subTest(name=name, source=source, mode=mode):
                        out = self.fmt(source, mode)
                        self.assertNotIn('`', out)
                        self.assertEqual(re.sub(' +', ' ', out), re.sub(' +', ' ', source.replace('`', '')))
        # `else`/`elif` themselves are keywords and keep them.
        self.assertEqual(self.fmt('`plain` = 1\n`elm` = 2\n`else` = 3\n'), 'plain = 1\nelm = 2\n`else` = 3\n')

    def test_colon_and_hash_lines_continue(self):
        # A line starting with `:` (type annotation) or `#` (bit selector)
        # continues the previous statement in the scanner and prpparse.
        cases = {
            'mut acc\n:U8 = a\n': 'mut acc:U8 = a\n',
            'res = (b)\n#[0..=3]\n': 'res = (b)#[0..=3]\n',
            'r = b\n  #sext[0..=3]\n': 'r = b#sext[0..=3]\n',
        }
        for mode in ('ai', 'human'):
            for source, expected in cases.items():
                with self.subTest(mode=mode, source=source):
                    self.assertEqual(self.fmt(source, mode), expected)

    def test_line_comment_before_pipe_or_fluid_latency_slot(self):
        # `pipe // lat` newline `[1] f(...)` was joined into `pipe// lat[1] f(...)`,
        # commenting out the whole header.
        cases = {
            'pipe // lat\n[1] add1(a:U8) -> (r:U9) {\n  r = a + 1\n}\n':
                'pipe // lat\n  [1] add1(a:U8) -> (r:U9) {\n  r = a + 1\n}\n',
            'fluid // lat\n[lat=1..=2] f(a:U8) -> (r:U8) {\n  r = a\n}\n':
                'fluid // lat\n  [lat=1..=2] f(a:U8) -> (r:U8) {\n  r = a\n}\n',
            'pipe /* a */ // lat\n[1] add1(a:U8) -> (r:U9) {\n  r = a + 1\n}\n':
                'pipe /* a */ // lat\n  [1] add1(a:U8) -> (r:U9) {\n  r = a + 1\n}\n',
            'pipe /* lat */ [1] g(a:U8) -> (r:U8) {\n  r = a\n}\n':
                'pipe /* lat */ [1] g(a:U8) -> (r:U8) {\n  r = a\n}\n',
            'fluid /* x */ [lat=1..=2] f(a:U8) -> (r:U8) {\n  r = a\n}\n':
                'fluid /* x */ [lat=1..=2] f(a:U8) -> (r:U8) {\n  r = a\n}\n',
        }
        for mode in ('ai', 'human'):
            for source, expected in cases.items():
                with self.subTest(mode=mode, source=source):
                    self.assertEqual(self.fmt(source, mode), expected)

    def test_block_comments_never_glue_and_stay_idempotent(self):
        cases = {
            # A comment after a (leading) comma stays after it.
            'comb g(a:U4\n  , /* c */\n/* d */\nb:U4) -> (r:U4) {\n  r = a\n}\n':
                'comb g(a:U4, /* c */ /* d */ b:U4) -> (r:U4) {\n  r = a\n}\n',
            'x = f(a\n, /* c */\nb)\n': 'x = f(a, /* c */ b)\n',
            'x = f(a , /*c*/\n  , /* c */\nb)\n': 'x = f(a, /*c*/ /* c */ b)\n',
            'type R = (\n  const s:U10 = nil,\n , /*c*/ )\n': 'type R = (const s:U10=nil, /*c*/ )\n',
            # A statement after a block comment on its line stays there.
            'mod m() {\n  /* c */ x = 1\n}\n': 'mod m() {\n  /* c */ x = 1\n}\n',
            # A `(` line after a comment that starts its line is a statement.
            'mod m<W>() {\n  wire a:Unsigned\n  /* c */\n  (bits=W) = nil\n  wire c:Unsigned\n  /* c */ (bits=W) = nil\n}\n':
                'mod m<W>() {\n  wire a:Unsigned\n  /* c */\n  (bits=W) = nil\n  wire c:Unsigned\n  /* c */ (bits=W) = nil\n}\n',
            # Own-line comments before a closer trail the item.
            'const At:Signed(min=33\n/* c */\n) = nil\nconst Bt = 3\n': 'const At:Signed(min=33 /* c */ ) = nil\nconst Bt = 3\n',
        }
        for mode in ('ai', 'human'):
            for source, expected in cases.items():
                with self.subTest(mode=mode, source=source):
                    out = self.fmt(source, mode)
                    if mode == 'ai' or '\nconst Bt' not in source:
                        self.assertEqual(out, expected)
                    self.assertEqual(self.fmt(out, mode), out)
                    self.assertNotRegex(out, r'\S/\*|\*/[^\s]')
        # In Human mode an `if` whose condition holds a comment on its own
        # line breaks every branch, so a value `match` breaks all its arms
        # on the first pass as it does on the next.
        source = 'const y = match a { == 1 { 4 } else { if b\n/* c */\n== 0 { 1 } else { 2 } } }\n'
        out = self.fmt(source, 'human')
        self.assertIn('== 1 {\n    4\n  }', out)
        self.assertEqual(self.fmt(out, 'human'), out)

    def test_nested_block_comments_format(self):
        # Block comments nest (`/* a /* b */ c */` is one comment, as in lhd):
        # the whole comment, inner `*/` included, prints byte for byte.
        cases = {
            'comb f(a:U4, b:U4) -> (r:U4) {\n  r = b\n  /* old /* note */ if a == 0 // */\n  {\n    r = a\n  }\n}\n':
                'comb f(a:U4, b:U4) -> (r:U4) {\n  r = b\n  /* old /* note */ if a == 0 // */\n  {\n    r = a\n  }\n}\n',
            'x = a /* x /* y */ z */ + b\n': 'x = a /* x /* y */ z */ + b\n',
            'x = f(/*a/*b*/c*/y)\n': 'x = f( /*a/*b*/c*/ y)\n',
            'x = 1\n/* multi\n   /* line\n     nested */\n end */\ny = 2\n':
                'x = 1\n/* multi\n   /* line\n     nested */\n end */\ny = 2\n',
            # A `;` before a line that continues the statement stays, also
            # past a nested comment.
            'mut x:U8;\n/* a /* b */ c */\n-b\n': 'mut x:U8;\n/* a /* b */ c */\n-b\n',
            # `/*...*/` and a `/` right after the opener are single comments.
            'mod cpu(/*...*/) -> (/*...*/) {\n  a = 1 /*/ x */\n}\n':
                'mod cpu( /*...*/ ) -> ( /*...*/ ) {\n  a = 1 /*/ x */\n}\n',
        }
        for mode in ('ai', 'human'):
            for source, expected in cases.items():
                with self.subTest(mode=mode, source=source):
                    out = self.fmt(source, mode)
                    self.assertEqual(out, expected)
                    self.assertEqual(self.fmt(out, mode), out)

    def test_block_comment_opening_a_block_leads_its_first_item(self):
        # `{ /* d */ r = a }`: the comment documents the first item, so the
        # block breaks with it opening the body line (it used to stay on the
        # `{` line with the item, leaving `}` alone below).
        cases = {
            'if a > b { /* d */ r = a }\n': 'if a > b {\n  /* d */ r = a\n}\n',
            'if a > b { /* a /* n */ */ r = a }\n': 'if a > b {\n  /* a /* n */ */ r = a\n}\n',
            'if a > b { /* x */ /* y */ r = a }\n': 'if a > b {\n  /* x */ /* y */ r = a\n}\n',
            'r = match c {\n  == 1 { /* c14 */ b }\n  else { a }\n}\n':
                'r = match c {\n  == 1 {\n    /* c14 */ b\n  }\n  else {\n    a\n  }\n}\n',
            # A comment alone on the `{` line still trails it.
            'if a > b { /* d */\n  r = a\n}\n': 'if a > b { /* d */\n  r = a\n}\n',
        }
        for mode in ('ai', 'human'):
            for source, expected in cases.items():
                with self.subTest(mode=mode, source=source):
                    out = self.fmt(source, mode)
                    self.assertEqual(out, expected)
                    self.assertEqual(self.fmt(out, mode), out)

    def test_moved_header_comments_keep_source_order(self):
        # Comments before a block's `{` or between `}` and `else` move into
        # the block; they come first, then the block's own `{` comment, so the
        # source order A B C D E is kept (it used to print B A E C D).
        cases = {
            'o = if a == 1 /*A*/{ /*B*/1 } /*C*/else /*D*/{ /*E*/0 }\n':
                'o = if a == 1 { /*A*/\n  /*B*/ 1\n} else { /*C*/\n  /*D*/\n  /*E*/ 0\n}\n',
            # Before `elif`, a moved block comment would pass the condition's
            # own comments: it prints after `elif` instead.
            'if a {\n  y = 2\n} /*c1*/ elif /*c2*/ q /*c3*/ {\n  y = 1\n}\n':
                'if a {\n  y = 2\n} elif /*c1*/ /*c2*/ q { /*c3*/\n  y = 1\n}\n',
            'if a {\n  y = 2\n}\n/*c1*/ elif q == /*c2*/ 1 /*c3*/ { /*c4*/ y = 1 }\n':
                'if a {\n  y = 2\n} elif /*c1*/ q == /*c2*/ 1 { /*c3*/\n  /*c4*/ y = 1\n}\n',
            # With no comment in the condition it still moves into the block.
            'if a {\n  y = 2\n} /*c1*/ elif q {\n  y = 1\n}\n': 'if a {\n  y = 2\n} elif q { /*c1*/\n  y = 1\n}\n',
        }
        for mode in ('ai', 'human'):
            for source, expected in cases.items():
                with self.subTest(mode=mode, source=source):
                    out = self.fmt(source, mode)
                    self.assertEqual(out, expected)
                    self.assertEqual(self.fmt(out, mode), out)

    def test_human_moved_header_comment_is_idempotent(self):
        # A comment before `{` moved into the block, a comment leading the
        # body and one before `}`: every comment length formats the same on
        # the next pass (the `|` group used to split, then join, at 132).
        c45, d24 = '/*' + 'c' * 45 + '*/', '/*' + 'd' * 24 + '*/'
        for n in (1, 2, 5, 10, 17, 24, 30, 36, 45):
            a = '/*' + 'a' * n + '*/'
            for body in (f'if c == 1 {a}{{ /*b*/ (rq & bl) {c45} | (rh & ab) {d24} }} else {{ rh & bl }}',
                         f'if c == 1 {{ {a} (rq & bl) {c45} | (rh & ab) {d24} }} else {{ rh & bl }}',
                         f'if c == 1 {{ rq }} {a} else /*b*/ {{ /*c*/ (rq & bl) {c45} | (rh & ab) {d24} }}'):
                source = f'comb t(rq:U8, bl:U8, ab:U8, rh:U8, c:U1) -> (o:U8) {{\n  const ahead = {body}\n  o = ahead\n}}\n'
                for mode in ('ai', 'human'):
                    with self.subTest(n=n, mode=mode, body=body):
                        out = self.fmt(source, mode)
                        self.assertEqual(self.fmt(out, mode), out)

    def test_division_after_a_comment_line_continues(self):
        # A continuation line starting with `/` (after an own-line comment or
        # a nested block comment) is `a / b`; it used to fail to parse.
        for source in ('r = a\n  // why\n  / b\n', 'r = a\n  /* q /* n */ */ / b\n'):
            for mode in ('ai', 'human'):
                with self.subTest(mode=mode, source=source):
                    self.assertEqual(self.fmt(source, mode), source)
        self.assertEqual(self.fmt('r = a\n  / b\n'), 'r = a/b\n')

    def test_match_arm_range_condition_stays_whole(self):
        source = ('comb f(a_long_input_name:U4, b_long_input_name:U4, s:U2) -> (r:U4) {\n  r = match s {\n'
                  '    == 0 { a_long_input_name }\n'
                  '    in 1..=2 { hh<D=1, W=4>(x=b_long_input_name ^ a_long_input_name ^ b_long_input_name, '
                  'y=a_long_input_name ^ b_long_input_name ^ a_long_input_name ^ b_long_input_name) }\n'
                  '    else { a_long_input_name }\n  }\n}\n')
        expected = ('comb f(a_long_input_name:U4, b_long_input_name:U4, s:U2) -> (r:U4) {\n  r = match s {\n'
                    '    == 0 { a_long_input_name }\n    in 1..=2 {\n      hh<D=1, W=4>(\n'
                    '        x=b_long_input_name ^ a_long_input_name ^ b_long_input_name,\n'
                    '        y=a_long_input_name ^ b_long_input_name ^ a_long_input_name ^ b_long_input_name,\n'
                    '      )\n    }\n    else { a_long_input_name }\n  }\n}\n')
        self.assertEqual(self.fmt(source, 'human'), expected)
        # A source break after one arm's `{` breaks that arm only.
        self.assertEqual(self.fmt(expected, 'human'), expected)

    def test_block_comment_spacing_next_to_tight_tokens(self):
        cases = {
            'r = x / /* d */ (b + 1)\n': 'r = x / /* d */ (b + 1)\n',
            'r = x /* d */ / (b + 1)\n': 'r = x /* d */ / (b + 1)\n',
            'r = x * /* d */ y\n': 'r = x * /* d */ y\n',
            'r = x /* d */ * y\n': 'r = x /* d */ * y\n',
            'r = x % /* d */ b\n': 'r = x % /* d */ b\n',
            'r = a#[/* c */ 0..<2]\n': 'r = a#[ /* c */ 0..<2]\n',
            'r = a#[0..<2 /* c */]\n': 'r = a#[0..<2 /* c */ ]\n',
            'mut x = a /* c */;\nelsev = 1\n': 'mut x = a /* c */\nelsev = 1\n',
            'r = x/*d*/+/*e*/y\n': 'r = x /*d*/ + /*e*/ y\n',
            'comb f(a:U4/*g*/ =/*h*/ 3) -> (r) {\n  r = a\n}\n': 'comb f(a:U4 /*g*/ = /*h*/ 3) -> (r) {\n  r = a\n}\n',
            'mod m(a:Unsigned/*c*/(bits=4)) -> (b:U4) {\n  b = a\n}\n':
                'mod m(a:Unsigned /*c*/ (bits=4)) -> (b:U4) {\n  b = a\n}\n',
            'o = f(a,/*c*/b)\n': 'o = f(a, /*c*/ b)\n',
            'o = f<T=1/*c*/>(a)\n': 'o = f<T=1 /*c*/ >(a)\n',
            'r = x/y\n': 'r = x/y\n',
        }
        for mode in ('ai', 'human'):
            for source, expected in cases.items():
                with self.subTest(mode=mode, source=source):
                    self.assertEqual(self.fmt(source, mode), expected)

    def test_block_comments_in_lists_are_idempotent(self):
        cases = {
            # An own-line block comment before `)` trails the last item.
            'const t = f(\n  a,\n  b,\n  /* c */\n)\n': 'const t = f(a, b /* c */ )\n',
            'const t = f(a, b, /* c */)\n': 'const t = f(a, b /* c */ )\n',
            'const v = (x,\n  /* c */\n)\n': 'const v = (x, /* c */ )\n',
            # ... unless that item ends with a line comment.
            'const u = f(a, b // x\n  /* c */)\n': 'const u = f(\n  a,\n  b, // x\n  /* c */\n)\n',
            # A block comment after an item's line comment leads the next item.
            'const q = f(a // x\n  /* c */ , b)\n': 'const q = f(\n  a, // x\n  /* c */ b,\n)\n',
        }
        for mode in ('ai', 'human'):
            for source, expected in cases.items():
                with self.subTest(mode=mode, source=source):
                    self.assertEqual(self.fmt(source, mode), expected)

    def test_human_block_comment_in_overflowing_expressions_is_idempotent(self):
        # Both used to alternate forever between a split and a joined line.
        a98, a117 = 'a' * 98, 'a' * 117
        chain = self.fmt(f'const b = {a117} + x /* c */ + y\n', 'human')
        self.assertEqual(chain, f'const b = {a117}\n  + x /* c */\n  + y\n')
        # A chain that fits within the modest overflow keeps its branches whole.
        for rest in ('x /* c */ + y', 'xxxxxxxxxxx + y'):
            source = f'const b = if c {{ {a98} }} else {{ {rest} }}\n'
            with self.subTest(rest=rest):
                out = self.fmt(source, 'human')
                self.assertNotRegex(out, r'\n *\+ y }')

    def test_own_line_block_comment_keeps_its_line(self):
        # A block comment that starts its source line keeps starting a line on
        # the next pass too (it used to join the line before it), and a branch
        # that must break for it breaks the whole `if` chain.
        cases = [
            'tick\n/* c */\n N { stmts }\n',
            'const r = a\n  /* c */\n  + b\n',
            'comb\n/* c */\n get_five() -> (v) { v = 5 }\n',
            'total =\n/* c */\n 3\n',
            'const q = if c {\n  a#\n  /* c */ [0]\n} else { b }\n',
            'const r = if c { a\n  /* c */ + 1 } else { b }\n',
        ]
        for mode in ('ai', 'human'):
            for source in cases:
                with self.subTest(mode=mode, source=source):
                    out = self.fmt(source, mode)
                    self.assertRegex(out, r'\n *[/][*] c [*][/]')
        self.assertNotIn('\n  else', self.fmt(cases[-1], 'human'))
        self.assertEqual(self.fmt('const t = a +\n  /* c */ b\n'), 'const t = a + /* c */ b\n')
        for mode in ('ai', 'human'):
            self.assertEqual(self.fmt('x = f(a,\n/* c */ b)\n', mode), 'x = f(a, /* c */ b)\n')
        # A chain that takes the statement layout keeps its own breaks also
        # as a call argument (the next pass sees it in that layout).
        self.fmt('const w = h(y=if c == // c\n 1 { m } else { n })\n', 'human')

    def test_long_flat_lists_format_in_linear_time(self):
        # A per-item scan of the whole list made 10000 arguments take 23 s.
        n = 20000
        source = 'const a = f(' + ', '.join(f'a{i}' for i in range(n)) + ')\n'
        with tempfile.TemporaryDirectory() as wd:
            path = pathlib.Path(wd) / 'long.prp'
            path.write_text(source)
            start = time.monotonic()
            r = subprocess.run([str(FMT), str(path)], capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stderr)
            self.assertEqual(r.stdout, source)
            self.assertLess(time.monotonic() - start, 5)

    def test_human_deep_nesting_is_idempotent(self):
        # Fixed-size solver stacks flattened layouts nested deeper than they
        # could hold, so a second pass printed them differently.
        for depth in (34, 40, 65):
            with self.subTest(blocks=depth):
                self.fmt('const a = ' + '{ ' * depth + '1' + ' }' * depth + '\n', 'human')
        source = 'const a = ' + 'if c { ' * 345 + '1' + ' } else { 2 }' * 345 + '\n'
        self.fmt(source, 'human')

    def test_paren_line_after_comment_is_a_statement(self):
        # A line starting with `(` is a new statement also after a declaration
        # that a `//` comment ends (both parsers), so it aligns like any other.
        source = 'for p in 0..<2 {\n  const g = 1\n  mut a:U8 // c\n(r, a)    = (1, 2)\n  q = 1\n}\n'
        self.assertEqual(self.fmt(source, 'human'),
                         'for p in 0..<2 {\n  const g = 1\n  mut a:U8 // c\n  (r, a) = (1, 2)\n  q      = 1\n}\n')

    def test_named_destructuring_items_are_compact(self):
        for mode in ('ai', 'human'):
            with self.subTest(mode=mode):
                self.assertEqual(self.fmt('const (x = t.p1, y=t.p2) = f(a=1)\n', mode),
                                 'const (x=t.p1, y=t.p2) = f(a=1)\n')

    def test_blank_lines_collapse_and_trim(self):
        source = ('\n\n// lead\n\nconst a = 1\n\n\n\nconst b = 2\ncomb f(a) -> (r) {\n\n  r = a\n\n}\n'
                  'const x = f(\n\n  a,\n\n  b,\n)\n\n\n')
        expected = '// lead\n\nconst a = 1\n\nconst b = 2\ncomb f(a) -> (r) {\n  r = a\n}\nconst x = f(a, b)\n'
        for mode in ('ai', 'human'):
            with self.subTest(mode=mode):
                self.assertEqual(self.fmt(source, mode), expected)

    def test_semicolon_before_paren_line_drops(self):
        # A line starting with `(` or `[` is a new statement, also after a
        # declaration whose type could take a `(...)` constraint (lhd and the
        # grammar agree: `mut x:U8` newline `(r, x) = ...` is two statements),
        # so a `;` before it drops like any other.
        for mode in ('ai', 'human'):
            for decl in ('mut x:U8', 'reg x:U8', 'type T = U8', 'mut x:[2]U8', 'mut x:MyT'):
                with self.subTest(mode=mode, decl=decl):
                    src = f'comb top(a:U8, b:U8) -> (r:U8) {{\n  {decl};\n  (r, x) = (b, a)\n}}\n'
                    self.assertEqual(self.fmt(src, mode), src.replace(';', ''))
            self.assertEqual(self.fmt('mut x:U8; (r, x) = (b, a)\nmut y:U8; // c\n(r, y) = (b, a)\n', mode),
                             'mut x:U8\n(r, x) = (b, a)\nmut y:U8 // c\n(r, y) = (b, a)\n')
            # A `;` ending a body-less lambda stays before a `{` line: without
            # it the `{` is the lambda's body.
            src = 'comb ext(a) -> (b);\n{\n  const q = 1\n}\ncomb e2(a) -> (b) /* c */ ;\n{\n  const q = 1\n}\n'
            self.assertEqual(self.fmt(src, mode), src)
            self.assertEqual(self.fmt('mut x:U8;\nr = 1\n', mode), 'mut x:U8\nr = 1\n')
            # At the top level too, a declaration's own `;` splits the line.
            self.assertEqual(self.fmt('mut x:U8; r = 1\n', mode), 'mut x:U8\nr = 1\n')

    def test_block_comment_before_header_brace_does_not_count(self):
        # A block comment between a `for`/`if` header and its `{` moves after
        # the `{`, where it is a trailing comment: pass 1 must not count it
        # against the header's width either (it split the index list, and
        # pass 2 joined it back).
        f = ('comb top(first_input_value:U8, second_input_value:U8) -> (s:U8) {\n  mut acc:U8 = 0\n'
             '  for (index_value, item_value) in [first_input_value, second_input_value, first_input_value]'
             ' /* walk every input exactly once here */ {\n    acc = acc ^ item_value ^ index_value\n  }\n  s = acc\n}\n')
        out = self.fmt(f, 'human')
        self.assertIn('  for (index_value, item_value) in [first_input_value, second_input_value, first_input_value]'
                      ' { /* walk every input exactly once here */\n', out)
        i = ('comb top(first_input_value:U8, second_input_value:U8) -> (s:U8) {\n  s = 0\n'
             '  if first_input_value == second_input_value and second_input_value != 3 and first_input_value != 4'
             ' /* zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz */ {\n    s = first_input_value\n  }\n}\n')
        self.assertIn('first_input_value != 4 { /* zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz */\n', self.fmt(i, 'human'))

    def test_comment_split_left_side_takes_no_padding(self):
        # A comment inside a declaration's left-hand side keeps its line
        # break, so the statement breaks before its `=`: it joins no alignment
        # group (the pad used to be its left side joined into one line). The
        # type continues on a line starting with `:` (a line starting with `(`
        # is a new statement, so a type's constraint never starts one).
        for source in ('mod m<W>() {\n  reg v = 0\n  reg d // c\n:Unsigned(bits=W) = 0\n}\n',
                       'mod m<W>() {\n  wire a\n  /* c */\n  :Unsigned(bits=W) = nil\n  wire b:Bool = nil\n}\n',
                       'mut v = 1\nmut d // c\n:Unsigned(bits=8) = 0\n'):
            with self.subTest(source=source):
                out = self.fmt(source, 'human')
                self.assertNotRegex(out, r'  = ')
        self.assertEqual(self.fmt('mut v = 1\nmut d // c\n:Unsigned(bits=8) = 0\n', 'human'),
                         'mut v = 1\nmut d // c\n  :Unsigned(bits=8) = 0\n')
        # A multi-line block comment keeps its break wherever it sits; one
        # that starts its line after `[`, `(`, a comma or an operator joins
        # that line, so the statement still aligns (on every pass).
        for lhs in ('reg a:U8:[async=false, /* c\n d */ posclk=true]', 'reg a:U8:[\n  /* c */ async=false]',
                    'reg a:Unsigned(\n  /* c */ bits=W)', 'reg a:U8:[async=false,\n  /* c */ posclk=true]'):
            with self.subTest(lhs=lhs):
                self.fmt(f'mod m<W>() {{\n  reg v = 0\n  {lhs} = 0\n  reg w = 0\n}}\n', 'human')
        # An inline block comment breaks nothing: the group still aligns.
        self.assertEqual(self.fmt('mut v = 1\nmut dd /* c */ = 0\n', 'human'),
                         'mut v          = 1\nmut dd /* c */ = 0\n')

    def test_nested_lambda_block_comment_keeps_brace_on_header(self):
        # A header that a block comment trails keeps its body after a blank
        # line, and a `{` starting the next line is the body too (Allman
        # style): the `{` ends the header line.
        expected = 'comb top() {\n  x = 1\n\n  comb b(a:U8) -> (r:U8) /* c */ {\n    r = a\n  }\n}\n'
        for mode in ('ai', 'human'):
            with self.subTest(mode=mode):
                self.assertEqual(self.fmt(expected, mode), expected)
                self.assertEqual(self.fmt(expected.replace('/* c */ {', '/* c */\n  {'), mode), expected)

    def test_whole_array_literal_value_drops_grouping(self):
        # Parentheses without a comma only group: a whole right-hand side or
        # named-argument value that is an array literal loses them.
        for mode in ('ai', 'human'):
            with self.subTest(mode=mode):
                self.assertEqual(self.fmt('const a = ([x, y])\nconst c = ((1, 2))\nx = f(x=(([1, 2])))\n', mode),
                                 'const a = [x, y]\nconst c = (1, 2)\nx = f(x=[1, 2])\n')
                # A positional argument keeps one layer, as for any value.
                self.assertEqual(self.fmt('y = f(([1, 2]))\n', mode), 'y = f(([1, 2]))\n')

    def test_short_destructuring_list_lets_the_right_side_split(self):
        # The destructuring or `for` index list is not the long part: the
        # right-hand side breaks at its commas, the short list stays inline.
        a1, a2 = 'a' * 60 + '1', 'a' * 60 + '2'
        self.assertEqual(self.fmt(f'const (x1, y1) = ({a1}, {a2})\n', 'human'),
                         f'const (x1, y1) = (\n  {a1},\n  {a2},\n)\n')
        self.assertEqual(self.fmt(f'(x3, y3) = f({a1}, {a2})\n', 'human'),
                         f'(x3, y3) = f(\n  {a1},\n  {a2},\n)\n')
        self.assertIn('  for (i, j) in zip(\n',
                      self.fmt(f'comb g() {{\n  for (i, j) in zip({a1}, {a2}) {{\n    x = i\n  }}\n}}\n', 'human'))
        # A list that is itself too long still goes one item per line.
        self.assertEqual(self.fmt(f'const ({a1}, {a2}, {a1}3) = t\n', 'human'),
                         f'const (\n  {a1},\n  {a2},\n  {a1}3,\n) = t\n')

    def test_deep_grouping_parentheses_format_in_linear_time(self):
        # A comment scan per parenthesis layer and a group-end scan per
        # nested list item made 8000 levels take 3 to 8 s.
        n = 8000
        for source in ('x = ' + '(' * n + '1' + ')' * n + '\n',
                       'x = ' + '(a + ' * n + 'a' + ')' * n + '\n',
                       'x = ' + 'not (' * n + 'a == b' + ')' * n + '\n'):
            with self.subTest(source=source[:12]), tempfile.TemporaryDirectory() as wd:
                path = pathlib.Path(wd) / 'deep.prp'
                path.write_text(source)
                start = time.monotonic()
                r = subprocess.run([str(FMT), str(path)], capture_output=True, text=True)
                self.assertEqual(r.returncode, 0, r.stderr)
                self.assertLess(time.monotonic() - start, 2)

    def test_empty_input_prints_nothing(self):
        # Blank lines at the start and the end of the file are removed, so a
        # file of only blank lines (or none) formats to an empty file.
        for source in ('', '\n', '\n\n', '  \n\t\n'):
            for mode in ('ai', 'human'):
                with self.subTest(source=source, mode=mode):
                    self.assertEqual(self.fmt(source, mode), '')


class DocsTest(unittest.TestCase):
    """The docs describe the tree as it is (stale paths and files were reported)."""

    def test_tests_readme_lists_every_test_file(self):
        tests = pathlib.Path(__file__).resolve().parent
        readme = (tests / 'README.md').read_text()
        for f in tests.iterdir():
            if f.suffix in ('.py', '.cc'):
                self.assertIn(f'`{f.name}`', readme, f'{f.name} is not described in tests/README.md')

    def test_readme_examples_match_the_formatter(self):
        # The generic/attribute line-comment examples show the one-item-per-
        # line result, and the blank-line rules are documented.
        readme = (pathlib.Path(__file__).resolve().parents[1] / 'README.md').read_text()
        self.assertIn('(`g<` newline `W // c` newline `=1` newline `>(a)`', readme)
        self.assertNotIn('`g<W // c` newline `=1>`', readme)
        self.assertIn('Blank lines: a run of blank lines', readme)
        # The block-comment line rule documents the joins the tests pin
        # (`const t = a + /* c */ b`, `f(a, /* c */ b)`).
        self.assertIn('Two places are the exception', readme)
        self.assertIn('`x = f(a,` newline `/* c */ b)` prints `x = f(a, /* c */ b)`', readme)

    def test_readme_fallback_runtime_matches_makefile(self):
        root = pathlib.Path(__file__).resolve().parents[1]
        makefile, readme = (root / 'Makefile').read_text(), (root / 'README.md').read_text()
        self.assertIn('TS_LIBS   := $(TS_DIR)/libtree-sitter.a', makefile)
        self.assertIn('`$(TS_DIR)/libtree-sitter.a`', readme)
        # The library sits in the checkout's root, not under its lib/.
        self.assertNotIn('│       └── libtree-sitter.a', readme)
    def test_scripts_find_the_formatter_from_any_cwd(self):
        # From prpfmt/ the old default `-b ../prpfmt` named the prpfmt
        # directory itself, so every file failed with "Permission denied".
        root = pathlib.Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory() as wd:
            (pathlib.Path(wd) / 'c1.prp').write_text('const a = 1\n')
            for cwd in (root, root / 'tests', root.parent):
                with self.subTest(cwd=cwd):
                    r = subprocess.run(['python3', str(root / 'tests' / 'verify_all.py'), wd],
                                       cwd=cwd, capture_output=True, text=True)
                    self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
                    self.assertIn('Passed:               1', r.stdout)
        # The benchmark and evaluation scripts read the docs corpus and call
        # the formatter by paths relative to themselves, not to the cwd.
        for name in ('run_benchmarks.py', 'generate_eval.py'):
            text = (root / 'tests' / name).read_text()
            self.assertNotIn('docs/tmp', text)
            self.assertNotIn('"../../prpfmt"', text)
            self.assertIn('full_pyrope', text)

if __name__ == '__main__':
    unittest.main()
