; highlights.scm

; Operators

[
  "!"
  "not"
  "~"
  "-"
  "+"
  "*"
  "/"
  "&"
  "|"
  "^"
  "~&"
  "~|"
  "~^"
  ">>"
  "<<"
  "and"
  "or"
  "implies"
  "!and"
  "!or"
  "!implies"
  "in"
  "!in"
  "++"
  "=="
  ">="
  "<="
  "!="
  ">"
  "<"

 (assignment_operator)
] @operator 

; Keywords

[
  "in"

  "let"
  "var"
  "reg"

  "proc"
  "fun"
  "enum"
] @keyword

[
  "while"
  "for"
] @repeat

[
  "if"
  "elif"
  "else"
  "match"
  "test"
] @conditional

; Types

[
  "int"

] @type

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
(string_type) @string

(constant) @number

; Functions

(
  (expression_list) @function
  (function_definition)
)

(simple_function_call
  (identifier) @function)

(function_call
  (identifier) @function)

; Variables

(assignment_or_declaration_statement
  lvalue: (expression_list
    item: (identifier) @variable))

(
  (identifier) @variable
  (assignment_operator)
)
