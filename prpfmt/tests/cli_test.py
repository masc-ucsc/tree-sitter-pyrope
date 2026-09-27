#!/usr/bin/env python3
"""Standalone formatter CLI behavior, including preservation on failure."""
import pathlib
import subprocess
import tempfile
import unittest

FMT = pathlib.Path(__file__).resolve().parents[1] / 'prpfmt'

class CliTest(unittest.TestCase):
    def test_cli(self):
        with tempfile.TemporaryDirectory() as wd:
            root = pathlib.Path(wd)
            source = root / 'input.prp'
            original = 'mod foo(a:u8,b:u8)->(x:u8@[0],y:u8@[0]){\nx=a+b\ny=a-b\n}\n'
            source.write_text(original)
            def run(*args, ok=True):
                result = subprocess.run([str(FMT), *map(str, args)], cwd=wd, capture_output=True, text=True)
                self.assertEqual(result.returncode == 0, ok, result.stderr)
                return result
            expected = run(source).stdout
            self.assertIn('\n  x = a + b', expected)
            self.assertEqual(run('--indent', 2, '--width', 132, source).stdout, expected)
            self.assertEqual(run(source, '--indent=2', '--width=132').stdout, expected)
            self.assertEqual(run('--indent', 4, source).stdout, run(source, '--indent', 4).stdout)
            self.assertIn('\n    x = a + b', run('--indent', 4, source).stdout)
            # A long signature fits at the new default but wraps at 80.
            wide = root / 'wide.prp'
            wide.write_text('mod foo(first_input:u8,second_input:u8,third_input:u8,fourth_input:u8)->(result:u8@[0]) {\nresult=first_input\n}\n')
            self.assertEqual(run(wide).stdout, run('--width', 132, wide).stdout)
            self.assertNotEqual(run(wide).stdout, run('--width', 80, wide).stdout)
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
                         ('--indent',), ('--width=',), ('--unknown',)]:
                run(source, *args, ok=False)
            run('-i', '-o', output, source, ok=False)
            run(ok=False)
            run('--indent', 2, '--help')
            malformed = 'mod broken(a:u8 ->\n'
            source.write_text(malformed)
            run('-i', source, ok=False)
            self.assertEqual(source.read_text(), malformed)
            run(source, '-o', output, ok=False)
            self.assertEqual(output.read_text(), expected)

    def test_declaration_keywords_survive(self):
        # `pub` and `wire` are grammar-ALIASED, which makes their nodes NAMED --
        # they used to fall through the printers' unnamed-only default arm and
        # vanish with exit 0, silently changing a module's visibility. Every one of
        # the 325 pub-declaring files in livehd's Pyrope corpus was affected, and
        # `-i` wrote the loss straight back over the source.
        with tempfile.TemporaryDirectory() as wd:
            source = pathlib.Path(wd) / 'keep.prp'
            source.write_text('pub comb f(a:u4) -> (r:u8) {\n  wire w:u8\n  w = a\n  r = w + 1\n}\n')
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
            source.write_text('pub comb f(a:u4) -> (r:u8) {\n  r = a\n}\n')
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
            r = subprocess.run([str(FMT), str(src), *args], capture_output=True, text=True)
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
            expected = source.replace(' elif', '\n  elif').replace(' else', '\n  else') + '\n'
            self.assertEqual(self.fmt(source, '--width', str(len(source) - 1), '-v'), expected)

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
        self.assertIn('\n  elif b { 2 }\n  else { 3 }\n', out)
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
        self.assertEqual(out, source)
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
        out = self.fmt('pub comb f(a:u4) -> (r:u8) {\n  wire w:u8\n  w = a\n  r = w + 1\n}\n', '-v')
        self.assertRegex(out, r'\bpub\s+comb\s+f\b')   # dropping `pub` changes visibility
        self.assertRegex(out, r'\bwire\s+w\b')

    def test_fluid_declaration_survives(self):
        out = self.fmt('mod m() -> () {\n  fluid mut req:u8\n  req = 1\n}\n', '-v')
        self.assertRegex(out, r'\bfluid\s+mut\s+req\b')  # a fluid handshake is a different interface

    def test_enum_type_annotation_survives(self):
        # Per the Pyrope rule an integer-typed enum switches OFF one-hot numbering,
        # so `enum V:signed` and `enum V` encode their variants differently.
        out = self.fmt('enum V6:signed = (a, b, c)\n', '-v')
        self.assertIn('enum V6:signed', out)

    def test_ref_argument_keeps_its_space(self):
        out = self.fmt('mod m(a:u8) -> (y:u8) {\n  y = g(clock_pin = ref clk, din = a)\n}\n', '-v')
        self.assertRegex(out, r'ref\s+clk')      # `refclk` is a DIFFERENT identifier
        self.assertNotIn('refclk', out)

    def test_step_keeps_its_space(self):
        out = self.fmt('mod m() -> () {\n  step 3\n}\n', '-v')
        self.assertRegex(out, r'step\s+3')       # `step3` is a DIFFERENT identifier
        self.assertNotIn('step3', out)

    def test_call_site_instance_attribute_parses(self):
        out = self.fmt('mod m(a:u8) -> (y:u8) {\n  mut u = g::[name=ux](a = a)\n  y = u\n}\n', '-v')
        self.assertIn('::[', out)
        self.assertIn('name', out)

    def test_declaration_attribute_still_parses(self):
        # Must keep working alongside the call form above: both spell `::[`, and
        # they compete at the LEXER, so a fix for one can silently break the other.
        out = self.fmt('const a::[note = 1] = 7\ncassert(a.[note] == 1)\n', '-v')
        self.assertIn('::[', out)

    def test_leading_word_operator_continuation(self):
        # `false \n or true` must stay ONE expression. Terminating the statement at
        # the newline kept only the first term.
        for op, rhs in (('or', 'true'), ('and', 'true'), ('implies', 'true'),
                        ('equals', '5'), ('does', 'u4'), ('in', '(1, 2, 3)')):
            lhs = '5' if op in ('equals',) else ('u8' if op == 'does' else ('5' if op == 'in' else 'false'))
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
        # `_0`/`_1` are the field names Pyrope gives an anonymous tuple.
        out = self.fmt('mod m() -> (y:u1) {\n  wire g:(_0:u1, _1:u1) = nil\n  y = g._0\n}\n', '-v')
        self.assertIn('_0', out)
        self.assertIn('_1', out)

    def test_line_comment_never_swallows_the_next_item(self):
        # Nothing may follow a `//` comment on the same output line. This needed TWO
        # passes to show: pass 1 moved a trailing comment onto its own line, pass 2
        # glued the next item back onto it -- commenting that code OUT, at exit 0,
        # invisibly to --verify (the result still parses, with fewer statements).
        src = ('const C = enum(\n  A = 1,\n  B = 2,   // keep me\n  D = 3\n)\n')
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


if __name__ == '__main__':
    unittest.main()
