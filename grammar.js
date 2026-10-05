'use strict';

// Helper Functions
function optseq() {
  return optional(seq.apply(null, arguments));
}

function repseq() {
  return repeat(seq.apply(null, arguments));
}

function listseq1() {
  const item = seq.apply(null, arguments);
  return seq(
    repeat(',')
    , item
    , repeat(seq(repeat1(','), item))
    , repeat(',')
  )
}

function statementInit($) {
  return optional($._init_clause);
}

function forBinding($) {
  return choice(
    seq('('
      , field('index', $.typed_identifier_list)
      , ')'
    )
    , alias($._binding_typed_identifier, $.typed_identifier)
  );
}

// Write-side attribute lists (`::[…]`, `:Type:[…]`). Modeled with a
// dedicated `attribute_sq` rule rather than reusing `tuple_sq` so that
// reserved keywords (`comptime`, ...) can appear as plain attribute names.
// `tuple_sq` admits `var_or_let_or_reg`-prefixed items, which keeps
// `comptime` in lookahead and makes the lexer pick the keyword token even
// when the parser would otherwise accept it as an identifier.
function attributeSuffix($) {
  return seq(':', $.attribute_sq);
}

// The attribute-only annotation `::[...]`: `::` is ONE token (spec 2026-09-29
// §8), so `x: :[a]`, `x:/* c */:[a]` and `x:` newline `:[a]` are syntax errors
// (prpparse lexes a single `coloncolon` too).
function attributeOnly($) {
  return seq('::', $.attribute_sq);
}

// Overparse: `::[...]` and `:Type:[...]` parse anywhere `type_cast` is
// admitted, including non-declaration contexts. See grammar_overparse.md #1.
function typedOrAttributed($) {
  return choice(
    seq(
      ':'
      , field('type', $._type)
      , optional($._timing_sequence)   // `out:U32@[2]` — cycle check on a typed declaration
      , field('attribute', optional(attributeSuffix($)))
    )
    , field('attribute', attributeOnly($))
  );
}

// `f(args)` and `f::[name=u_inst](args)`. The attribute block between the callee
// and the argument tuple is how a call names its instance (`::[name=…]`), and it
// is the dominant shape in the sim/loop/hierarchy fixtures. Without it the parser
// consumed `f::[name=u]` and then wanted a binary operator before `(`, reporting
// MISSING op_mul -- 19 of the corpus's 22 valid-but-unparseable files.
function tupleCall($, precedence, argRule, callee = $._complex_identifier) {
  return prec(precedence, seq(
    field('function', callee)
    , field('argument', argRule)
  ));
}

// Call-site explicit generic binding in EXPRESSION position
// (`f<Signed,String>(args)`). The `<` must be glued to the callee (owner
// ruling 107): a `<` after a blank is always a comparison (the scanner's
// zero-width `_spaced_lt` before it, see `_binary_compare`), so `a < b > (c)`
// is a comparison chain. A glued `ident<…>(…)` is still ambiguous with a
// chained comparison, and static precedence can resolve it neither way
// (committing to the call breaks plain `a<b`; committing to compare makes the
// generic form unreachable). So this variant carries its own incomparable
// precedence level ('generic_call') to force a GLR fork, and dynamic
// precedence picks the call parse when both survive. That never steals a
// legal program: `f<T>(x)` as a comparison is mixed-direction, which Pyrope
// already rejects semantically (grammar_overparse.md #6).
function genericTupleCall($, argRule) {
  return prec.dynamic(1, prec('generic_call', seq(
    field('function', $._complex_identifier)
    , '<'
    , field('generic', $.generic_type_list)
    , '>'
    , field('argument', argRule)
  )));
}

// The escapes of a double-quoted string and of a backticked identifier (spec
// 2026-09-29 §8, Python/Rust style): exactly `\n \t \r \\ \" \' \0`, `\xNN`
// (two hex digits, at most 7F: owner ruling 95), `\u{N}` (1 to 6 hex digits
// naming a Unicode scalar value: at most 10FFFF, no surrogate D800-DFFF,
// ruling 96) and `` \` ``. Every other backslash sequence (`\{`, `\}`, `\q`,
// `\u0041` without braces, `\u{}`, `\u{110000}`, `\u{D800}`, `\x80`) is a syntax
// error; a literal brace in a string is `{{` / `}}`. `\u{...}` is one escape,
// never an interpolation hole. prpparse accepts the same set (lexer.cpp
// escape_end). UNICODE_BMP4 is a 4-digit code point that is no surrogate.
const UNICODE_BMP4 = '([0-9a-cA-CeEfF][0-9a-fA-F]{3}|[dD][0-7][0-9a-fA-F]{2})';
const STRING_ESCAPE = new RegExp('\\\\([ntr\\\\"\'0`]|x[0-7][0-9a-fA-F]|u\\{([0-9a-fA-F]{1,3}|0{0,2}' + UNICODE_BMP4 +
  '|0?[1-9a-fA-F][0-9a-fA-F]{4}|10[0-9a-fA-F]{4})\\})');

// The built-in type words spelled as whole words (the sized `U<N>`/`S<N>` are
// kept out of `identifier` by its regex).
const TYPE_WORDS = ['Unsigned', 'Signed', 'Bool', 'String', 'Clock', 'Reset'];

// The OLD lowercase type spellings (`u8`, `s20`, `i32`, `bool`, `boolean`,
// `unsigned`, `signed`, `string`) are NOT reserved (owner ruling 2026-09-30,
// reversing spec 2026-09-29 §7): they are ordinary identifiers, usable as a
// variable, port, parameter, field or lambda name with no backticks. Only the
// exact new type words (`U<N>`, `S<N>`, `Unsigned`, `Signed`, `Bool`, `String`,
// `Clock`, `Reset`) are reserved. The "`u8` was renamed `U8`" diagnostic is
// lhd's (a lowercase word used as a type or cast callee that is not a declared
// name); neither parser rejects them.

// Every Pyrope keyword is a RESERVED word (02-basics.md "Identifiers"): it is
// never a name being BOUND -- a declaration, a destructuring declaration, a
// parameter or output, a generic parameter, a loop induction variable, an
// init-clause declaration. Such a name must be backticked (`` `in` ``). Keep this
// list in sync with src/scanner.c (reserved_words, the field-name rule) and
// prpparse/prp_keywords.def. `tick` and `step` are reserved everywhere
// (ALWAYS_RESERVED).
const KEYWORDS = [
  'import', 'as', 'break', 'continue', 'return', 'if', 'elif', 'else', 'unique',
  'for', 'in', 'while', 'loop', 'match', 'test', 'formal', 'type', 'impl', 'enum',
  'tick', 'step', 'ref', 'pub', 'comptime', 'fluid', 'const', 'mut', 'reg', 'wire',
  'stage', 'wrap', 'sat', 'comb', 'mod', 'pipe', 'sext', 'zext', 'not', 'and', 'or',
  'implies', 'has', 'case', 'does', 'equals', 'true', 'false',
];

// The keywords that are no name ANYWHERE (owner ruling 99), like the type words:
// not a `.field`, a tuple field, a named argument, a lambda or method name
// (`x.step`, `(mut tick = 0)`, `comb step()` are errors); the backticked
// `` `step` `` is an ordinary name. prpparse agrees (parser.hpp leaf).
const ALWAYS_RESERVED = ['tick', 'step'];

// A name being bound (see KEYWORDS): an `identifier` under the `binding` reserved
// set, so the lexer reads every keyword there as the keyword (a syntax error)
// even where that keyword could not otherwise appear. tree-sitter applies a
// reserved set to the word token only where it is written directly in a
// production, so each binding rule spells `reserved('binding', $.identifier)`
// itself.
function bindingTypedIdentifier($) {
  return prec.left('typed_identifier', seq(
    field('identifier', reserved('binding', $.identifier))
    , optional($._timing_sequence)
    , field('type', optional($.type_cast))
  ));
}

// The kind part of a declaration (`var_or_let_or_reg`): `fluid x = …`,
// `fluid mut x:T` (a fluid handshake declaration, optionally combined with a
// storage kind) or a storage kind alone.
function fluidOrStorage($) {
  return choice(
    seq(
      field('fluid', alias('fluid', $.fluid_decl))
      , field('storage', optional($._storage_kind))
    )
    , field('storage', $._storage_kind)
  );
}

function dottedChain(item, tail, precedence, subprecedence, associativity = prec.left) {
  return associativity(precedence, seq(
    field('item', item)
    , repeat1(associativity(subprecedence, seq('.', tail)))
  ));
}

// The suffix chains (`x[i]`, `x#[i]`, `x.[attr]`), shared by the expression
// rules (head `_suffix_head`) and the assignment-target rules (head
// `_lvalue_target`, a chain rooted at a name).
function memberSelection($, head) {
  return prec('member_selection', seq(
    field('argument', head)
    , field('select', prec.right('select', repeat1($.select)))
  ));
}

function bitSelection($, head) {
  return prec('bit_selection', seq(
    field('argument', head)
    , field('select', prec('select', seq(
      '#'
      , optional(choice(
        field('reduction', choice(
          alias('|', $.reduction_or)
          , alias('&', $.reduction_and)
          , alias('^', $.reduction_xor)
          , alias('+', $.reduction_popcount)
        ))
        , field('extension', choice(
          alias('sext', $.sign_extend)
          , alias('zext', $.zero_extend)
        ))
      ))
      , field('select', $.select)
    )))
  ));
}

function attributeRead($, head) {
  return prec.left('member_selection', seq(
    field('argument', head)
    , field('attrs', repeat1(prec('select', seq(
      '.', $.attribute_list
    ))))
  ));
}

