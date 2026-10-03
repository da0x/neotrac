# AGENTS.md

Conventions for working in this repository, for people and assistants alike.

- neotrac is written in uione. One `.one` file per feature. What uione can't say
  yet is added to uione first, then used here, rather than worked around.
- Everything here is original and AGPL-3.0. Every source file starts with:
  `// Copyright 2026 Daher Alfawares` and `// SPDX-License-Identifier: AGPL-3.0-only`.
  Nothing is copied in from another codebase.
- Names in `.one` files are snake_case, file names lowercase kebab-case, and names
  are whole words.
- Work is committed to `main` and pushed after `one check .` passes and
  `one build .` succeeds. Commits are signed, and messages are short plain
  sentences with no attribution lines.
