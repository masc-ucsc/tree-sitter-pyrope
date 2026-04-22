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
  ]
  , extras: $ => [$._space, $.comment]
  , word: $ => $.identifier

  , precedences: $ => [
    [
      'dot_type'
      , 'dot_type_sub'
      , 'array_type'
      , 'range_type'
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
      , 'unary'
      , 'range'
      , 'step'
      , 'type_spec'
      , 'binary_times'
      , 'binary_plus'
      , 'binary_shift'
      , 'binary_compare'
      , 'binary_equal'
      , 'scalar_and'
      , 'scalar_nand'
      , 'scalar_or'
      , 'scalar_nor'
      , 'scalar_xor'
      , 'scalar_xnor'
      , 'logical_and'
      , 'logical_nand'
      , 'logical_or'
      , 'logical_nor'
      , 'induction'
      , 'tuple_relation'
      , 'tuple_concat'
      , 'type_compare'
      , 'type_equal'
      , 'expression'
    ]
    // Types
    , [
      'statement'
      , 'expression'
    ]
    , [
      'tuple_list'
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

  , supertypes: $ => [
    $._expression
  ]

  , rules: {
    // Top
    description: $ => repseq($._statement, repeat(';'))

    // Statements
    , _statement: $ => prec('statement', choice(
      // Synthesizable
      $.scope_statement
      , $.declaration_statement
      , seq($.assignment, $._semicolon)
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
        $.module_path
        , $._string_literal
      ))
      , 'as'
      , field('alias', $.identifier)
      , $._semicolon
    )
    , module_path: $ => seq(
      $.identifier
      , repeat(seq('.', $.identifier))
    )
    , control_statement: $ => choice(
      choice('continue', 'break')
      , seq(
        'return'
        , field('argument', optional($._expression_with_comprehension))
        , $._semicolon
      )
    )
    , stmt_list: $ => prec.left('tuple_list', seq(
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
      , field('match_list', optional($.match_list))
      , '}'
    )
    , match_list: $ => repeat1(seq(
      field('condition', choice(
        seq(optional($.match_operator), $.expression_list)
        , 'else'
      ))
      , field('code', $.scope_statement)
    ))
    , match_operator: $ => choice(
      'and', '!and', 'or', '!or', '&', '^', '|', '~&', '~^', '~|',
      '<', '<=', '>', '>=', '==', '!=', 'has', '!has', 'case', '!case', 'in', '!in',
      'equals', '!equals', 'does', '!does', 'is', '!is'
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
            $.lambda_decl
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
    , tuple: $ => seq('(', optional($.tuple_list), ')')

    , tuple_sq: $ => seq('[', optional($.tuple_list), ']')

    , tuple_list: $ => prec('tuple_list', listseq1(field('item', $._tuple_item)))
    , _tuple_item: $ => choice(
      $.ref_identifier
      , $._expression_with_comprehension
      , $.assignment
      , seq(
        field('decl', $.var_or_let_or_reg)
        , field('lvalue', $.typed_identifier)
      )
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
      optional('comptime')
      , choice('const', 'mut', 'reg', prec.right(seq('await', optional($.tuple_sq))))
    )

    // Attribute lists: ::[identifier [= expression], ...]
    , attribute_item: $ => seq(
      field('name', $.identifier)
      , field('value', optseq('=', choice($._expression, $.ref_identifier)))
    )
    , attribute_list: $ => seq('[', optional(listseq1(field('item', $.attribute_item))), ']')
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
      '(', optional(listseq1($.arg_item)), ')'
    )
    , arg_item: $ => seq(
      field('mod', optional(choice('...', 'ref', 'reg')))
      , $.typed_identifier
      , field('definition', optseq('=', $._expression_with_comprehension))
    )

    , _complex_identifier: $ => choice(
      $.identifier
      , $.dot_expression
      , $.member_selection
      , $.bit_selection
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

    // Expressions
    , _expression: $ => prec('expression', choice(
      $.type_specification
      , $.unary_expression
      , $.binary_expression
      , $.if_expression
      , $.match_expression
      , $._restricted_expression
      , $.scope_statement
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
      , field('select', $.member_select)
    ))
    , bit_selection: $ => prec('bit_selection', seq(
      field('argument', $._restricted_expression)
      , field('select', $.bit_select)
    ))
    , type_specification: $ => prec('type_spec', seq(
      field('argument', $._restricted_expression)
      , ':'
      , typedOrAttributed($)
    ))
    , unary_expression: $ => prec.left('unary', seq(
      field('operator', choice('!', 'not', '~', '-', '...'))
      , field('argument', $._expression)
    ))
    , optional_expression: $ => seq(
      field('argument', $._expression)
      , field('operator', '?')
    )
    , binary_expression: $ => choice(
      ...[
        ['..=', 'range']
        , ['..<', 'range']
        , ['..+', 'range']
        , ['step', 'step']
        , ['and', 'logical_and']
        , ['!and', 'logical_nand']
        , ['or', 'logical_or']
        , ['!or', 'logical_nor']
        , ['implies', 'induction']
        , ['!implies', 'induction']
        , ['>>', 'binary_shift']
        , ['<<', 'binary_shift']
        , ['&', 'scalar_and']
        , ['^', 'scalar_xor']
        , ['|', 'scalar_or']
        , ['!&', 'scalar_nand']
        , ['!^', 'scalar_xnor']
        , ['!|', 'scalar_nor']
        , ['*', 'binary_times']
        , ['/', 'binary_times']
        , ['%', 'binary_times']
        , ['+', 'binary_plus']
        , ['-', 'binary_plus']
        , ['<', 'binary_compare']
        , ['<=', 'binary_compare']
        , ['>', 'binary_compare']
        , ['>=', 'binary_compare']
        , ['==', 'binary_equal']
        , ['!=', 'binary_equal']
        , ['++', 'tuple_concat']
        , ['has', 'tuple_relation']
        , ['!has', 'tuple_relation']
        , ['in', 'tuple_relation']
        , ['!in', 'tuple_relation']
        , ['equals', 'type_equal']
        , ['!equals', 'type_equal']
        , ['case', 'type_compare']
        , ['!case', 'type_compare']
        , ['does', 'type_compare']
        , ['!does', 'type_compare']
        , ['is', 'type_compare']
        , ['!is', 'type_compare']
      ].map(([operator, precedence]) =>
        prec.left(precedence, seq(
          field('left', $._expression)
          , field('operator', operator)
          , field('right', $._expression)
        ))
      )
    )
    , dot_expression: $ => dottedChain(
      $._restricted_expression
      , choice(
        $.identifier
        , $._constant
      )
      , 'dot'
      , 'dot_sub'
    )
    , _restricted_expression: $ => prec('expression', choice(
      $._complex_identifier
      , $._constant
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
        , seq('pipe', optional($.tuple_sq)
        )))
      , field('name', $.identifier)
      , $.function_definition_decl
      , field('code', optional($.scope_statement))
    )
    , lambda_decl: $ => seq(
      field('func_type', choice(
        'comb'
        , 'mod'
        , 'pipe'
      ))
      , $.function_definition_decl
    )
    // Operators
    , assignment_operator: $ => token(choice(
      '=', '+=', '-=', '*=', '/=', '|=', '&=', '^=', '<<=', '>>=', '++=', 'or=', 'and='
    ))

    // Selects
    , select: $ => seq(
      '['
      , $.select_options
      , ']'
    )
    , select_options: $ => choice(
      field('list', $.expression_list)
      , field('open_range', '..')
      , field('open_range', seq($._expression, '..'))
      , field('from_zero', seq(choice('..=', '..<'), $._expression))
    )
    , member_select: $ => prec.right('select', repeat1($.select))
    , bit_select: $ => prec('select', seq(
      '#', field('type', optional($.bit_select_type)), field('select', $.select)
    ))
    , bit_select_type: $ => choice('|', '&', '^', '+', 'sext', 'zext')

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
      , $._constant
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
      $.unsized_integer_type
      , $.sized_integer_type
      , $.range_type
      , 'string'
      , 'bool'
    )
    , unsized_integer_type: $ => choice(
      'int'
      , 'integer'
      , 'uint'
      , 'unsigned'
    )
    , sized_integer_type: $ => token(/[siu]\d+/)
    , range_type: $ => seq('range', '(', $.select_options, ')')

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
    , _constant: $ => choice(
      $._number
      , $._bool_literal
      , $._string_literal
      , $._unknown_literal
    )

    // Numbers. Each form accepts an optional leading '-' as part of the token
    // so that negative literals are one token (preferred over '-' operator + literal via prec(2)).
    , _number: $ => choice(
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
    , _binary_number: $ => token(choice(prec(2, /-0(s|S)?(b|B)[0-1\?][0-1_\?]*/), /0(s|S)?(b|B)[0-1\?][0-1_\?]*/))
    , _typed_number: $ => token(choice(prec(2, /-(0|[1-9][0-9]*)[sui][0-9]+/), /(0|[1-9][0-9]*)[sui][0-9]+/))

    // Booleans
    , _bool_literal: $ => token(choice('true', 'false'))

    // Unknown/uninitialized value
    , _unknown_literal: $ => token('?')

    // Strings
    , _string_literal: $ => choice(
      $._simple_string_literal
      , $.complex_string_literal
    )
    , _simple_string_literal: $ => token(/\'[^\'\n]*\'/)
    , complex_string_literal: $ => seq(
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
