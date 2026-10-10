#!/bin/bash
# Copyright 2026 Daher Alfawares
# SPDX-License-Identifier: GPL-3.0-only

# The neotrac command, end to end, against neotrac's own backend on the Firebase
# emulators: a person sets up a phase worked by agents, a service with a key works it.
#
#   1. a project on the bugs workflow, with Investigate worked by Agent, a person's
#      step into it, and Agent's step out of it to Verifying;
#   2. a service, Scout, given Agent and a key;
#   3. a running `neotrac queue --watch --json` shows the issue a person moves in;
#   4. two services claim it at once: one wins, the other exits 5;
#   5. the service comments, is refused a move its role can't make (exit 5, the
#      moves it can make listed) and makes the one it can;
#   6. the issue's history names the service;
#   7. once its key is revoked, the next command exits 3.
#
# Needs the emulators (firestore localhost:8080, auth localhost:9099), ../build/api
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
	rm -rf "$XDG_CONFIG_HOME" "$logs"
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
	-d '{"email":"maintainer@example.com","password":"password","returnSecureToken":true}' | python3 -c 'import json,sys; print(json.load(sys.stdin)["idToken"])')
as "$person" projects/project/create '{"slug":"e2e","name":"E2E","workflow":"bugs","visibility":"private"}' >/dev/null
as "$person" projects/phase/create '{"board":"e2e-main","project":"e2e","title":"Investigate","worked_by":["e2e-agent"]}' >/dev/null
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
as "$person" projects/issue/move '{"id":"e2e-1","phase":"e2e-main-investigate"}' >/dev/null
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
winner=$scout
[ $a -ne 0 ] && winner=$ranger
"$neotrac" comment 1 -m "Found it: a nil map on save." >/dev/null 2>&1 || true
NEOTRAC_KEY=$winner "$neotrac" move 1 closed >"$logs/refused" 2>&1
refused=$?
check "a move its role can't make exits 5" test $refused -eq 5
check "and lists the moves it can make" grep -q "you can move it to: verifying" "$logs/refused"
NEOTRAC_KEY=$winner "$neotrac" move 1 verifying >"$logs/moved" 2>&1
check "the move its role can make is made" grep -q "Moved 1 to Verifying" "$logs/moved"
"$neotrac" show 1 --history >"$logs/show" 2>&1
check "the history names the service" grep -Eq "^  (Scout|Ranger): phase" "$logs/show"

as "$person" projects/service/revoke '{"id":"e2e-scout"}' >/dev/null
"$neotrac" whoami >/dev/null 2>&1
check "a revoked key exits 3" test $? -eq 3

if [ $failures -eq 0 ]; then echo "all passed"; else echo "$failures failed"; exit 1; fi
