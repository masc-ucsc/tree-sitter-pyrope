; highlights.scm
((identifier) @variable (#set! "priority" 95))

; Operators

[
  "&"
  "^"
  "|"
  "~&"
  "~^"
  "~|"
  "<"
  "<="
  ">"
  ">="
  "=="
  "!="
 
  "!"
  "~"
  "-"
  "..."

  "?"

  "..="
  "..<"
  "..+"
  ">>"
  "<<"
  "!&"
  "!^"
  "!|"
  "*"
  "/"
  "%"
  "+"
  "-"
  "|>" 
  "++"

  (assignment_operator)
] @operator 

[
  "and"
  "!and"
  "or"
  "!or"
  "has"
  "!has"
  "case"
  "!case"
  "equals"
  "!equals"
  "does"
  "!does"
  "is"
  "!is"

  "not"

  "step"
  "implies"
  "!implies"
  "and_then"
  "or_else"
  "in"
  "!in"
] @keyword.operator

; Keywords

[
  "in"

  "enum"

  "var"
  "let"
  "reg"
] @keyword

(fun_tok) @keyword.function
(proc_tok) @keyword.function

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

(constant) @constant

; Types

(primitive_type) @type.builtin

(expression_type) @type

(function_type) @type

; Function calls

(simple_function_call
  function: (complex_identifier) @function.call)

(function_call
  function: (complex_identifier) @function.call)

(function_inline
  fun_name: (identifier) @function.call)

; Parameters 

(simple_function_call
  argument: (expression_list
    item: (complex_identifier) @parameter))

(function_call
  argument: (tuple
    (tuple_list
      item: (complex_identifier) @parameter)))

; Function definitions 

(function_definition_statement
  lvalue: (complex_identifier) @function)

(assignment_or_declaration_statement
  lvalue: (complex_identifier) @function
  rvalue: (lambda))

(simple_assignment
  lvalue: (identifier) @function
  rvalue: (lambda))

; Verification 

((identifier) @debug
  (#any-of? @debug "assert" "cassert" "optimize" "verify" "test"))

; Comments
(comment) @comment @spell
