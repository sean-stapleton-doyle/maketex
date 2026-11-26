# VHS tape for the live `maketex watch` TUI demo.
# Regenerate from the repo root with: vhs demo/watch.tape

Output docs/assets/watch.gif

Set Shell "bash"
Set FontSize 16
Set Width 920
Set Height 460
Set Padding 16
Set Margin 16
Set MarginFill "#0f111a"
Set BorderRadius 10
Set WindowBar Colorful
Set Theme "TokyoNight"
Set TypingSpeed 45ms
Set Framerate 24

Hide
Type `set +m`
Enter
Type `export PATH="$PWD/build/cli:$PATH"`
Enter
Type `export PS1='\[\e[1;32m\]❯\[\e[0m\] '`
Enter
Type `WATCH_DIR=/tmp/maketex-vhs-watch && rm -rf "$WATCH_DIR" && mkdir -p "$WATCH_DIR"`
Enter
Type `cp testdata/bibtex/main.tex testdata/bibtex/refs.bib "$WATCH_DIR/"`
Enter
Type `cd "$WATCH_DIR"`
Enter
# Schedule a live typo edit and a fix while the watch TUI is running
Type `(sleep 2.8; perl -pi -e 's/\\section\{Introduction\}/\\sectoin\{Introduction\}/' main.tex; sleep 2.8; perl -pi -e 's/\\sectoin\{Introduction\}/\\section\{Introduction\}/' main.tex) >/dev/null 2>&1 & disown`
Enter
Type "clear"
Enter
Sleep 300ms
Show

Type "maketex watch"
Enter
Sleep 8.2s

Hide
Type "q"
Sleep 300ms
Type `rm -rf "$WATCH_DIR"`
Enter
Show
