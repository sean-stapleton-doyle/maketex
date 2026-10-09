+++
title = 'Documentation'
date = '2026-06-11T00:00:00Z'
+++

# maketex Documentation

## Overview

- [Usage](usage/) — the `maketex` command line: build, watch, clean
- [CI](ci/) — caching `.maketex/`, `--format=github`/`json`, GitHub Actions recipe
- [Architecture](architecture/) — layer diagram, data flow, type system, key design decisions
- [Configuration](configuration/) — the `maketex.toml` config file: discovery, schema, precedence

Building: the core library always builds; `MAKETEX_BUILD_CLI`,
`MAKETEX_BUILD_HARNESS`, and `MAKETEX_BUILD_TESTS` control the rest and
default to ON only when maketex is the top-level CMake project, so embedding
consumers get just the library (and only its two dependencies).

## Other

- [Testdata cases](testdata-cases/) — fixture inventory (success + failure cases)
- [Bibtex execution walkthrough](bibtex-execution-walkthrough/) — step-by-step
  trace of a 3-pass bibtex build
