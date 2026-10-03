# neotrac

Open project tracking for everyone: projects, their people, and the issues they
work on, with every change kept. It's written in [uione](https://github.com/da0x/uione),
and will be served at neotrac.org.

AGPL-3.0: anyone who runs a changed neotrac as a service shares their changes with its users. See `LICENSE`.

## What's here

- `neotrac.one`: the project's settings and its front page.
- `projects.one`: projects, public or private, and the people in each, with a role.
- `reports.one`: problems reported to a project privately, like a security hole,
  read only by its people and whoever filed each one.
- `issues.one`: issues numbered within their project, with comments and a history of
  every change.

## Working on it

With uione's compiler, `one`:

```
one check .
one build .
```

`one build` writes the web app, the Go backend and the Firestore rules into
`build/`. The backend builds against the published `one` library, and the web app
installs uione's packages from npm.
