
//const PREC = { }

//const IDENTIFIER_CHARS = /[^\x00-\x1F\s:;`"'@$#.,|^&<=>+\-*/\\%?!~()\[\]{}]*/;
//const LOWER_ALPHA_CHAR = /[^\x00-\x1F\sA-Z0-9:;`"'@$#.,|^&<=>+\-*/\\%?!~()\[\]{}]/;

module.exports = grammar({
  name: 'pyrope',

  externals: (_) => [],

  extras: $ => [
    / /
    ,/\t/
    , $._comment
  ]

  ,word: $ => $.trivial_identifier

  ,conflicts: $ => [ ] // No conflicts SLR grammar :D

  ,supertypes: $ => [
    $.stmt_base
    //,$.typecase
  ]

  ,inline: $ => [
    $.comma_tok
    ,$.dot_tok
    ,$.ok_tok
    ,$.ck_tok
    ,$.ob_tok
    ,$.cb_tok
    ,$.op_tok
    ,$.cp_tok
    ,$.colon_tok
    // ,$.stmt_seq
    // ,$.if_elif
    // ,$.else_line
    ,$.expr_binary_cont
    ,$.expr_logical_cont
    ,$.expr_range_cont
    ,$.expr_cont
    ,$.factor_first
    //,$.factor_second
    ,$.factor_simple
    ,$.factor_simple_fcall
    ,$.variable_base_field
    ,$.literal_select_field
    ,$.tuple_seq
    ,$.stmt_base
    // ,$.factor_simple_fcall
    // ,$.select_sequence
  ]

  ,rules: {

    start: $ =>
      seq(
        optional($._newline)
        ,optional($.stmt_seq)
      )

    ,stmt_seq: $ =>
      seq(
         $.stmt_base
        ,repeat(
          seq(
            $._newline
            ,$.stmt_base
          )
        )
        ,optional($._newline)
      )

    ,stmt_base: $ =>
      choice(
        $.type_stmt
        ,$.while_stmt
        ,$.assign_decl_stmt
        ,$.multiple_stmt
        ,$.tuple_pipe
        ,$.ctrl_stmt
        ,$.scope_pipe_stmt
        ,$.try_stmt
        ,$.factor_expr_stmt
        ,$.constrained_scope_stmt
      )

    ,gate_stmt: $ =>
      seq(
        choice($.when_tok, $.unless_tok)
        ,field("cond",$.expr_entry)
      )

    ,repipe_expr: $ =>
      seq(
        $.repipe_tok
        ,$.variable_base_field
        ,repeat(
          seq(
            repeat1($.comma_tok)
            ,$.variable_base_field
          )
        )
        ,$.to_tok
        ,$.variable_base_field
        ,repeat(
          seq(
            repeat1($.comma_tok)
            ,$.variable_base_field
          )
        )
      )

    ,if_stmt: $ =>
      seq(
        optional($.unique_tok)
        ,$.if_tok
        ,field("cond",$.expr_entry)
        ,field("code",$.scope_stmt)
        ,repeat($.if_elif)
        ,optional($.else_line)
      )

    ,match_stmt: $ =>
      seq(
        $.match_tok
        ,$._expr_seq1
        ,$.ok_tok
        ,repeat($.match_stmt_line)
        ,optional($.match_stmt_else)
        ,$.ck_tok
      )

    ,match_stmt_line: $ =>
      seq(
        optional($._newline)
        ,choice(
          $.expr_logical_cont
          ,field("in",$.in_expr_seq1)
          ,field("does",$.does_typecase)
        )
        ,field("code",$.scope_stmt)
      )

    ,match_stmt_else: $ =>
      seq(
        $.else_tok
        ,field("else_code",$.scope_stmt)
      )

    ,if_elif: $ =>
      seq(
        $.elif_tok
        ,field("cond",$.expr_entry)
        ,field("code",$.scope_stmt)
      )

    ,else_line: $ =>
      seq(
        $.else_tok
        ,field("else_code",$.scope_stmt)
      )

    ,while_stmt: $ =>
      seq(
        $.while_tok
        ,field("cond",$.expr_entry)
        ,field("code",$.scope_stmt)
      )

    ,for_stmt: $ =>
      seq(
        $.for_tok
        ,optional($.mut_tok)
        ,$.trivial_identifier  // value
        ,optional(
          seq(
            repeat1($.comma_tok)
            ,field("id2",$.trivial_identifier) // index
            ,optional(
              seq(
                repeat1($.comma_tok)
                ,field("id3",$.trivial_identifier) // key
              )
            )
          )
        )
        ,field("in",$.in_expr_seq1)
        ,field("code",$.scope_stmt)
      )

    ,ctrl_stmt: $ =>
      seq(
        choice(
          $.return_tok
          ,$.continue_tok
          ,$.break_tok
        )
        ,optional($._expr_seq1)
        ,optional($.gate_stmt)
      )

    ,type_stmt: $ =>
      seq(
        optional($.pub_tok)
        ,$.type_tok
        ,$.variable_base_field
        ,choice(
          seq(
            $.implements_tok
            ,$.variable_base_field
            ,$.with_tok
            ,$.tuple
          )
          ,seq(
            optional($.typecase)
            ,$.assignment_cont
            ,optional($.gate_stmt)
          )
        )
      )

    ,start_assign_flags: $ =>
      choice(
        seq(
          choice($.defer_read_tok, $.defer_write_tok)
          ,optional($.pub_tok)
          ,optional($.attributes)
        )
        ,seq(
          $.pub_tok
          ,optional($.attributes)
        )
        ,$.attributes
      )

    // Very close to multiple_stmt
    ,assign_decl_stmt: $ =>
      seq(
        optional($.start_assign_flags)
        ,choice(
          $.let_tok
          ,$.var_tok
          ,$.reg_tok
        )
        ,field("lhs",$._expr_seq1)
        ,optional(
          $.assignment_cont2
        )
        ,optional($._assign_multiple_end)
      )

    ,tuple_pipe: $ =>
      seq(
        $.tuple
        ,$._assign_multiple_end
      )

    ,multiple_stmt: $ =>
      seq(
        optional($.start_assign_flags)
        ,$.factor_simple_fcall
        ,optional($.expr_cont)
        ,optional(
          choice(
            seq(
              $.comma_tok
              ,$._expr_simple_seq1
              ,$.assignment_cont2
            )
            ,seq(
              $._expr_simple_fcall_seq1
            )
            ,$.assignment_cont2
          )
        )
        ,optional($._assign_multiple_end)
      )

    ,_assign_multiple_end: $ =>
      choice(
        repeat1($.fcall_pipe)
        ,$.gate_stmt
      )

    ,fcall_pipe: $ =>
      seq(
        $.pipe_tok
        ,$.factor_second
      )

    ,assignment_cont: $ =>
      seq(
        choice($.equal_tok, $.plusplus_equal_tok)
        ,$.expr_entry
      )

    ,assignment_cont2: $ =>
      seq(
        field("assign",
          choice(
            choice($.equal_tok, $.plusplus_equal_tok)
            ,$.assign_tok
            ,seq(
              $.eq_pound_tok
              ,optional($.selector1)
            )
          )
        )
        ,choice(
          $.repipe_expr
          ,seq(
            $.factor_first
            ,optional(
              choice(
                $.expr_cont
                ,seq(
                  $.comma_tok
                  ,$._expr_simple_seq1
                )
                ,seq(
                  $._expr_simple_fcall_seq1
                )
              )
            )
          )
        )
      )

    ,scope_stmt: $ =>
      seq(
        optional($.attributes)
        ,$.ok_tok
        ,optional($._newline)
        ,optional($.stmt_seq)
        ,$.ck_tok
      )

    ,scope_pipe_stmt: $ =>
      choice(
        seq(
          choice($.defer_read_tok, $.defer_write_tok)
          ,$.scope_stmt
        )
        ,seq(
          $.scope_stmt
          ,repeat(
            seq(
              $.scope_pipe_tok
              ,$.ok_tok
              ,optional($._newline)
              ,optional($.stmt_seq)
              ,$.ck_tok
            )
          )
        )
      )

    ,scope_expr: $ =>
      seq(
        $.ok_tok
        ,optional($._newline)
        ,optional($.stmt_seq)
        ,$.ck_tok
      )

    ,lambda_def: $ =>
      seq(
        $.ok_lambda_tok
        ,$.lambda_def_constrains
        ,optional($.stmt_seq)
        ,$.ck_tok
      )

    ,try_stmt: $ =>
      seq(
         $.try_tok
        ,field("code",$.scope_stmt)
        ,optional($.else_line)
      )

    ,constrained_scope_stmt: $ =>
      seq(
        $.restrict_tok
        ,choice($.string_literal,$.simple_string_literal)  // ID or explanation
        ,optional($.gate_stmt)
        ,field("code",$.scope_stmt)
      )

    ,expr_entry: $ =>
      seq(
        $.factor_first
        ,optional(
          choice(
            $.expr_cont
            ,$.lambda_def
          )
        )
        ,field("in",optional($.in_range))
      )

    ,expr_simple_entry: $ =>
      seq(
        $.factor_simple
        ,optional($.expr_cont)
      )

    ,expr_simple_fcall_entry: $ =>
      seq(
        $.factor_simple_fcall
        ,optional($.expr_cont)
      )

    ,expr_cont: $=>
      choice(
        $.does_typecase
        ,repeat1(
          choice(
            $.expr_binary_cont
            ,$.expr_range_cont
          )
        )
      )

    ,does_typecase: $ =>
      seq(
        choice(
          $.does_tok
          ,seq($.not_tok, $.equals_tok)
          ,$.equals_tok
        )
        ,$.factor_second
      )

    ,expr_binary_cont: $ =>
      choice(
        seq(
          $.binary_op_tok
          ,$.factor_second
        )
        ,$.expr_logical_cont
      )

    ,expr_logical_cont: $ =>
      seq(
        $.logical_op_tok
        ,$.factor_second
      )

    ,expr_range_cont: $ =>
      choice(
        $.range_open_tok
        ,seq(
          $.range_op_tok
          ,$.factor_second
          ,optional(
            seq(
              $.by_tok
              ,$.factor_second
            )
          )
        )
      )

    ,in_range: $ =>
      seq(
        optional($.not_tok)
        ,$.in_tok
        ,choice(
          seq(
            $.factor_simple_fcall
            ,optional($.expr_range_cont)
          )
          ,$.tuple
        )
      )

    ,in_expr_seq1: $ =>
      seq(
        $.in_tok
        ,$._expr_seq1
      )

    ,factor_first: $ =>
      choice(
        $.factor_second
        ,$.expr_range_cont   // open range
      )

    ,factor_second: $ =>
      choice(
        $.factor_simple
        ,$.if_stmt
        ,$.for_stmt
        ,$.match_stmt
        ,$.scope_expr
        ,$.lambda_def
        ,seq(
          $.tuple
          ,repeat($.select_sequence)
          ,optional($.variable_base_last)
        )
      )

    ,factor_typecase: $ =>
      choice(
        $.factor_simple_fcall
        ,seq(
          $.inplace_concat_tok
          ,$.factor_second
        )
        ,$.lambda_def
        ,seq(
          $.tuple
          ,repeat($.select_sequence)
          ,optional($.qmark_tok)
        )
      )


    ,factor_simple: $ =>
      choice(
        $.factor_simple_fcall
        ,seq(
          $.inplace_concat_tok
          ,$.factor_second
        )
        ,$.typecase
      )


    ,factor_simple_fcall: $ =>
      choice(
        seq(
          choice($.unary_op_tok, $.not_tok)
          ,$.tuple
        )
        ,seq(
          optional(choice($.unary_op_tok, $.not_tok))
          ,$.fcall_or_variable
          ,optional($.typecase)
        )
      )

    ,factor_expr_stmt: $ =>
      choice(
        $.if_stmt
        ,$.match_stmt
        ,$.for_stmt
        ,$.expr_range_cont   // open range
        ,$.lambda_def
        ,seq(
          $.tuple
          ,repeat($.select_sequence)
          ,optional($.variable_base_last)
        )
      )

    ,factor_expr_start: $ =>
      choice(
        $.typecase
        ,$.scope_expr
        ,seq(
          choice($.unary_op_tok, $.not_tok)
          ,choice(
            $.tuple
            ,seq(
              $.fcall_or_variable
              ,optional($.typecase)
            )
          )
        )
      )

    ,fcall_or_variable: $ =>
      seq(
        choice(
          $.variable_base_field
          ,$.literal_select_field
        )
        ,optional($.variable_prev_field)
        ,optional($.variable_base_last)
      )

    ,attributes: $ =>
      choice(
         seq($.comptime_tok, optional($.debug_tok))
        ,$.debug_tok
      )

    ,lambda_def_constrains: $ =>
      seq(
        choice(
          $.trivial_or_caps_identifier_seq1 // just trivial sequence IDs no types no nothing or complex pattern
          ,seq(
            field("capture"
              ,optional($.capture_list)
            )
            ,field("input"
              ,optional($.tuple)
            )
            ,field("output"
              ,optional(
                seq(
                  $._arrow_tok
                  ,choice(
                    $.tuple
                    ,$.typecase
                  )
                )
              )
            )
            ,optional(
              seq(
                $.where_tok
                ,field("cond",$.expr_entry)
              )
            )
          )
        )
        ,$.bar_tok
        ,optional($._newline)
      )

    ,capture_list: $ =>
      seq(
        $.ob_tok
        ,optional($.tuple_seq)
        ,$.cb_tok
      )

    ,trivial_or_caps_identifier_seq1: $ =>
      seq(
         repeat($.comma_tok)
        ,seq(
          choice($.trivial_identifier,$.all_cap_identifier)
          ,repeat(
            seq(
               repeat1($.comma_tok)
              ,choice($.trivial_identifier,$.all_cap_identifier)
            )
          )
        )
        ,repeat($.comma_tok)
      )

    ,typecase: $ =>
      seq(
        $.colon_tok
        ,choice(
          $.factor_typecase
          ,repeat1($.selector0)   // untyped Array
          ,$.qmark_tok            // just qmark
        )
      )

    ,variable_base_field: $ =>
      seq(
        choice($.complex_identifier, $.trivial_identifier, $.all_cap_identifier)
        ,repeat($.select_sequence)
      )

    ,literal_select_field: $ =>
      seq(
        choice(
          $.bool_literal
          ,$.natural_literal
          ,$.string_literal
          ,$.simple_string_literal
        )
        ,repeat($.select_sequence)
      )

    ,variable_base_last: $ =>
       choice(
         $.qmark_tok
         // ,$.bang_tok  // CONFLICT
         ,repeat1($.variable_bit_sel)
       )

    ,select_sequence: $ =>
      choice(
        $.dot_selector
        ,seq(optional($.qmark_tok), $.selector1)
        ,$.tuple
      )

    ,dot_selector: $ =>
      seq(
        choice($.qmark_dot_tok, $.dot_tok)
        ,choice($.trivial_identifier, $.natural_literal)
      )

    ,variable_prev_field: $ =>
      seq(
         $.pob_tok
        ,$._expr_seq1
        ,$.cb_tok
      )

    ,selector1: $ =>
      seq(
         $.ob_tok
        ,$._expr_seq1
        ,$.cb_tok
      )

    ,selector0: $ =>
      seq(
         $.ob_tok
        ,optional($._expr_seq1)
        ,$.cb_tok
      )

    ,_expr_seq1: $ =>
      seq(
        repeat($.comma_tok)
        ,seq(
          $.expr_entry
          ,repeat(
            seq(
              repeat1($.comma_tok)
              ,$.expr_entry
            )
          )
        )
      )

    ,_expr_simple_seq1: $ =>
      seq(
        seq(
          $.expr_simple_entry
          ,repeat(
            seq(
              repeat1($.comma_tok)
              ,$.expr_simple_entry
            )
          )
        )
      )

    ,_expr_simple_fcall_seq1: $ =>
      seq(
        seq(
          $.expr_simple_fcall_entry
          ,repeat(
            seq(
              repeat1($.comma_tok)
              ,$.expr_simple_entry
            )
          )
        )
      )

    ,tuple: $ =>
      seq(
        $.op_tok
        ,optional($.tuple_seq)
        ,$.cp_tok
      )

    ,tuple_seq: $ =>
      choice(
        $._newline
        ,seq(
          repeat($.comma_tok)
          ,seq(
            $.tuple_entry
            ,repeat(
              seq(
                repeat1($.comma_tok)
                ,$.tuple_entry
              )
            )
          )
          ,repeat($.comma_tok)
        )
      )

    ,tuple_entry: $ =>
      choice(
        seq(
          choice($.always_after_tok, $.always_before_tok)
          ,$.assignment_cont
        )
        ,seq(
          optional($.pub_tok)
          ,optional(choice($.var_tok, $.let_tok, $.reg_tok))
          ,field("lhsrhs", $.expr_entry)
          ,optional($.assignment_cont)
        )
      )

    ,variable_bit_sel: $ =>
      seq(
        $.at_tok
        ,optional($.bit_sel_tok)
        ,$.selector0
      )

    ,bit_sel_tok: () => token(choice('sext', 'zext', '|', '&', '^', '+'))

    ,bool_literal: (_) => token(choice("true","false"))
    ,natural_literal: (_) => token(/[0-9][\?\w\d_]*/)

    ,comma_tok: () => seq(/\s*,/)
    ,qmark_dot_tok: () => seq(/\?\./)
    ,dot_tok: () => seq(/\s*\./)

    ,binary_op_tok: () =>
      token(
        seq(
          /\s*/
          ,choice(
            '++', '--' // tuple ops
            ,'+', '-', '*', '/', '|', '&', '^' // scalar op
          )
        )
      )

    ,def_op_tok: () =>
      token(
        seq(
          /\s*/
          ,choice('++', '--')
        )
      )

    ,logical_op_tok: () =>
      token(
        seq(
          /\s*/
          ,choice(
            'or', 'and', 'has', 'implies', '<<', '>>', '<', '<=', '==', '!=', '>=', '>' // logical op
          )
        )
      )

    ,_arrow_tok: () =>
      token(
        seq(
          /\s*/
          ,'->'
        )
      )


    ,else_tok: () =>
      token(
        seq(
          /\s*/
          ,'else'
        )
      )

    // No spaces before, or alias for new line statement
    ,debug_tok: () => token('debug')
    ,comptime_tok: () => token('comptime')
    ,defer_read_tok: () => token('defer_read')
    ,defer_write_tok: () => token('defer_write')

    ,always_before_tok: () => token('always_before')
    ,always_after_tok: () => token('always_after')

    ,where_tok: () => token('where')
    ,let_tok: () => token('let')
    ,union_tok: () => token('union')
    ,pub_tok: () => token('pub')
    ,var_tok: () => token('var')
    ,reg_tok: () => token('reg')

    ,elif_tok: () =>
      token(
        seq(
          /\s*/
          ,'elif'
        )
      )

    ,by_tok: $ =>
      token(
        seq(
          /\s*/
          ,'by'
        )
      )

    ,in_tok: $ =>
      token(
        seq(
          /\s*/
          ,'in'
        )
      )

    ,unary_op_tok: $ =>
      token(
        seq(
          /\s*/
          ,choice('-', '~', '!')
        )
      )

    ,restrict_tok: () => token(choice('restrict', 'fail', 'test'))

    ,implements_tok: () => token('implements')
    ,with_tok: () => token('with')

    ,unless_tok: () => token('unless')
    ,when_tok: () => token('when')
    ,repipe_tok: () => token('repipe')
    ,to_tok: () => token('to')
    ,if_tok: () => token('if')
    ,unique_tok: () => token('unique')
    ,match_tok: () => token('match')
    ,while_tok: () => token('while')
    ,for_tok: () => token('for')
    ,mut_tok: () => token('mut')

    ,return_tok: () => token('return')
    ,continue_tok: () => token('continue')
    ,break_tok: () => token('break')

    ,type_tok: () => token('type')
    ,try_tok: () => token('try')
    ,not_tok: () => token('not')
    ,equals_tok: () => token('equals')
    ,does_tok: () => token(choice('does','doesnt'))

    ,inplace_concat_tok: () => token('...')

    ,range_open_tok: () => token('..')
    ,range_op_tok: () =>
      token(
        seq(
          /\s*/
          ,choice('..<', '..=', '..+')
        )
      )

    ,equal_tok: () => token(/\s*=/)
    ,plusplus_equal_tok: () => token(/\s*\+\+=/)

    ,assign_tok: () =>
      token(
        seq(
          /\s*/
          ,choice(':=', '+=', '-=', '*=', '/=', '|=', '&=', '^=', 'or=', 'and=', '<<=', '>>=')
        )
      )
    ,eq_pound_tok: () =>
      token(
        seq(
          /\s*/
          ,choice('=#', ':=#', '++=#', '+=#', '-=#', '*=#', '/=#', '|=#', '&=#', '^=#', 'or=#', 'and=#', '<<=#', '>>=#')
        )
      )

    ,pipe_tok: () => token(/\s*\|>/)
    ,scope_pipe_tok: () => token(/#>/)

    // No ok_tok because it should have space after only if statement (more complicated per rule case)
    ,ok_tok: () => seq(/\{/)
    ,ok_lambda_tok: () => seq(/\{\|/)
    ,ck_tok: () => seq(/\s*}/)

    ,ob_tok: () => seq(/\[/) // No newline because the following line may be a expr (not a self-contained statement)
    ,cb_tok: () => seq(/\s*\]/)

    ,lt_tok: () => token('<')
    ,gt_tok: () => seq(/\s*>/)

    ,op_tok: () => seq(/\(/)
    ,cp_tok: () => seq(/\s*\)/)

    ,colon_tok: () => seq(/\s*:/)
    ,colonl_tok: () => seq(/\s*:\{/)

    ,bar_tok: () => token('|')
    ,pob_tok: () => token('#[')

    ,qmark_tok: () => token('?')
    ,bang_tok: () => token('!')
    ,at_tok: () => token('@')

    ,simple_string_literal: (_) => token(/\'[^\'\n]*\'/)

    ,string_literal: ($) =>
      seq(
        '"'
        ,repeat(choice($._escape_sequence, /[^"\\\n]+/))
        ,token.immediate('"')
      )

    ,_escape_sequence: (_) => token( prec(1, /\\./))

    //,utype: (_) => token(prec(2,/u[\d]+/))
    //,itype: (_) => token(prec(2,/i[\d]+/))
    //,btype: (_) => token(prec(2,/boolean/))

    ,complex_identifier: (_) => token(/[$%#][\.a-zA-Z\d_]*/)
    ,all_cap_identifier: (_) => token(/[A-Z][A-Z\d_]*/)


    ,trivial_identifier: (_) =>
      token(
        choice(
          /[A-Z]?[a-z_][a-zA-Z\d_]*/
          ,seq(
            '`'
            ,repeat(choice(prec(1,/\\./), /[^`\\\n]+/))
            ,token.immediate('`')
          )
        )
      )

    //,identifier: (_) => token(/[a-zA-Zα-ωΑ-Ωµ_][\.a-zA-Zα-ωΑ-Ωµ\d_]*/)

    //,_comment2: (_) => token(prec(1,/\/\/[^\n]*/))
    ,_comment: (_) => token(prec(1,/\s*\/\/[^\n]*/))
    //,doc_comment: (_) => token(seq("///", /.*/))
    //,line_comment: (_) => token(seq("//", /.*/))

    ,_newline: (_) => token(prec(-1,/\s*[;\n\r]+/))
    //,_newline: $ => repeat1(choice(/;/,/\n/,/\\\r?\n/))
  }
});
