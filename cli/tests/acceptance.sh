#!/bin/bash
# Copyright 2026 Daher Alfawares
# SPDX-License-Identifier: GPL-3.0-only

# The neotrac command, end to end, against neotrac's own backend on the Firebase
# emulators: a person sets up a stage worked by agents, a service with a key works it.
#
#   1. a project on the bugs workflow, with Investigate worked by Agent, a person's
#      step into it, and Agent's step out of it to Verifying;
#   2. a service, Scout, given Agent and a key;
#   3. a running `neotrac queue --watch --json` shows the issue a person moves in;
#   4. two services claim it at once: one wins, the other exits 5, and can't move it
#      either until the winner's lease runs out, when it takes it instead;
#   5. the service comments, is refused a move its role can't make (exit 5, the
#      moves it can make listed) and makes the one it can;
#   6. the issue's history names the service;
#   7. once its key is revoked, the next command exits 3;
#   8. a person logs in through the browser, here a script that presses Authorize,
#      and a turned-down login exits 3;
#   9. neotrac work takes an issue moved into Investigate, runs a program on it, its
#      attachments beside it, attaches what it made, and moves it on, or lets it go
#      when the program fails;
#  10. a file is attached to an issue, listed with it, and downloaded as it was.
#
# Needs the emulators (firestore localhost:8080, auth localhost:9099, storage
# localhost:9199), ../build/api
# from one build, and ./neotrac from make. Project demo-uione is cleared first.

set -u
here=$(cd "$(dirname "$0")/.." && pwd)
api=http://localhost:8099
auth=http://localhost:9099/identitytoolkit.googleapis.com/v1
failures=0
check() { # check NAME CONDITION...
	local name=$1
	shift
	if "$@"; then echo "ok: $name"; else echo "FAIL: $name"; failures=$((failures + 1)); fi
}

export NEOTRAC_SERVER=$api NEOTRAC_FIREBASE_PROJECT=demo-uione NEOTRAC_FIRESTORE_HOST=localhost:8080 NEOTRAC_AUTH_HOST=localhost:9099
export XDG_CONFIG_HOME=$(mktemp -d)
logs=$(mktemp -d)
cleanup() {
	[ -n "${backend:-}" ] && kill "$backend" 2>/dev/null
	[ -n "${watching:-}" ] && kill "$watching" 2>/dev/null
	# KEEP_LOGS=1 keeps what each command said, to see why a check failed.
	if [ -n "${KEEP_LOGS:-}" ]; then echo "logs kept in $logs" >&2; else rm -rf "$XDG_CONFIG_HOME" "$logs"; fi
}
trap cleanup EXIT

curl -s -X DELETE "http://localhost:8080/emulator/v1/projects/demo-uione/databases/(default)/documents" >/dev/null
curl -s -X DELETE "http://localhost:9099/emulator/v1/projects/demo-uione/accounts" >/dev/null
# Built first and run as itself, so stopping it stops it, as go run's child wouldn't.
(cd "$here/../build/api" && GOFLAGS=-mod=mod go build -o "$logs/api" .) || { echo "the backend didn't build"; exit 1; }
FIRESTORE_EMULATOR_HOST=localhost:8080 FIREBASE_AUTH_EMULATOR_HOST=localhost:9099 GOOGLE_CLOUD_PROJECT=demo-uione PORT=8099 "$logs/api" >"$logs/api.log" 2>&1 &
backend=$!
for _ in $(seq 1 120); do curl -s "$api/health" >/dev/null && break; sleep 1; done

as() { # as TOKEN COMMAND JSON: a person's command
	curl -s -X POST -H 'Content-Type: application/json' -H "Authorization: Bearer $1" "$api/api/$2" -d "$3"
}
person=$(curl -s -X POST -H 'Content-Type: application/json' "$auth/accounts:signUp?key=any" \
	-d '{"email":"maintainer@example.com","password":"password","displayName":"Maya","returnSecureToken":true}' | python3 -c 'import json,sys; print(json.load(sys.stdin)["idToken"])')
