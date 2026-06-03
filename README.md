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

## Syntax highlighting (Neovim)

Pyrope syntax highlighting in Neovim is provided through the
[nvim-treesitter](https://github.com/nvim-treesitter/nvim-treesitter) plugin.

> These instructions target the **`main` branch** of nvim-treesitter (the
> current rewrite, used by recent LazyVim). The older `master`-branch API
> (`get_parser_configs()`, manual query copying, `:TSEnable`) no longer applies.

### lazy.nvim / LazyVim

Copy [`editors/nvim/pyrope.lua`](editors/nvim/pyrope.lua) into your plugins
directory (e.g. `~/.config/nvim/lua/plugins/pyrope.lua`). It registers the
parser from this repo, maps `*.prp` files to the `pyrope` filetype, and adds
`pyrope` to `ensure_installed`:

```lua
return {
  {
    "nvim-treesitter/nvim-treesitter",
    init = function()
      vim.filetype.add({ extension = { prp = "pyrope" } })
    end,
    opts = function(_, opts)
      require("nvim-treesitter.parsers").pyrope = {
        install_info = {
          url = "https://github.com/masc-ucsc/tree-sitter-pyrope",
          branch = "main",
          queries = "queries",
        },
      }
      opts.ensure_installed = opts.ensure_installed or {}
      vim.list_extend(opts.ensure_installed, { "pyrope" })
    end,
  },
}
```

Restart Neovim (or `:Lazy reload nvim-treesitter`), then run `:TSInstall pyrope`.

### Plain nvim-treesitter (no LazyVim)

Add the following to your `init.lua`, then run `:TSInstall pyrope`:

```lua
require("nvim-treesitter.parsers").pyrope = {
  install_info = {
    url = "https://github.com/masc-ucsc/tree-sitter-pyrope",
    branch = "main",
    queries = "queries",
  },
}

vim.filetype.add({ extension = { prp = "pyrope" } })

vim.api.nvim_create_autocmd("FileType", {
  pattern = "pyrope",
  callback = function() vim.treesitter.start() end,
})
```

The `queries = "queries"` field installs this repo's `queries/*.scm`
(highlights, folds, indents) automatically — no manual copying needed.

### Verify

Open a `.prp` file and check:

- `:set ft?` shows `filetype=pyrope`
- `:checkhealth nvim-treesitter` lists `pyrope` as installed
- `:Inspect` on a token shows the matched `@capture` and highlight group

### Local development

To highlight from a local checkout instead of the public repo (so grammar and
query edits show up locally), replace `url`/`branch` with a `path`:

```lua
install_info = {
  path = vim.fn.expand("~/projs/tree-sitter-pyrope"),
  queries = "queries",
},
```

Query edits are picked up live (symlinked). After changing `grammar.js`, run
`npm run generate` and then `:TSInstall! pyrope` to rebuild the parser.

## `prpfmt`
A vertical alignment tool for pyrope is in progress.
It will align pyrope code based on the tree-sitter nodes.

### Compile
Clone the tree-sitter repository. 
Run make in the tree-sitter directory to generate a static library called `lib-treesitter.a`.

Compile prpfmt.c using `clang -I tree-sitter/lib/include prpfmt.c tree-sitter-pyrope/src/parser.c tree-sitter-pyrope/src/scanner.c tree-sitter/libtree-sitter.a`. 
Since the program uses the tree-sitter C API, its path must be included in the compile process.
