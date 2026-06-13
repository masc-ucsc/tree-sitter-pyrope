; highlights.scm — Pyrope (regenerated against the current grammar)
; Ordering: the generic (identifier) rule is first; more specific rules follow
; so they override it (Neovim's highlighter lets a later match win).

(identifier) @variable

; Comments

(comment) @comment @spell

; Literals

(integer_literal) @number
(bool_literal) @boolean
(string_literal) @string
(interpolated_string_literal) @string
(unknown_literal) @constant.builtin

; Types

[
  (uint_type)
  (sint_type)
  (bool_type)
  (string_type)
] @type.builtin

[
  "int"
  "uint"
  "unsigned"
  "integer"
] @type.builtin

[
  (expression_type)
  (array_type)
  (dot_expression_type)
  (function_call_type)
] @type

; A bare identifier in type position is a (user) type name
(expression_type (identifier) @type)

; Operators (symbolic)

[
  (op_add) (op_sub) (op_mul) (op_div) (op_mod)
  (op_shl) (op_sra)
  (op_bit_and) (op_bit_or) (op_bit_xor) (op_bit_not)
  (op_eq) (op_ne) (op_lt) (op_le) (op_gt) (op_ge)
  (op_unary_minus) (op_spread) (op_step)
  (op_range_inclusive) (op_range_exclusive) (op_range_count)
  (op_log_not)
  (reduction_and) (reduction_or) (reduction_xor) (reduction_popcount)
  (assignment_operator)
  (open_all)
] @operator

; Bare comparison/bitwise tokens also appear directly in match cases
[
  "!=" "==" "<" "<=" ">" ">="
  "&" "^" "|"
  ".."
] @operator

; Operators that read as words
[
  (op_log_and) (op_log_or)
  (op_is) (op_in) (op_has) (op_does)
  (op_case) (op_equals) (op_implies)
] @keyword.operator

[
  "and" "or"
  "is" "in" "has" "does"
  "case" "equals"
] @keyword.operator

; Keywords

[
  "if" "elif" "else" "match"
] @keyword.conditional

[
  "for" "while" "loop"
] @keyword.repeat

"return" @keyword.return

[
  "import" "as"
] @keyword.import

[
  (comb_lambda) (mod_lambda) (pipe_lambda)
] @keyword.function

[
  (const_decl) (mut_decl) (reg_decl) (stage_decl)
  (comptime_modifier)
] @keyword

[
  "break" "continue"
  "unique" "ref" "wrap" "sat" "pipe"
  "spawn" "stage" "impl" "enum" "type" "test"
] @keyword

; Function definitions

(lambda name: (identifier) @function)
(assignment lvalue: (identifier) @function rvalue: (lambda))
(assignment
  lvalue: (typed_identifier identifier: (identifier) @function)
  rvalue: (lambda))

; Function calls

(function_call_expression function: (identifier) @function.call)
(function_call_type function: (identifier) @function.call)

; Parameters

(arg_list (typed_identifier identifier: (identifier) @variable.parameter))

; Member access (the trailing field of a dot expression)

(dot_expression (identifier) @variable.member .)

; Pyrope attributes (e.g. foo.[bits])

(attribute_list name: (identifier) @attribute)

; Builtins / verification

((identifier) @function.builtin
  (#any-of? @function.builtin
    "puts" "print" "format"
    "assert" "cassert" "cputs" "verify" "optimize"))

; Punctuation

["(" ")" "[" "]" "{" "}"] @punctuation.bracket
["," ";" ":" "::" "->" "."] @punctuation.delimiter
["@" "#"] @punctuation.special