as "$person" projects/project/create '{"slug":"e2e","name":"E2E","workflow":"bugs","visibility":"private"}' >/dev/null
as "$person" projects/stage/create '{"board":"e2e-main","project":"e2e","title":"Investigate","worked_by":["e2e-agent"]}' >/dev/null
as "$person" projects/step/create '{"board":"e2e-main","project":"e2e","from":"e2e-main-reported","to":"e2e-main-investigate","roles":["e2e-maintainer"]}' >/dev/null
as "$person" projects/step/create '{"board":"e2e-main","project":"e2e","from":"e2e-main-investigate","to":"e2e-main-verifying","roles":["e2e-agent"]}' >/dev/null
key_of() { # key_of TITLE: a service with Agent, and its key
	local id
	id=$(as "$person" projects/service/create "{\"project\":\"e2e\",\"title\":\"$1\",\"role\":\"e2e-agent\"}" | python3 -c 'import json,sys; print(json.load(sys.stdin)["id"])')
	as "$person" projects/service/key "{\"id\":\"$id\"}" | python3 -c 'import json,sys; print(json.load(sys.stdin)["key"])'
}
scout=$(key_of Scout)
ranger=$(key_of Ranger)
as "$person" projects/issue/create '{"project":"e2e","board":"e2e-main","title":"Crash on save"}' >/dev/null

neotrac="$here/neotrac"
echo "$scout" | "$neotrac" login --key >"$logs/login" 2>&1
check "login with a key says who it is" grep -q "Signed in as Scout, a service, in e2e" "$logs/login"
check "the credentials are kept where only their owner reads them" test "$(stat -c %a "$XDG_CONFIG_HOME/neotrac/credentials")" = 600
"$neotrac" queue --json >"$logs/empty" 2>&1
check "nothing is waiting before a person moves an issue in" grep -qx '\[\]' "$logs/empty"

"$neotrac" queue --watch --json >"$logs/watch" 2>"$logs/watch.err" &
watching=$!
sleep 3
as "$person" projects/issue/move '{"id":"e2e-1","stage":"e2e-main-investigate"}' >/dev/null
moved=$(date +%s%N)
for _ in $(seq 1 50); do grep -q '"number":1' "$logs/watch" && break; sleep 0.1; done
seen=$(date +%s%N)
check "a running queue --watch shows the issue moved in" grep -q '"title":"Crash on save"' "$logs/watch"
check "within a second" test $(((seen - moved) / 1000000)) -lt 1000

"$neotrac" queue >"$logs/table" 2>&1
check "the queue reads as a table, the issue free to take" grep -Eq "^#1 +normal +Investigate +Crash on save +free$" "$logs/table"
"$neotrac" show 1 >"$logs/before" 2>&1
check "an issue shows the moves its reader can make" grep -q "^Moves you can make: verifying$" "$logs/before"

NEOTRAC_KEY=$scout "$neotrac" claim 1 >"$logs/claim1" 2>&1 &
first=$!
NEOTRAC_KEY=$ranger "$neotrac" claim 1 >"$logs/claim2" 2>&1 &
second=$!
wait $first
a=$?
wait $second
b=$?
check "two claims at once: one wins, the other exits 5" test "$(printf '%s\n' $a $b | sort | tr '\n' ' ')" = "0 5 "
winner=$scout loser=$ranger
[ $a -ne 0 ] && winner=$ranger loser=$scout
"$neotrac" show 1 >"$logs/held" 2>&1
check "a claimed issue shows who holds it, and until when" grep -Eq ", held by (Scout|Ranger) until [0-9]{2}:[0-9]{2}" "$logs/held"
NEOTRAC_KEY=$loser "$neotrac" move 1 verifying >"$logs/not-theirs" 2>&1
check "nobody else moves an issue someone holds" test $? -eq 5
check "and says why" grep -q "someone else holds it" "$logs/not-theirs"
# Half an hour on, the lease runs out: its end is put in the past.
curl -s -X PATCH -H "Authorization: Bearer owner" -H "Content-Type: application/json" \
	"http://localhost:8080/v1/projects/demo-uione/databases/(default)/documents/projects_issue/e2e-1?updateMask.fieldPaths=held_until" \
	-d '{"fields":{"held_until":{"timestampValue":"2020-01-01T00:00:00Z"}}}' >/dev/null
