# Boards and workflows

A project has as many boards as it needs, like its product's work, its website and
its decisions, side by side. Its people and roles are the same on every board.

## A board's stages

A board's issues move through its **stages**, which are its columns: To do, In
progress and Done, or whatever the work needs. A new board starts from a workflow:

- **Basic task management**: To do, In progress, Done.
- **Software development**: from the backlog to completed, a step forward or back at a
  time.
- **Editorial & content planning**: an idea to what's published.
- **Bug & issue tracker**: reported to closed, with fixes that don't hold going back.
- **Architecture decisions**: proposed, then accepted or rejected, and later
  deprecated or superseded.
- **Custom**: Open and Done, to make your own.

## Its workflow

A board's **Workflow** page changes any of it, with nothing to deploy: add, rename,
reorder or remove stages, and say which **steps** between them each role may take. A
step back is a step of its own, so an issue can move forward without being able to
come back.

Each stage also says:

- its **status**, which is the dot on its cards: not started, waiting, working,
  success or failed; an issue that depends on another waits until that one reaches a
  stage whose status is success;
- which roles **work** it, so its issues are in their queue, as [services and
  agents](/docs/agents) see theirs;
- whether it **chimes** when an issue arrives, for whoever has the board open.

## How a board is shown

**Edit board** chooses how its issues are drawn, and switching keeps each where it
was:

- **List**: one under another, as tickets are.
- **Board**: in columns by stage, dragged from one to the next.
- **Pipeline**: a lane for each kind of issue, by its labels, along the stages it
  goes through.
- **Whiteboard**: notes placed anywhere, in a column for each stage.

## Private boards

In a public project, a board can be **private**: it, its issues and their history are
seen by the project's people alone, and left out of its public pages. In a private
project every board is private. Made public again, a project's boards stay private
until its people open each, so nothing is shown by surprise.

## Closing an issue

An issue is closed as finished, as not to be done, or as a duplicate of another, from
whatever stage it's in, and keeps that stage, so reopening it puts it back where it
was. A closed issue leaves its board and everyone's queue, and its board's **Closed**
page lists it.
