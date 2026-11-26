# VHS tape for the main (all-green) maketex CLI demo.
# Regenerate from the repo root with: vhs demo/demo.tape

Output docs/assets/demo.gif

Set Shell "bash"
Set FontSize 16
Set Width 920
Set Height 480
Set Padding 20
Set Margin 16
Set MarginFill "#0f111a"
Set BorderRadius 10
Set WindowBar Colorful
Set Theme "TokyoNight"
Set TypingSpeed 45ms
Set Framerate 24

Hide
Type `export PATH="$PWD/build/cli:$PATH"`
Enter
Type `export PS1='\[\e[1;32m\]❯\[\e[0m\] '`
Enter
Type `export VIMINIT='set number | syntax on | set shortmess+=I'`
Enter
Type `DEMO_DIR=/tmp/maketex-vhs-demo && rm -rf "$DEMO_DIR" && mkdir -p "$DEMO_DIR"`
Enter
Type `cp testdata/bibtex/main.tex testdata/bibtex/refs.bib "$DEMO_DIR/"`
Enter
Type `cd "$DEMO_DIR" && clear`
Enter
Sleep 400ms
Show

# 1. Start with a clean LaTeX project
Type "ls"
Enter
Sleep 800ms

# 2. Zero-config cold build (auto-runs pdflatex + bibtex until convergence)
Type "maketex"
Enter
Sleep 2.0s

# 3. Project root stays spotless — aux files live in .maketex/
Type "ls"
Enter
Sleep 1.2s

# 4. Content-hash cache makes unchanged rebuilds instant
Type "maketex"
Enter
Sleep 1.2s

# 5. Edit prose in Vim -> smart incremental rebuild takes just 1 pass (skips bibtex)
Type "vim main.tex"
Enter
Sleep 700ms
Type "32G"
Sleep 400ms
Type "cc"
Type "Incremental prose edits rebuild in a single pass."
Escape
Sleep 600ms
Type ":wq"
Enter
Sleep 500ms

Type "maketex"
Enter
Sleep 2.8s

Hide
Type `rm -rf "$DEMO_DIR"`
Enter
Show
