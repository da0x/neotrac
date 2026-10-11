# neotrac, the command

The issues waiting for you, or for a service, on the command line. A service, like
an AI agent, signs in with the key its project gave it in **Services**; a person runs
`neotrac login`, which opens neotrac in their browser to sign in and press
**Authorize**. Then:

```
$ neotrac queue
#12    high     Investigate  Crash on save                            free
#9     normal   Investigate  Search misses archived issues            yours

$ neotrac claim 12
$ neotrac show 12
$ neotrac comment 12 -m "Found it: a nil map on save."
$ neotrac move 12 review
```

`queue` lists the issues in the phases a role of yours works, free or yours. A phase
says which roles work it in its board's **Workflow**; a service holds the role its
project gave it. It moves an issue only where the workflow lets its role.

Taking an issue holds it for half an hour, so nobody else works it; `neotrac work`
renews the hold while its program runs, and one that stops, like a laptop gone to
sleep, lets it go by itself. Moving it on ends the hold.

```
$ neotrac work --run ./investigate --done review --failed backlog
```

`work` waits for issues in the phases its roles work, takes each as it comes, and
runs the program on it: the issue as JSON on its input, the same as `neotrac show 42
--json`, in a folder of its own, `$WORK_DIR`. When the program finishes, what it wrote
last is commented on the issue, and the issue moves to `--done`; when it fails, what
it wrote to stderr is, and the issue moves to `--failed`, or is let go for another to
take. `--once` stops after one.

Commands read as a verb and what it acts on: `neotrac list issues`, `neotrac edit page
how_to_build`. At a terminal, leaving out which one opens a list to pick it from, moved
through with the arrows or `j` and `k`, and `new` and `edit` open your editor
(`$VISUAL`, `$EDITOR` or `vi`) on a short file: its fields first, like `Title:` and
`Priority:`, then a blank line and its text, in markdown. A program, like an agent, names
what it means and gives what it writes as flags instead, and is never asked anything.

| Command | What it does |
|---|---|
| `neotrac login` | signs in as yourself: it opens neotrac in your browser (`$BROWSER`, or the system's), where you press **Authorize** |
| `neotrac login --key` | signs in as a service, with its key, read without showing it, or from `NEOTRAC_KEY` |
| `neotrac whoami`, `neotrac logout` | who's signed in, with their projects and roles; signing out |
| `neotrac select project [p]`, `neotrac select board [b]` | the project, and the board in it, your commands work in, kept until `neotrac unselect project` or `board`; `--project` and `--board` work in another for one command |
| `neotrac list projects\|boards\|issues\|pages\|milestones\|phases\|people` | what there is; `list projects --all` adds every public project |
| `neotrac show project\|board\|issue\|page\|milestone [which]` | one of them; `neotrac show 42 [--history] [--watch]` is an issue, its comments, and the moves you can make |
| `neotrac new project\|issue\|page\|milestone` | makes one |
| `neotrac edit project\|issue\|page\|milestone [which]` | changes one |
| `neotrac delete project [p]` | deletes it and all it holds, once you type its address; a program says `--confirm <address>` |
| `neotrac queue [--all] [--watch]` | what's waiting for you; `--watch` keeps it up to date as issues come and go |
| `neotrac claim 42`, `neotrac release 42` | hold an issue for half an hour so nobody else works it, or let it go |
| `neotrac work --run ./program --done review [--failed backlog] [--once]` | works each issue as it comes, with the program |
| `neotrac move 42 review` | move it, by its phase's name or title |
| `neotrac comment 42` | comment, in your editor, or with `-m "…"`, or on stdin |
| `neotrac link 42 blocked_by 9` | one issue bearing on another: `blocks`, `blocked_by`, `relates`, `duplicates`, `replaces`; neotrac keeps the other side |
| `neotrac unlink 42 relates 9` | takes a link away, and its other side |

The flags for writing are `--title`, `--body` (`-` reads it from stdin), `--priority`,
`--labels a,b`, `--milestone`, `--due 2026-11-30`, `--name`, `--visibility`, `--slug`
and `--workflow`; each sets the field of that name, and the rest stay as they are.

Every command takes `--json`: what it says on stdout, one JSON line for each change
with `--watch`, which is what an agent waits on, and `{"error", "code"}` on stderr.
`--server` uses another neotrac than neotrac.org.

Exit codes: 0 done, 1 left the list or changed nothing, 2 wrong usage, 3 not signed in or the key refused, 4 not found,
5 the workflow or a rule said no, 6 no connection, 7 too many requests.

A key, or a person's sign-in, is kept in `$XDG_CONFIG_HOME/neotrac/credentials`,
which only you may read; the command refuses one others can. In CI, give it
`NEOTRAC_KEY` instead. Signing in through the browser needs the browser on the same
computer, since it comes back to the command on `127.0.0.1`; over SSH, use a key.

## Building

g++ 14 or later and [Botan 3](https://botan.randombit.net/):

```
make
sudo make install                    # to /usr/local/bin
```

It uses [libember](https://github.com/da0x/libember)'s header from `/usr/include` or
`/usr/local/include` when its package is installed. Otherwise `make` downloads
libember 0.1.0's header once into `ember/` and checks its sha256. `EMBER_INCLUDE=…`
points at another copy.

Everything it knows about neotrac is in `generated/neotrac.hpp`, which
`one build` writes from neotrac's `.one` files; `make header` writes it again.

`tests/acceptance.sh` runs it end to end against neotrac's backend on the Firebase
emulators.

## License

GPL-3.0-only, in `LICENSE`.
