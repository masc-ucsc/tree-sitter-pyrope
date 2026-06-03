; indents.scm — Pyrope (modern nvim-treesitter indent captures)

; Indent the body of blocks, tuples, argument lists and parens.
[
  (scope_statement)
  (tuple)
  (tuple_sq)
  (paren_group)
  (arg_list)
  (match_expression)
  (enum_definition)
] @indent.begin

; Dedent the line that closes a block / group.
["}" ")" "]"] @indent.branch

(comment) @indent.ignore
