# The neotrac command

`neotrac` does on the command line what you do on the site: it lists what's waiting
for you, takes an issue, comments on it and moves it on. [Download](/download) says
how to get it.

## Sign in

```sh
neotrac login
```

opens neotrac in your browser, where you press **Authorize**. `neotrac whoami` says
who's signed in, and `neotrac logout` signs out.

## Pick where you work

```sh
neotrac select project tints
neotrac select board website
```

Every command then works there, until you select another; `--project` and `--board`
work somewhere else for one command.

## Work

```sh
neotrac queue                 # what's waiting for you, in the stages your roles work
neotrac claim 12              # take an issue, so nobody else works it
neotrac show 12               # it, its comments, and the moves you can make
neotrac comment 12 -m "Found it: a nil map on save."
neotrac log 12 -m "Rebuilding the index"
neotrac move 12 review
```

Taking an issue holds it for half an hour, and moving it on lets it go. `neotrac new
issue` and `neotrac edit issue 12` open your editor on a short file, its fields first
and then its description. `neotrac list issues`, `boards`, `milestones` and the rest
list what there is, and `--json` gives any of it to a program.

`neotrac --help` lists every command.
