# Editor Integrations

Because `maketex` publishes `<job>.pdf` and `<job>.synctex.gz` directly beside your root `.tex` file (while keeping `.aux`, `.fls`, `.log`, `.bbl`, and other clutter inside `.maketex/`), **PDF previews and forward/inverse SyncTeX work out of the box** in every editor below.

---

## VS Code (LaTeX Workshop)

Add `maketex` as a tool and recipe in your VS Code `settings.json` (user or workspace):

```json
{
  "latex-workshop.latex.tools": [
    {
      "name": "maketex",
      "command": "maketex",
      "args": [
        "--plain",
        "%DOCFILE%"
      ]
    },
    {
      "name": "maketex (preamble cache)",
      "command": "maketex",
      "args": [
        "--plain",
        "--preamble-cache",
        "%DOCFILE%"
      ]
    }
  ],
  "latex-workshop.latex.recipes": [
    {
      "name": "maketex",
      "tools": ["maketex"]
    },
    {
      "name": "maketex (fast preamble cache)",
      "tools": ["maketex (preamble cache)"]
    }
  ]
}
```

Tips:
- LaTeX Workshop picks the first recipe by default (`maketex`), so saving a file runs `maketex` automatically.
- Keep `"latex-workshop.latex.outDir": "%DIR%"` (the default) so SyncTeX and the PDF viewer find `main.pdf` and `main.synctex.gz` right beside `main.tex`.
- If you prefer `maketex`'s own native `kqueue`/`inotify` watcher in an integrated terminal, run `maketex watch` and set `"latex-workshop.latex.autoBuild.run": "never"` so VS Code's PDF viewer auto-reloads whenever `maketex watch` publishes a new PDF.

---

## Neovim (VimTeX)

Configure VimTeX's `generic` compiler in your `init.lua` (shown with `lazy.nvim`):

```lua
{
  "lervag/vimtex",
  lazy = false,
  init = function()
    vim.g.vimtex_compiler_method = "generic"
    vim.g.vimtex_compiler_generic = {
      command = "maketex --plain",
    }
    -- Optional: enable precompiled preamble caching for pdflatex projects
    -- vim.g.vimtex_compiler_generic = { command = "maketex --plain --preamble-cache" }
  end,
}
```

Notes:
- VimTeX invokes the command with your root `.tex` file; `\ll` runs a build and `\lv` opens your configured PDF viewer with SyncTeX.
- For continuous watch mode inside Neovim, you can also open a terminal split and run `maketex watch` (or pass `maketex watch --no-tui`).

---

## Zed

Add project or global tasks in `.zed/tasks.json` (or `~/.config/zed/tasks.json`):

```json
[
  {
    "label": "maketex: build",
    "command": "maketex",
    "args": [],
    "cwd": "$ZED_WORKTREE_ROOT",
    "reveal": "always"
  },
  {
    "label": "maketex: build (preamble cache)",
    "command": "maketex",
    "args": ["--preamble-cache"],
    "cwd": "$ZED_WORKTREE_ROOT",
    "reveal": "always"
  },
  {
    "label": "maketex: watch",
    "command": "maketex",
    "args": ["watch"],
    "cwd": "$ZED_WORKTREE_ROOT",
    "reveal": "always"
  }
]
```

---

## Emacs (AUCTeX)

Add `maketex` to `TeX-command-list` in your `init.el`:

```elisp
(with-eval-after-load 'tex
  (add-to-list 'TeX-command-list
               '("maketex" "maketex --plain %t" TeX-run-TeX nil t
                 :help "Build document with maketex"))
  (setq TeX-command-default "maketex"))
```

Now `C-c C-c` defaults to `maketex`, and `C-c C-v` opens the published PDF with SyncTeX.

---

## TeXstudio

1. Open **Settings → Configure TeXstudio → Build**.
2. Add a user command under **User Commands**:
   - Name: `user0:maketex`
   - Command: `maketex --plain %.tex`
3. Set **Default Compiler** to `txs:///user0` (or keep `Default` and trigger `maketex` via **Tools → User → maketex**).

Because `maketex` publishes `<job>.pdf` and `<job>.synctex.gz` alongside `<job>.tex`, TeXstudio's built-in PDF viewer and Ctrl-click SyncTeX work without changing the output directory setting.
