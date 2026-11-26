# VHS tape for the main maketex CLI demo.
# Regenerate from the repo root with: vhs demo/demo.tape

Output docs/assets/demo.gif

Set Shell "bash"
Set FontSize 16
Set Width 920
Set Height 500
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
Sleep 900ms

# 2. Zero-config cold build (auto-runs pdflatex + bibtex until convergence)
Type "maketex"
Enter
Sleep 2.2s

# 3. Project root stays spotless — aux files live in .maketex/
Type "ls"
Enter
Sleep 1.3s

# 4. Content-hash cache makes unchanged rebuilds instant
Type "maketex"
Enter
Sleep 1.3s

# 5. Clean compiler-style diagnostics with source snippets
Type `sed -i '' 's/\\section/\\sectoin/' main.tex`
Enter
Sleep 400ms
Type "maketex"
Enter
Sleep 3.2s

Hide
Type `rm -rf "$DEMO_DIR"`
Enter
Show
