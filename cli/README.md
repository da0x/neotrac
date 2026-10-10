# neotrac, the command

The issues waiting for you, or for a service, on the command line. A service, like
an AI agent, signs in with the key its project gave it in **Services**; a person signs
in with GitHub. Then:

```
$ neotrac queue
#12    high     Investigate  Crash on save                            free
#9     normal   Investigate  Search misses archived issues            yours

$ neotrac claim 12
$ neotrac show 12
$ neotrac comment 12 -m "Found it: a nil map on save."
$ neotrac move 12 review
```

It lists the issues in the phases a role of yours works, free or yours. A phase says
which roles work it in its board's **Workflow**; a service holds the role its project
gave it. It moves an issue only where the workflow lets its role.

| Command | What it does |
|---|---|
| `neotrac login --key` | signs in as a service, with its key, read without showing it, or from `NEOTRAC_KEY` |
| `neotrac login` | signs in as yourself, with GitHub |
| `neotrac whoami` | who's signed in, and where |
| `neotrac queue [--board main] [--all] [--watch]` | what's waiting; `--watch` keeps it up to date as issues come and go |
| `neotrac show 42 [--history] [--watch]` | an issue, its comments, and the moves you can make |
| `neotrac claim 42`, `neotrac release 42` | take an issue so nobody else works it, or let it go |
| `neotrac move 42 review` | move it, by its phase's name or title |
| `neotrac comment 42 -m "…"` | comment, or with the comment on stdin |

Every command takes `--json`: what it says on stdout, one JSON line for each change
with `--watch`, which is what an agent waits on, and `{"error", "code"}` on stderr.
`--server` uses another neotrac than neotrac.org.

Exit codes: 0 done, 2 wrong usage, 3 not signed in or the key refused, 4 not found,
5 the workflow or a rule said no, 6 no connection, 7 too many requests.

A key is kept in `$XDG_CONFIG_HOME/neotrac/credentials`, which only you may read; the
command refuses one others can. In CI, give it `NEOTRAC_KEY` instead.

## Building

g++ 14 or later, [Botan 3](https://botan.randombit.net/), and
[libember](https://github.com/da0x/libember)'s header:

```
make                                 # libember's header in /usr/include
make EMBER_INCLUDE=~/libember/include
```

Everything it knows about neotrac is in `generated/neotrac.hpp`, which
`one build` writes from neotrac's `.one` files; `make header` writes it again.

`tests/acceptance.sh` runs it end to end against neotrac's backend on the Firebase
emulators.

## License

GPL-3.0-only, in `LICENSE`.
