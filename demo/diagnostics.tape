# VHS tape demonstrating compiler-style diagnostics after editing in Vim.
# Regenerate from the repo root with: vhs demo/diagnostics.tape

Output docs/assets/diagnostics.gif

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
Set TypingSpeed 50ms
Set Framerate 24

Hide
Type `export PATH="$PWD/build/cli:$PATH"`
Enter
Type `export PS1='\[\e[1;32m\]❯\[\e[0m\] '`
Enter
Type `DIAG_DIR=/tmp/maketex-vhs-diag && rm -rf "$DIAG_DIR" && mkdir -p "$DIAG_DIR"`
Enter
Type `cp testdata/bibtex/main.tex testdata/bibtex/refs.bib "$DIAG_DIR/"`
Enter
Type `export VIMINIT='set number | syntax on | set shortmess+=I'`
Enter
Type `cd "$DIAG_DIR" && maketex >/dev/null 2>&1 && clear`
Enter
Sleep 400ms
Show

# Open main.tex in Vim, jump to line 26, and introduce a typo (\section -> \sectoin)
Type "vim main.tex"
Enter
Sleep 900ms
Type "26G"
Sleep 600ms
Type "fi"
Sleep 400ms
Type "xp"
Sleep 800ms
Type ":wq"
Enter
Sleep 600ms

# Run maketex to see the compiler-style error with file:line and source snippet
Type "maketex"
Enter
Sleep 3.5s

Hide
Type `rm -rf "$DIAG_DIR"`
Enter
Show
