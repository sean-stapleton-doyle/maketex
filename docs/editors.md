# Editor Setup

`maketex` publishes `<job>.pdf` and `<job>.synctex.gz` beside your root `.tex` file, so PDF preview and SyncTeX work with default viewer settings.

## VS Code (LaTeX Workshop)

Add this to `settings.json`:

```json
{
  "latex-workshop.latex.tools": [
    {
      "name": "maketex",
      "command": "maketex",
      "args": ["--plain", "%DOCFILE%"]
    },
    {
      "name": "maketex (preamble cache)",
      "command": "maketex",
      "args": ["--plain", "--preamble-cache", "%DOCFILE%"]
    }
  ],
  "latex-workshop.latex.recipes": [
    { "name": "maketex", "tools": ["maketex"] },
    { "name": "maketex (fast preamble cache)", "tools": ["maketex (preamble cache)"] }
  ]
}
```

Keep `"latex-workshop.latex.outDir": "%DIR%"` (the default). To use `maketex watch` in a terminal instead, set `"latex-workshop.latex.autoBuild.run": "never"`.

## Neovim (VimTeX)

In `init.lua` (`lazy.nvim`):

```lua
{
  "lervag/vimtex",
  lazy = false,
  init = function()
    vim.g.vimtex_compiler_method = "generic"
    vim.g.vimtex_compiler_generic = {
      command = "maketex --plain",
      -- or: "maketex --plain --preamble-cache"
    }
  end,
}
```

Use `\ll` to build and `\lv` to open the PDF viewer with SyncTeX.

## Zed

Add to `.zed/tasks.json`:

```json
[
  {
    "label": "maketex: build",
    "command": "maketex",
    "cwd": "$ZED_WORKTREE_ROOT"
  },
  {
    "label": "maketex: watch",
    "command": "maketex",
    "args": ["watch"],
    "cwd": "$ZED_WORKTREE_ROOT"
  }
]
```

## Emacs (AUCTeX)

Add to `init.el`:

```elisp
(with-eval-after-load 'tex
  (add-to-list 'TeX-command-list
               '("maketex" "maketex --plain %t" TeX-run-TeX nil t
                 :help "Build with maketex"))
  (setq TeX-command-default "maketex"))
```

## TeXstudio

1. Open **Settings → Configure TeXstudio → Build**.
2. Under **User Commands**, add `user0:maketex` with command `maketex --plain %.tex`.
3. Set **Default Compiler** to `txs:///user0`.
