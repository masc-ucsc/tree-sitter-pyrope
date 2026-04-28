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
  return optseq($.stmt_list, ';');
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

function attributeSuffix($) {
  return seq(':', $.attribute_list);
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
    , [$.function_call_statement, $._restricted_expression]
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
      , 'binary_compare'   // Pyrope priority 4: <, <=, ==, !=, >=, >, has/in/is/case/does/equals (+ ! variants)
      , 'binary_logical'   // Pyrope priority 5: and, or, implies (+ ! variants)
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
      , $.function_call_statement
      , $.control_statement
      , $.while_statement
      , $.for_statement
      , $.lambda
      , seq($.enum_assignment, $._semicolon)
      , $.spawn_statement
      , $.loop_statement
      , seq($._expression_with_comprehension, $._semicolon)
      // Verification Only
      , $.test_statement
      , $.type_statement
      , $.impl_statement
      , $.assert_statement
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
    , break_statement: $ => 'break'
    , continue_statement: $ => 'continue'
    , return_statement: $ => seq(
      'return'
      , field('argument', optional($._expression_with_comprehension))
      , $._semicolon
    )
    , stmt_list: $ => prec.left('_tuple_list', seq(
      field('item', $._tuple_item)
      , repeat(seq(repeat1(';'), field('item', $._tuple_item)))
    ))
    , if_expression: $ => prec('statement', seq(
      optional('unique')
      , 'if'
      , field('init', statementInit($))
      , field('condition', $._expression)
      , field('code', $.scope_statement)
      , field('elif', repseq(
        'elif'
        , field('init', statementInit($))
        , field('condition', $._expression)
        , field('code', $.scope_statement)
      ))
      , field('else', optseq('else', $.scope_statement))
    ))
    , for_statement: $ => seq(
      'for'
      , field('attributes', optseq('::', $.attribute_list))
      , field('init', statementInit($))
      , forBinding($) // NOTE: maybe constraint to max 3 (elem,index,key)
      , 'in'
      , choice(
        $.ref_identifier
        , field('data', $.expression_list)
      )
      , field('code', $.scope_statement)
    )
    , while_statement: $ => seq(
      'while'
      , field('attributes', optseq('::', $.attribute_list))
      , field('init', statementInit($))
      , field('condition', $._expression)
      , field('code', $.scope_statement)
    )
    , loop_statement: $ => prec('statement', seq(
      'loop'
      , field('attributes', optseq('::', $.attribute_list))
      , field('init', optional($.stmt_list))
      , field('code', $.scope_statement)
    ))
    , match_expression: $ => seq(
      'match'
      , field('init', statementInit($))
      , field('condition', $._expression)
      , '{'
      , field('match_list', optional(repeat1(seq(
        field('condition', choice(
          seq(optional(choice(
            'and', '!and', 'or', '!or', '&', '^', '|', '~&', '~^', '~|',
            '<', '<=', '>', '>=', '==', '!=', 'has', '!has', 'case', '!case', 'in', '!in',
            'equals', '!equals', 'does', '!does', 'is', '!is'
          )), $.expression_list)
          , 'else'
        ))
        , field('code', $.scope_statement)
      ))))
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
                'comb'
                , 'mod'
                , 'pipe'
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
    , assert_statement: $ => seq(
      optional('always')
      , choice('assert', 'cassert')
      , field('condition', $._expression)
      , field('msg', optional(seq(
        ','
        , $._string_literal
      )))
      , $._semicolon
    )
    , expression_list: $ => prec.left(seq(
      field('item', $._expression)
      , repseq(',', field('item', $._expression))
    ))

    // Function Call
    , function_call_expression: $ => tupleCall($, 'function_call_expression')
    //
    , function_call_statement: $ => seq(
      field('function', $._complex_identifier)
      , field('argument', $.expression_list)
      , $._semicolon
    )
    // Tuple
    , tuple: $ => seq('(', optional($._tuple_list), ')')

    , tuple_sq: $ => seq('[', optional($._tuple_list), ']')

    , _tuple_list: $ => prec('_tuple_list', listseq1(field('item', $._tuple_item)))
    // Overparse: tuple-literal fields require a kind keyword
    // (`mut`/`const`/`reg`/`comb`/...); bare `a = 1` parses here because the
    // same node also models named call arguments. See grammar_overparse.md #3.
    , _tuple_item: $ => choice(
      $.ref_identifier
      , $._expression_with_comprehension
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
        , field('value', $._expression_with_comprehension)
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
        $._expression_with_comprehension
        , $.enum_definition
        , $.ref_identifier
      ))
    )
    , lvalue_item: $ => choice(
      $.typed_identifier
      , seq(
        field('identifier', $._complex_identifier)
        , field('type', optional($.type_cast))
      )
    )
    , lvalue_list: $ => listseq1(field('item', $.lvalue_item))
    , var_or_let_or_reg: $ => seq(
      field('comptime', optional(alias('comptime', $.comptime_modifier)))
      , field('storage', choice(
        alias('const', $.const_decl)
        , alias('mut', $.mut_decl)
        , alias('reg', $.reg_decl)
        , $.await_decl
      ))
    )
    , await_decl: $ => prec.right(seq('await', optional($.tuple_sq)))

    // Attribute lists: ::[identifier [= expression], ...]
    , attribute_list: $ => seq('[', optional(listseq1(field('item', seq(
      field('name', $.identifier)
      , field('value', optseq('=', choice($._expression, $.ref_identifier)))
    )))), ']')
    // Overparse: the `[...]` slot is a comptime-parameter list; entries must
    // declare a `:Type`. The grammar accepts old capture-list shapes.
    // See grammar_overparse.md #4.
    , function_definition_decl: $ => seq(
      field('generic', optseq('<', $.typed_identifier_list, '>'))
      , field('capture', optseq($.tuple_sq))
      , field('pipe_config', optseq('::', $.attribute_list))
      , field('input', $.arg_list)
      , field('output', optseq('->', choice(
        $.arg_list
        , field('type', $.type_cast)
        , $.typed_identifier
      )))
    )
    , enum_definition: $ => seq(
      choice('enum', 'variant')
      , field('input', $.arg_list)
    )
    , enum_assignment: $ => seq(
      choice('enum', 'variant')
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
        , field('definition', optseq('=', $._expression_with_comprehension))
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
      , field('timing', seq('[', $._expression, ']'))
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
    , _expression_with_comprehension: $ => seq(
      $._expression
      , optional($.for_comprehension)
    )
    , for_comprehension: $ => seq(
      'for'
      , forBinding($)
      , 'in'
      , field('data', $.expression_list)
      , optional(
        seq(
          'if'
          , field('condition', $.stmt_list)
        )
      )
    )
    , member_selection: $ => prec('member_selection', seq(
      field('argument', $._restricted_expression)
      , field('select', prec.right('select', repeat1($.select)))
    ))
    , bit_selection: $ => prec('bit_selection', seq(
      field('argument', $._restricted_expression)
      , field('select', prec('select', seq(
        '#'
        , field('reduction', optional(choice(
          alias('|', $.reduction_or)
          , alias('&', $.reduction_and)
          , alias('^', $.reduction_xor)
          , alias('+', $.reduction_popcount)
          , alias('sext', $.sign_extend)
          , alias('zext', $.zero_extend)
        )))
        , field('select', $.select)
      )))
    ))
    , attribute_read: $ => prec.left('member_selection', seq(
      field('argument', $._restricted_expression)
      , field('attrs', repeat1(prec('select', seq(
        '.', $.attribute_list
      ))))
    ))
    // Overparse: `:Type` is only legal at declaration sites; the grammar
    // permits it on any expression. See grammar_overparse.md #2.
    , type_specification: $ => prec('type_spec', seq(
      field('argument', $._restricted_expression)
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
    , optional_expression: $ => seq(
      field('argument', $._expression)
      , field('operator', '?')
    )
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
    // Pyrope priority 4: <, <=, >, >=, ==, !=, has/in/is/case/does/equals (+ ! variants)
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
      , alias('!has', $.op_not_has)
      , alias('in', $.op_in)
      , alias('!in', $.op_not_in)
      , alias('case', $.op_case)
      , alias('!case', $.op_not_case)
      , alias('does', $.op_does)
      , alias('!does', $.op_not_does)
      , alias('is', $.op_is)
      , alias('!is', $.op_not_is)
      , alias('equals', $.op_equals)
      , alias('!equals', $.op_not_equals)
    )
    // Pyrope priority 5: and, or, implies (+ ! variants)
    , _binary_logical: $ => prec.left('binary_logical', seq(
      field('operand', $._pri4_operand)
      , repeat1(seq(
        field('operator', $.binary_logical_op)
        , field('operand', $._pri4_operand)
      ))
    ))
    , binary_logical_op: $ => choice(
      alias('and', $.op_log_and)
      , alias('!and', $.op_log_nand)
      , alias('or', $.op_log_or)
      , alias('!or', $.op_log_nor)
      , alias('implies', $.op_implies)
      , alias('!implies', $.op_not_implies)
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
      $._restricted_expression
      , choice(
        $.identifier
        , $.constant
      )
      , 'dot'
      , 'dot_sub'
    )
    , _restricted_expression: $ => prec('expression', choice(
      $._complex_identifier
      , $.constant
      , $.function_call_expression
      , $.lambda
      , $.tuple
      , $.tuple_sq
      , $.optional_expression
    ))
    , lambda: $ => seq(
      field('func_type', choice(
        'comb'
        , 'mod'
        , 'proc'
        , seq('pipe', optional($.tuple_sq)
        )))
      , field('name', $.identifier)
      , $.function_definition_decl
      , field('code', optional($.scope_statement))
    )
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
    , select: $ => seq(
      '['
      , choice(
        field('list', $.expression_list)
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
    , array_type: $ => prec.left('array_type', seq(
      field('length', $.tuple_sq)
      , optional(field('base', choice(
        $._primitive_type
        , $.array_type
        , $.lambda
        , $.expression_type
      )))
    ))
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
      choice('when', 'unless')
      , field('condition', $._expression)
    )
  }
});
