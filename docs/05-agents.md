# Services and agents

A **service** is a program that works beside a project's people, like an AI agent.
It holds a role made for services, signs in with a key, and does only what that role
allows.

## Add one

In the project's **Settings**, **Services**, **Add a service** names it and gives it
a role; neotrac's own is **Agent**, which may take issues, comment, log, attach files
and move them where the workflow lets it. **New key** makes its key, shown once:
keep it where the program runs. **Revoke** stops a key at once.

## Give it work

On a board's **Workflow**, a stage says which roles work it. Give one to the
service's role, and that stage's issues are in the service's queue.

## Run it

```sh
export NEOTRAC_KEY=…          # or: neotrac login --key
neotrac work --run ./investigate --done review --failed backlog
```

`neotrac work` waits for issues in the stages its role works, takes each as it comes,
and runs the program on it: the issue as JSON on its input, in a folder of its own
with the issue's files. While it runs, what it prints is written to the issue's
**Log** every few seconds. When it finishes, what it put in its `out` folder is
attached, what it wrote last is commented, and the issue moves to `--done`; when it
fails, it moves to `--failed`. A hold that isn't renewed, like a laptop gone to sleep,
lets the issue go for another to take.

Several copies can run on one key: each takes a different issue.

## Watch them

Each project has an **Agents** page, linked from the project's own, with a tile for
each agent saying how it is now. Give an agent a name, a slug like `npm-publishing`,
and it reports itself there as it works:

```sh
neotrac work --run ./investigate --done review --agent investigator --title "Investigator"
```

- **Working**: on an issue, saying which, and how far it is when the program says.
- **Standby**: what it has waits on other issues.
- **Idle**: nothing to take.
- **Offline**: stopped, or not heard from for half a minute.
- **Stopped**: it couldn't go on.

A program says how far it is with a line like `progress: 2/5 Uploading`. Every few
seconds the agent also reports the CPU and memory of what it runs, and its machine's
CPU, memory, GPU, GPU memory and network, which its own page draws as charts.
