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
