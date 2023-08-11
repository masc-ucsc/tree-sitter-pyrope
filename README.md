# tree-sitter-pyrope

A tree-sitter grammar and parser for pyrope

This repo is a tree-sitter Pyrope grammar. 

## Usage:

You will need to install tree-sitter-cli in order to use parser.

Yarn usage:
```
yarnpkg install
yarnpkg run generate
```

npm usage:
```
npm install
npm run generate
```

Example of how to run the command line tree-sitter parser:
```
./node_modules/tree-sitter-cli/tree-sitter parse -q -t ./benchtest/large1.prp
./node_modules/tree-sitter-cli/tree-sitter parse -t ./snippets/delegate.prp
```

Arguments
- -t outputs elapsed time
- -q doesnt printout constructed tree
- -D debug mode printsout constructed tree


To test some examples:
```
./test.sh
```

To run some benchmark:
```
./bench.sh
```

## Syntax highlighting:
Pyrope syntax highlighting in neovim is possible using the nvim-treesitter plugin.
Install the plugin with your package manager.

### Install the parser
To install the parser using nvim-treesitter, add this to your init.lua. 
Update the url line to point to the tree-sitter-pyrope repository. 
```
local parser_config = require "nvim-treesitter.parsers".get_parser_configs()
parser_config.pyrope = {
  install_info = {
    url = "~/projects/tree-sitter-pyrope", -- local path or git repo
    files = {"src/parser.c", "src/scanner.c"},
  },
}
```

In neovim run `:TSInstall pyrope`.

### Copying queries
Locate the nvim-treesitter folder where neovim plugins are installed. 
Make a pyrope directory in nvim-treesitter/queries. 
Copy tree-sitter-pyrope/queries/highlights.scm into nvim-treesitter/queries.

### Usage
After opening a .prp file, run `:setf pyrope` to tell nvim-treesitter which parser to use. 
Then, run `:TSEnable highlight` and highlights should be visible.
