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

] @operator 

(assignment_operator) @operator

(unary_expression 
  operator: _ @operator)

(binary_expression
  operator: _ @operator)

; Keywords

[
  "in"

  "proc"
  "fun"
  "enum"
] @keyword

(var_or_let_or_reg) @keyword

[
  "test"
  "restrict"
] @keyword.verification

[
  "while"
  "for"
] @repeat

[
  "if"
  "elif"
  "else"
  "match"
] @conditional

; Types

[
  "int"

] @type

(unsized_integer_type) @number
(sized_integer_type) @number
(bounded_integer_type) @number

(constant) @constant

(boolean_type) @boolean

(string_type) @string

(typed_identifier
  (identifier) @identifier)
  
(type_cast
  (expression_type
    (identifier) @type))

(type_specification
  argument: (identifier) @variable
  attribute: (attributes
    attr: (_
      (tuple_list
        (identifier) @type))))

(primitive_type) @type


; Functions

(
  (expression_list) @function
  (function_definition)
)

(simple_function_call
  (identifier) @function)

(function_call
  (identifier) @function)

(
  (identifier) @function.verification
  (#match? @function.verification "assert|assume|verify|waitfor")
)

(cycle_select) @function.pipeline
(pipestage_scope_statement
  "#>" @function.pipeline
  fsm: (_) @function.pipeline)

; Variables

(assignment_or_declaration_statement
  lvalue: (expression_list
    item: (identifier) @variable))

(
  (identifier) @variable
  (assignment_operator)
)

