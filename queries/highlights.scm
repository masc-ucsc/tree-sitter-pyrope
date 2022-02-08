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
  "type"
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

(function_call
  name: (identifier) @Special
)

(number) @Constant
(string_literal) @string

(declaration
  (type_qualifier) @Type
)