NEOTRAC_KEY=$loser "$neotrac" claim 1 >"$logs/retaken" 2>&1
check "once a lease runs out, another takes the issue" test $? -eq 0
winner=$loser
NEOTRAC_KEY=$winner "$neotrac" status 1 working --note "reading the save path" >"$logs/status" 2>&1
check "whoever holds an issue says how it's going" grep -q "1 is working." "$logs/status"
curl -s -H "Authorization: Bearer owner" "http://localhost:8080/v1/projects/demo-uione/databases/(default)/documents/projects_issue/e2e-1" >"$logs/data"
check "for its card: its status, since when, and a note" python3 -c '
import json,sys; d=json.load(open(sys.argv[1]))["fields"]["data"]["mapValue"]["fields"]
assert d["status"]["stringValue"] == "working" and "since" in d and d["note"]["stringValue"] == "reading the save path"' "$logs/data"
other=$scout; [ "$winner" = "$scout" ] && other=$ranger
NEOTRAC_KEY=$other "$neotrac" status 1 waiting >"$logs/not-held" 2>&1
check "someone who doesn't hold it says nothing of it" test $? -eq 5
NEOTRAC_KEY=$winner "$neotrac" move 1 closed >"$logs/refused" 2>&1
refused=$?
check "a move its role can't make exits 5" test $refused -eq 5
check "and lists the moves it can make" grep -q "you can move it to: verifying" "$logs/refused"
NEOTRAC_KEY=$winner "$neotrac" move 1 verifying >"$logs/moved" 2>&1
check "the move its role can make is made" grep -q "Moved 1 to Verifying" "$logs/moved"
"$neotrac" show 1 --history >"$logs/show" 2>&1
check "the history names the service" grep -Eq "^  (Scout|Ranger): stage" "$logs/show"

as "$person" projects/service/revoke '{"id":"e2e-scout"}' >/dev/null
"$neotrac" whoami >/dev/null 2>&1
check "a revoked key exits 3" test $? -eq 3

# The browser: it opens neotrac's /authorize page as the person signed in there, who
# presses Authorize (or Cancel, with BROWSER_ANSWER=denied), and is sent back to the
# command on 127.0.0.1.
cat >"$logs/browser" <<'BROWSER'
#!/bin/bash
url=$1
value() { sed -n "s/.*[?&]$1=\([^&]*\).*/\1/p" <<<"$url"; }
port=$(value port) state=$(value state) challenge=$(value challenge)
if [ "${BROWSER_ANSWER:-}" = denied ]; then
	answer="error=denied"
else
	code=$(curl -s -X POST -H 'Content-Type: application/json' -H "Authorization: Bearer $PERSON" "$NEOTRAC_SERVER/api/authorize" -d "{\"challenge\":\"$challenge\"}" |
		python3 -c 'import json,sys; print(json.load(sys.stdin)["code"])')
	answer="code=$code"
