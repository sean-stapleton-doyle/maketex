# Configuration (`maketex.toml`)

Use `maketex.toml` to store per-project build defaults.

## Discovery & precedence

1. `--config <path>` uses that exact file.
2. Otherwise, `maketex` walks up from the document directory (or current directory) and uses the first `maketex.toml` it finds. It stops at `.git`, `.hg`, or `.svn`, and never reads `$HOME/maketex.toml`.
3. `--no-config` ignores config files.

Precedence per field: **CLI flag > config value > built-in default.**

Relative paths in `maketex.toml` resolve against the config file's directory. Unknown keys emit a warning; invalid types or values fail the build.

## Full schema

All keys are optional. Values below are examples.

```toml
# Default root document when running `maketex` with no file argument
main = "thesis.tex"

# Override output PDF and aux basename
jobname = "thesis-final"

# "pdflatex", "xelatex", or "lualatex" (default: auto-detected)
engine = "lualatex"

# Build directory (default: ".maketex", with PDF copied beside the .tex file).
# Setting an explicit path keeps all outputs, including the PDF, inside it.
output-dir = "build"

# Replace the default tool roster:
# ["biber", "makeindex", "nomencl", "makeglossaries", "bib2gls", "pythontex", "bibtex"]
tools = ["biber", "makeindex"]

# Extra kpathsea search paths (trailing "//" searches recursively)
texinputs = ["styles//", "../common"]
bibinputs = ["../bib"]
bstinputs = ["styles"]
indexstyle = ["styles"]

shell-escape = false    # default: false
synctex = true          # default: true
preamble-cache = false  # cache pdflatex preamble as .fmt (default: false)
halt-on-error = true    # default: true
max-passes = 8          # default: 8
timeout = 300           # per-process timeout in seconds (default: none)
install-missing = true  # default: true (explicit true skips sudo prompt)

[engine-args]
program = "lualatex-dev"
extra-options = ["-cnf-line=max_print_line=1000"]

[env]
TEXMFHOME = "/path/to/texmf"

# Ignore volatile lines when hashing files (ECMAScript regex per extension)
[content-filters]
aux = "^\\\\myVolatileStamp\\{"

[diagnostics]
warnings-as-errors = false
fail-on = ["undefined-citation", "undefined-reference"]
ignore = ["overfull-hbox", "underfull-hbox"]

# Custom auxiliary tools (repeatable). Placeholders: $job, $outdir, $sourcedir
[[tool]]
name = "asymptote"
command = ["asy", "$job-*.asy"]
triggers = [".asy"]
outputs = [".pdf"]
working-dir = "output"  # "output" (default) or "source"

[watch]
interval-ms = 300       # fallback poll interval (default: 300)
extra-paths = ["figures"]

[clean]
extra-extensions = [".tikz-cache"]
```

## Config-to-flag reference

| Config key | CLI flag |
|---|---|
| `main` | positional `file` |
| `jobname` | `--jobname` |
| `engine` | `--engine` |
| `engine-args.program` | `--engine-program` |
| `engine-args.extra-options` | `--engine-opt` (repeatable) |
| `tools` | `--tools a,b,c` |
| `output-dir` | `-o` / `--output-dir` |
| `shell-escape` | `--shell-escape` / `--no-shell-escape` |
| `synctex` | `--synctex` / `--no-synctex` |
| `preamble-cache` | `--preamble-cache` / `--no-preamble-cache` |
| `halt-on-error` | `--halt-on-error` / `--no-halt-on-error` |
| `max-passes` | `--max-passes` |
| `timeout` | `--timeout` |
| `install-missing` | `--install-missing` / `--no-install-missing` |
| `diagnostics.warnings-as-errors` | `--warnings-as-errors` / `--no-warnings-as-errors` |
| `watch.interval-ms` | `--interval-ms` (`watch`) |

Config-only keys: `texinputs`, `bibinputs`, `bstinputs`, `indexstyle`, `env`, `content-filters`, `diagnostics.fail-on`, `diagnostics.ignore`, `[[tool]]`, `watch.extra-paths`, `clean.extra-extensions`.

CLI-only flags: `--verbose`, `--plain`, `--format`, `--config`, `--no-config`, `--no-tui`, `--all`, `--dry-run`.
