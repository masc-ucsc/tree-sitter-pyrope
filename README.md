# tree-sitter-pyrope

A [tree-sitter](https://tree-sitter.github.io/tree-sitter/) grammar and parser
for [Pyrope](https://github.com/masc-ucsc/livehd), plus the tooling built on top
of it.

## What's in here

| Path | What it is |
|------|------------|
| `grammar.js`, `src/` | The tree-sitter grammar and the generated parser. This is the package. |
| `queries/` | Editor queries — `highlights.scm`, `indents.scm`, `folds.scm`. |
| `editors/` | Editor integration (currently a Neovim / nvim-treesitter plugin). |
| `prpfmt/` | A Pyrope code formatter (C) that walks the tree-sitter tree. See [`prpfmt/README.md`](prpfmt/README.md). |
| `prpparse/` | Design for a future recursive-descent parser for LiveHD (faster than tree-sitter, not for editors). See [`prpparse/plan.md`](prpparse/plan.md). |
| `scripts/` | `extract.rb` (build the test corpus from docs) and `test.sh` (parse it). |
| `full_pyrope/` | Auto-generated test corpus (git-ignored — rebuild with `make corpus`). |

## Build & test

The repo uses a single `Makefile` as the entry point. It needs `tree-sitter-cli`,
installed locally via npm:

```bash
npm install            # one-time: installs node_modules/tree-sitter-cli
```

| Command | Does |
|---------|------|
| `make` / `make generate` | Regenerate `src/parser.c` from `grammar.js`. |
| `make test` / `make test-grammar` | Parse all `full_pyrope/*.prp` — the canonical regression (must stay green). |
| `make test-all` | Also run prpfmt + prpparse (prpfmt is WIP, so this may be red). |
| `make corpus` | Rebuild `full_pyrope/` from the Pyrope docs in `../docs`. |
| `make prpfmt` / `make test-prpfmt` | Build the formatter / verify it over the corpus. |
| `make clean` | Clean build artifacts. |

### Parsing a single file (debugging the grammar)

```bash
# Print the parse tree (-t adds timing):
./node_modules/tree-sitter-cli/tree-sitter parse -t ./full_pyrope/file1.prp

# Just check it parses (no tree), the way scripts/test.sh does:
./node_modules/tree-sitter-cli/tree-sitter parse -q ./full_pyrope/file1.prp
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
local function register_pyrope()
  require("nvim-treesitter.parsers").pyrope = {
    install_info = {
      url = "https://github.com/masc-ucsc/tree-sitter-pyrope",
      branch = "main",
      queries = "queries",
    },
  }
end

return {
  {
    "nvim-treesitter/nvim-treesitter",
    init = function()
      vim.filetype.add({ extension = { prp = "pyrope" } })
      vim.api.nvim_create_autocmd("User", {
        pattern = "TSUpdate", -- re-register before every install/update
        callback = register_pyrope,
      })
      -- Pyrope uses C-style comments; needed for `gc`/`gcc` commenting.
      vim.api.nvim_create_autocmd("FileType", {
        pattern = "pyrope",
        callback = function()
          vim.bo.commentstring = "// %s"
        end,
      })
    end,
    opts = function(_, opts)
      opts.ensure_installed = opts.ensure_installed or {}
      vim.list_extend(opts.ensure_installed, { "pyrope" })
    end,
  },
}
```

Restart Neovim (or `:Lazy reload nvim-treesitter`), then run `:TSInstall pyrope`.

> The parser is registered on the `User TSUpdate` event, not once at startup.
> nvim-treesitter reloads its parser table from disk before every install/update
> (wiping runtime registrations) and re-fires this event so out-of-tree parsers
> can re-register. Registering only at startup yields
> `skipping unsupported language: pyrope`.

### Plain nvim-treesitter (no LazyVim)

Add the following to your `init.lua`, then run `:TSInstall pyrope`:

```lua
vim.api.nvim_create_autocmd("User", {
  pattern = "TSUpdate", -- re-register before every install/update
  callback = function()
    require("nvim-treesitter.parsers").pyrope = {
      install_info = {
        url = "https://github.com/masc-ucsc/tree-sitter-pyrope",
        branch = "main",
        queries = "queries",
      },
    }
  end,
})

vim.filetype.add({ extension = { prp = "pyrope" } })

vim.api.nvim_create_autocmd("FileType", {
  pattern = "pyrope",
  callback = function()
    vim.treesitter.start()
    vim.bo.commentstring = "// %s" -- Pyrope uses C-style comments
  end,
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
`make generate` and then `:TSInstall! pyrope` to rebuild the parser.

## prpfmt

`prpfmt` is a Pyrope code formatter (think `clang-format`): it parses with the
tree-sitter grammar and re-emits standardized, vertically-aligned Pyrope. Build
it with `make prpfmt`; details and options are in [`prpfmt/README.md`](prpfmt/README.md).

## prpparse

`prpparse` is a fast, recursive-descent Pyrope parser intended for LiveHD (not
editors), using this grammar as a differential oracle. The parser is implemented
(Phases 0–3: lexer, tree schema, parser; accept-parity on all 428 corpus files)
and the next step is a streaming statement interface so the LiveHD front-end can
replace tree-sitter — see [`prpparse/plan.md`](prpparse/plan.md).
