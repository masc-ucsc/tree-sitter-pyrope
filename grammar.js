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
    , $.typed_identifier
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

// Overparse: `::[...]` and `:Type:[...]` parse anywhere `type_cast` is
// admitted, including non-declaration contexts. See grammar_overparse.md #1.
function typedOrAttributed($) {
  return choice(
    seq(
      field('type', $._type)
      , field('attribute', optional(attributeSuffix($)))
    )
    , field('attribute', attributeSuffix($))
  );
}

function tupleCall($, precedence) {
  return prec(precedence, seq(
    field('function', $._complex_identifier)
    , field('argument', $.tuple)
  ));
}

function dottedChain(item, tail, precedence, subprecedence, associativity = prec.left) {
  return associativity(precedence, seq(
    field('item', item)
    , repeat1(associativity(subprecedence, seq('.', tail)))
  ));
}

// Grammar
module.exports = grammar({
  name: 'pyrope'

  , externals: $ => [$._automatic_semicolon]
  , conflicts: $ => [
    [$._complex_identifier, $.typed_identifier]
    , [$._complex_identifier, $.expression_type]
    , [$._tuple_item, $._restricted_expression]
    , [$.lambda]
    , [$.timed_identifier, $.typed_identifier]
    , [$.assignment, $._restricted_expression]
    , [$.lvalue_item, $._restricted_expression]
    , [$.assignment, $.lvalue_item, $._restricted_expression]
    , [$.lvalue_item, $.typed_identifier_list]
    , [$._binary_times, $._pri2_operand]
    , [$._binary_other, $._pri3_operand]
    , [$._binary_compare, $._pri4_operand]
    , [$._binary_times]
    , [$._binary_other]
    , [$._binary_compare]
    , [$._binary_logical]
    , [$._expression, $._binary_logical]
    , [$._expression, $.array_length]
    , [$.tuple_sq, $.array_length]
    , [$.assignment, $._suffix_head]
    , [$.assignment, $.lvalue_item, $._suffix_head]
    , [$.lvalue_item, $._suffix_head]
    , [$.paren_group, $.tuple]
    , [$._tuple_item, $.paren_group]
    , [$.named_lvalue, $._complex_identifier, $.typed_identifier]
    , [$._tuple_item, $.array_length]
  ]
  , extras: $ => [$._space, $.comment]
  , word: $ => $.identifier

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
      , 'binary_other'     // Pyrope priority 3: +, -, ++, <<, >>, &, |, ^, !&, !|, !^, ..=, ..<, ..+, step
      , 'binary_compare'   // Pyrope priority 4: <, <=, ==, !=, >=, >, has/in/is/case/does/equals
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
  ]

  , supertypes: $ => []

  , rules: {
    // Top
    description: $ => repseq($._statement, repeat(';'))

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
      , $.spawn_statement
      , $.loop_statement
      , seq($._expression, $._semicolon)
      // Verification Only
      , $.test_statement
      , $.type_statement
      , $.impl_statement
    )
    )
    , scope_statement: $ => seq(
      '{'
      , repseq($._statement)
      , '}'
    )
    , declaration_statement: $ => seq(
      field('decl', $.var_or_let_or_reg)
      , choice(
        seq('(', field('lvalue', $.typed_identifier_list), ')')
        , field('lvalue', $.typed_identifier)
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
    , return_statement: $ => seq(
      'return'
      , field('argument', optional($._expression))
      , $._semicolon
    )
    , stmt_list: $ => prec.left('_tuple_list', seq(
      field('item', $._tuple_item)
      , repeat(seq(repeat1(';'), field('item', $._tuple_item)))
    ))
    , _if_branch: $ => seq(
      field('init', statementInit($))
      , field('condition', $._expression)
      , field('code', $.scope_statement)
    )
    , if_expression: $ => prec('statement', seq(
      optional('unique')
      , 'if'
      , $._if_branch
      , field('elif', repseq('elif', $._if_branch))
      , field('else', optseq('else', $.scope_statement))
    ))
    , _attr_prefix: $ => seq('::', $.attribute_sq)
    , _init_clause: $ => seq($.stmt_list, ';')
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
      , field('code', $.scope_statement)
    )
    , while_statement: $ => seq(
      'while'
      , field('attributes', optional($._attr_prefix))
      , field('init', statementInit($))
      , field('condition', $._expression)
      , field('code', $.scope_statement)
    )
    , loop_statement: $ => prec('statement', seq(
      'loop'
      , field('attributes', optional($._attr_prefix))
      , field('init', optional($.stmt_list))
      , field('code', $.scope_statement)
    ))
    , match_expression: $ => seq(
      'match'
      , field('init', statementInit($))
      , field('condition', $._expression)
      , '{'
      , field('cases', repeat(seq(
        field('condition', seq(optional(choice(
          'and', 'or', '&', '^', '|', '~&', '~^', '~|',
          '<', '<=', '>', '>=', '==', '!=', 'has', 'case', 'in',
          'equals', 'does', 'is'
        )), $._expression))
        , field('code', $.scope_statement)
      )))
      , 'else'
      , field('else_code', $.scope_statement)
      , '}'
    )
    , test_statement: $ => seq(
      'test'
      , field('args', $.expression_list)
      , field('code', $.scope_statement)
    )
    , type_statement: $ => seq(
      'type'
      , field('name', $.identifier)
      , field('generic', optseq('<', $.typed_identifier_list, '>'))
      , choice(
        field('definition', $.tuple),  // trait definition: type Name ( ... )
        seq('=',
          choice(
            seq(
              field('func_type', choice(
                alias('comb', $.comb_lambda)
                , alias('mod', $.mod_lambda)
                , $.pipe_lambda
              ))
              , $.function_definition_decl
            )
            , field('alias', $._type)  // type alias: type Name = Type
          )
        )
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
    , function_call_expression: $ => tupleCall($, 'function_call_expression')
    // Tuple
    , tuple: $ => seq('(', optional($._tuple_list), ')')

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
      field('lvalue', $.identifier)
      , '='
      , field('rvalue', choice($._expression, $.ref_identifier))
    )

    , _tuple_list: $ => prec('_tuple_list', listseq1(field('item', $._tuple_item)))
    // Overparse: tuple-literal fields require a kind keyword
    // (`mut`/`const`/`reg`/`comb`/...); bare `a = 1` parses here because the
    // same node also models named call arguments. See grammar_overparse.md #3.
    , _tuple_item: $ => choice(
      $.ref_identifier
      , $._expression
      , $.assignment
      , seq(
        field('decl', $.var_or_let_or_reg)
        , field('lvalue', $.typed_identifier)
      )
      // Positional tuple field with explicit mutability override:
      // `(1, const 3)` / `(mut 3, 4)`. Per 04-variables.md, only `const` /
      // `mut` are meaningful here, but the grammar overparses and accepts
      // any var_or_let_or_reg prefix; the semantic pass narrows it.
      , prec(-1, seq(
        field('decl', $.var_or_let_or_reg)
        , field('value', $._expression)
      ))
      , $.lambda
    )

    // Assignment (single or tuple lvalue)
    , assignment: $ => seq(
      field('decl', optional($.var_or_let_or_reg))
      , choice(
        seq('(', field('lvalue', $.lvalue_list), ')')
        , field('lvalue', $.typed_identifier)
        , seq(
          field('lvalue', $._complex_identifier)
          , field('type', optional($.type_cast))
        )
      )
      , field('operator', $.assignment_operator)
      , field('rvalue', choice(
        $._expression
        , $.enum_definition
        , $.ref_identifier
      ))
    )
    // An lvalue inside a parenthesized destructuring assignment.
    //  * `x` / `x:u8` — bare identifier (optionally typed). Binds the
    //    matching return field BY NAME — the rvalue must have a field
    //    whose name matches `x`. (Mirrors call-site rule for named args.)
    //  * `name = local` — explicit named binding. Local `local` receives
    //    rvalue field `name`. Order on the LHS is irrelevant.
    //  * `obj.field` / `arr[i]` — assign into an existing complex lvalue.
    //    Bound by position (no name to match).
    , lvalue_item: $ => choice(
      $.typed_identifier
      , seq(
        field('identifier', $._complex_identifier)
        , field('type', optional($.type_cast))
      )
      , $.named_lvalue
    )
    , named_lvalue: $ => seq(
      field('name', $.identifier)
      , '='
      , field('lvalue', choice($.typed_identifier, $.dot_expression))
    )
    , lvalue_list: $ => listseq1(field('item', $.lvalue_item))
    , var_or_let_or_reg: $ => seq(
      field('comptime', optional(alias('comptime', $.comptime_modifier)))
      , field('storage', choice(
        alias('const', $.const_decl)
        , alias('mut', $.mut_decl)
        , alias('reg', $.reg_decl)
        , $.stage_decl
      ))
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
      field('generic', optseq('<', $.typed_identifier_list, '>'))
      , field('pipe_config', optional($._attr_prefix))
      , field('input', $.arg_list)
      , field('output', optseq('->', choice(
        $.arg_list
        , field('type', $.type_cast)
        , $.typed_identifier
      )))
    )
    , enum_definition: $ => seq(
      'enum'
      , field('input', $.arg_list)
    )
    , enum_assignment: $ => seq(
      'enum'
      , field('name', $.identifier)
      , choice(
        seq('=', field('values', $.tuple)),
        field('body', $.arg_list)
      )
    )
    , spawn_statement: $ => seq(
      'spawn'
      , field('name', $.identifier)
      , seq('=', $.scope_statement)
      , $._semicolon
    )
    , ref_identifier: $ => seq(
      'ref'
      , $._complex_identifier
    )
    , arg_list: $ => seq(
      '(', optional(listseq1(seq(
        field('mod', optional(choice('...', 'ref', 'reg')))
        , $.typed_identifier
        , field('definition', optseq('=', $._expression))
      ))), ')'
    )

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
    , typed_identifier_list: $ => listseq1(field('item', $.typed_identifier))

    // Expressions. Built from the tiered binary-expression operand chain:
    // _pri4_operand covers everything tighter than tier-5, _binary_logical
    // is tier-5. Single path eliminates ambiguity between "full expression"
    // and "operand of an outer tier". Tier-5 is aliased to expression_item
    // so the tree shows a visible `expression_item` wrapper.
    , _expression: $ => prec('expression', choice(
      $._pri4_operand
      , alias($._binary_logical, $.expression_item)
    ))
    , member_selection: $ => prec('member_selection', seq(
      field('argument', $._suffix_head)
      , field('select', prec.right('select', repeat1($.select)))
    ))
    , bit_selection: $ => prec('bit_selection', seq(
      field('argument', $._suffix_head)
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
    ))
    , attribute_read: $ => prec.left('member_selection', seq(
      field('argument', $._suffix_head)
      , field('attrs', repeat1(prec('select', seq(
        '.', $.attribute_list
      ))))
    ))
    // Overparse: `:Type` is only legal at declaration sites; the grammar
    // permits it on any expression. See grammar_overparse.md #2.
    , type_specification: $ => prec('type_spec', seq(
      field('argument', $._suffix_head)
      , ':'
      , typedOrAttributed($)
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
    // Pyrope priority 3: +, -, ++, <<, >>, &, |, ^, !&, !|, !^, ..=, ..<, ..+, step
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
      , alias('!&', $.op_bit_nand)
      , alias('!|', $.op_bit_nor)
      , alias('!^', $.op_bit_xnor)
      , alias('..=', $.op_range_inclusive)
      , alias('..<', $.op_range_exclusive)
      , alias('..+', $.op_range_count)
      , alias('step', $.op_step)
    )
    // Pyrope priority 4: <, <=, >, >=, ==, !=, has/in/is/case/does/equals
    // Overparse: chained comparisons must all point the same direction;
    // `a <= b > c` parses but is illegal. See grammar_overparse.md #6.
    , _binary_compare: $ => prec.left('binary_compare', seq(
      field('operand', $._pri3_operand)
      , repeat1(seq(
        field('operator', $.binary_compare_op)
        , field('operand', $._pri3_operand)
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
      , alias('is', $.op_is)
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
      $.type_specification
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
    , _pri4_operand: $ => prec('expression', choice(
      $._pri3_operand
      , alias($._binary_compare, $.expression_item)
    ))
    , dot_expression: $ => dottedChain(
      $._suffix_head
      , choice(
        $.identifier
        , $.constant
      )
      , 'dot'
      , 'dot_sub'
    )
    // Standalone expression operand. Allowed anywhere a leaf expression is
    // expected (operand position, statement-level expression).
    , _restricted_expression: $ => prec('expression', choice(
      $._complex_identifier
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
      , $.lambda
      , $.paren_group
      , $.tuple
      , $.tuple_sq
    ))
    , lambda: $ => seq(
      field('func_type', choice(
        alias('comb', $.comb_lambda)
        , alias('mod', $.mod_lambda)
        , $.pipe_lambda
      ))
      , field('name', $.identifier)
      , $.function_definition_decl
      , field('code', optional($.scope_statement))
    )
    // `pipe[N]` / `pipe[A..=B]` — a single depth expression or range slot.
    , pipe_lambda: $ => prec.right(seq('pipe', field('depth', optional($.select))))
    // Operators
    , assignment_operator: $ => choice(
      alias('=', $.assign)
      , alias('+=', $.assign_add)
      , alias('-=', $.assign_sub)
      , alias('*=', $.assign_mul)
      , alias('/=', $.assign_div)
      , alias('|=', $.assign_bit_or)
      , alias('&=', $.assign_bit_and)
      , alias('^=', $.assign_bit_xor)
      , alias('<<=', $.assign_shl)
      , alias('>>=', $.assign_sra)
      , alias('++=', $.assign_tuple_concat)
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
    , type_cast: $ => prec.left('type_cast', seq(
      ':'
      , typedOrAttributed($)
    ))
    , _type: $ => prec('type', choice(
      $._primitive_type
      , $.array_type
      , $.expression_type
      , $._timing_sequence
    ))
    , expression_type: $ => prec('expression_type', choice(
      $.identifier
      , $.constant
      , $.tuple
      , $.if_expression
      , $.match_expression
      , $.dot_expression_type
      , $.function_call_type
    ))
    , dot_expression_type: $ => dottedChain(
      $.expression_type
      , field('item', $.expression_type)
      , 'dot_type'
      , 'dot_type_sub'
      , prec.right
    )
    , function_call_type: $ => tupleCall($, 'function_call_type')
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
    , _primitive_type: $ => choice(
      $.uint_type
      , $.sint_type
      , $.bool_type
      , $.string_type
    )
    , uint_type: $ => choice(
      'uint'
      , 'unsigned'
      , token(/u\d+/)
    )
    , sint_type: $ => choice(
      'int'
      , 'integer'
      , token(/[si]\d+/)
    )
    , bool_type: $ => 'bool'
    , string_type: $ => 'string'

    // Identifiers
    , identifier: $ => token(
      choice(
        // \p{L}  : Letter
        // \p[Nd} : Decimal Digit Number
        /[\p{L}_][\p{L}\p{Nd}_$]*/
        // To support all Verilog identifiers. Example:
        //   `foo is . strange!\nidentifier` = 4
        , seq(
          '`'
          , repeat(choice(prec(1, /\\./), /[^`\\\n]+/))
          , '`'
        )
      )
    )

    // Constants
    , constant: $ => choice(
      $.integer_literal
      , $.bool_literal
      , $._string_literal
      , $.unknown_literal
    )

    // Numbers. Each form accepts an optional leading '-' as part of the token
    // so that negative literals are one token (preferred over '-' operator + literal via prec(2)).
    , integer_literal: $ => choice(
      $._simple_number
      , $._scaled_number
      , $._hex_number
      , $._decimal_number
      , $._octal_number
      , $._binary_number
      , $._typed_number
    )
    , _simple_number: $ => token(choice(prec(2, /-(0|[1-9][0-9]*)/), /0|[1-9][0-9]*/))
    , _scaled_number: $ => token(choice(prec(2, /-(0|[1-9][0-9]*)[KMGT]/), /(0|[1-9][0-9]*)[KMGT]/))
    , _hex_number: $ => token(choice(prec(2, /-0(s|S)?(x|X)[0-9a-fA-F][0-9a-fA-F_]*/), /0(s|S)?(x|X)[0-9a-fA-F][0-9a-fA-F_]*/))
    , _decimal_number: $ => token(choice(prec(2, /-0(s|S)?(d|D)?[0-9][0-9_]*/), /0(s|S)?(d|D)?[0-9][0-9_]*/))
    , _octal_number: $ => token(choice(prec(2, /-0(s|S)?(o|O)[0-7][0-7_]*/), /0(s|S)?(o|O)[0-7][0-7_]*/))
    , _binary_number: $ => token(choice(prec(2, /-0(s|S|u|U)?(b|B)[0-1\?][0-1_\?]*/), /0(s|S|u|U)?(b|B)[0-1\?][0-1_\?]*/))
    , _typed_number: $ => token(choice(prec(2, /-(0|[1-9][0-9]*)[sui][0-9]+/), /(0|[1-9][0-9]*)[sui][0-9]+/))

    // Booleans
    , bool_literal: $ => token(choice('true', 'false'))

    // Unknown/uninitialized value
    , unknown_literal: $ => token('?')

    // Strings
    , _string_literal: $ => choice(
      $.string_literal
      , $.interpolated_string_literal
    )
    , string_literal: $ => token(/\'[^\'\n]*\'/)
    , interpolated_string_literal: $ => seq(
      '"'
      , repeat(
        choice(
          prec(2, /\\./)
          , /[^{"\\\n]+/
          , seq('{', optional($._expression), optional($._format_spec), '}')
        )
      )
      , '"'
    )
    , _format_spec: $ => token(/:[^}"\n]*/)

    // Special
    , _space: $ => token(/[\s\p{Zs}\uFEFF\u2060\u200B]/)
    , comment: $ => token(choice(
      /\/\/.*/
      , seq('/*', /[^*]*\*+([^/*][^*]*\*+)*/, '/')
    ))
    , _semicolon: $ => seq(
      field('gate', optional($.when_unless_cond))
      , choice($._automatic_semicolon, ';')
    )
    , when_unless_cond: $ => seq(
      field('kind', choice(
        alias('when', $.when_kw)
        , alias('unless', $.unless_kw)
      ))
      , field('condition', $._expression)
    )
  }
});
