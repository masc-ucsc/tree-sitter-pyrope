'use strict';

// Helper Functions
function optseq(rule) {
  return optional(prec.left(seq.apply(null, arguments)));
}

function repseq(rule) {
  return repeat(prec.left(seq.apply(null, arguments)));
}

function listseq1(rule) {
  return seq(
    repeat(',')
    , seq.apply(null, arguments)
    , repseq(repeat1(','), seq.apply(null, arguments))
    , repeat(',')
  )
}

// Grammar
module.exports = grammar({
  name: 'pyrope'

  , externals: $ => [$._automatic_semicolon]
  , conflicts: $ => [
    [$._expression, $.function_type]
    , [$._restricted_expression, $.typed_identifier]
    , [$._assignment_or_declaration, $._restricted_expression]
    , [$.complex_identifier, $.typed_identifier]
    , [$.complex_identifier, $.expression_type]
    , [$.complex_identifier_list, $._restricted_expression]
    , [$.var_or_let_or_reg, $.arg_item]
    , [$.tuple_sq, $.function_definition]
    , [$.function_type, $.function_call_type]
    , [$.expression_type, $.typed_identifier]
    , [$.complex_identifier, $.typed_identifier, $.expression_type]
    , [$.tuple, $.arg_list]
    , [$.function_inline, $.complex_identifier]
    , [$.function_inline, $.tuple_method_definition, $.complex_identifier]
  ]
  , extras: $ => [$._space, $.comment]
  , word: $ => $.identifier
  , inline: $ => []

  , precedences: $ => [
    [
      'dot_type'
      , 'dot_type_sub'
      , 'array_type'
      , 'range_type'
      , 'function_type'
      , 'function_call_type'
      , 'mixin_type_sub'
      , 'expression_type'
      , 'type'
      , 'typed_identifier'
      // Expressions
      , 'dot_sub'
      , 'dot'
      , 'select'
      , 'function'
      , 'member_selection'
      , 'bit_selection'
      , 'unary'
      , 'range'
      , 'step'
      , 'type_spec'
      , 'pipe_concat'   // higher prio than == to allow a |> b == c 
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
      , 'sequential_condition'
      , 'cycle_condition'
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
      'argument_list'
      , 'tuple_list'
    ]
    , [
      'expression'
      , 'simple_function_call'
    ]
    , [
      'expression'
      , 'function_call'
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
    description: $ => repseq($.statement, repeat(';'))

    // Statements
    , statement: $ => prec.left('statement', choice(
      // Synthesizable
      $.scope_statement
      , $.declaration_statement
      , $.assignment_or_declaration_statement
      , $.function_call_statement
      , $.control_statement
      , $.while_statement
      , $.for_statement
      , $.function_definition_statement
      , $.enum_assignment_statement
      , $.loop_statement
      , $.expression_statement
      // Verification Only
      , $.test_statement
      , $.type_statement
      , $.impl_statement
      , $.cassert_statement
    )
    )
    , scope_statement: $ => prec.left('statement', seq(
      '{'
      , repseq($.statement)
      , '}'
    ))
    , assignment_or_declaration_statement: $ => prec.right(seq(
      $._assignment_or_declaration
      , $._semicolon
    ))
    , declaration_statement: $ => prec.right(seq(
      field('decl', $.var_or_let_or_reg)
      , choice(
        seq('(', field('lvalue', $.complex_identifier_list), ')')
        , field('lvalue', choice($.identifier, $.type_cast, $.type_specification))
      )
      , $._semicolon
    ))
    , function_call_statement: $ => seq(
      $.simple_function_call
      , $._semicolon
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
    , if_expression: $ => prec.left('statement', seq(
      optional('unique')
      , 'if'
      , field('init', optseq($.stmt_list, ';'))
      , field('condition', $._expression)
      , field('code', $.scope_statement)
      , field('elif', repseq(
        'elif'
        , field('condition', $._expression)
        , field('code', $.scope_statement)
      ))
      , field('else', optseq('else', $.scope_statement))
    ))
    , for_statement: $ => seq(
      'for'
      , field('attributes', optseq('::', $.tuple_sq))
      , field('init', optseq($.stmt_list, ';'))
      , choice(
        seq('('
          , field('index', $.typed_identifier_list) // NOTE: maybe constraint to max 3 (elem,index,key)
          , ')'
        )
        , $.typed_identifier
      )
      , 'in'
      , choice(
        $.ref_identifier
        , field('data', $.expression_list)
      )
      , field('code', $.scope_statement)
    )
    , while_statement: $ => seq(
      'while'
      , field('attributes', optseq('::', $.tuple_sq))
      , field('init', optseq($.stmt_list, ';'))
      , field('condition', $._expression)
      , field('code', $.scope_statement)
    )
    , loop_statement: $ => seq(
      'loop'
      , field('attributes', optseq('::', $.tuple_sq))
      , field('init', optional($.stmt_list))
      , field('code', $.scope_statement)
    )
    , match_expression: $ => seq(
      'match'
      , field('init', optseq($.stmt_list, ';'))
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
    , expression_statement: $ => prec.right(seq(
      $._expression_with_comprehension
      , $._semicolon
    ))
    , test_statement: $ => prec.right(seq(
      'test'
      , field('args', $.expression_list)
      , field('condition', optseq('where', $.expression_list))
      , field('code', $.scope_statement)
    ))
    , type_statement: $ => seq(
      'type'
      , field('name', $.identifier)
      , field('generic', optseq('<', $.typed_identifier_list, '>'))
      , choice(
        field('definition', $.tuple),  // trait definition: type Name ( ... )
        seq('=', field('alias', $._type))  // type alias: type Name = Type
      )
    )
    , impl_statement: $ => seq(
      'impl'
      , field('trait_name', $.identifier)
      , 'for'
      , field('type_name', $.identifier)
      , field('implementation', $.tuple)
    )
    , cassert_statement: $ => seq(
      'cassert'
      , field('condition', $._expression)
      , $._semicolon
    )
    , expression_list: $ => prec.left(seq(
      field('item', $._expression)
      , repseq(',', field('item', $._expression))
    ))

    // Function Call
    , simple_function_call: $ => prec.left('simple_function_call', seq(
      field('function', $.complex_identifier)
      , field('argument', $.expression_list)
    ))

    // Tuple
    , tuple: $ => seq('(', optional($.tuple_list), ')')

    , tuple_sq: $ => seq('[', optional($.tuple_list), ']')

    , tuple_list: $ => prec.left('tuple_list', seq(
      repeat(',')
      , field('item', $._tuple_item)
      , repeat(seq(repeat1(','), field('item', $._tuple_item)))
      , repeat(',')
    ))
    , _tuple_item: $ => prec.left(choice(
      $.ref_identifier
      , $._expression_with_comprehension
      , $.simple_assignment
      , $.typed_declaration
      , prec(1, seq($.function_inline, $.scope_statement))
      , $.function_type
      , $.function_inline
      , $.function_definition_statement
      , $.tuple_method_definition
    ))
    , function_inline: $ => prec.left('function_type', seq(
      field('func_type', choice($.fun_tok, $.comb_tok, $.pipe_tok, $.flow_tok))
      , field('fun_name', $.identifier)
      , field('generic', optseq('<', $.typed_identifier_list, '>'))
      , field('input', optional($.arg_list))
      , field('output', optseq('->', choice($.arg_list, $.expression_type)))
    ))
    , tuple_method_definition: $ => prec(2, seq(
      field('func_type', choice($.fun_tok, $.comb_tok, $.pipe_tok, $.flow_tok))
      , field('fun_name', $.identifier)
      , field('generic', optseq('<', $.typed_identifier_list, '>'))
      , field('input', optional($.arg_list))
      , field('output', optseq('->', choice($.arg_list, $.expression_type)))
      , field('body', $.scope_statement)
    ))
    , attributes: $ => seq(':', $.tuple_sq)

    // Assignment/Declaration
    , simple_assignment: $ => prec.right(seq(
      field('decl', optional($.var_or_let_or_reg))
      , field('lvalue', choice($.identifier, $.type_cast, $.type_specification))
      , field('operator', $.assignment_operator)
      , field('delay', optional($.assignment_delay))
      , field('rvalue', choice(
        $._expression_with_comprehension
        , $.ref_identifier
        //,$.simple_function_call
      ))
    ))
    , typed_declaration: $ => seq(
      field('decl', $.var_or_let_or_reg)
      , field('lvalue', choice($.identifier, $.type_cast, $.type_specification))
    )
    , function_definition_statement: $ => prec.left('statement', seq(
      field('func_type', choice($.fun_tok, $.comb_tok, $.pipe_tok, $.flow_tok))
      , field('pipe_config', optseq('::', $.tuple_sq))
      , field('lvalue', $.complex_identifier)
      , $.function_definition
    ))
    , enum_assignment_statement: $ => prec.left('statement', seq(
      $.enum_assignment
      , $._semicolon
    ))
    , _assignment_or_declaration: $ => prec.right(seq(
      field('decl', optional($.var_or_let_or_reg))
      , choice(
        seq('('
          , field('lvalue', $.complex_identifier_list)
          , ')'
        )
        , seq(
          field('lvalue', $.complex_identifier)
          , field('type', optional($.type_cast))
        )
      )
      , field('operator', $.assignment_operator)
      , field('delay', optional($.assignment_delay))
      , field('rvalue', choice(
        $._expression_with_comprehension
        //,$.simple_function_call
        , $.enum_definition
        , $.ref_identifier
      )
      )))
    , assignment_delay: $ => choice(
      seq(
        $.delay_tok
        , '['
        , $._expression
        , ']'
      )
      , seq(
        '@'
        , '['
        , $._expression
        , ']'
      )
      , seq(
        '@'
        , $.constant
      )
    )
    , var_or_let_or_reg: $ => choice('var', 'let', 'reg')
    , function_definition: $ => seq(
      field('capture', optseq('[', listseq1($.typed_identifier), ']'))
      , field('generic', optseq('<', $.typed_identifier_list, '>'))
      , field('pipe_config', optseq('::', $.tuple_sq))
      , field('input', optional($.arg_list))
      , field('output', optseq('->', choice($.arg_list, $.type_or_identifier)))
      , field('condition', optseq('where', $.expression_list))
      , field('verification', repeat($.func_def_verification))
      , field('code', $.scope_statement)
    )
    , func_def_verification: $ => seq(
      choice('requires', 'ensures')
      , $._expression
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
    , ref_identifier: $ => seq(
      'ref'
      , $.complex_identifier
    )
    , arg_list: $ => prec.left(seq(
      '(', optional($.arg_item_list), ')'
    ))
    , arg_item_list: $ => seq(
      repeat(',')
      , $.arg_item
      , repeat(seq(repeat1(','), $.arg_item))
      , repeat(',')
    )
    , arg_item: $ => seq(
      field('mod', optional(choice('...', 'ref', 'reg')))
      , $.typed_identifier
      , field('definition', optseq('=', $._expression_with_comprehension))
    )

    , type_or_identifier: $ => choice(
      field('type', $.type_cast)
      , $.typed_identifier
    )

    , complex_identifier: $ => choice(
      $.identifier
      , $.dot_expression
      , $.selection
      , $.timed_identifier
    )
    , timed_identifier: $ => prec.left(1, seq(
      field('identifier', $.identifier)
      , '@'
      , field('timing', choice(
        $.constant
        , seq('[', $._expression, ']')
      ))
    ))
    , complex_identifier_list: $ => prec.left(listseq1(field('item', $.complex_identifier)))

    , typed_identifier: $ => seq(
      field('identifier', $.identifier)
      , field('type', optional($.type_cast))
    )
    , typed_identifier_list: $ => prec.left(listseq1(field('item', $.typed_identifier)))

    // Expressions
    , _expression: $ => prec.left('expression', choice(
      $.type_specification
      , $.unary_expression
      , $.binary_expression
      , $._restricted_expression
    ))
    , _expression_with_comprehension: $ => seq(
      $._expression
      , optional($.for_comprehension)
    )
    , for_comprehension: $ => seq(
      'for'
      , choice(
        seq('('
          , field('index', $.typed_identifier_list) // NOTE: maybe constraint to max 3 (elem,index,key)
          , ')'
        )
        , $.typed_identifier
      )
      , 'in'
      , field('data', $.expression_list)
      , optional(
        seq(
          'if'
          , field('condition', $.stmt_list)
        )
      )
    )
    , selection: $ => choice(
      $.member_selection
      , $.bit_selection
    )
    , member_selection: $ => prec.right('member_selection', seq(
      field('argument', $._restricted_expression)
      , field('select', $.member_select)
    ))
    , bit_selection: $ => prec.right('bit_selection', seq(
      field('argument', $._restricted_expression)
      , field('select', $.bit_select)
    ))
    , type_specification: $ => prec.left('type_spec', seq(
      field('argument', $._restricted_expression)
      , ':'
      , choice(
        seq(
          field('type', $._type)
          , field('attribute', optional($.attributes))
        )
        , field('attribute', $.attributes)
      )
    ))
    , unary_expression: $ => prec.left('unary', seq(
      field('operator', choice('!', 'not', '~', '-', '...'))
      , field('argument', $._expression)
    ))
    , optional_expression: $ => prec.right(seq(
      field('argument', $._expression)
      , field('operator', '?')
    ))
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
        , ['and_then', 'sequential_condition']
        , ['or_else', 'sequential_condition']
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
        , ['|>', 'pipe_concat']
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
    , dot_expression: $ => prec.left('dot', seq(
      field('item', $._restricted_expression)
      , choice(
        repeat1(prec.left('dot_sub', seq('.',
          choice(
            $.identifier
            , $.constant
            , field('attribute', $.tuple_sq)
          ))))
      )
    ))
    , function_call: $ => prec.left('function_call', seq(
      field('function', $.complex_identifier)
      , field('argument', $.tuple)
    ))
    , _restricted_expression: $ => prec('expression', choice(
      $.complex_identifier
      , $.constant
      , $.function_call
      , $.lambda
      , $.tuple
      , $.optional_expression
      //,$.for_expression
      , $.if_expression
      , $.match_expression
    ))
    , lambda: $ => seq(
      field('func_type', choice($.fun_tok, $.comb_tok, $.pipe_tok, $.flow_tok))
      , $.function_definition
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
      , field('open_range', seq('..'))
      , field('open_range', seq($._expression, '..'))
      , field('from_zero', seq(choice('..=', '..<'), $._expression))
    )
    , member_select: $ => prec.right('select', repeat1($.select))
    , bit_select: $ => prec.right('select', seq(
      '#', field('type', optional($.bit_select_type)), field('select', $.select)
    ))
    , bit_select_type: $ => choice('|', '&', '^', '+', 'sext', 'zext')

    // Types
    , type_cast: $ => prec.left('type_cast', seq(
      ':'
      , choice(
        seq(
          field('type', $._type)
          , field('attribute', optional($.attributes))
        )
        , field('attribute', $.attributes)
      )
    ))
    , trivial_identifier_list: $ => seq(
      repeat(',')
      , $.identifier
      , repeat(seq(repeat1(','), $.identifier))
      , repeat(',')
    )
    , _type: $ => prec.left('type', choice(
      $.primitive_type
      , $.array_type
      , $.function_type
      , $.expression_type
    ))
    , expression_type: $ => prec.left('expression_type', choice(
      $.identifier
      , $.constant
      , $.tuple
      , $.if_expression
      , $.match_expression
      , $.dot_expression_type
      , $.function_call_type
    ))
    , dot_expression_type: $ => prec.right('dot_type', seq(
      field('item', $.expression_type)
      , repeat1(prec.right('dot_type_sub', seq('.', field('item', $.expression_type))))
    ))
    , function_call_type: $ => prec.right('function_call_type', seq(
      field('function', $.complex_identifier)
      , field('argument', $.tuple)
    ))
    , array_type: $ => prec.left('array_type', seq(
      field('length', $.tuple_sq)
      , optional(field('base', choice(
        $.primitive_type
        , $.array_type
        , $.function_type
        , $.expression_type
      )))
    ))
    , function_type: $ => prec.right('function_type', seq(
      field('func_type', choice($.fun_tok, $.comb_tok, $.pipe_tok, $.flow_tok))
      , field('generic', optseq('<', $.typed_identifier_list, '>'))
      , field('input', optional($.arg_list))
      , field('output', optseq('->', choice($.arg_list, $.expression_type)))
    ))
    , primitive_type: $ => prec.left(choice(
      $.unsized_integer_type
      , $.sized_integer_type
      , $.bounded_integer_type
      , $.range_type
      //,$.enum_type
      , $.string_type
      , $.boolean_type
      , $.type_type
    ))
    , unsized_integer_type: $ => choice(
      'int'
      , 'integer'
      , 'signed'
      , 'uint'
      , 'unsigned'
    )
    , bounded_integer_type: $ => seq(
      $.unsized_integer_type
      , field('constrained', seq('(', $.select_options, ')'))
    )
    , sized_integer_type: $ => token(/[siu]\d+/)
    , range_type: $ => seq('range', '(', $.select_options, ')')
    //,enum_type: $ => seq('enum', $.tuple)
    , string_type: $ => token('string')
    , boolean_type: $ => token('boolean')
    , type_type: $ => token('type')
    , fun_tok: $ => token('fun')
    , comb_tok: $ => token('comb')
    , pipe_tok: $ => token('pipe')
    , flow_tok: $ => token('flow')

    // Identifiers
    , delay_tok: $ => token('delay')
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
      $._number
      , $._bool_literal
      , $._string_literal
    )

    // Numbers
    , _number: $ => choice(
      $._simple_number
      , $._scaled_number
      , $._hex_number
      , $._decimal_number
      , $._octal_number
      , $._binary_number
      , $._typed_number
    )
    , _simple_number: $ => token(/0|[1-9][0-9]*/)
    , _scaled_number: $ => token(/(0|[1-9][0-9]*)[KMGT]/)
    , _hex_number: $ => token(/0(s|S)?(x|X)[0-9a-fA-F][0-9a-fA-F_]*/)
    , _decimal_number: $ => token(/0(s|S)?(d|D)?[0-9][0-9_]*/)
    , _octal_number: $ => token(/0(s|S)?(o|O)[0-7][0-7_]*/)
    , _binary_number: $ => token(/0(s|S)?(b|B)[0-1\?][0-1_\?]*/)
    , _typed_number: $ => token(/(0|[1-9][0-9]*)[sui][0-9]+/)

    // Booleans
    , _bool_literal: $ => token(choice('true', 'false'))

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
          , seq('{', optional($._expression), '}')
        )
      )
      , '"'
    )

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
