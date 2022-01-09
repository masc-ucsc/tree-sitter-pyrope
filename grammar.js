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
      ,seq.apply(null, arguments)
      ,repseq(repeat1(','), seq.apply(null, arguments))
      ,repeat(',')
  )
}

// Grammar
module.exports = grammar({
  name: 'pyrope'

  ,externals: $ => []
  ,conflicts: $ => [ [$.assignment] ]
  ,extras:    $ => [ $._space, $._comment ]
  ,word:      $ => $.identifier
  ,inline:    $ => []

  ,precedences: $ => [
    [
      'member'
      ,'function'
      ,'selection'
      ,'unary'
      ,'range'
      ,'type_spec'
      ,'step'
      ,'binary_times'
      ,'binary_plus'
      ,'binary_shift'
      ,'binary_compare'
      ,'binary_equal'
      ,'scalar_and'
      ,'scalar_or'
      ,'scalar_xor'
      ,'logical_and'
      ,'logical_or'
      ,'induction'
      ,'sequential_condition'
      ,'cycle_condition'
      ,'pipe_concat'
      ,'tuple_concat'
      ,'tuple_relation'
      ,'type_compare'
      ,'type_equal'
    ]
    ,[
      'statement'
      ,'expression'
    ]
    ,[
      'argument_list'
      ,'tuple_list'
    ]
  ]

  ,rules: {
    // Top
    description: $ => repseq($.statement, repeat(';'))

    // Statements
    ,statement: $ => prec.left('statement', seq(
      optional(choice(
        'debug'
        ,'defer_read'
        ,'defer_write'
        ,'comptime'
      ))
      ,choice(
        // Synthesizable
        $.scope_statement
        ,$.assignment_or_declaration
        ,$.control_statement
        ,$.if_statement
        ,$.for_statement
        ,$.while_statement
        ,$.match_statement
        ,$.type_declaration
        ,$.type_extension
        ,$.expression
        // Verification Only
        ,$.test_statement
        ,$.cover_statement
        ,$.covercase_statement
        ,$.assert_statement
        ,$.verify_statement
        ,$.wait_statement
        ,$.step_statement
        ,$.puts_statement
        ,$.print_statement
        ,$.lec_statement
        ,$.poke_statement
        ,$.peek_statement
      )
    ))
    ,scope_statement: $ => prec.left('statement', seq(
      '{'
      ,repseq($.statement, repeat(';'))
      ,'}'
      ,repseq(
        '#>'
        ,'{'
        ,repseq($.statement, repeat(';'))
        ,'}'
      )
    ))
    ,assert_statement: $ => prec.right(seq(
      'assert'
      ,$.expression
      ,optseq(',', $.expression)
    ))
    ,cover_statement: $ => prec.right(seq('cover', $.expression_list))
    ,covercase_statement: $ => prec.right(seq('covercase', $.expression_list))
    ,verify_statement: $ => prec.right(seq(
      'verify'
      ,$.expression
      ,optseq(',', $.expression)
    ))
    ,control_statement: $ => prec.right(seq(
      choice(
        // TODO: Decide which keywords to use
        'ret', 'return', 'cont', 'continue', 'brk', 'break', 'last'
      )
      ,optional($.expression)
    ))
    ,type_declaration: $ => prec.left(seq(
      'type', $.identifier, optseq('=', $.expression)
    ))
    ,type_extension: $ => prec.left(seq(
      'type', $.identifier, 'extends', $.expression, 'with', '(', $.tuple_list, ')'
    ))
    ,if_statement: $ => prec.left(seq(
      'if'
      ,field('condition', $.expression)
      ,field('code', $.scope_statement)
      ,field('elif', repseq(
        'elif'
        ,field('condition', $.expression)
        ,field('code', $.scope_statement)
      ))
      ,field('else', optseq(
        'else'
        ,field('code', $.scope_statement)
      ))
    ))
    ,for_statement: $ => seq(
      'for'
      ,field('index', $.identifier_list)
      ,'in'
      ,field('data', $.tuple_list)
      ,field('code', $.scope_statement)
    )
    ,while_statement: $ => seq(
      'while'
      ,field('condition', $.expression)
      ,field('code', $.scope_statement)
    )
    ,match_statement: $ => seq(
      'match'
      ,$.expression_list
      ,'{'
      ,optional($.match_list)
      ,'}'
    )
    ,match_list: $ => repeat1(seq(
      choice(
        seq('==', $.expression)
        ,seq('in', $.expression_list)
        ,$.expression
        ,'else'
      )
      ,$.scope_statement
    ))
    ,test_statement: $ => prec.right(seq('test', $.expression, $.scope_statement))
    ,puts_statement: $ => prec.right(seq('puts', optional($.expression_list)))
    ,print_statement: $ => prec.right(seq('print', optional($.expression_list)))
    ,step_statement: $ => prec.right(seq('step', optional($.expression)))
    ,wait_statement: $ => prec.right(seq('waitfor', $.expression))
    ,lec_statement: $ => prec.left(seq('lec', $.expression_list))
    ,poke_statement: $ => prec.left(seq('poke', $.expression_list))
    ,peek_statement: $ => prec.left(seq('peek', $.expression_list))
    ,expression_list: $ => prec.left(seq(
      $.expression, repseq(',', $.expression)
    ))

    // Function Call
    // ,simple_function_call: $ => prec.left(seq(
    //   $.identifier
    //   ,$.expression_list
    // ))

    // Tuple
    ,tuple: $ => prec.left(seq(
      '('
      ,optional($.tuple_list)
      ,')'
      ,optional($.type_cast)
    ))
    ,tuple_list: $ => prec.left('tuple_list', seq(
      repeat(',')
      ,$.tuple_item
      ,repeat(seq(repeat1(','), $.tuple_item))
      ,repeat(',')
    ))
    ,tuple_item: $ => prec.left(choice(
      $.expression
      ,$.assignment_or_declaration
    ))
    
    // Assignment/Declaration
    ,assignment_or_declaration: $ => choice(
      $.assignment
      ,$.declaration
    )
    ,assignment: $ => prec.right(seq(
      choice(
        seq(
          field('debug', optional('debug'))
          ,field('scope', optional('pub'))
          ,field('qualifier', optional($.type_qualifier))
        )
        ,field('overflow', optional(choice('wrap', 'saturate')))
      )
      ,field('lvalue', $.expression_list)
      ,field('operator', $.assignment_operator)
      ,field('delay', optional(choice($.cycle_select, '#')))
      ,field('rvalue', choice(
        $.expression
        ,$.introspection
        ,$.library_import
      ))
    ))
    ,declaration: $ => seq(
      choice(
        seq(field('scope', optional('pub')) ,field('qualifier', $.type_qualifier))
        ,seq(field('scope', 'pub'))
      )
      ,field('lvalue', $.expression_list)
    )
    ,function_definition: $ => seq(
      field('func_type', choice('fun', 'proc'))
      ,optseq('[', $.capture_list, ']')
      ,optseq('<',  $.identifier_list, '>')
      ,$.tuple
      ,optseq('->', $.tuple)
      ,optseq('where', $.expression)
      ,$.scope_statement
    )
    ,capture_list: $ => listseq1($.identifier, optseq('=', $.expression))
    ,identifier_list: $ => listseq1($.identifier)
    ,introspection: $ => prec.left(seq('punch', $.expression))
    ,library_import: $ => prec.left(seq('import', $.expression))

    // Expressions
    ,expression: $ => prec.left(choice(
      $.primary
      ,$.constant
      ,$.selection
      ,$.type_specification
      ,$.type_cast
      ,$.function_call
      ,$.function_definition
      ,$.tuple
      ,$.unary_expression
      ,$.optional_expression
      ,$.binary_expression
      ,$.for_expression
      ,$.if_expression
      ,$.match_expression
      ,$.scope_expression
      // ,seq('(', $.expression, ')')
    ))
    ,selection: $ => prec.right('selection', choice(
      $.member_selection
      ,$.bit_selection
      ,$.cycle_selection
    ))
    ,member_selection: $ => seq($.expression, $.member_select)
    ,bit_selection: $ => seq($.expression, $.bit_select)
    ,cycle_selection: $ => seq($.expression, $.cycle_select)
    ,type_specification: $ => seq($.expression, $.type_cast)
    ,unary_expression: $ => prec.left('unary', seq(
      field('operator', choice('!', 'not', '~', '-', '...', 'no'))
      ,field('argument', $.expression)
    ))
    ,optional_expression: $ => prec.right(seq(
      field('argument', $.expression)
      ,field('operator', '?')
    ))
    ,binary_expression: $ => choice(
      ...[
        ['..=', 'range']
        ,['..<', 'range']
        ,['..+', 'range']
        ,['by', 'step']
        ,['and', 'logical_and']
        ,['or', 'logical_or']
        ,['implies', 'induction']
        ,['and_then', 'sequential_condition']
        ,['or_else', 'sequential_condition']
        ,['>>', 'binary_shift']
        ,['<<', 'binary_shift']
        ,['&', 'scalar_and']
        ,['^', 'scalar_xor']
        ,['|', 'scalar_or']
        ,['*', 'binary_times']
        ,['/', 'binary_times']
        ,['+', 'binary_plus']
        ,['-', 'binary_plus']
        ,['<', 'binary_compare']
        ,['<=', 'binary_compare']
        ,['>', 'binary_compare']
        ,['>=', 'binary_compare']
        ,['==', 'binary_equal']
        ,['!=', 'binary_equal']
        ,['when', 'cycle_condition']
        ,['unless', 'cycle_condition']
        ,['|>', 'pipe_concat']
        ,['++', 'tuple_concat']
        ,['has', 'tuple_relation']
        ,['in', 'tuple_relation']
        ,['equals', 'type_equal']
        ,['does', 'type_compare']
        ,['doesnt', 'type_compare']
        ,['.', 'member']
      ].map(([operator, precedence]) =>
        prec.left(precedence, seq(
          field('left', $.expression)
          ,field('operator', operator)
          ,field('right', $.expression)
        ))
      )
    )
    ,function_call: $ => prec.right('function', seq(
      field('name', $.primary)
      ,'('
      ,optional($.tuple_list)
      ,')'
    ))
    ,for_expression: $ => prec.right('expression', alias($.for_statement, $.for_expression))
    ,if_expression: $ => prec.right('expression', alias($.if_statement, $.if_expression))
    ,match_expression: $ => prec.right('expression', alias($.match_statement, $.match_expression))
    ,scope_expression: $ => prec.right('expression', alias($.scope_statement, $.scope_expression))

    ,constant: $ => prec.left(seq(
      choice(
        $.number
        ,$.bool_literal
        ,$.string_literal
      )
    ))
    ,primary: $ => prec.left(seq(
      $.hierarchical_identifier
    ))

    // Operators
    ,assignment_operator: $ => token(choice(
      '=', '+=', '-=', '*=', '/=', '|=', '&=', '^=', '<<=', '>>=', '++=' /*, 'or=', 'and=' */
    ))

    // Selects
    ,select: $ => seq(
      '['
      ,optional(choice(
        $.tuple_list
        // TODO: clean this up
        ,seq($.tuple_item, '..')
        ,seq(choice('..=', '..<', '..+'), $.tuple_item)
      ))
      ,']'
    )
    ,member_select: $ => prec.left(repeat1($.select))
    ,bit_select: $ => prec.left(seq(
      repeat1(
        seq('@', optional($.bit_select_type), $.select)
      )
    ))
    ,bit_select_type: $ => token(choice('|', '&', '^', '+', 'sext', 'zext'))
    ,cycle_select: $ => seq('#', $.select)

    // Variable Properties
    ,type_qualifier: $ => choice('var', 'mut', 'let', 'reg')

    // Types
    ,type_cast: $ => prec.left(seq(':', $.type, optional('?')))
    ,type: $ => choice(
      $.primitive_type
      ,$.tuple_type
      ,$.array_type
      ,$.function_type
      ,$.hierarchical_identifier
      ,$.constant
      // TODO: Support type from expression
    )
    ,tuple_type: $ => prec.left(seq(
      '(', $.tuple_list, ')'
    ))
    ,array_type: $ => prec.right(repeat1($.select))
    ,function_type: $ => prec.left(seq(
      field('func_type', choice('fun', 'proc'))
      ,optseq('<',  $.identifier_list, '>')
      ,optseq('(', optional($.tuple_list), ')')
      ,optseq('->', '(', optional($.tuple_list), ')')
    ))
    ,primitive_type: $ => prec.left(choice(
      $.unsized_integer_type
      ,$.sized_integer_type
      ,$.bounded_integer_type
      ,$.range_type
      ,$.string_type
      ,$.boolean_type
    ))
    ,unsized_integer_type: $ => choice(
      'int'
      ,'integer'
      ,'signed'
      ,'uint'
      ,'unsigned'
    )
    ,bounded_integer_type: $ => seq(
      $.unsized_integer_type
      ,'('
      ,optseq('max', '=')
      ,$.expression
      ,','
      ,optseq('min', '=')
      ,$.expression
      ,')'
    )
    ,sized_integer_type: $ => token(/[siu]\d+/)
    ,range_type: $ => token('range')
    ,string_type: $ => token('string')
    ,boolean_type: $ => token('boolean')

    // Identifiers
    ,hierarchical_identifier: $ => prec.left(seq(
      $.identifier
      ,repseq('.', $.identifier)
    ))
    ,identifier: $ => token(
      choice(
        // \p{L}  : Letter
        // \p[Nd} : Decimal Digit Number
        /[\p{L}_$][\p{L}\p{Nd}_$]*/
        // To support all Verilog identifiers. Example:
        //   `foo is . strange!\nidentifier` = 4
        ,seq(
          '`'
          ,repeat(choice(prec(1, /\\./), /[^`\\\n]+/))
          ,'`'
        )
      )
    )

    // Numbers
    ,number: $ => choice(
      $.simple_number
      ,$.scaled_number
      ,$.hex_number
      ,$.decimal_number
      ,$.octal_number
      ,$.binary_number
    )
    ,simple_number:  $ => token(/0|[1-9][0-9]*/)
    ,scaled_number:  $ => token(/(0|[1-9][0-9]*)[kKmMgG]/)
    ,hex_number:     $ => token(/0(s|S)?(x|X)[0-9a-fA-F][0-9a-fA-F_]*/)
    ,decimal_number: $ => token(/0(s|S)?(d|D)?[0-9][0-9_]*/)
    ,octal_number:   $ => token(/0(s|S)?(o|O)[0-7][0-7_]*/)
    ,binary_number:  $ => token(/0(s|S)?(b|B)[0-1\?][0-1_\?]*/)
    ,bool_literal:   $ => token(choice('true', 'false'))

    // Strings
    ,string_literal: $ => choice(
      $.simple_string_literal
      ,$.complex_string_literal
    )
    ,simple_string_literal: $ => token(/\'[^\'\n]*\'/)
    ,complex_string_literal: $ => token(
      seq('"', repeat(choice(prec(1, /\\./), /[^"\\\n]+/)), '"')
    )

    // Special
    ,_space:   $ => token(/\s/)
    // ,_space: $ => token(/[\s\f\uFEFF\u2060\u200B]|\\\r?\n/)
    ,_comment: $ => token(choice(
      /\/\/.*/
      ,seq('/*', /[^*]*\*+([^/*][^*]*\*+)*/, '/')
    ))
    ,_newline: $ => token(/[;\n\r]/)
  }
});