fi
curl -s -o /dev/null -w '%{redirect_url}' "http://127.0.0.1:$port/?$answer&state=$state" >"$BROWSER_SAW"
BROWSER
chmod +x "$logs/browser"
export BROWSER="$logs/browser" PERSON="$person" BROWSER_SAW="$logs/browser.saw"
timeout 30 "$neotrac" login >"$logs/person" 2>"$logs/person.err"
check "a person logs in through the browser" grep -qx "Signed in as Maya." "$logs/person"
check "and the browser is sent back to say so" grep -q "/authorize?done=yes$" "$logs/browser.saw"
# A person in two projects selects the one their commands work in, once.
as "$person" projects/project/create '{"slug":"lab","name":"Lab","workflow":"bugs","visibility":"private"}' >/dev/null
"$neotrac" whoami >"$logs/whoami" 2>&1
check "whoami lists the person's projects, without asking which" grep -Eq "^ +lab, Maintainer$" "$logs/whoami"
"$neotrac" show 1 >/dev/null 2>"$logs/which"
check "in two projects, a command asks which, naming them" grep -q "you're in e2e, lab; pick one with neotrac select project" "$logs/which"
"$neotrac" list projects --json >"$logs/projects"
check "list projects names both" grep -q '"project":"lab"' "$logs/projects"
"$neotrac" select project nowhere >/dev/null 2>&1
check "select refuses a project the person isn't in" test $? -eq 4
"$neotrac" select project </dev/null >/dev/null 2>"$logs/select"
check "without a terminal, select says which there are" grep -q "say which project: e2e, lab" "$logs/select"
"$neotrac" select project e2e >"$logs/use" 2>&1
check "select keeps the project" grep -q "^Selected e2e." "$logs/use"
"$neotrac" show 1 >"$logs/person.show" 2>&1
check "signed in, the person reads the issue" grep -q "Crash on save" "$logs/person.show"
"$neotrac" whoami >"$logs/whoami" 2>&1
check "whoami marks the project selected" grep -Eq "^  \* e2e, Maintainer$" "$logs/whoami"
"$neotrac" select board main >/dev/null && "$neotrac" list boards >"$logs/boards"
check "a board is selected too" grep -Eq "^\* main " "$logs/boards"
"$neotrac" new board --title "Releases" --visibility private --workflow basic >"$logs/board" 2>&1
check "a person makes a private board" grep -q "^Made Releases. neotrac select board releases works in it." "$logs/board"
"$neotrac" list boards >"$logs/boards"
check "listed among the project's boards for its people" grep -Eq "^  releases +Releases$" "$logs/boards"
"$neotrac" new stage --board releases --title "Waiting on you" --status waiting --notify chime --position 2.5 --worked-by agent >"$logs/stage" 2>&1
check "a stage is added that chimes" grep -q "^Added Waiting on you to Releases." "$logs/stage"
"$neotrac" edit stage in_progress --board releases --title "Working" --status working >"$logs/edited" 2>&1
check "and a stage renamed, with its dot" grep -q "^Saved Working." "$logs/edited"
"$neotrac" new step --board releases --from working --to waiting_on_you >"$logs/step" 2>&1
check "issues move into it" grep -q "^Issues move from Working to Waiting on you." "$logs/step"
"$neotrac" new step --board releases --from waiting_on_you --to done --roles agent >/dev/null 2>&1
check "a step for a role named" test $? -eq 0
"$neotrac" list stages --board releases >"$logs/stages"
check "the board's stages say themselves" grep -Eq "^  waiting_on_you +Waiting on you$" "$logs/stages"
check "the new one between Working and Done, where it was put" test "$(awk '{print $1}' "$logs/stages" | tr '\n' ' ')" = "to_do working waiting_on_you done "
"$neotrac" show board releases >"$logs/releases"
check "worked by the role it names" grep -Eq "^  Waiting on you .*worked by Agent$" "$logs/releases"
"$neotrac" new service --title Courier --role agent --json >"$logs/service" 2>&1
courier=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["key"])' "$logs/service" 2>/dev/null)
NEOTRAC_KEY=$courier "$neotrac" whoami >"$logs/courier" 2>&1
check "a service made with its key, which signs it in" grep -q "Courier" "$logs/courier"
"$neotrac" delete board releases --confirm Releases >/dev/null 2>&1
check "and the private board is deleted, its title typed" test $? -eq 0
"$neotrac" unselect board >/dev/null && "$neotrac" list boards >"$logs/boards"
check "and unselected" grep -Eq "^  main " "$logs/boards"

# Everything is written with flags by a program, as a person would in their editor.
"$neotrac" new issue --title "Slow start" --priority high --labels "speed, start" --body "Takes ten seconds." --json >"$logs/new"
check "a new issue is opened with its fields" grep -q '"id":"e2e-2"' "$logs/new"
echo "It's the cache." | "$neotrac" edit issue 2 --body - >/dev/null
"$neotrac" show issue 2 --json >"$logs/issue2"
check "an issue is edited, its text from stdin, its fields kept" grep -q '"body":"It'"'"'s the cache.","stage":"Reported","board":"E2E","priority":"high","labels":\["speed","start"\]' "$logs/issue2"
"$neotrac" list issues >"$logs/issues"
check "list issues shows them newest first" test "$(head -1 "$logs/issues" | cut -c1-6)" = "#2    "
"$neotrac" new page --title "How to build" --body "Run make." >/dev/null
"$neotrac" edit page how_to_build --title "Building" >/dev/null
"$neotrac" show page building >"$logs/page"
check "a wiki page is written and edited" grep -qx "Building" "$logs/page"
check "keeping its text" grep -qx "Run make." "$logs/page"
"$neotrac" new milestone --title "Beta" --due 2026-12-01 >/dev/null
"$neotrac" list milestones >"$logs/milestones"
check "a milestone is added with its date" grep -Eq "^  beta +2026-12-01 +Beta$" "$logs/milestones"
"$neotrac" list stages --json >"$logs/stages"
check "the board's stages are listed" grep -q '"stage":"investigate"' "$logs/stages"

