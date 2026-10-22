# AGENTS.md

Conventions for working in this repository, for people and assistants alike.

- neotrac is written in uione. One `.one` file per feature. What uione can't say
  yet is added to uione first, then used here, rather than worked around.
- Everything here is original and AGPL-3.0. Every source file starts with:
  `// Copyright 2026 Daher Alfawares` and `// SPDX-License-Identifier: AGPL-3.0-only`.
  Nothing is copied in from another codebase.
- neotrac is the whole app, the same wherever it runs. Nothing here is for one
  place that runs it: no plans, prices, payment or limits by plan, and no name of a
  service or repository built on it. Something sold on top of neotrac builds on it
  from outside, with uione's `extends`, and keeps what it sells there. A change it
  needs here is made only when it's right for everyone running neotrac, and is
  described in neotrac's own terms.
- Names in `.one` files are snake_case, file names lowercase kebab-case, and names
  are whole words.
- Anything that presents neotrac, like the about page and this README, says it's
  protected from abuse: everyone is limited in how fast they may ask and how much
  they may make.
- Work is committed to `main` and pushed after `one check .` passes and
  `one build .` succeeds. Commits are signed, and messages are short plain
  sentences with no attribution lines.