// Grammar
module.exports = grammar({
  name: 'pyrope'

  // `comment` is scanned by src/scanner.c so block comments can NEST:
  // `/* a /* b */ c */` is one comment (lhd's lexer and prpparse nest too).
  // `_string_content` (the literal text runs of a double-quoted string) is
  // external too: tree-sitter asks the external scanner for its extras (the
  // comment) even inside a string, so the scanner must know when it is in string
  // text to never read `"//x"` or `"/* a */"` as a comment.
  //
  // `_field_word` is a reserved word used as a FIELD name: src/scanner.c emits
  // it (only where the grammar takes it) for a keyword followed by `=` (not
  // `==`) or `:`, so `(if = 1)`, `f(pub = 1)`, `(const type = 1)`, `(in:U8)` and
  // `::[comptime = 1]` name a field or argument even where the keyword could
  // also start an expression. It is always aliased to `identifier`.
  //
  // `_string_newline` is never part of a rule: the scanner returns it for a raw
  // newline in "..." string text, which therefore always fails to parse
  // (prpparse agrees: "newline in interpolated string").
  //
  // `_never` is never returned by the scanner: it only guards the placeholder
  // word and `nil` so those tokens exist in the grammar (a reserved word must
  // be a token) while no parse can ever accept one.
  //
  // A lambda that is a whole tuple ENTRY (`(comb f(self) { }, 1)`) is never
  // the operand of a binary operator or the head of a suffix there, so
  // `(comb f(self) { } + 1)` and `(comb f(self) { }#[0])` are syntax errors,
  // as in prpparse (a lambda tuple entry must be followed by `,`, `)` or
  // `]`). Elsewhere (a statement, an argument, a field value) a lambda still
  // heads an expression. The zero-width `_tuple_lambda_end` follows such an
  // entry: src/scanner.c returns it when the next token (past blanks,
  // newlines and comments) closes the entry, and otherwise returns a token no
  // rule accepts, so the expression reading of the entry fails too.
  //
  // A line never starts with `@` (02-basics): `const a = x` newline `@[1]`,
  // `if x` newline `@[1] == 1 { }` and `f(a` newline `@[1])` are syntax errors
  // (prpparse agrees, lexer.cpp "line-starts-with-at"). src/scanner.c returns
  // `_string_newline` (a token no rule accepts) for blanks holding a newline
  // before a `@`.
  //
  // `_enum_call` is the retired expression form `enum(...)` (spec 2026-09-29
  // §7): where an expression or a type can start, src/scanner.c returns it for
  // the word `enum` followed (past blanks) by `(`, and the grammar then demands
  // `_never`, so `const C = enum(a, b)`, `type V = enum(a)` and `f(enum(a))`
  // are syntax errors. Elsewhere `enum` stays contextual (`x.enum`, `f(enum)`).
  //
  // `_line_end` is never returned either: it marks where a construct that
  // prpparse ends at a newline may end although no statement ends there: a
  // statement's assignment target (`_single_assignment`), and a header (the
  // condition of an `if`/`elif`/`while`, the iterable of a `for`, the subject
  // and each arm of a `match`, the count of a `tick`). A line starting with
  // `(` or `[` never continues one (02-basics: it starts a new statement), so
  // src/scanner.c returns `_string_newline` (a token no rule accepts) there
  // when blanks holding a newline come before that `(` or `[`: `mut x:U8`
  // newline `(r, x) = (1, 2)` is a declaration plus a destructuring (the GLR
  // reading `mut x:U8(r, x) = ...` dies), `mut x:Unsigned` newline `(bits=4) =
  // 0` and `if a` newline `(b) { }` are errors. Inside brackets the line
  // continues (`f(a` newline `(b))`, `comb f(a:Foo` newline `(b))`). prpparse
  // agrees (parser.cpp term_stop; a header resets the bracket depth).
  //
  // `_lambda_body` is never returned: it marks where a lambda's body `{` may
  // follow, so src/scanner.c inserts no automatic semicolon before a `{` that
  // starts the next line there (`comb f() -> (c)` newline `{ c = 1 }` is one
  // lambda with its body, as in prpparse).
  , externals: $ => [$._automatic_semicolon, $.comment, $._string_content, $._field_word, $._string_newline, $._never, $._enum_call, $._tuple_lambda_end, $._lambda_body, $._line_end, $._spaced_lt]
  , conflicts: $ => [
    [$._complex_identifier, $.typed_identifier]
    // `x:Foo(…)` — the call parens are ambiguous between a type-position
    // call (tuple) and an expression call (arg_tuple); GLR resolves it.
    , [$.tuple, $.arg_tuple]
    , [$._arg_item, $._tuple_item]
    , [$._complex_identifier, $.expression_type]
    , [$._stmt_item, $._restricted_expression]
    , [$.timed_identifier, $.typed_identifier]
    , [$.timed_identifier, $.expression_type]
    , [$.var_or_let_or_reg, $.fluid_lambda]
    , [$.tuple_sq, $.array_length]
    // The binding-name variants (`_binding_typed_identifier`, `_binding_lvalue_*`,
    // `_binding_named_lvalue`: names that are bound, where every keyword is
    // reserved) and the keyword field name (`_field_word` in `_field_assignment`,
    // `named_lvalue`, `arg_assignment`) fork exactly like the plain rules above.
    , [$._complex_identifier, $._binding_typed_identifier]
    , [$.timed_identifier, $._binding_typed_identifier]
    , [$.timed_identifier, $._binding_typed_identifier, $.typed_identifier]
    , [$._binding_typed_identifier, $.typed_field]
    , [$.arg_assignment, $.assignment_operator]
    , [$.timed_identifier, $._binding_typed_name]
    , [$.timed_identifier, $._binding_typed_identifier, $._binding_typed_name]
    , [$._tuple_item, $.paren_group]
    , [$._tuple_item, $.array_length]
    // `x:U8(...)` in a type position: the constrained `uint_type`, or (only if
    // a suffix and a call follow, `function_call_type` on a `_suffix_head`)
    // the conversion call `U8(...)`; GLR drops the value branch by lookahead.
    , [$.uint_type, $._type_word_name]
    , [$.sint_type, $._type_word_name]
    // `f<Signed>(x)` vs comparison `f < Signed …` — GLR forks; dynamic precedence
    // on the generic call wins when both parses survive (see
    // genericTupleCall).
    , [$.function_call_expression, $._restricted_expression]
    // `f<N=U8.[max]>(x)` vs the comparison `f < U8.[max] ...`: the same GLR
    // fork as a name-headed read (above), for a type-word head.
    , [$.attribute_read, $.generic_attribute_read]
    // `x.y` + `.`: end of the dotted name (then `.[`) or one more `.field`.
    , [$.generic_dotted_name]
    , [$.arg_assignment, $._lvalue_target, $.typed_identifier]
    , [$._binding_slot_name, $._binding_typed_identifier]
    , [$._lvalue_target, $._complex_identifier, $._binding_typed_identifier, $._binding_typed_name]
    , [$._lvalue_target, $._binding_typed_name]
    , [$._lvalue_target, $._binding_typed_identifier, $._binding_typed_name]
    , [$._lvalue_target, $._complex_identifier, $.typed_identifier, $.typed_field, $._binding_typed_identifier]
    , [$._lvalue_target, $._complex_identifier, $.typed_identifier, $.typed_field]
    , [$.named_lvalue, $._lvalue_target, $.typed_identifier]
    , [$._slot_name, $._complex_identifier]
    , [$._lvalue_target, $._complex_identifier, $.typed_identifier]
    , [$._lvalue_target, $.typed_identifier]
    , [$._lvalue_target, $._complex_identifier]
    // `(a:` -- a typed field, a typed declared item, or a field-assignment
    // target followed by its type (`(a:U8 = 1)`).
    , [$._lvalue_target, $.typed_identifier, $.typed_field]
    , [$._lvalue_target, $.typed_identifier, $.typed_field, $._binding_typed_identifier]
    // A dotted type name (`x:a.b`, `_type_name_part`) vs a dotted value: the
    // callee of a type-position call (`x:a.b(c)`) or a bare postfix generic
    // argument (`f<N=x.[bits]>`, `<N=cfg.w.[max]>`), where only a following
    // `.[` picks the attribute read.
    , [$._complex_identifier, $._type_name_part]
    , [$._complex_identifier, $.generic_attribute_read, $.generic_dotted_name, $._type_name_part]
    , [$._complex_identifier, $.expression_type, $._type_name_part]
    // `x:3` (a constant type) vs the head of a type call's dotted callee
    // (`x:3.f(a)`, an expression `integer_literal`).
    , [$._type_constant, $.integer_literal]
    , [$._type_constant, $.constant]
    // `if a; b {` / `if a; {`: after `a` a `;` either ends an init-clause item
    // or trails the condition (a `;` run means a newline); what follows the
    // run decides.
    , [$.stmt_list]
    , [$._stmt_item, $._if_branch]
    , [$._stmt_item, $.while_statement]
    , [$._stmt_item, $.match_expression]
  ]
  , extras: $ => [$._space, $.comment]
  , word: $ => $.identifier
  // The type words spelled as whole words are RESERVED in every context: the
  // lexer never reads them as an `identifier`, even where only a name is
  // valid (a declaration, a `.field`, a named argument), so `const Bool = 1`
  // and `x.String` are syntax errors. The sized words `U<N>`/`S<N>` are
  // excluded by the `identifier` regex itself.
  //
  // `tick`/`step` are reserved in every position too (ALWAYS_RESERVED). The
  // other keywords are contextual in general (a `.field`, an attribute name, a
  // lambda name may be spelled `type`, `comptime`, `enum`, ...) but
  // RESERVED where a name is bound (the `binding` set, see KEYWORDS and
  // bindingTypedIdentifier). A keyword FIELD name before `=`/`:` is the
  // external `_field_word`.
  , reserved: {
    global: $ => TYPE_WORDS.concat(ALWAYS_RESERVED, [$._reserved_placeholder_word])
    // `nil` is no keyword (a value elsewhere) but never a name being bound
    // (`const nil = 1` is an error; prpparse agrees).
    , binding: $ => TYPE_WORDS.concat([$._reserved_placeholder_word], KEYWORDS, ['nil'])
  }

  , precedences: $ => [
    [
      'dot_type'
      , 'dot_type_sub'
      , 'array_type'
      , 'function_call_type'
      , 'expression_type'
      , 'type'
      , 'typed_identifier'
      // Expressions
      , 'dot_sub'
      , 'dot'
      , 'select'
      , 'member_selection'
      , 'bit_selection'
      , 'unary'            // Pyrope priority 1: !, not, ~, -
      , 'type_spec'
      , 'binary_times'     // Pyrope priority 2: *, /, %
      , 'binary_other'     // Pyrope priority 3: +, -, <<, >>, &, |, ^, ..=, ..<, ..+
      , 'binary_step'      // `step` binds looser than the range ops: (a..=b) step c
      , 'binary_compare'   // Pyrope priority 4: <, <=, ==, !=, >=, >, has/in/case/does/equals
      , 'binary_logical'   // Pyrope priority 5: and, or, implies
      , 'expression'
    ]
    // Types
    , [
      'statement'
      , 'expression'
    ]
    , [
      '_tuple_list'
    ]
    , [
      'expression'
      , 'function_call_expression'
      , 'function_call_type'
    ]
    , [
      'type_spec'
      , 'type_cast'
    ]
    // Deliberately related to nothing: the explicit-generic call
    // (`f<Signed>(…)`) must stay statically unresolved against the comparison
    // chain so the GLR fork + dynamic precedence can decide (see
    // genericTupleCall).
    , [
      'generic_call'
    ]
  ]

  , supertypes: $ => []

  , rules: {
    // Top. A `;` means a newline (02-basics "Semicolons"), so any run of them
    // may separate statements, lead the file or a block, or fill it
    // (prpparse agrees: parse_description / parse_scope).
    description: $ => seq(repeat(';'), repseq($._statement, repeat(';')))

    // Statements
    , _statement: $ => prec('statement', choice(
      // Synthesizable
      $.scope_statement
      , $.declaration_statement
      // Overparse: `wrap`/`sat` are only meaningful when narrowing an integer
      // RHS into a smaller integer LHS. See grammar_overparse.md #5.
      , seq(field('overflow', optional(choice('wrap', 'sat'))), $.assignment, $._semicolon)
      , $.import_statement
      , $.control_statement
      , $.while_statement
      , $.for_statement
      , $.lambda
      , seq($.enum_assignment, $._semicolon)
      , $.loop_statement
      , seq($._expression, $._semicolon)
      // Verification Only
      , $.test_statement
      , $.formal_statement
      , $.tick_statement
      , $.step_statement
      , $.type_statement
      , $.impl_statement
      // Unreachable (`_never` is never scanned): declares the placeholder word and
      // the `nil` token (reserved only where a name is bound, see `reserved`),
      // and keeps the retired `enum(...)` expression node (`enum_definition`,
      // with its `arg_list`) in the symbol table only so consumers that still
      // name it (prpfmt print_enum_definition) keep compiling. Drop
      // `enum_definition`/`arg_list` once prpfmt no longer references them.
      , seq($._never, choice($._reserved_placeholder_word, 'nil', $.enum_definition, $.unknown_literal))
    )
    )
    , scope_statement: $ => seq(
      '{'
      , field('attributes', optional($._attr_prefix))
      , repeat(';')
      , repseq($._statement, repeat(';'))
      , '}'
    )
    , declaration_statement: $ => seq(
      field('decl', $.var_or_let_or_reg)
      , choice(
        seq('(', field('lvalue', $.typed_identifier_list), ')')
        , field('lvalue', alias($._binding_typed_identifier, $.typed_identifier))
      )
      , $._semicolon
    )
    , import_statement: $ => seq(
      'import'
      , field('module', choice(
        seq(
          $.identifier
          , repeat(seq('.', $.identifier))
        )
        , $._string_literal
      ))
      , 'as'
      , field('alias', $.identifier)
      , $._semicolon
    )
    , control_statement: $ => choice(
      $.break_statement
      , $.continue_statement
      , $.return_statement
    )
    , break_statement: $ => seq('break', $._semicolon)
    , continue_statement: $ => seq('continue', $._semicolon)
    // `return` is a terminator only — it never carries a value
    // (06-functions.md "Output tuple" rule 3). Assign outputs first,
    // then `return`.
    , return_statement: $ => seq('return', $._semicolon)
    // An init clause's statements (`if x = f(); x > 3 { }`): tuple items. A
    // destructuring assignment is not one (`if (a, b) = f(); a { }` is an
    // error, spec 2026-09-29 §7).
    , stmt_list: $ => seq(
      field('item', $._stmt_item)
      , repeat(seq(repeat1(';'), field('item', $._stmt_item)))
    )
    // Like `_tuple_item`, but a declaration here BINDS a variable (`while mut
    // i = 0; i < 3 { }`), so its name is a binding (no keyword, no field-name
    // rule).
    , _stmt_item: $ => choice(
      $.ref_identifier
      , $._expression
      , alias($._single_assignment, $.assignment)
      , $.typed_field
      , seq(
        field('decl', $.var_or_let_or_reg)
        , field('lvalue', alias($._binding_typed_identifier, $.typed_identifier))
      )
      , prec(-1, seq(
        field('decl', $.var_or_let_or_reg)
        , field('value', $._expression)
      ))
      , $.lambda
    )
    , _if_branch: $ => seq(
      field('init', statementInit($))
      , field('condition', $._expression)
      , optional($._line_end)
      , repeat(';')
      , field('code', $.scope_statement)
    )
    , if_expression: $ => prec('statement', seq(
      optional('unique')
      , 'if'
      , $._if_branch
      , field('elif', repseq('elif', $._if_branch))
      , field('else', optseq('else', $.scope_statement))
    ))
    , _attr_prefix: $ => attributeOnly($)
    , _init_clause: $ => seq($.stmt_list, repeat1(';'))
    , for_statement: $ => seq(
      'for'
      , field('attributes', optional($._attr_prefix))
      , field('init', statementInit($))
      , forBinding($) // NOTE: maybe constraint to max 3 (elem,index,key)
      , 'in'
      , choice(
        $.ref_identifier
        , field('data', $._expression)
      )
      , optional($._line_end)
      , field('code', $.scope_statement)
    )
    , while_statement: $ => seq(
      'while'
      , field('attributes', optional($._attr_prefix))
      , field('init', statementInit($))
      , field('condition', $._expression)
      , optional($._line_end)
      , repeat(';')
      , field('code', $.scope_statement)
    )
    , loop_statement: $ => prec('statement', seq(
      'loop'
      , field('attributes', optional($._attr_prefix))
      , optseq(field('init', $.stmt_list), repeat(';'))
      , field('code', $.scope_statement)
    ))
    , match_expression: $ => seq(
      'match'
      , field('init', statementInit($))
      , field('condition', $._expression)
      , optional($._line_end)
      , repeat(';')
      , '{'
      // At least one arm: `match x { }` and the else-only `match x { else { } }`
      // are errors (prpparse "match has no arms"). The unreachable `else`
      // branch keeps `else` the keyword where the first arm starts (never an
      // arm named `else`), as prpparse reads it.
      , choice(
        seq(
          field('cases', repeat1(seq(
            field('condition', seq(optional(choice(
              'and', 'or', '&', '^', '|',
              '<', '<=', '>', '>=', '==', '!=', 'has', 'case', 'in',
              'equals', 'does'
            )), $._expression))
            , optional($._line_end)
            , field('code', $.scope_statement)
          )))
          , optseq('else', field('else_code', $.scope_statement))
        )
        , seq('else', $._never)
      )
      , '}'
    )
    // A `test` resembles a `comb name(...)` lambda but (1) has no `-> (...)`
    // return and (2) names the test with a dotted selector path (`counter.foo`)
    // so groups and leaves are addressable from the command line. The optional
    // `(...)` carries runtime test parameters (typed, with defaults) — the same
    // `arg_list` a lambda uses for its inputs. `test counter.foo { }` (no
    // params) and `test counter.foo(max_cycles:U32 = 10000) { }` both parse.
    , test_statement: $ => seq(
      'test'
      , field('name', $.test_name)
      , field('input', optional(alias($._binding_arg_list, $.arg_list)))
      , field('code', $.scope_statement)
    )
    // Dotted selector path for a test name: `identifier ('.' identifier)*`.
    // Kept distinct from `dot_expression` so the trailing `(...)` is the test's
    // parameter list, never folded into a function call on the name.
    , test_name: $ => prec.left(seq(
      $.identifier
      , repeat(seq('.', $.identifier))
    ))
    // A `formal` block (05-assert.md "Formal blocks") is a declarative
    // verification overlay: `formal name.path { stmts+ }`. Named by the same
    // dotted selector path as a `test` (the name is the enable/filter handle
    // for `lhd formal verify --formal <glob>`), but it takes no parameter
    // list — a formal block has no runtime arguments. The body is ordinary
    // statement syntax; only the formal tool consumes it (the design compile
    // skips it). Mirrors prpparse parse_formal (parser.cpp).
    , formal_statement: $ => seq(
      'formal'
      , field('name', alias($.test_name, $.formal_name))
      , field('code', $.scope_statement)
    )
    // Cycle-driven test loop: `tick N { ... }` runs N cycles (one clock per
    // iteration, the DUT is called inside the body). The count is mandatory:
    // an unbounded `tick { }` is not supported yet, and a tick takes no
    // `clocks=`/`resets=` clauses (owner answer 2026-09-30). The count lives
    // in the `value` field, the body in `code` -- matching prpparse
    // (parser.cpp parse_tick_statement), whose node the simulation backend
    // (inou/prp/prp_sim.cpp) consumes.
    , tick_statement: $ => prec('statement', seq(
      'tick'
      , field('value', $._expression)
      , optional($._line_end)
      , field('code', $.scope_statement)
    ))
    // Cycle advance inside a `test`: `step [N]` advances N cycles (default 1) --
    // `step`, `step 5`, and `step(1000)` all parse. Unlike `tick` it has no body
    // (a leaf statement terminated like any expression statement); the optional
    // count lives in the `value` field. Statement-leading `step` is this; `step`
    // as a range stride (`a..=b step c`) stays the `binary_step` operator below.
    , step_statement: $ => prec('statement', seq(
      'step'
      , field('value', optional($._expression))
      , $._semicolon
    ))
    , type_statement: $ => seq(
      // `pub type X = …` — exportable type alias, matching the visibility
      // field shape used by declarations and lambdas.
      field('pub', optional(alias('pub', $.pub_modifier)))
      , 'type'
      , field('name', $.identifier)
      , field('generic', optseq('<', alias($.generic_identifier_list, $.typed_identifier_list), '>'))
      // The `=` is mandatory, as for `enum` (owner ruling 106): `type Pt (x:S8)`
      // is an error (write `type Pt = (x:S8)`; prpparse agrees).
      , '='
      , choice(
        seq(
          field('func_type', choice(
            alias('comb', $.comb_lambda)
            , alias('mod', $.mod_lambda)
            , $.pipe_lambda
            , $.fluid_lambda
          ))
          , $.function_definition_decl
        )
        , field('alias', $._type)  // type alias: type Name = Type
      )
      , $._semicolon
    )
    , impl_statement: $ => seq(
      'impl'
      , field('trait_name', $.identifier)
      , 'for'
      , field('type_name', $.identifier)
      , field('implementation', $.tuple)
      , $._semicolon
    )
    , expression_list: $ => prec.left(seq(
      field('item', $._expression)
      , repseq(',', field('item', $._expression))
    ))

    // Function Call
    , function_call_expression: $ => choice(
      tupleCall($, 'function_call_expression', $.arg_tuple)
      , genericTupleCall($, $.arg_tuple)
      // `f::[name=u_inst](args)` — a call that names its instance. The callee is
      // an ordinary `attribute_set` (`f` then the single `::` token). This shape
      // is the dominant one in the sim/loop/hierarchy fixtures: without it the
      // parser consumed `f::[name=u]` and then wanted a binary operator before
      // `(`, reporting MISSING op_mul on 19 corpus files.
      , prec('function_call_expression', seq(
        field('function', $.attribute_set)
        , field('argument', $.arg_tuple)
      ))
    )
    // Tuple
    , tuple: $ => seq('(', optional($._tuple_list), ')')

    // Call-site argument tuple. Arguments are plain bindings — positional
    // `expr`, named `name = expr` (dotted names allowed, expanding nested
    // fields), `ref x`, or spread `...expr`. Unlike data-tuple literals,
    // items can NOT be declarations: no kind keywords, no `name:type`, no
    // `::[attr]`, no compound assignment operators. The same restriction
    // applies semantically to the RHS tuple of a typed lvalue
    // (`mut x:T = (…)`), which the grammar still parses as a data tuple
    // (see grammar_overparse.md #3).
    , arg_tuple: $ => seq('(', optional(prec('_tuple_list', listseq1(field('item', $._arg_item)))), ')')
    , _arg_item: $ => choice(
      $.ref_identifier
      , $._expression
      , $.arg_assignment
    )
    , arg_assignment: $ => seq(
      field('lvalue', choice($.identifier, alias($._field_word, $.identifier), $.dot_expression))
      , '='
      , field('rvalue', choice($._expression, $.ref_identifier))
    )

    , tuple_sq: $ => seq('[', optional($._tuple_list), ']')

    // Write-side attribute bracket (`::[…]`, `:Type:[…]`). Distinct from
    // `tuple_sq` so the lookahead at `[` does not include
    // `var_or_let_or_reg`, letting the lexer accept reserved keywords like
    // `comptime` as bare attribute names.
    , attribute_sq: $ => seq('[', optional(listseq1(field('item', $._attribute_item))), ']')
    , _attribute_item: $ => choice(
      $._expression
      , $.ref_identifier
      , $.attribute_assignment
    )
    , attribute_assignment: $ => seq(
      field('lvalue', choice($.identifier, alias($._field_word, $.identifier)))
      , '='
      , field('rvalue', choice($._expression, $.ref_identifier))
    )

    , _tuple_list: $ => prec('_tuple_list', listseq1(field('item', $._tuple_item)))
    // Overparse: tuple-literal fields require a kind keyword
    // (`mut`/`const`/`wire`/`reg`/`comb`/...); bare `a = 1` parses here because the
    // same node also models named call arguments and typed construction fields.
    // See grammar_overparse.md #3.
    , _tuple_item: $ => choice(
      $.ref_identifier
      , $._expression
      // A named field / field assignment (`(a=1)`, `(a.b=1)`, `(mut x:U8=0)`);
      // a destructuring `(a, b) = f()` is a statement, never a tuple item
      // (`const q = ((a, b) = g())` is an error; prpparse agrees).
      , alias($._field_assignment, $.assignment)
      // Bare `name:type` field. Only meaningful inside tuple TYPES — inline
      // annotations (`ar:(x:U3, y:S4)`), enum bodies (`enum(str:String)`),
      // and type-shape operands of `does`/`equals`/`case`. In a data-tuple
      // literal a named field needs a kind keyword and a value; the semantic
      // pass rejects bare typed fields there.
      , $.typed_field
      , seq(
        field('decl', $.var_or_let_or_reg)
        , field('lvalue', choice(
          $.typed_identifier
          // `(const type:U8)`: a keyword field name (see `_field_word`)
          , alias($._field_typed_word, $.typed_identifier)
        ))
      )
      // Positional tuple field with explicit mutability override:
      // `(1, const 3)` / `(mut 3, 4)`. Per 04-variables.md, only `const` /
      // `mut` are meaningful here (not `wire`/`reg`), but the grammar
      // overparses and accepts
      // any var_or_let_or_reg prefix; the semantic pass narrows it.
      , prec(-1, seq(
        field('decl', $.var_or_let_or_reg)
        , field('value', $._expression)
      ))
      , seq($.lambda, $._tuple_lambda_end)
    )

    // Assignment (single or tuple lvalue). The target is a name, a field, a
    // selector or a bit-select ROOTED AT A NAME (`_lvalue_target`), optionally
    // typed, or -- as a STATEMENT only (never in an init clause or an
    // expression) -- a parenthesized destructuring list of names `(a, b) =
    // f()`. `f(x) = 3`, `f(x).a = 3`, `(a+b).c = 3`, `a + b = 3`, `(1) = 2` and
    // `(a, f(x)) = g()` are syntax errors (prpparse agrees).
    , assignment: $ => choice(
      $._destructuring_assignment
      , $._single_assignment
    )
    // With a declaration keyword (`const (a, b) = f()`) the names are BOUND
    // (`_binding_lvalue_list`); without one they are existing targets. The
    // operator is only `=`: `(a, b) += f()` is an error.
    , _destructuring_assignment: $ => seq(
      choice(
        seq(
          field('decl', $.var_or_let_or_reg)
          , '('
          , field('lvalue', alias($._binding_lvalue_list, $.lvalue_list))
          , ')'
        )
        , seq('(', field('lvalue', $.lvalue_list), ')')
      )
      , field('operator', alias($._plain_assign, $.assignment_operator))
      , field('rvalue', $._assignment_rvalue)
    )
    , _plain_assign: $ => alias('=', $.assign)
    // A statement (or init-clause) assignment. With a declaration keyword the
    // target name is BOUND (`mut x = 1`): no keyword can spell it.
    , _single_assignment: $ => seq(
      choice(
        seq(
          field('decl', $.var_or_let_or_reg)
          , choice(
            // `mut x:U8 = 1`: a typed name is a typed_identifier; an untyped
            // target stays bare (`mut x = 1`, `mut x@[1] = 2`), as in prpparse.
            field('lvalue', alias($._binding_typed_name, $.typed_identifier))
            , seq(
              field('lvalue', $._lvalue_target)
              , field('type', optional($.type_cast))
            )
          )
        )
        , choice(
          field('lvalue', $.typed_identifier)
          , seq(
            field('lvalue', $._lvalue_target)
            , field('type', optional($.type_cast))
          )
        )
      )
      // The target ends at a newline before `(` or `[` (`_line_end`): `mut
      // d:Unsigned` newline `(bits=8) = 0` is an error, as in prpparse.
      , optional($._line_end)
      , field('operator', $.assignment_operator)
      , field('rvalue', $._assignment_rvalue)
    )
    // A tuple-entry assignment: a (possibly declared) FIELD. A keyword may name
    // the field when `=` or `:` follows it (`(if = 1)`, `(const type = 1)`,
    // `(in:U8 = 0)`; see `_field_word`).
    , _field_assignment: $ => seq(
      field('decl', optional($.var_or_let_or_reg))
      , choice(
        field('lvalue', $.typed_identifier)
        , seq(
          field('lvalue', $._lvalue_target)
          , field('type', optional($.type_cast))
        )
        , field('lvalue', alias($._field_word, $.identifier))
        , field('lvalue', alias($._field_typed_word, $.typed_identifier))
      )
      , field('operator', $.assignment_operator)
      , field('rvalue', $._assignment_rvalue)
    )
    // A keyword field name with a type: `(in:U8 = 0)`, `(const type:U8)`.
    , _field_typed_word: $ => prec.left('typed_identifier', seq(
      field('identifier', alias($._field_word, $.identifier))
      , field('type', $.type_cast)
    ))
    // The expression form `enum(a, b)` is gone (spec 2026-09-29 §7): an enum
    // is only the declaration `enum E = (...)` / `enum E:T = (...)`
    // (`enum_assignment`), so `const C = enum(a, b)` is an error.
    , _assignment_rvalue: $ => choice(
      $._expression
      , $.ref_identifier
    )
    // A slot of a parenthesized destructuring assignment (spec 2026-09-29
    // §7/§8): a bare NAME (`b`: binds the RHS field `b`, or by position for an
    // unnamed RHS) or a rename `local = source.path` (`x=dox.b`,
    // `v=deep.payload.inner.value`). A slot never carries a type, a timing or
    // a field/selector/bit-select target: `(a.b, c[1]) = f()`, `(a:U32, b) =
    // f()` and `(a=b[1]) = f()` are errors (prpparse agrees).
    , lvalue_item: $ => choice(
      alias($._slot_name, $.typed_identifier)
      , $.named_lvalue
    )
    , _slot_name: $ => field('identifier', $.identifier)
    , named_lvalue: $ => seq(
      field('name', $.identifier)
      , '='
      , field('lvalue', $._slot_path)
    )
    // The RHS field path of a rename slot: a name or a dotted name.
    , _slot_path: $ => choice(
      $.identifier
      , alias($.generic_dotted_name, $.dot_expression)
    )
    // In a destructuring DECLARATION (`const (a, b) = f()`, `const (x1=r.a) =
    // r()`) the local names are BOUND (no keyword can spell them).
    , _binding_named_lvalue: $ => seq(
      field('name', reserved('binding', $.identifier))
      , '='
      , field('lvalue', $._slot_path)
    )
    , _binding_slot_name: $ => field('identifier', reserved('binding', $.identifier))
    , lvalue_list: $ => listseq1(field('item', $.lvalue_item))
    , _binding_lvalue_list: $ => listseq1(field('item', alias($._binding_lvalue_item, $.lvalue_item)))
    , _binding_lvalue_item: $ => choice(
      alias($._binding_slot_name, $.typed_identifier)
      , alias($._binding_named_lvalue, $.named_lvalue)
    )
    , var_or_let_or_reg: $ => seq(
      // `pub reg mem:[1024]U8 = nil`, `pub const mytup = (…)` — visible
      // outside this file.
      field('pub', optional(alias('pub', $.pub_modifier)))
      , choice(
        // `comptime` alone is shorthand for `comptime const` (04b-attributes.md
        // "comptime modifier"): `comptime c = 1` (prpparse agrees). A kind word
        // after it always belongs to the declaration (`comptime mut x`).
        // `comptime` only modifies a VALUE (`const`/`mut`), in either order
        // (owner ruling 2026-10-01): `const comptime x` == `comptime const x`;
        // prpfmt prints the `comptime`-first spelling. `comptime reg`,
        // `comptime wire`, `comptime stage` and `comptime fluid` are errors.
        prec.right(seq(
          field('comptime', alias('comptime', $.comptime_modifier))
          , optional(field('storage', $._value_storage_kind))
        ))
        , seq(
          field('storage', $._value_storage_kind)
          , field('comptime', alias('comptime', $.comptime_modifier))
        )
        , fluidOrStorage($)
      )
    )

    , _value_storage_kind: $ => choice(
      alias('const', $.const_decl)
      , alias('mut', $.mut_decl)
    )

    , _storage_kind: $ => choice(
      alias('const', $.const_decl)
      , alias('mut', $.mut_decl)
      , alias('wire', $.wire_decl)
      , alias('reg', $.reg_decl)
      , $.stage_decl
    )
    // `stage` annotation on the LHS of a pipelined assignment inside a
    // `mod`. Picks how many pipeline stages the RHS `pipe` call inserts.
    // Forms:
    //   `stage`        — declaration without picking a count.
    //   `stage[N]`     — request exactly N pipeline stages from the call.
    //   `stage[A..<B]` — accept any count in the range.
    //   `stage[]`      — let the toolchain pick (default or auto).
    , stage_decl: $ => prec.right(seq('stage', optional($.timing_slot)))

    // Read-side attribute name (`.[identifier]`). Exactly one identifier in
    // the brackets — reads never carry `=value`, and the docs show one
    // attribute per `.[…]` (chain reads via repeated `.[a].[b]`). Kept as a
    // distinct rule (not folded into `tuple_sq`) because attribute names
    // routinely collide with reserved keywords (`comptime`, ...); routing
    // through `$.identifier` directly lets the lexer accept those keywords
    // here as plain identifiers.
    , attribute_list: $ => seq('[', field('name', $.identifier), ']')
    , function_definition_decl: $ => seq(
      field('generic', optseq('<', alias($.generic_identifier_list, $.typed_identifier_list), '>'))
      , field('pipe_config', optional($._attr_prefix))
      , field('input', alias($._binding_arg_list, $.arg_list))
      , field('output', optseq('->', choice(
        alias($._binding_arg_list, $.arg_list)
        , field('type', $.type_cast)
        , alias($._binding_typed_identifier, $.typed_identifier)
      )))
    )
    // `enum(...)` as an expression or a type: always a syntax error (see
    // `_enum_call` in externals).
    , _retired_enum_expression: $ => seq($._enum_call, $._never)
    // Retired (see `_statement`): never produced by a parse.
    , enum_definition: $ => seq(
      'enum'
      , field('input', $.arg_list)
    )
    , enum_assignment: $ => seq(
      'enum'
      , field('name', $.identifier)
      , field('type', optional($.type_cast))
      // The `=` is required: `enum E = (a, b)`, `enum E:U8 = (a, b)`;
      // `enum E:U8 (a, b)` is a syntax error (prpparse agrees).
      , '='
      , field('values', alias($._enum_tuple, $.tuple))
    )
    // An enum body: a tuple whose entries are the members. A member is a FIELD
    // of the enum (`E.a`), so a bare type word can not be one: `enum E = (a,
    // U8)` is an error like `(const U8 = 1)` (backtick it: `` (a, `U8`) ``).
    // Only the direct entries are checked (a nested `l1 = (...)` value is an
    // ordinary tuple); prpparse agrees (parse_enum_assignment).
    , _enum_tuple: $ => seq('(', optional(prec('_tuple_list', listseq1(field('item', $._enum_item)))), ')')
    , _enum_item: $ => choice(
      $.ref_identifier
      , $._enum_value
      , alias($._field_assignment, $.assignment)
      , $.typed_field
      , seq(
        field('decl', $.var_or_let_or_reg)
        , field('lvalue', choice(
          $.typed_identifier
          , alias($._field_typed_word, $.typed_identifier)
        ))
      )
      , prec(-1, seq(
        field('decl', $.var_or_let_or_reg)
        , field('value', $._expression)
      ))
      , seq($.lambda, $._tuple_lambda_end)
    )
    // `_expression` minus a bare type word (`_type_word_name`).
    , _enum_value: $ => prec('expression', choice(
      $._retired_enum_expression
      , alias($._binary_logical, $.expression_item)
      , alias($._binary_compare, $.expression_item)
      , alias($._binary_step, $.expression_item)
      , alias($._binary_other, $.expression_item)
      , alias($._binary_times, $.expression_item)
      , $.attribute_set
      , $.unary_expression
      , $.if_expression
      , $.match_expression
      , $.scope_statement
      , $._complex_identifier
      , alias($._type_word_call, $.function_call_expression)
      , $.constant
      , $.function_call_expression
      , $.tuple
      , $.tuple_sq
    ))
    , ref_identifier: $ => seq(
      'ref'
      , $._complex_identifier
    )
    // `enum(a, b)` variant names (field names: `enum(and, or)` is legal).
    , arg_list: $ => seq(
      '(', optional(listseq1(seq(
        field('mod', optional(choice('...', 'ref', 'reg')))
        , $.typed_identifier
        , field('definition', optseq('=', $._expression))
      ))), ')'
    )
    // Parameters and outputs of a lambda / test: names being bound.
    , _binding_arg_list: $ => seq(
      '(', optional(listseq1(seq(
        field('mod', optional(choice('...', 'ref', 'reg')))
        , alias($._binding_typed_identifier, $.typed_identifier)
        , field('definition', optseq('=', $._expression))
      ))), ')'
    )

    // An assignment TARGET: a name (optionally timed) or a field, selector,
    // bit-select or attribute chain ROOTED AT A NAME. The same node kinds as
    // the expression chains (`dot_expression`, `member_selection`, ...), but
    // the head can never be a call, a parenthesized expression, a tuple, a
    // literal or a type word: `f(x).a = 3`, `f(x)[0] = 3`, `f(x)#[0] = 3`,
    // `(a+b).c = 3` and `U8(x)#[0] = 1` are errors (prpparse agrees).
    , _lvalue_target: $ => choice(
      $.identifier
      , $.timed_identifier
      , alias($._target_dot, $.dot_expression)
      , alias($._target_member, $.member_selection)
      , alias($._target_bit, $.bit_selection)
      , alias($._target_attr, $.attribute_read)
    )
    , _target_dot: $ => dottedChain($._lvalue_target, $.identifier, 'dot', 'dot_sub')
    , _target_member: $ => memberSelection($, $._lvalue_target)
    , _target_bit: $ => bitSelection($, $._lvalue_target)
    , _target_attr: $ => attributeRead($, $._lvalue_target)

    , _complex_identifier: $ => choice(
      $.identifier
      , $.dot_expression
      , $.member_selection
      , $.bit_selection
      , $.attribute_read
      , $.timed_identifier
    )
    , timed_identifier: $ => prec(1, seq(
      field('identifier', $.identifier)
      , $._timing_sequence
    ))
    , _timing_sequence: $ => seq(
      '@'
      , field('timing', $.timing_slot)
    )
    // Bracketed slot used by `stage[…]` on the LHS of a pipelined
    // assignment and by `x@[…]` on a value read.
    //   * In `stage[N]`, `N` is the number of pipeline stages the called
    //     `pipe` should insert (a `pipe` may accept a range; `stage[N]`
    //     picks within it). `stage[]` lets the toolchain pick a default.
    //   * In `x@[N]`, `N` is the absolute cycle (counted from the
    //     enclosing `mod`/`pipe`'s inputs) at which the value should be
    //     read or produced — a compile-time typecheck. `x@[]` opts out
    //     of that check.
    , timing_slot: $ => seq(
      '['
      , optional(choice(
        field('index', $._expression)
        , field('range', $.selection_range)
      ))
      , ']'
    )

    , typed_identifier: $ => prec.left('typed_identifier', seq(
      field('identifier', $.identifier)
      , optional($._timing_sequence)
      , field('type', optional($.type_cast))
    ))
    // Like typed_identifier but the type is mandatory: a bare `name:type`
    // tuple-type field (see _tuple_item).
    , typed_field: $ => seq(
      field('identifier', choice($.identifier, alias($._field_word, $.identifier), alias($._anon_slot, $.identifier)))
      , field('type', $.type_cast)
    )
    // The anonymous tuple-type entry marker `_` of `_:T` (`(_:U4, _:U8)`): a lone
    // `_` is no name anywhere else (see `identifier`), so it only parses here.
    , _anon_slot: $ => token('_')
    // A list of names being bound (`mut (a, b)`, `for (i, x) in ...`).
    , typed_identifier_list: $ => listseq1(field('item', alias($._binding_typed_identifier, $.typed_identifier)))
    , _binding_typed_identifier: $ => bindingTypedIdentifier($)
    // A bound name that carries a type (`x:U8`, `x@[1]:U8`, `x::[attr]`).
    , _binding_typed_name: $ => prec.left('typed_identifier', seq(
      field('identifier', reserved('binding', $.identifier))
      , optional($._timing_sequence)
      , field('type', $.type_cast)
    ))
    // Generic-parameter DECLARATION list (`<T, K=1>`). A typed_identifier
    // that may carry a `= default`. The default is a generic ARGUMENT (see
    // _generic_value) — never a full expression: a full expression would
    // swallow the closing `>` as a greater-than operator. Aliased to
    // typed_identifier so consumers see one node kind with an optional
    // `definition` field (matches prpparse).
    , generic_identifier: $ => prec.left('typed_identifier', seq(
      field('identifier', reserved('binding', $.identifier))
      , optional($._timing_sequence)
      , field('type', optional($.type_cast))
      , field('definition', optseq('=', $._generic_value))
    ))
    , generic_identifier_list: $ => listseq1(field('item', alias($.generic_identifier, $.typed_identifier)))
    // Call-site generic binding list (`f<Signed,String>(…)`): one generic
    // argument per generic name, in declaration order. Not typed_identifiers
    // — but a binding may be NAMED (`f<T=U8, K=10>(…)`), following the same
    // naming rules as call arguments; aliased to arg_assignment so the
    // named-argument machinery applies (matches prpparse). The rvalue is a
    // generic argument, not an expression, for the same closing-`>` reason
    // as above.
    , generic_type_list: $ => listseq1(field('item', choice(
      $._generic_value
      , alias($.generic_assignment, $.arg_assignment)
    )))
    , generic_assignment: $ => seq(
      field('lvalue', choice($.identifier, alias($._field_word, $.identifier)))
      , '='
      , field('rvalue', $._generic_value)
    )
    // A generic ARGUMENT — at the call site (`f<N=…>`) and as a parameter
    // default (`<N=…>`) alike (06-functions.md): a type, a literal, a name, a
    // dotted field (`cfg.w`; all through the type grammar), or a POSTFIX
    // attribute read of a (dotted) name written bare: `x.[bits]`,
    // `cfg.w.[max]`. It surfaces as the same `attribute_read` node an
    // expression produces (argument = identifier / dot_expression), so
    // consumers lower it like any attribute read (matches prpparse).
    // Anything else is parenthesized: an operator expression (`<N=(W*2)>` —
    // bare, `>`/`>>` would be ambiguous) or a call (`<N=(g(x))>`; a bare
    // `g(x)` is a type-position call). The `x` / `x.y` prefix is shared with
    // the type path (`expression_type`/`dot_expression_type`) until `.[`
    // decides it — a declared GLR conflict.
    , _generic_value: $ => choice(
      $._type
      , alias($._negative_generic_value, $.expression_type)
      , alias($.generic_attribute_read, $.attribute_read)
    )
    , generic_attribute_read: $ => seq(
      field('argument', choice(
        $.identifier
        , alias($.generic_dotted_name, $.dot_expression)
        // `<N=U8.[max]>`: an attribute of a type word (see `attribute_read`)
        , $._type_word_name
      ))
      , field('attrs', repeat1(seq('.', $.attribute_list)))
    )
    // Same shape as an expression `dot_expression` (only the head is an
    // `item` field), so `cfg.w.[max]` reads like its expression spelling.
    , generic_dotted_name: $ => seq(
      field('item', $.identifier)
      , repeat1(seq('.', $.identifier))
    )

    // Expressions. Built from the tiered binary-expression operand chain:
    // _pri4_operand covers everything tighter than tier-5, _binary_logical
    // is tier-5. Single path eliminates ambiguity between "full expression"
    // and "operand of an outer tier". Tier-5 is aliased to expression_item
    // so the tree shows a visible `expression_item` wrapper.
    , _expression: $ => prec('expression', choice(
      $._pri4_operand
      , alias($._binary_logical, $.expression_item)
    ))
    , member_selection: $ => memberSelection($, $._suffix_head)
    , bit_selection: $ => bitSelection($, $._suffix_head)
    // `x.[bits]`. A bare type word may head it too -- `U8.[max]` reads an
    // attribute of the type -- which is the only suffix a bare type word takes
    // (`U8.x`, `U8[0]`, `U8#[0]` are errors); the read's result is then an
    // ordinary suffix head (`U8.[max]#[0]`).
    , attribute_read: $ => attributeRead($, choice($._suffix_head, $._type_word_name))
    // `expr::[attr=…]` — write-side attribute bracket applied in expression
    // position, e.g. RTL-instantiation port reads like
    // `ram.port[0][addr]::[rdport=0]`. This is what remains of the removed
    // expression-level typecheck (`expr:type` is no longer Pyrope; types
    // appear only at declaration sites and inside tuple types).
    , attribute_set: $ => prec('type_spec', seq(
      field('argument', $._suffix_head)
      , field('attribute', attributeOnly($))
    ))
    , unary_expression: $ => prec.left('unary', seq(
      field('operator', choice(
        alias('!', $.op_log_not)
        , alias('not', $.op_log_not)
        , alias('~', $.op_bit_not)
        , alias('-', $.op_unary_minus)
        , alias('...', $.op_spread)
      ))
      , field('argument', $._pri1_operand)
    ))
    // expression_item: flat chain of same-priority operators.
    // Each tier admits only TIGHTER-tier operands, so `a + b + c` parses as a
    // single node with three operands (not ((a + b) + c)).
    , expression_item: $ => choice(
      $._binary_times
      , $._binary_other
      , $._binary_step
      , $._binary_compare
      , $._binary_logical
    )
    // Pyrope priority 2: *, /, %
    , _binary_times: $ => prec.left('binary_times', seq(
      field('operand', $._pri1_operand)
      , repeat1(seq(
        field('operator', $.binary_times_op)
        , field('operand', $._pri1_operand)
      ))
    ))
    , binary_times_op: $ => choice(
      alias('*', $.op_mul)
      , alias('/', $.op_div)
      , alias('%', $.op_mod)
    )
    // Pyrope priority 3: +, -, <<, >>, &, |, ^, ..=, ..<, ..+
    , _binary_other: $ => prec.left('binary_other', seq(
      field('operand', $._pri2_operand)
      , repeat1(seq(
        field('operator', $.binary_other_op)
        , field('operand', $._pri2_operand)
      ))
    ))
    , binary_other_op: $ => choice(
      alias('+', $.op_add)
      , alias('-', $.op_sub)
      , alias('++', $.op_tuple_concat)
      , alias('<<', $.op_shl)
      , alias('>>', $.op_sra)
      , alias('&', $.op_bit_and)
      , alias('|', $.op_bit_or)
      , alias('^', $.op_bit_xor)
      , alias('..=', $.op_range_inclusive)
      , alias('..<', $.op_range_exclusive)
      , alias('..+', $.op_range_count)
    )
    // `step` binds looser than the range/arith operators, so `a..=b step c`
    // parses as `(a..=b) step c` — one range-with-stride node, not a flat
    // mixed-operator chain. Modeled as its own tier between `binary_other`
    // and `binary_compare`. Per 04-variables.md the step amount must be a
    // positive integer; `step -1` still parses (unary minus on the operand)
    // and is rejected by the semantic pass.
    , _binary_step: $ => prec.left('binary_step', seq(
      field('operand', $._pri3_operand)
      , repeat1(seq(
        field('operator', $.binary_step_op)
        , field('operand', $._pri3_operand)
      ))
    )
    )
    , binary_step_op: $ => alias('step', $.op_step)
    // Pyrope priority 4: <, <=, >, >=, ==, !=, has/in/case/does/equals
    // Overparse: chained comparisons must all point the same direction;
    // `a <= b > c` parses but is illegal. See grammar_overparse.md #6.
    //
    // `_spaced_lt` (external, zero width) marks a `<` with whitespace before
    // it (owner ruling 107): such a `<` is always a comparison, never the
    // opening of a call-site generic list (`genericTupleCall` needs the `<`
    // glued to the callee: `f<N=3>(x)`). The scanner returns it only when a
    // single `<` follows the blanks, so `a < b > (c)` is a comparison chain.
    , _binary_compare: $ => prec.left('binary_compare', seq(
      field('operand', $._pri_step_operand)
      , repeat1(seq(
        optional($._spaced_lt)
        , field('operator', $.binary_compare_op)
        , field('operand', $._pri_step_operand)
      ))
    ))
    , binary_compare_op: $ => choice(
      alias('<', $.op_lt)
      , alias('<=', $.op_le)
      , alias('>', $.op_gt)
      , alias('>=', $.op_ge)
      , alias('==', $.op_eq)
      , alias('!=', $.op_ne)
      , alias('has', $.op_has)
      , alias('in', $.op_in)
      , alias('case', $.op_case)
      , alias('does', $.op_does)
      , alias('equals', $.op_equals)
    )
    // Pyrope priority 5: and, or, implies
    , _binary_logical: $ => prec.left('binary_logical', seq(
      field('operand', $._pri4_operand)
      , repeat1(seq(
        field('operator', $.binary_logical_op)
        , field('operand', $._pri4_operand)
      ))
    ))
    , binary_logical_op: $ => choice(
      alias('and', $.op_log_and)
      , alias('or', $.op_log_or)
      , alias('implies', $.op_implies)
    )
    // Operand tiers: each level adds its own expression_item kind.
    , _pri1_operand: $ => prec('expression', choice(
      $.attribute_set
      , $.unary_expression
      , $.if_expression
      , $.match_expression
      , $._restricted_expression
      , $.scope_statement
    ))
    , _pri2_operand: $ => prec('expression', choice(
      $._pri1_operand
      , alias($._binary_times, $.expression_item)
    ))
    , _pri3_operand: $ => prec('expression', choice(
      $._pri2_operand
      , alias($._binary_other, $.expression_item)
    ))
    , _pri_step_operand: $ => prec('expression', choice(
      $._pri3_operand
      , alias($._binary_step, $.expression_item)
    ))
    , _pri4_operand: $ => prec('expression', choice(
      $._pri_step_operand
      , alias($._binary_compare, $.expression_item)
    ))
    // Dot tails are identifiers only: positional entries are selected with
    // `[N]`, never `.N` (named fields are unordered, so a numeric position
    // has no meaning for them).
    , dot_expression: $ => dottedChain(
      $._suffix_head
      , $.identifier
      , 'dot'
      , 'dot_sub'
    )
    // Standalone expression operand. Allowed anywhere a leaf expression is
    // expected (operand position, statement-level expression).
    , _restricted_expression: $ => prec('expression', choice(
      $._retired_enum_expression
      , $._complex_identifier
      , $._type_word_name
      , alias($._type_word_call, $.function_call_expression)
      , $.constant
      , $.function_call_expression
      , $.lambda
      , $.tuple
      , $.tuple_sq
    ))
    // Single-expression parenthesized grouping. Lets `(expr).foo`,
    // `(expr)#[..]`, `(expr):type` work where the parens are just grouping
    // one expression. Distinct from `tuple` so that multi-element /
    // assignment-form tuples cannot serve as suffix heads (the RD-lookahead
    // problem only bites for those).
    , paren_group: $ => seq('(', $._expression, ')')
    // Head of a suffix chain (`.field`, `[i]`, `#[bits]`, `.[attr]`, `:type`).
    // Includes bare `tuple` / `tuple_sq` so UFCS forms `(a,b).foo()` and
    // `[x,y,z].foo()` parse — needed for receiver-style calls on tuple /
    // array literals. Single-expression parens still resolve via
    // `paren_group` (which has higher static precedence than `tuple` with a
    // single item via the listed conflict).
    , _suffix_head: $ => prec('expression', choice(
      $._complex_identifier
      , $.constant
      , $.function_call_expression
      // A conversion call's result is a value: `U8(x)#[0]`, `U8(x).f`.
      , alias($._type_word_call, $.function_call_expression)
      , $.lambda
      , $.paren_group
      , $.tuple
      , $.tuple_sq
    ))
    , lambda: $ => prec.right(seq(
      // `pub comb f() …` — visible outside this file.
      field('pub', optional(alias('pub', $.pub_modifier)))
      , field('func_type', choice(
        alias('comb', $.comb_lambda)
        , alias('mod', $.mod_lambda)
        , $.pipe_lambda
        , $.fluid_lambda
      ))
      , field('name', $.identifier)
      , $.function_definition_decl
      // A `{` after the signature is always the body, also on a later line
      // (Allman style); a body-less lambda followed by a separate block is
      // never the reading, not even where the block could belong to an
      // enclosing header (`if mod f() -> (g) { .. }` has no `if` block),
      // as in prpparse parse_lambda. The marker `_lambda_body` keeps
      // src/scanner.c from ending the statement at a newline before that
      // `{`, and `prec.right` makes the body greedy.
      , optional($._lambda_body)
      , field('code', optional($.scope_statement))
    ))
    // `pipe[N]` / `pipe[A..=B]` — a single depth expression or range slot.
    , pipe_lambda: $ => prec.right(seq('pipe', field('depth', optional($.select))))
    // `fluid` / `fluid[N]` / `fluid[lat=1..=70, ordered=true]` — like `pipe`
    // but the bracket admits a named-attribute list, not just a depth.
    , fluid_lambda: $ => prec.right(seq('fluid', field('config', optional($.attribute_sq))))
    // Operators
    , assignment_operator: $ => choice(
      alias('=', $.assign)
      , alias('+=', $.assign_add)
      , alias('++=', $.assign_tuple_concat)
      , alias('-=', $.assign_sub)
      , alias('*=', $.assign_mul)
      , alias('/=', $.assign_div)
      , alias('|=', $.assign_bit_or)
      , alias('&=', $.assign_bit_and)
      , alias('^=', $.assign_bit_xor)
      , alias('<<=', $.assign_shl)
      , alias('>>=', $.assign_sra)
      , alias('or=', $.assign_log_or)
      , alias('and=', $.assign_log_and)
    )

    // Selects
    // A selector takes a single expression (integer, string, range, or any
    // expression producing one of those, including a conditional). Multi-entry
    // tuple indices like `a[0,1]` or `foo#[0,2,3]` are intentionally not
    // accepted — the ordering of a bit/field set is ambiguous; use one
    // bit-range assignment per group instead.
    , select: $ => seq(
      '['
      , choice(
        field('index', $._expression)
        , field('range', $.selection_range)
      )
      , ']'
    )
    , selection_range: $ => choice(
      alias('..', $.open_all)
      , seq(field('open_from', $._expression), '..')
      , seq('..=', field('from_zero_inclusive', $._expression))
      , seq('..<', field('from_zero_exclusive', $._expression))
    )

    // Types
    , type_cast: $ => prec.left('type_cast', typedOrAttributed($))
    , _type: $ => prec('type', choice(
      $._primitive_type
      , $.array_type
      , $.expression_type
      , $._timing_sequence
      , $.lambda_type
    ))
    // Body-less lambda SIGNATURE in type position — the typed interface of a
    // `cpp()` binding or any lambda-valued field:
    //   type GcdModel = ( call_method1: comb(a:U8, b:U3) -> (foo:U8, bar:U33) )
    // (07-typesystem.md "The typed interface is the source of truth"). The
    // `type X = comb(...)` statement form keeps its own func_type branch in
    // type_statement (same shape as prpparse); this rule covers the nested
    // type positions — hence prec(-1), so the statement branch wins the
    // overlap.
    , lambda_type: $ => prec(-1, seq(
      field('func_type', choice(
        alias('comb', $.comb_lambda)
        , alias('mod', $.mod_lambda)
        , $.pipe_lambda
        , $.fluid_lambda
      ))
      , $.function_definition_decl
    ))
    , expression_type: $ => prec('expression_type', choice(
      $._retired_enum_expression
      , $.identifier
      , alias($._type_constant, $.constant)
      , $.tuple
      , $.if_expression
      , $.match_expression
      , $.dot_expression_type
      , $.function_call_type
    ))
    // A dotted type NAME (`x:pkg.T`, `x:a.b.c`): every part is a name, so a
    // field of a tuple type (`reg f:(a:Bool).flags`, `x:(a:U8).b`), of a call
    // (`x:f(a).b`) or of a literal is a syntax error (prpparse agrees). The
    // parts keep their `expression_type` wrapper; the chain nests to the right.
    , dot_expression_type: $ => prec.right('dot_type', seq(
      field('item', alias($._type_name_part, $.expression_type))
      , '.'
      , field('item', choice(
        alias($._type_name_part, $.expression_type)
        , alias($._type_name_chain, $.expression_type)
      ))
    ))
    , _type_name_part: $ => $.identifier
    , _type_name_chain: $ => $.dot_expression_type
    // Type-position calls (`enum(str:String)`, generic instantiation) keep
    // the general tuple: their items are type fields, not call bindings.
    , function_call_type: $ => tupleCall($, 'function_call_type', $.tuple)
    // Array length is one expression per dimension, or empty `[]` to defer
    // sizing to the initializer. Multi-dim is chained (`[8][8]u16`, not
    // `[8,8]u16`), so each length slot is either empty or a single
    // expression / range.
    , array_type: $ => prec.left('array_type', seq(
      field('length', $.array_length)
      , optional(field('base', choice(
        $._primitive_type
        , $.array_type
        , $.lambda
        , $.expression_type
      )))
    ))
    , array_length: $ => seq(
      '['
      , optional(choice(
        field('index', $._expression)
        , field('range', $.selection_range)
      ))
      , ']'
    )
    // Built-in types. The type words are CAPITALIZED and RESERVED: `U<N>`
    // (unsigned, N bits: `U1`, `U8`, `U1333`), `S<N>` (signed), `Unsigned`,
    // `Signed`, `Bool`, `String`, `Clock`, `Reset`. They are never
    // identifiers (see `identifier` and `reserved`): a variable, port,
    // parameter or field spelled like one is written in backticks (`` `U4` ``),
    // and the backticked spelling is then an ordinary name (never the type) in
    // every position. The old lowercase spellings (`u8`, `s4`, `i32`, `bool`,
    // `boolean`, `unsigned`, `signed`, `string`) are ORDINARY identifiers (owner
    // ruling 2026-09-30): they are no type here, so `x:u8` / `u8(x)` parse as a
    // name used as a type expression / call and lhd reports "`u8` was renamed
    // `U8`" when it is not a declared name. prpparse (lexer.cpp,
    // Token_kind::type_word) reserves the same type words.
    , _primitive_type: $ => choice(
      $.uint_type
      , $.sint_type
      , $.bool_type
      , $.string_type
      , $.clock_type
      , $.reset_type
    )
    , uint_type: $ => seq(
      choice('Unsigned', $._UN)
      // Optional bitwidth/range constraint list: `Unsigned(bits=8, max=300)`.
      , optional(field('constraint', $.tuple))
    )
    , sint_type: $ => seq(
      choice('Signed', $._SN)
      // Optional bitwidth/range constraint list: `Signed(bits=8)`.
      , optional(field('constraint', $.tuple))
    )
    , bool_type: $ => 'Bool'
    , string_type: $ => 'String'
    , clock_type: $ => 'Clock'
    , reset_type: $ => 'Reset'
    // The sized type words, reserved (see `identifier`).
    , _UN: $ => token(/U[0-9]+/)
    , _SN: $ => token(/S[0-9]+/)
    // Reserved for future placeholders: _[digit][alnum]* (Unicode categories).
    , _reserved_placeholder_word: $ => token(/_[\p{Nd}][\p{L}\p{Nd}]*/)
    // A type word USED AS A VALUE -- an operand `x does U8` / `a equals S4`,
    // a tuple value `(t=U8)`, a conversion callee `U8(x)` / `Bool(x)` /
    // `Unsigned(bits=8)` -- is an `identifier` node (lhd reads its text: a
    // bare `U8` is the type, a backticked `` `U8` `` a name). It is never a
    // name being bound or a field name: a declaration, parameter, tuple field,
    // named argument, `.U8` selector or assignment target spelled with a bare
    // type word is a syntax error (prpparse agrees). It heads a suffix chain
    // only through an attribute read (`U8.[max]`, see `attribute_read`):
    // `U8.x`, `U8[0]`, `U8#[0]` and `U8@[1]` are errors. prec(-1): where a
    // position admits both a type and a value, `U8(` continues the
    // constrained `uint_type`/`sint_type`.
    , _type_word_name: $ => prec(-1, alias(choice(
      $._UN, $._SN, 'Unsigned', 'Signed', 'Bool', 'String', 'Clock', 'Reset'
    ), $.identifier))
    // The conversion call `U8(x)` / `Bool(y)` in value position: a
    // `function_call_expression` whose callee is the type word. Its result is
    // an ordinary value, so it heads any suffix chain (`U8(x)#[0]`,
    // `U8(x).f`; see `_suffix_head`). In a TYPE position `U8(...)` is the
    // constrained `uint_type`.
    , _type_word_call: $ => prec('function_call_expression', seq(
      field('function', $._type_word_name)
      , field('argument', $.arg_tuple)
    ))

    // Identifiers
    , identifier: $ => token(
      choice(
        // \p{L}  : Letter
        // \p[Nd} : Decimal Digit Number
        // `$` is NOT an identifier character: `foo$bar` must be quoted in backticks
        // (prpparse lexer.cpp is_ident_cont agrees).
        //
        // The sized type words `U<N>`/`S<N>` (`U4`, `S20`: a `U` or `S`
        // followed by ASCII digits only) are RESERVED TYPE KEYWORDS, never
        // names: a bare `U4` is always the type (`uint_type`/`sint_type`), so
        // it cannot be a variable, port, parameter or field name. A name
        // spelled like one must be written in backticks (`` `U4` ``), and the
        // backticked spelling is then an ordinary identifier (never the type)
        // in every position. The two branches below are the letter-led words
        // MINUS those spellings: a word led by any letter but U/S, or a `U`/`S`
        // alone or followed by something other than only ASCII digits (`U`,
        // `Sx`, `U8x`, `S_0`, `U4é`). The other type words (`Unsigned`,
        // `Signed`, `Bool`, `String`, `Clock`, `Reset`) are excluded through
        // `reserved` below. prpparse lexer.cpp (is_type_word) reserves the same
        // words.
        /[^\P{L}US][\p{L}\p{Nd}_]*/
        , /[US]([0-9]*([\p{L}_]|[^\P{Nd}0-9])[\p{L}\p{Nd}_]*)?/
        // A lone `_` is not an identifier. _[digit][alnum]* is reserved
        // through the reserved sets; imported names such as `_0` need backticks.
        // A later underscore makes a different name (`_1_a` remains legal).
        , /_[\p{L}\p{Nd}_][\p{L}\p{Nd}_]*/
        // To support all Verilog identifiers. Example:
        //   `foo is . strange!\nidentifier` = 4
        // The escapes are the string escapes (STRING_ESCAPE): `` `a\`b` ``;
        // The name must be nonempty; any other backslash sequence is an error.
        , seq(
          '`'
          , repeat1(choice(prec(1, STRING_ESCAPE), /[^`\\\n]+/))
          , '`'
        )
      )
    )

    // Constants
    , constant: $ => choice(
      $.integer_literal
      , $.bool_literal
      , $._string_literal
    )
    // A constant in a type position (`expression_type`): never negative.
    , _type_constant: $ => choice(
      alias($._type_integer, $.integer_literal)
      , $.bool_literal
      , $._string_literal
    )

    // A negative literal is a generic value (`f<N=-3>(y=1)`, owner ruling 102)
    // though no type (`x:-3` is an error); it reads like `<N=3>`
    // (`expression_type` > `constant` > `integer_literal`), as in prpparse.
    , _negative_generic_value: $ => alias($._negative_constant, $.constant)
    , _negative_constant: $ => alias($._negative_number, $.integer_literal)

    // Numbers. A negative literal is one token (`_negative_number`, preferred
    // over the '-' operator + literal via prec(2)). It is not a type: a type
    // position takes only the non-negative forms (`_type_integer`); prpparse
    // agrees ("expected a type").
    , integer_literal: $ => choice(
      $._type_integer
      , $._negative_number
    )
    , _type_integer: $ => choice(
      $._simple_number
      , $._scaled_number
      , $._hex_number
      , $._decimal_number
      , $._octal_number
      , $._binary_number
    )
    // Underscores are digit-group separators with no meaning and are allowed
    // inside every numeric body (02-basics.md): the bare decimal, the
    // K/M/G/T magnitude, and all radix prefixes. `1_000` / `1_000K` /
    // `0xF_a_0` are each a single token.
    , _simple_number: $ => token(/0|[1-9][0-9_]*/)
    , _scaled_number: $ => token(/(0|[1-9][0-9_]*)[KMGT]/)
    // The sign letters `u`/`s` combine with every radix letter (owner ruling
    // 109): `0ux`/`0sx`, `0ud`/`0sd`, `0uo`/`0so`, `0ub`/`0sb`; plain `0x`,
    // `0o`, `0d` and decimals are unsigned. A sign without a radix letter
    // (`0s12`) is an error: `0` then the plain name `s12` (prpparse:
    // "missing-radix").
    , _hex_number: $ => token(/0(s|S|u|U)?(x|X)[0-9a-fA-F][0-9a-fA-F_]*/)
    , _decimal_number: $ => token(/0((s|S|u|U)?(d|D))?[0-9][0-9_]*/)
    , _octal_number: $ => token(/0(s|S|u|U)?(o|O)[0-7][0-7_]*/)
    // Binary literals require explicit signedness: 0ub / 0sb, never 0b.
    , _binary_number: $ => token(/0(s|S|u|U)(b|B)[0-1\?][0-1_\?]*/)
    , _negative_number: $ => token(prec(2, choice(
      /-(0|[1-9][0-9_]*)[KMGT]?/
      , /-0(s|S|u|U)?(x|X)[0-9a-fA-F][0-9a-fA-F_]*/
      , /-0((s|S|u|U)?(d|D))?[0-9][0-9_]*/
      , /-0(s|S|u|U)?(o|O)[0-7][0-7_]*/
      , /-0(s|S|u|U)(b|B)[0-1\?][0-1_\?]*/
    )))

    // Booleans
    // Keywords (so reserved where a name is bound, and never an assignment
    // target: `true = 3` is an error; prpparse agrees).
    , bool_literal: $ => choice('true', 'false')

    // Reserved for future validity checks (`foo?`), never accepted today.
    // Keep this node only behind `_never` for existing formatter consumers.
    , unknown_literal: $ => token('?')

    // Strings
    , _string_literal: $ => choice(
      $.string_literal
      , $.interpolated_string_literal
    )
    , string_literal: $ => token(/\'[^\'\n]*\'/)
    // The string's own pieces are token.immediate: no extras (whitespace or a
    // comment) may appear between them, so `"/* a"` is a string holding
    // `/* a`, never the start of a block comment that swallows the closing
    // quote. The text runs (`[^{"\\\n]+`, plus each literal `{{`) are the external `_string_content`:
    // src/scanner.c scans them, and never a comment, wherever they are valid.
    // Inside `{...}` interpolation, normal extras (comments too) apply again.
    , interpolated_string_literal: $ => seq(
      '"'
      , repeat(
        choice(
          token.immediate(prec(2, STRING_ESCAPE))
          , $._string_content
          // A hole: `{expr}` or `{expr:spec}`. It always holds an expression:
          // `"{}"`, `"{ }"` and `"{:b}"` are syntax errors (prpparse agrees).
          // `{{` and `}}` never reach here: they are literal text, and a lone
          // `}` outside a hole is an error (src/scanner.c).
          , seq(token.immediate('{'), $._expression, optional($._format_spec), '}')
        )
      )
      , token.immediate('"')
    )
    // `{n:b}` format spec. A comment after it is a `comment` node, never spec
    // text: `"{n:b /* c */}"` and `"{n:b // c` newline `}"` both hold spec `:b`
    // (plus trailing blanks), so the spec stops before `//` or `/*`. The spec
    // is NOT empty: after the `:` and any blanks comes a character other than
    // `}`, `"`, `{`, a newline or a comment opener, so `"{x:}"`, `"{x: }"` and
    // `"{x:/* c */b}"` are syntax errors; a spec never holds `{` or a
    // backslash (`"{x:\x4}"` is an error; prpparse agrees: parser.cpp
    // check_format_spec). prpparse builds no spec node: lhd reads the text
    // between the hole's expression and its `}` with comments removed.
    , _format_spec: $ => token(/:[ \t]*([^}"{\n\/ \t\\]|\/[^}"{\n\/*\\])([^}"{\n\/\\]|\/[^}"{\n\/*\\])*/)

    // Special
    // Only ASCII blanks separate tokens (owner ruling 108): a non-ASCII space
    // (NBSP, U+3000, a zero-width space, a BOM) outside a string or a comment
    // is an error (prpparse agrees: "non-ASCII space").
    , _space: $ => token(/[ \t\n\r\f\v]/)
    // `comment` (both `// ...` and nesting `/* ... */`) is an external token:
    // see `externals` above and src/scanner.c.
    , _semicolon: $ => choice($._automatic_semicolon, ';')
  }
});