printf 'panic: nil map\n' >"$logs/trace.log"
"$neotrac" attach 2 "$logs/trace.log" >"$logs/attach" 2>&1
check "a file is attached to an issue" grep -qx "Attached trace.log to #2." "$logs/attach"
"$neotrac" show issue 2 --json >"$logs/attached"
check "and listed with it" grep -q '"attachments":\[{"name":"trace.log","path":"files/' "$logs/attached"
"$neotrac" download 2 "$logs/got" >/dev/null
check "and downloaded as it was" cmp -s "$logs/trace.log" "$logs/got/trace.log"

"$neotrac" edit issue 2 --milestone beta >/dev/null
"$neotrac" link 2 blocked_by 1 >"$logs/link"
"$neotrac" link issue 2 blocked-by 1 >"$logs/again"
"$neotrac" show issue 2 --json >"$logs/linked"
check "an issue is put in a milestone" grep -q '"milestone":"Beta"' "$logs/linked"
check "and linked to another" grep -q '"links":\[{"relation":"blocked_by","to":1}\]' "$logs/linked"
check "linking it again changes nothing" grep -qx "#2 already blocked_by #1." "$logs/again"
"$neotrac" show issue 1 --json >"$logs/other"
check "the other side is kept, said from there" grep -q '"links":\[{"relation":"blocks","to":2}\]' "$logs/other"
"$neotrac" link 1 relates 2 >/dev/null
"$neotrac" unlink 2 relates 1 >"$logs/unlink"
"$neotrac" show issue 1 --json >"$logs/unlinked"
check "unlinking takes both sides away" grep -q '"links":\[{"relation":"blocks","to":2}\]' "$logs/unlinked"
check "and says so" grep -qx "#2 no longer relates #1." "$logs/unlink"
check "unlinking what isn't linked changes nothing" bash -c '! "$0" unlink 2 relates 1 >/dev/null 2>&1' "$neotrac"

# At a terminal: a list to pick from, and the person's editor.
cat >"$logs/editor" <<'EDITOR'
#!/bin/bash
sed -i -e 's/^Title: .*/Title: Picked and edited/' "$1"
EDITOR
chmod +x "$logs/editor"
at_terminal() { # at_terminal KEYS COMMAND...: runs it in a terminal, pressing KEYS once it's drawn
	python3 - "$@" <<'PTY'
import os, pty, sys, time, select
keys = sys.argv[1].encode().decode('unicode_escape').encode()
pid, fd = pty.fork()
if pid == 0:
    os.execvp(sys.argv[2], sys.argv[2:])
out, sent = b"", False
while True:
    r, _, _ = select.select([fd], [], [], 0.2)
    if r:
        try: chunk = os.read(fd, 4096)
        except OSError: break
        if not chunk: break
        out += chunk
    elif not sent and out:
        os.write(fd, keys); sent = True
_, status = os.waitpid(pid, 0)
sys.stdout.write(out.decode(errors="replace"))
sys.exit(os.waitstatus_to_exitcode(status))
PTY
}
at_terminal 'j\r' "$neotrac" select project >"$logs/picked" 2>&1
check "select at a terminal picks from a list" grep -q "Selected lab." "$logs/picked"
"$neotrac" select project e2e >/dev/null
EDITOR="$logs/editor" at_terminal 'j\r' "$neotrac" edit issue >"$logs/edited" 2>&1
check "edit at a terminal picks the issue, then opens the editor" grep -q "Saved #1." "$logs/edited"
"$neotrac" show issue 1 --json >"$logs/issue1"
check "and saves what was written there" grep -q '"title":"Picked and edited"' "$logs/issue1"
at_terminal 'q' "$neotrac" show milestone >/dev/null 2>&1
check "leaving the list does nothing, and says so by its exit" test $? -eq 1

