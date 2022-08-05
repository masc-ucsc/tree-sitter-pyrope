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

  ,externals: $ => [ $._automatic_semicolon ]
  ,conflicts: $ => [ [$.assignment], [$.declaration] ]
  ,extras:    $ => [ $._space, $._comment ]
  ,word:      $ => $.identifier
  ,inline:    $ => []

  ,precedences: $ => [
    [  
      'dot_type'
      ,'dot_type_sub'
      ,'array_type'
      ,'range_type'
      ,'function_type'
      ,'enum_type'
      ,'function_call_type'
      ,'mixin_type'
      ,'mixin_type_sub'
      ,'expression_type'
      ,'type'
    // Expressions
      ,'dot_sub'
      ,'dot'
      ,'select'
      ,'function'
      ,'member_selection'
      ,'bit_selection'
      ,'cycle_selection'
      ,'unary'
      ,'range'
      ,'step'
      ,'type_spec'
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
      ,'tuple_relation'
      ,'tuple_concat'
      ,'type_compare'
      ,'type_equal'
      ,'expression'
    ]
    // Types
    ,[
      'statement'
      ,'expression'
    ]
    ,[
      'argument_list'
      ,'tuple_list'
    ]
    ,[
      'expression'
      ,'simple_function_call'
    ]
    ,[
      'type_spec'
      ,'type_cast'
    ]
  ]

  ,supertypes: $ => [
    $._expression
    ,$.constant
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
        ,$.pipestage_statement
        ,$.assignment_or_declaration_statement
        ,$.function_call_statement
        ,$.control_statement
        ,$.if_statement
        ,$.for_statement
        ,$.while_statement
        ,$.match_statement
        ,$.enum_declaration
        ,$.type_declaration
        ,$.type_extension
        ,$.expression_statement
        // Verification Only
        ,$.test_statement
        ,$.restrict_statement
      )
    ))
    ,scope_statement: $ => prec.left('statement', seq(
      '{'
      ,repseq($.statement)
      ,'}'
    ))
    ,pipestage_statement: $ => prec.left('statement', seq(
      field('scope', $.scope_statement)
      ,repeat1(seq('#>', field('scope', $.scope_statement)))
    ))
    ,assignment_or_declaration_statement: $ => prec.right(seq(
      $._assignment_or_declaration
      ,$._semicolon
    ))
    ,function_call_statement: $ => prec.right(seq(
      $.simple_function_call
      ,$._semicolon
    ))
    ,control_statement: $ => prec.right(seq(
      field('type', choice(
        // TODO: Decide which keywords to use
        'ret', 'return', 'cont', 'continue', 'brk', 'break', 'last'
      ))
      ,field('argument', optional($._expression))
      ,$._semicolon
    ))
    ,enum_declaration: $ => prec.left(seq(
      'enum'
      ,field('name', $.identifier)
      ,field('type', optional($.type_cast))
      ,field('definition', optseq('=', $._expression))
      ,$._semicolon
    ))
    ,type_declaration: $ => prec.left(seq(
      optional('pub')
      ,'type'
      ,field('name', $.identifier)
      ,field('definition', optseq('=', choice(
        $._expression
        ,$._type
      )))
      ,$._semicolon
    ))
    ,type_extension: $ => prec.left(seq(
      optional('pub')
      ,'type'
      ,field('name', $.identifier)
      ,'extends'
      ,field('parent', $._expression)
      ,'with'
      ,$.tuple
      ,$._semicolon
    ))
    ,if_statement: $ => prec.left('statement', seq(
      optional('unique')
      ,'if'
      ,field('condition', $._expression)
      ,field('code', $.scope_statement)
      ,field('elif', repseq(
        'elif'
        ,field('condition', $._expression)
        ,field('code', $.scope_statement)
      ))
      ,field('else', optseq(
        'else'
        ,field('code', $.scope_statement)
      ))
    ))
    ,for_statement: $ => seq(
      'for'
      ,field('index', $.identifier_list) // NOTE: maybe constraint to max 3 (elem,index,key)
      ,'in'
      ,choice(
        field('ref',
          seq(
            'ref'
            ,$.identifier  // not allowed in for expression
          )
        )
        ,field('data', $.expression_list)
      )
      ,field('code', $.scope_statement)
    )
    ,while_statement: $ => seq(
      'while'
      ,field('condition', $._expression)
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
      field('condition', choice(
        seq('==', $._expression)
        ,seq('in', $.expression_list)
        ,$._expression
        ,'else'
      ))
      ,field('code', $.scope_statement)
    ))
    ,expression_statement: $ => prec.right(seq(
      choice(
        $.identifier
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
        ,$.dot_expression
      )
      ,$._semicolon
    ))
    ,test_statement: $ => prec.right(seq(
      'test'
      ,field('name', $.string_literal)
      ,field('condition', optseq('when', $._expression))
      ,field('code', $.scope_statement)
    ))
    ,restrict_statement: $ => prec.right(seq(
      'restrict'
      ,field('name', $.string_literal)
      ,field('condition', $._expression)
      ,field('code', $.scope_statement)
    ))
    ,expression_list: $ => prec.left(seq(
      field('item', $._expression)
      ,repseq(',', field('item', $._expression))
    ))

    // Function Call
    ,simple_function_call: $ => prec.left('simple_function_call', seq(
      field('function', $._restricted_expression)
      ,field('argument', $.expression_list)
    ))

    // Tuple
    ,tuple: $ => prec.left(seq(
      '(', optional($.tuple_list), ')'
    ))
    ,tuple_list: $ => prec.left('tuple_list', seq(
      repeat(',')
      ,field('item', $._tuple_item)
      ,repeat(seq(repeat1(','), field('item', $._tuple_item)))
      ,repeat(',')
    ))
    ,_tuple_item: $ => prec.left(choice(
      $._expression
      ,$._assignment_or_declaration
    ))
    
    // Assignment/Declaration
    ,_assignment_or_declaration: $ => choice($.assignment, $.declaration)
    ,assignment: $ => prec.right(seq(
      choice(
        field('debug', optional('debug'))
        ,field('overflow', optional(choice('wrap', 'saturate')))
      )
      ,field('lvalue', $.expression_list)
      ,field('operator', $.assignment_operator)
      ,field('delay', optional(choice($.cycle_select, '#')))
      ,field('rvalue', choice(
        $._expression
        ,$.simple_function_call
      ))
    ))
    ,declaration: $ => seq(
      field('comptime', optional('comptime'))
      ,choice(
        seq(field('scope', optional('pub')), field('qualifier', $.type_qualifier))
        ,seq(field('scope', 'pub'))
      )
      ,field('lvalue', $.expression_list)
      ,optseq(
        field('operator', $.assignment_operator)
        ,field('delay', optional(choice($.cycle_select, '#')))
        ,field('rvalue', choice(
          $._expression
          ,$.simple_function_call
        ))
      )
    )
    ,function_definition: $ => seq(
      field('func_type', choice('fun', 'proc'))
      ,field('capture', optseq('[', $.capture_list, ']'))
      ,field('generic', optseq('<',  $.identifier_list, '>'))
      ,field('input', optional($.tuple))
      ,field('output', optseq('->', $.tuple))
      ,field('condition', optseq('where', $._expression))
      ,field('code', $.scope_statement)
    )
    ,capture_list: $ => listseq1(
      field('identifier', $.identifier), optseq('=', field('expression', $._expression))
    )
    ,identifier_list: $ => prec.left(listseq1(field('item', $.identifier)))

    // Expressions
    ,_expression: $ => prec.left('expression', choice(
      $.type_specification
      ,$.type_cast
      ,$.unary_expression
      ,$.binary_expression
      ,$._restricted_expression
    ))
    ,selection: $ => choice(
      $.member_selection
      ,$.bit_selection
      ,$.cycle_selection
    )
    ,member_selection: $ => prec.right('member_selection', seq(
      field('argument', $._restricted_expression)
      ,field('select', $.member_select)
    ))
    ,bit_selection: $ => prec.right('bit_selection', seq(
      field('argument', $._restricted_expression)
      ,field('select', $.bit_select)
    ))
    ,cycle_selection: $ => prec.right('cycle_selection', seq(
      field('argument', $._restricted_expression)
      ,field('select', $.cycle_select)
    ))
    ,type_specification: $ => prec.left('type_spec', seq(
      field('argument', $._restricted_expression)
      ,choice(':', 'is')
      ,field('type', $._type)
      ,field('optional', optional('?'))
    ))
    ,unary_expression: $ => prec.left('unary', seq(
      field('operator', choice('!', 'not', '~', '-', '...', 'no', 'unless', 'when'))
      ,field('argument', $._expression)
    ))
    ,optional_expression: $ => prec.right(seq(
      field('argument', $._expression)
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
      ].map(([operator, precedence]) =>
        prec.left(precedence, seq(
          field('left', $._expression)
          ,field('operator', operator)
          ,field('right', $._expression)
        ))
      )
    )
    ,dot_expression: $ => prec.left('dot', seq(
      field('item', $._restricted_expression)
      ,repeat1(prec.left('dot_sub', seq('.', field('item', $._restricted_expression))))
    ))
    ,function_call: $ => prec.right('function', seq(
      field('function', $._restricted_expression)
      ,field('argument', $.tuple)
    ))
    ,for_expression: $ => prec.right('expression', alias($.for_statement, $.for_expression))
    ,if_expression: $ => prec.right('expression', alias($.if_statement, $.if_expression))
    ,match_expression: $ => prec.right('expression', alias($.match_statement, $.match_expression))
    ,scope_expression: $ => prec.right('expression', alias($.scope_statement, $.scope_expression))
    ,_restricted_expression: $ => prec('expression', choice(
      $.identifier
      ,$.constant
      ,$.selection
      ,$.function_call
      ,$.function_definition
      ,$.tuple
      ,$.optional_expression
      ,$.dot_expression
      ,$.for_expression
      ,$.if_expression
      ,$.match_expression
      ,$.scope_expression
    ))

    // Operators
    ,assignment_operator: $ => token(choice(
      '=', '+=', '-=', '*=', '/=', '|=', '&=', '^=', '<<=', '>>=', '++=', 'or=', 'and='
    ))

    // Selects
    ,select: $ => seq(
      '['
      ,optional(choice(
        field('list', $.expression_list)
        // TODO: clean this up
        ,field('open_range', seq($._expression, '..'))
        ,field('from_zero', seq(choice('..=', '..<', '..+'), $._expression))
      ))
      ,']'
    )
    ,member_select: $ => prec.right('select', repeat1($.select))
    ,bit_select: $ => prec.right('select', seq(
      '@', field('type', optional($.bit_select_type)), field('select', $.select)
    ))
    ,bit_select_type: $ => choice('|', '&', '^', '+', 'sext', 'zext')
    ,cycle_select: $ => seq('#', $.select)

    // Variable Properties
    ,type_qualifier: $ => choice('var', 'let', 'reg', 'ref', 'alias')

    // Types
    ,type_cast: $ => prec.left('type_cast', seq(
      choice(':', 'is')
      ,field('type', $._type)
      ,field('optional', optional('?'))
    ))
    ,_type: $ => prec.left('type', choice(
      $.primitive_type
      ,$.array_type
      ,$.enum_type
      ,$.function_type
      ,$.expression_type
    ))
    ,expression_type: $ => prec.left('expression_type', choice(
      $.identifier
      ,$.constant
      // NOTE: Use dot instead of select to avoid ambiguity
      //       `:  some_type[2]  ` means an array of `some_type` with a size of 2
      //       `:  some_type.2   ` means the type of the element at position 2 of `some_type`
      ,$.tuple
      ,$.for_expression
      ,$.if_expression
      ,$.match_expression
      ,$.scope_expression
      // NOTE: These expressions need to be restricted due to ambiguity
      //       `a: b:int.c`
      ,$.dot_expression_type
      ,$.function_call_type
      ,$.mixin_type
    ))
    ,dot_expression_type: $ => prec.right('dot_type', seq(
      field('item', $.expression_type)
      ,repeat1(prec.right('dot_type_sub', seq('.', field('item', $.expression_type))))
    ))
    ,function_call_type: $ => prec.right('function_call_type', seq(
      field('function', $.expression_type)
      ,field('argument', $.tuple)
    ))
    ,mixin_type: $ => prec.left('mixin_type', seq(
      $._type
      ,repeat1(prec.left('mixin_type_sub', seq('++', $._type)))
    ))
    ,array_type: $ => prec.left('array_type', seq(
      optional(field('base', choice(
        $.primitive_type
        ,$.array_type
        ,$.enum_type
        ,$.function_type
        ,$.expression_type
      )))
      ,field('length', $.select)
    ))
    ,enum_type: $ => prec.left('enum_type', seq('enum', $.tuple))
    ,function_type: $ => prec.left('function_type', seq(
      field('type', choice('fun', 'proc'))
      ,field('generic', optseq('<',  $.identifier_list, '>'))
      ,field('input', optional($.tuple))
      ,field('output', optseq('->', $.tuple))
    ))
    ,primitive_type: $ => prec.left(choice(
      $.unsized_integer_type
      ,$.sized_integer_type
      ,$.parameter_sized_integer_type
      ,$.bounded_integer_type
      ,$.range_type
      ,$.string_type
      ,$.boolean_type
      ,$.type_type
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
      ,choice(
        seq(
          optseq('max', '=')
          ,field('max_value', $._expression)
          ,','
          ,optseq('min', '=')
          ,field('min_value', $._expression)
        )
        ,seq(
          field('min_value', $._expression)
          ,'..'
        )
        ,seq(
          '..'
          ,field('max_value', $._expression)
        )
        ,field('expression', $._expression)
      )
      ,')'
    )
    ,sized_integer_type: $ => token(/[siu]\d+/)
    ,parameter_sized_integer_type: $ => seq(
      field('sign', /[siu]/)
      ,'('
      ,field('parameter', $._expression)
      ,')'
    )
    ,range_type: $ => prec.left('range_type', seq('range', '(', $._expression, ')'))
    ,string_type: $ => token('string')
    ,boolean_type: $ => token('boolean')
    ,type_type: $ => token('type')

    // Identifiers
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

    // Constants
    ,constant: $ => choice(
      $.number
      ,$.bool_literal
      ,$.string_literal
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

    // Booleans
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
    ,_space:   $ => token(/[\s\p{Zs}\uFEFF\u2060\u200B]/)
    ,_comment: $ => token(choice(
      /\/\/.*/
      ,seq('/*', /[^*]*\*+([^/*][^*]*\*+)*/, '/')
    ))
    ,_semicolon: $ => choice($._automatic_semicolon, ';')
  }
});
