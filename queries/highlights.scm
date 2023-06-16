; highlights.scm

; Operators

[
  "|"
  "^"
  "~&"
  "~|"
  "~^"
  ">>"
  "<<"

  ":"
] @operator 

(assignment_operator) @operator

(unary_expression 
  operator: _ @operator)

(binary_expression
  operator: _ @operator)

; Keywords

[
  "in"

  "enum"

  "var"
  "let"
  "reg"
] @keyword

(fun_tok) @keyword
(proc_tok) @keyword

[
  "test"
] @keyword.verification

[
  "if"
  "elif"
  "else"
  "match"
  "while"
  "for"
] @conditional

["{" "}" "(" ")" "[" "]"] @punctuation.bracket
["," "."] @punctuation.delimiter

; Types

((constant) @boolean
  (#any-of? @boolean "true" "false"))
((identifier) @boolean
  (#any-of? @boolean "true" "false"))

[
  "int"

] @type

(unsized_integer_type) @type
(sized_integer_type) @type
(bounded_integer_type) @type

(constant) @constant

(boolean_type) @type
(string_type) @type

(typed_identifier
  (identifier) @identifier)
  
(type_cast
  (_) @type)

(type_specification
  argument: (complex_identifier) @variable
  (_) @type)

(primitive_type) @type

; Functions

(
  (expression_list) @function
  (function_definition)
)


(function_call
  (complex_identifier) @function)

(
  (identifier) @debug
  (#any-of? @debug "assert" "cassert" "verify")
)

(cycle_select) @function.pipeline
(pipestage_scope_statement
  "#>" @function.pipeline
  fsm: (_) @function.pipeline)

; Variables

(assignment_or_declaration_statement
  lvalue: (complex_identifier_list
    item: (complex_identifier) @variable))

(assignment_or_declaration_statement
  lvalue: (complex_identifier) @variable)

(
  (identifier) @variable
  (assignment_operator)
)

; Comments
(comment) @comment @spell