# Deleting a project: the address typed, or --confirm for a program.
"$neotrac" delete project lab </dev/null >/dev/null 2>"$logs/delete"
check "deleting asks a program to confirm" grep -q "say --confirm lab to mean it" "$logs/delete"
"$neotrac" delete project lab --confirm lap >/dev/null 2>&1
check "a wrong address deletes nothing" test $? -eq 5
at_terminal 'lab\n' "$neotrac" delete project lab >"$logs/deleted" 2>&1
check "at a terminal, the address is typed" grep -q "Deleted lab." "$logs/deleted"
"$neotrac" list projects --json >"$logs/projects"
check "and it's gone" test "$(grep -c '"project":"lab"' "$logs/projects")" = 0

BROWSER_ANSWER=denied timeout 30 "$neotrac" login >/dev/null 2>"$logs/denied"
check "a login turned down in the browser exits 3" test $? -eq 3
check "and says so" grep -q "it was turned down in the browser" "$logs/denied"

# A worker: neotrac work takes what's moved into a stage its role works, runs a
# program on it, says what the program wrote, and moves it on, or lets it go when the
# program fails.
issue_in_investigate() {
	local id
	id=$(as "$person" projects/issue/create "{\"project\":\"e2e\",\"board\":\"e2e-main\",\"title\":\"$1\"}" | python3 -c 'import json,sys; print(json.load(sys.stdin)["id"])')
	as "$person" projects/issue/move "{\"id\":\"$id\",\"stage\":\"e2e-main-investigate\"}" >/dev/null
	echo "${id#e2e-}"
}
cat >"$logs/fix" <<'FIX'
#!/bin/bash
title=$(python3 -c 'import json,sys; print(json.load(sys.stdin)["title"])')
[ -d "$WORK_DIR/out" ] || exit 9
if [ "$title" = "Flaky test" ]; then
	sed 's/^/seen: /' "$WORK_DIR/notes.txt" >"$WORK_DIR/out/report.txt" || exit 8
	echo "Looked at $title: a race in setup."
else
	echo "can't reproduce $title" >&2
	echo "its log is $HOME/.cache/runs/fix.log" >&2
	exit 1
