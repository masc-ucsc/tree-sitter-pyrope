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
            self.assertEqual(run('--mode=ai', source).stdout, expected)
            self.assertEqual(run('--indent', 2, '--width', 132, source).stdout, expected)
            self.assertEqual(run(source, '--indent=2', '--width=132').stdout, expected)
            self.assertEqual(run('--indent', 4, source).stdout, run(source, '--indent', 4).stdout)
            self.assertIn('\n    x = a + b', run('--indent', 4, source).stdout)
            # A long signature fits at the new default but wraps at 80.
            wide = root / 'wide.prp'
            wide.write_text('mod foo(first_input:u8,second_input:u8,third_input:u8,fourth_input:u8)->(result:u8@[0]) {\nresult=first_input\n}\n')
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

    def test_generic_postfix_argument_round_trips(self):
        # Ruling 2026-09-27: a generic argument / default may be a bare postfix
        # attribute read. The formatter must keep `<N=x.[bits]>` glued (a space
        # before `>` or around `.` still parses, but the output must be stable),
        # at any width, and must still refuse the unparenthesized operator form.
        source = ('comptime const cfg = (const w = 5)\n'
                  'comb addn<N=Z.[bits]>(x:u8) -> (y:u8) { y = x }\n'
                  'mod low<N=cfg.w.[max], T=u8>(x:u8) -> (y:u8) { y = x }\n'
                  'type Row<N=Z.[bits]> = unsigned(bits=N)\n'
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
                    compact = ''.join(out.split())
                    for frag in ('addn<N=Z.[bits]>(', 'low<N=cfg.w.[max], T=u8>(', 'Row<N=Z.[bits]>',
                                 'low<N=a.[bits]>(x=b)', 'addn<N=cfg.w.[max]>(x=b)', 'addn<a.[bits]>(x=b)',
                                 'addn<N=b.[bits].[max]>(x=b)'):
                        self.assertIn(''.join(frag.split()), compact)
                    self.assertEqual(self.fmt(out, '--mode', mode, '--width', width, '-v'), out)
        self.assertIn('f<N=x.[bits]>(a)', self.fmt('r = f<N=x . [bits] >(a)\n', '-v'))
        with tempfile.TemporaryDirectory() as wd:
            for bad in ('r = f<N=W*2>(x=b)\n', 'comb f<N=Z.[bits]+1>(x:u8) -> (y:u8) { y = x }\n'):
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
        source = 'comb f(a:u8) -> (out:u8) { if a == 1 { out = 2 } elif a == 3 { out = 4 } else { out = 0 } }\n'
        expected = ('comb f(a:u8) -> (out:u8) {\n'
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
        source = 'const x = a\nconst much_longer = b\ntype A = u8\ntype VeryLong = u16\n'
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
        source = ('comb f(first:u16, // first port\n second:u16) -> (out:u16 // output port\n) {\n'
                  'out = g(z=first, // z input\n a=second)\n}\n')
        for mode in ('ai', 'human'):
            out = self.fmt(source, mode)
            for expected in ('first:u16, // first port', 'out:u16 // output port', 'z=first, // z input'):
                self.assertIn(expected, out)
            self.assertIn(') -> (\n', out)

    def test_header_explodes_inputs_first(self):
        source = 'comb f(' + ', '.join(f'input_{i}_with_a_long_name:u16' for i in range(5)) + ') -> (out:u16) { out = 0 }\n'
        out = self.fmt(source, 'human')
        self.assertTrue(out.startswith('comb f(\n'))
        self.assertIn(') -> (out:u16) {', out)
        self.assertTrue(all(len(line) <= 132 for line in out.splitlines()))

    def test_generic_lists_spacing_and_wrapping(self):
        source = 'const out = f<Z=(WIDTH*(N - 1)), A=cfg.width>(z=3, a=2)\n'
        self.assertIn('<Z=(WIDTH*(N - 1)), A=cfg.width>', self.fmt(source))
        source = 'const out = f<' + ', '.join(f'Param{i}=very_long_generic_value_{i}' for i in range(5)) + '>(x=1)\n'
        out = self.fmt(source, 'human')
        self.assertTrue(all(len(line) <= 132 for line in out.splitlines()), out)
        self.assertIn('f<\n', out)

    def test_consistent_binding_spacing(self):
        source = ('reg x:u8:[reset_pin = ref rst, async = true] = 0\n'
                  'const t = (aw = (addr = 2, id = 3), bits = N)\n'
                  'const out = f::[name = inst](rst = rst, bits = N)\n')
        for mode in ('ai', 'human'):
            out = self.fmt(source, mode)
            self.assertIn('[reset_pin=ref rst, async=true]', out)
            self.assertIn('aw=(addr=2, id=3)', out)
            self.assertIn('f::[name=inst](bits=N, rst)', out)

    def test_named_call_and_tuple_sorting(self):
        for mode in ('ai', 'human'):
            self.assertEqual(self.fmt('const x = f(z=z, a=1, m=2)\n', mode), 'const x = f(a=1, m=2, z)\n')
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
        source = 'comb f(fixed, ...rest) -> () {}\nf(z=z, fixed=fixed)\n'
        out = self.fmt(source)
        self.assertIn('f(fixed, z=z)', out)

    def test_alignment_only_consecutive_same_kind(self):
        source = ('const a = 1\nconst much_longer = 2\ncputs("barrier")\nconst b = 3\n'
                  'wrap value = 1\nconst c = 4\nmut x = 1\nmut longer = 2\n'
                  'type A = u8\ntype Long = u16\n')
        out = self.fmt(source, 'human')
        self.assertIn('const a           = 1\nconst much_longer = 2', out)
        self.assertIn('const b = 3\nwrap value = 1\nconst c = 4', out)
        self.assertIn('mut x      = 1\nmut longer = 2', out)
        self.assertIn('type A    = u8\ntype Long = u16', out)
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
                self.assertEqual(self.fmt(source, 'human'), source)

    def test_alignment_kind_ignores_source_whitespace(self):
        out = self.fmt('comptime const a = 1\ncomptime   const longer = 2\n', 'human')
        self.assertEqual(out, 'comptime const a      = 1\ncomptime const longer = 2\n')

    def test_header_width_includes_opening_brace(self):
        source = 'comb f(' + 'a' * 100 + ':u8) -> (out:u8) { out = 1 }\n'
        for width in (120, 122, 123, 124, 125, 126, 127, 128, 132):
            out = self.fmt(source, 'human', width)
            self.assertTrue(all(len(line) <= width for line in out.splitlines()), out)

    def test_comparisons_tighten_under_logical_operators(self):
        # Precedence spacing, like `i*32 + 1`: a comparison that is an operand of
        # a looser and/or/implies drops the spaces around its comparator.
        tightened = {
            'const d = foo != bar or bar == foo\n': 'const d = foo!=bar or bar==foo\n',
            'if foo != bar or bar == foo {\n  x = 1\n}\n': 'if foo!=bar or bar==foo {\n  x = 1\n}\n',
            'const c = a < b and b < c\n': 'const c = a<b and b<c\n',
            'const i = a >= 1 implies b <= 2\n': 'const i = a>=1 implies b<=2\n',
            'const m = i * 32 == x and y#[0..<3] == 0b101\n': 'const m = i*32==x and y#[0..<3]==0b101\n',
            'const k = f(a) == t.[bits] or (p, q) != r[1]\n': 'const k = f(a)==t.[bits] or (p, q)!=r[1]\n',
            'const g = n == 0 or (n > 0 and (z == 0))\n': 'const g = n==0 or (n>0 and (z==0))\n',
            'const n = a == (b or c == d)\n': 'const n = a == (b or c==d)\n',
            'cassert(x == 1 and y != 2)\n': 'cassert(x==1 and y!=2)\n',
            # A negative literal is one token: `x==-1` regroups nothing.
            'const q = x == -1 and y > -2\n': 'const q = x==-1 and y>-2\n',
            # Word comparators keep their spaces without holding back the rest.
            'const w = a in b or c == d\n': 'const w = a in b or c==d\n',
            'const o = got.cfg has \'bar\' and got.cfg.bar == 3\n': "const o = got.cfg has 'bar' and got.cfg.bar==3\n",
            # Through a logical `not`/`!` of a parenthesized comparison.
            'const l = a <= b and not (b <= a) or !(c == d)\n': 'const l = a<=b and not (b<=a) or !(c==d)\n',
        }
        kept = (
            # A comparison on its own is not under a looser operator.
            'const s = a == b\n',
            'if a == b {\n  x = 1\n}\n',
            'cassert(a == b)\n',
            'const f = f(x == y) or z\n',
            # The chain decides once: one comparison that cannot be tight (spaced
            # operand, unary sign, `<-` arrow look, generic call next to `<`/`>`,
            # comment in its parens) keeps every one spaced.
            'const p = a + 1 == b or c == d\n',
            'const q = x < -1 and y == 0\n',
            'const r = a < -b or c\n',
            'const g = f<N=3>(x) < y or c == d\n',
            'const h = x == 1 or (a == b /* c */ )\n',
            'const u = a has b and c in d\n',
            'const v = not a == b or c\n',
            # `(c == d,)` is a one-element tuple, not a grouping.
            'const t = x or (c == d,)\n',
            'const z = not (c == d,) or e\n',
        )
        for mode in ('ai', 'human'):
            for source, expected in tightened.items():
                with self.subTest(mode=mode, source=source):
                    self.assertEqual(self.fmt(source, mode), expected)
            for source in kept:
                with self.subTest(mode=mode, source=source):
                    self.assertEqual(self.fmt(source, mode), source)
            # A tight standalone comparison is re-spaced; the spelling depends
            # only on the tree, never on the source spacing.
            self.assertEqual(self.fmt('const s = a==b\nconst d = a == b or c\n', mode),
                             'const s = a == b\nconst d = a==b or c\n')

    def test_tight_comparisons_wrap_at_the_logical_operator(self):
        source = ('const y = if instr.first_long_condition == 1 or instr.second_long_condition != 2 '
                  'or instr.slli { 1 } else { 2 }\n')
        out = self.fmt(source, 'human', 40)
        self.assertIn('instr.first_long_condition==1\n', out)
        self.assertIn('\n  or instr.second_long_condition!=2\n', out)

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
            'const g = (0)\n',
            'const s = (a, b)\n',
        )
        for mode in ('ai', 'human'):
            for source in kept:
                with self.subTest(mode=mode, source=source):
                    self.assertEqual(self.fmt(source, mode), source)
            self.assertEqual(self.fmt('mut d = (,,1,,)\n', mode), 'mut d = (1,)\n')
            self.assertEqual(self.fmt('const s = (a, b,)\n', mode), 'const s = (a, b)\n')
            self.assertEqual(self.fmt('const e = (0, // c\n)\n', mode), 'const e = (\n  0, // c\n)\n')
            # Comments before a stray leading comma lead the item; they are no
            # empty slot that would steal the one-element tuple's comma.
            self.assertEqual(self.fmt('const x = (\n  // c\n  , 1\n  ,\n)\n', mode), 'const x = (\n  // c\n  1,\n)\n')
            self.assertTrue(self.fmt('const x = (/* c */, 1,)\n', mode).endswith(' 1,)\n'))
            # A destructuring one-element lvalue keeps its comma, with no stray space.
            self.assertEqual(self.fmt('(a,) = f()\n', mode), '(a,) = f()\n')


if __name__ == '__main__':
    unittest.main()
