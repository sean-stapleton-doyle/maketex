#!/usr/bin/env bash
# Generates docs-site/content/docs/*.md from docs/*.md, so the Hugo site and
# the in-repo docs never drift: edit docs/, then run this (build-docs.sh
# does). Adds Hugo front matter and rewrites links between the docs
# (`configuration.md`) to Hugo page slugs (`configuration/`).

set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$REPO/docs-site/content/docs"

# source file | page slug | title
PAGES=(
    "USAGE.md|usage|Using maketex"
    "configuration.md|configuration|Configuration"
    "ci.md|ci|maketex in CI"
    "architecture.md|architecture|Architecture"
    "testdata_cases.md|testdata-cases|Test Fixture Cases"
    "bibtex_execution_walkthrough.md|bibtex-execution-walkthrough|BibTeX Build Walkthrough"
)

link_rewrites=()
for page in "${PAGES[@]}"; do
    IFS='|' read -r src slug _ <<< "$page"
    link_rewrites+=(-e "s#\](${src})#](${slug}/)#g" -e "s#\](${src}\#\([^)]*\))#](${slug}/\#\1)#g")
done

for page in "${PAGES[@]}"; do
    IFS='|' read -r src slug title <<< "$page"
    {
        printf "+++\ntitle = '%s'\n+++\n\n" "$title"
        sed "${link_rewrites[@]}" "$REPO/docs/$src"
    } > "$OUT/$slug.md"
    echo "wrote $OUT/$slug.md"
done
