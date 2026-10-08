# neotrac

Open project tracking for everyone: projects, the boards their work is on, and the
issues on them, with every change kept. It's served at [neotrac.org](https://neotrac.org),
and written in [uione](https://github.com/da0x/uione).

neotrac is a thank-you to [Trac](https://trac.edgewall.org), and the ideas that made it
good, rebuilt for the way we work now: [neotrac.org/about](https://neotrac.org/about)
says why.

AGPL-3.0: anyone who runs a changed neotrac as a service shares their changes with its
users. See `LICENSE`.

## What it does

- **Projects**, public or private, found and searched on the front page, with the
  people in each and the roles they hold. A project defines its own roles, and what
  each may do, on its pages.
- **Boards**, several to a project, like its product's work, its marketing and its
  executive matters, each a card on the project's page with how its issues stand,
  and links that open it filtered: assigned to me, high priority, or changed this
  week. Anyone signed in follows a board, and finds it on their front page.
- **Workflows** of a board's own: the phases its issues go through, and which role may
  move an issue from which phase to which, edited as a grid or a diagram, with no
  deploy. A board starts from a preset: basic, agile, editorial, bugs, architecture
  decisions (ADRs), or custom.
- **Issues**, numbered within their project, like neotrac.org/neotrac/12, shown as a
  table with a tab for each phase or as a board whose cards are dragged along the
  moves a person's roles allow. Each has comments, a priority, labels, and a history
  of every change, which the project's timeline gathers. An issue moves to another
  of its project's boards keeping its number, and links to others on any of them:
  it blocks one, relates to another, duplicates or replaces a third.
- **What's new**: on your front page, every change on the boards you follow and to
  the issues assigned to you, leaving out your own, and only from projects you may
  read. What came since you last looked is marked, and counted beside neotrac's
  name on every page.
- **Assignees**: the people working on an issue, picked from the project's own, and
  what's assigned to you listed on your front page and a filter on every board.
- **Milestones and a roadmap**: what a release, or any goal, needs done and by when,
  each a card with how its issues stand.
- **A wiki**: pages a project's people write together in Markdown, each keeping every
  change, arranged by dragging and listed in that order on the project's page,
  beside its roadmap and recent activity.
- **Private reports** of problems, like a security hole, read only by the project's
  people and whoever filed each one.

## What's here

One `.one` file for each part:

- `neotrac.one`: the project's settings: where it runs, how people sign in, its look.
- `projects.one`: projects, their people and roles, the front page, and a project's
  page.
- `boards.one`: boards, a board's page, and following one.
- `workflow.one`: phases, the moves between them, the presets a board starts from,
  and a board's Workflow page.
- `issues.one`: issues, their comments and history, and an issue's page.
- `reports.one`: problems reported to a project privately.
- `milestones.one`: milestones and the roadmap.
- `wiki.one`: a project's wiki.
- `migrations.one`: changes to what's stored that a deploy brought, each done once.
  They're kept after they've run, as examples of the language.
- `about.one` and `about/`: the thank-you to Trac.

## Working on it

With uione's compiler, `one`:

```
one check .
one build .
```

`one build` writes the web app, the Go backend, the Firestore rules and the Pulumi
program that deploys them into `build/`. The backend builds against the published
`one` library, and the web app installs uione's packages from npm, both at the version
`neotrac.one` names.

neotrac.org is deployed from the uione studio, at [uione.io](https://uione.io), which
builds what's pushed here in neotrac's own Google Cloud project.

See `AGENTS.md` for how the code is written here.
