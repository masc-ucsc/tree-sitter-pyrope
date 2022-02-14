; Operators

; Keywords
[
  "debug"
  "defer_read"
  "defer_write"
  "comptime"
] @keyword

[
  "enum"
  "extends"
  "with"
  "in"
  "when"
  "wrap"
  "saturate"
  "pub"
  "let"
  "var"
  "reg"
  "ref"
  "loc"
  "mut"
  "where"
] @keyword

[
  "and"
  "or"
  "implies"
  "and_then"
  "or_else"
] @keyword

[ 
  "ret"
  "return"
  "cont"
  "continue"
  "brk"
  "break"
  "last"
  "if"
  "elif"
  "else"
  "for"
  "while"
  "match"
  "test"
  "restrict"
] @keyword

[
  "fun"
  "proc"
] @Special

(unsized_integer_type) @Type
(sized_integer_type) @Type
(boolean_type) @Type
(string_type) @Type
(range_type) @Type
(type_type) @Type

(type_declaration
  "type" @keyword
)

(type_extension
  "type" @keyword
)

(function_call
  function: (identifier) @Special
)

(function_call_type
  function: (expression_type (identifier)) @Special
)

(number) @Constant
(string_literal) @string

(declaration
  (type_qualifier) @Type
)