fi
FIX
chmod +x "$logs/fix"
worked=$(issue_in_investigate "Flaky test")
echo "fails one run in ten" >"$logs/notes.txt"
"$neotrac" attach "$worked" "$logs/notes.txt" >/dev/null
NEOTRAC_KEY=$ranger timeout 60 "$neotrac" work --run "$logs/fix" --done verifying --once >/dev/null 2>"$logs/work"
check "a worker takes an issue, runs its program, and finishes" test $? -eq 0
"$neotrac" show issue "$worked" --json >"$logs/worked.json"
"$neotrac" show issue "$worked" >"$logs/worked"
check "moving it where it goes when done" grep -q '"stage":"Verifying"' "$logs/worked.json"
check "saying what the program wrote" grep -q "Looked at Flaky test: a race in setup." "$logs/worked"
check "and holding it no longer" grep -q '"held_by":""' "$logs/worked.json"
check "logging what it wrote, as it ran" grep -q '"log":\[{"by":"[^"]*","text":"Looked at Flaky test: a race in setup."' "$logs/worked.json"
NEOTRAC_KEY=$ranger "$neotrac" log "$worked" -m "checked again: $HOME/runs is clean" >/dev/null
"$neotrac" show issue "$worked" --json >"$logs/logged.json"
check "neotrac log adds to it, newest first, the home folder said as ~" grep -q '"log":\[{"by":"[^"]*","text":"checked again: ~/runs is clean"' "$logs/logged.json"
check "attaching what it made beside what it was given" grep -q '"attachments":\[{"name":"notes.txt",[^]]*{"name":"report.txt"' "$logs/worked.json"
"$neotrac" download "$worked" "$logs/made" >/dev/null
check "made from the attachment it was given" grep -qx "seen: fails one run in ten" "$logs/made/report.txt"
failing=$(issue_in_investigate "Ghost bug")
NEOTRAC_KEY=$ranger timeout 60 "$neotrac" work --run "$logs/fix" --done verifying --once >/dev/null 2>"$logs/work"
"$neotrac" show issue "$failing" --json >"$logs/failed.json"
"$neotrac" show issue "$failing" >"$logs/failed"
check "a program that fails leaves the issue where it was" grep -q '"stage":"Investigate"' "$logs/failed.json"
check "saying why" grep -q "can't reproduce Ghost bug" "$logs/failed"
check "naming the program, not where it is on this machine" grep -q "^fix exited 1" "$logs/failed"
check "nor the home folder, said as ~" sh -c '! grep -q "$1" "$2" && grep -q "its log is ~/.cache/runs/fix.log" "$2"' - "$HOME" "$logs/failed"
check "and lets it go, for another to take" grep -q '"held_by":""' "$logs/failed.json"
cat >"$logs/later" <<'LATER'
#!/bin/sh
echo "waiting on the compiler"
exit 75
LATER
chmod +x "$logs/later"
NEOTRAC_KEY=$ranger "$neotrac" claim "$failing" >/dev/null && NEOTRAC_KEY=$ranger "$neotrac" move "$failing" verifying >/dev/null
waiting=$(issue_in_investigate "Not yet")
NEOTRAC_KEY=$ranger timeout 60 "$neotrac" work --run "$logs/later" --done verifying --failed closed --once >/dev/null 2>"$logs/work"
"$neotrac" show issue "$waiting" --json >"$logs/waiting.json"
check "a program that isn't ready leaves the issue where it was" grep -q '"stage":"Investigate"' "$logs/waiting.json"
check "let go, for when it's ready" grep -q '"held_by":""' "$logs/waiting.json"
curl -s -H "Authorization: Bearer owner" "http://localhost:8080/v1/projects/demo-uione/databases/(default)/documents/projects_issue/e2e-$waiting" >"$logs/waiting.data"
check "its card waiting, on what the program said" python3 -c '
import json,sys; d=json.load(open(sys.argv[1]))["fields"]["data"]["mapValue"]["fields"]
assert d["status"]["stringValue"] == "waiting" and d["note"]["stringValue"] == "waiting on the compiler"' "$logs/waiting.data"
# Looked at again, still not ready, it keeps waiting since it first did: its timer
# doesn't start over at each look.
sleep 1.5
NEOTRAC_KEY=$ranger timeout 60 "$neotrac" work --run "$logs/later" --done verifying --failed closed --once >/dev/null 2>"$logs/work"
curl -s -H "Authorization: Bearer owner" "http://localhost:8080/v1/projects/demo-uione/databases/(default)/documents/projects_issue/e2e-$waiting" >"$logs/waiting.again"
check "looked at again, it's waiting since it first was" python3 -c '
import json,sys; f=lambda p: json.load(open(p))["fields"]["data"]["mapValue"]["fields"]
a, b = f(sys.argv[1]), f(sys.argv[2])
assert b["status"]["stringValue"] == "waiting" and b["since"] == a["since"]' "$logs/waiting.data" "$logs/waiting.again"

# An issue blocked by another waits on it: no one takes it, and it says on what, until
# the other is at a stage whose status is success, Closed here; then it's taken.
NEOTRAC_KEY=$ranger "$neotrac" claim "$waiting" >/dev/null 2>&1 && NEOTRAC_KEY=$ranger "$neotrac" move "$waiting" verifying >/dev/null 2>&1
needed=$(as "$person" projects/issue/create '{"project":"e2e","board":"e2e-main","title":"The fix it needs"}' | python3 -c 'import json,sys; print(json.load(sys.stdin)["id"])')
dependent=$(issue_in_investigate "After the fix")
as "$person" projects/link/create "{\"project\":\"e2e\",\"from\":\"e2e-$dependent\",\"to\":\"$needed\",\"relation\":\"blocked_by\"}" >/dev/null
NEOTRAC_KEY=$ranger "$neotrac" claim "$dependent" >"$logs/depends" 2>&1
check "an issue blocked by another isn't taken" test $? -eq 5
check "and says what it waits on" grep -q "waiting on The fix it needs" "$logs/depends"
as "$person" projects/issue/move "{\"id\":\"$needed\",\"stage\":\"e2e-main-closed\"}" >/dev/null
NEOTRAC_KEY=$ranger "$neotrac" claim "$dependent" >"$logs/depends" 2>&1
check "once what it waits on has succeeded, it's taken" test $? -eq 0

if [ $failures -eq 0 ]; then echo "all passed"; else echo "$failures failed"; exit 1; fi
