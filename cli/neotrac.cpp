// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: GPL-3.0-only

// neotrac: neotrac's projects, boards, issues, wiki and milestones on the command
// line. A service, like an AI agent, signs in with the key a project gave it; a person
// signs in through their browser. It lists the issues in the stages a role of theirs
// works, takes one, comments, and moves it on where the workflow lets it; and lists,
// shows, makes, edits and links the rest, picked from a list and written in the
// person's editor at a terminal (terminal.hpp), or named and given as flags by a
// program.
//
// Everything it knows about neotrac comes from the client one build writes,
// generated/neotrac.hpp, so a change to neotrac's .one files reaches it at the next
// build.

#include <neotrac.hpp>

#include "terminal.hpp"

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <termios.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

	using namespace neotrac;

	constexpr const char* version = "0.6.0";

	// What went wrong, as the exit code a script reads.
	enum exit_code : int {
		done = 0,
		other = 1,
		usage = 2,
		signed_out = 3,  // no key or sign-in, or one the app refused
		not_found = 4,
		said_no = 5,  // the workflow or a require refused it
		network = 6,
		too_many = 7,  // rate-limited, after waiting
	};

	struct stop : std::runtime_error {
		int code;
		stop(int code, const std::string& message) : std::runtime_error(message), code(code) {}
	};

	// What was asked: the command, its words, and its flags.
	struct asked {
		std::string command;
		std::vector<std::string> words;
		std::map<std::string, std::string> flags;  // --json is {"json", ""}
		bool has(const std::string& flag) const { return flags.contains(flag); }
		std::string flag(const std::string& name, const std::string& otherwise = "") const {
			auto it = flags.find(name);
			return it == flags.end() ? otherwise : it->second;
		}
	};

	// Flags that take a value, like --board main; any other is on or off.
	const std::set<std::string> valued{"board", "project", "server", "message", "m", "note", "title", "body", "priority", "labels",
	                                   "due", "name", "summary", "visibility", "workflow", "slug", "confirm", "run", "done", "failed",
	                                   "milestone", "status", "notify", "from", "to", "position", "role", "worked-by", "roles"};

	asked read_arguments(int count, char** given) {
		asked out;
		for (int i = 1; i < count; ++i) {
			std::string word = given[i];
			if (word.starts_with("--") || (word.size() == 2 && word[0] == '-' && std::isalpha(static_cast<unsigned char>(word[1])))) {
				std::string name = word.substr(word.starts_with("--") ? 2 : 1);
				std::string value;
				if (auto equals = name.find('='); equals != std::string::npos) {
					value = name.substr(equals + 1);
					name = name.substr(0, equals);
				} else if (valued.contains(name)) {
					if (i + 1 >= count) throw stop(usage, "--" + name + " needs a value");
					value = given[++i];
				}
				if (name == "m") name = "message";
				out.flags[name] = value;
			} else if (out.command.empty()) {
				out.command = word;
			} else {
				out.words.push_back(word);
			}
		}
		return out;
	}

	// Whether to ask the person: at a terminal, and not asked for JSON.
	bool asking(const asked& a) { return terminal::present() && !a.has("json"); }

	// JSON, written by hand: strings, numbers and lists of them.
	std::string json(const std::string& s) { return detail::json(s); }
	std::string json(double n) { return detail::json(n); }
	std::string json(bool b) { return b ? "true" : "false"; }
	std::string json(const std::vector<std::string>& list) { return detail::json(list); }

	// What a sign-in keeps between runs, in $XDG_CONFIG_HOME/neotrac/credentials, which
	// only its owner may read.
	struct kept {
		std::string server, key, refresh_token, project, board;  // board: the selected one's name, in project
	};

	std::filesystem::path credentials_path() {
		const char* config = std::getenv("XDG_CONFIG_HOME");
		const char* home = std::getenv("HOME");
		std::filesystem::path base = config && *config ? config : std::filesystem::path(home ? home : ".") / ".config";
		return base / "neotrac" / "credentials";
	}

	kept read_credentials() {
		kept out;
		auto path = credentials_path();
		struct stat about {};
		if (::stat(path.c_str(), &about) != 0) return out;
		if (about.st_mode & 077) {
			throw stop(signed_out, path.string() + " can be read by others; run chmod 600 " + path.string() + ", or sign in again");
		}
		std::ifstream in(path);
		std::stringstream text;
		text << in.rdbuf();
		auto v = ember::parse_json(text.str());
		out.server = detail::text(v["server"]);
		out.key = detail::text(v["key"]);
		out.refresh_token = detail::text(v["refresh_token"]);
		out.project = detail::text(v["project"]);
		out.board = detail::text(v["board"]);
		return out;
	}

	void write_credentials(const kept& k) {
		auto path = credentials_path();
		std::filesystem::create_directories(path.parent_path());
		std::string text = "{\"server\":" + json(k.server) + ",\"key\":" + json(k.key) + ",\"refresh_token\":" + json(k.refresh_token) +
		                   ",\"project\":" + json(k.project) + ",\"board\":" + json(k.board) + "}\n";
		// Made private before anything secret is written to it.
		std::ofstream(path, std::ios::trunc).close();
		::chmod(path.c_str(), 0600);
		std::ofstream(path, std::ios::trunc) << text;
	}

	// A secret typed without being shown.
	std::string read_hidden(const std::string& prompt) {
		std::string line;
		if (!::isatty(STDIN_FILENO)) {
			std::getline(std::cin, line);
			return line;
		}
		std::fprintf(stderr, "%s", prompt.c_str());
		termios before{};
		::tcgetattr(STDIN_FILENO, &before);
		termios hidden = before;
		hidden.c_lflag &= ~static_cast<tcflag_t>(ECHO);
		::tcsetattr(STDIN_FILENO, TCSANOW, &hidden);
		std::getline(std::cin, line);
		::tcsetattr(STDIN_FILENO, TCSANOW, &before);
		std::fprintf(stderr, "\n");
		return line;
	}

	std::string env(const char* name) {
		const char* value = std::getenv(name);
		return value ? value : "";
	}

	// The app, signed in as the key or the person the credentials say, or as the
	// environment says: NEOTRAC_KEY wins over what's kept.
	struct session {
		std::unique_ptr<ember::queue_executor> here = std::make_unique<ember::queue_executor>();
		std::unique_ptr<client> app;
		kept credentials;
		caller who;
		std::string project;
		std::string board;               // the board's name, when one is selected or named
		std::vector<std::string> roles;  // the ids of the roles held in the project
		projects::mine mine;               // every project the person is in, with their roles
	};

	options options_for(const std::string& server) {
		options o;
		o.server = server;
		o.project = env("NEOTRAC_FIREBASE_PROJECT");
		o.firestore_host = env("NEOTRAC_FIRESTORE_HOST");
		o.auth_host = env("NEOTRAC_AUTH_HOST");
		return o;
	}

	// The projects a person is in, each once, in the order mine lists them.
	std::vector<std::string> projects_in(const projects::mine& mine) {
		std::vector<std::string> out;
		for (const auto& row : mine.rows) {
			if (std::ranges::find(out, row.project) == out.end()) out.push_back(row.project);
		}
		return out;
	}

	std::string joined(const std::vector<std::string>& words) {
		std::string out;
		for (const auto& w : words) out += (out.empty() ? "" : ", ") + w;
		return out;
	}

	// Signs in from what's kept. A command that works on issues needs a project; whoami
	// and use don't, so they pass needs_project = false.
	session sign_in(const asked& a, bool needs_project = true) {
		session s;
		s.credentials = read_credentials();
		std::string server = a.flag("server", env("NEOTRAC_SERVER"));
		if (server.empty()) server = s.credentials.server.empty() ? "https://neotrac.org" : s.credentials.server;
		s.app = std::make_unique<client>(options_for(server), s.here.get());
		std::string key = env("NEOTRAC_KEY");
		if (key.empty()) key = s.credentials.key;
		if (!key.empty()) {
			s.app->sign_in_with_key(key);
		} else if (!s.credentials.refresh_token.empty()) {
			s.app->sign_in_with_refresh_token(s.credentials.refresh_token);
		} else {
			throw stop(signed_out, "not signed in: run neotrac login --key for a service, or neotrac login for yourself");
		}
		s.who = s.app->me();
		// The project: a service's own, or the one asked for, kept, or the person's only one.
		s.project = s.who.service ? s.who.place : a.flag("project", s.credentials.project);
		s.mine = s.app->mine().get();
		auto& mine = s.mine;
		if (s.project.empty()) {
			auto projects = projects_in(mine);
			if (projects.size() == 1) s.project = projects.front();
			else if (needs_project && projects.empty()) throw stop(not_found, "you aren't in any project yet; join one on neotrac first");
			else if (needs_project) throw stop(usage, "you're in " + joined(projects) + "; pick one with neotrac select project, or --project");
		}
		for (const auto& row : mine.rows) {
			if (row.project == s.project) s.roles.push_back(row.role);
		}
		// The board named, or the one selected in this project.
		s.board = a.flag("board", s.project == s.credentials.project ? s.credentials.board : "");
		if (s.who.service && !s.who.role.empty() && std::ranges::find(s.roles, s.who.role) == s.roles.end()) s.roles.push_back(s.who.role);
		return s;
	}

	bool shares(const std::vector<std::string>& a, const std::vector<std::string>& b) {
		return std::ranges::any_of(a, [&](const auto& x) { return std::ranges::find(b, x) != b.end(); });
	}

	std::string lower(std::string s) {
		std::ranges::transform(s, s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return s;
	}

	// An issue named by its number, like 42 or #42.
	double issue_number(const std::string& word) {
		std::string digits = word.starts_with("#") ? word.substr(1) : word;
		char* end = nullptr;
		double n = std::strtod(digits.c_str(), &end);
		if (digits.empty() || *end != '\0' || n < 1) throw stop(usage, word + " isn't an issue's number, like 42");
		return n;
	}

	// One issue waiting in a stage a role of yours works, and who holds it, if its
	// lease hasn't run out.
	struct waiting {
		std::string id, title, stage_title, board_title, priority, holder;
		double number = 0;
		bool yours = false;
	};

	// The order issues are worked in: most urgent first, then oldest.
	int urgency(const std::string& priority) {
		static const std::vector<std::string> order{"critical", "high", "normal", "low", "trivial"};
		auto it = std::ranges::find(order, priority);
		return it == order.end() ? 2 : static_cast<int>(it - order.begin());
	}

	// What's worked by each of your roles, on the boards asked about: the issues free
	// to take and yours, or, with --all, those others hold too.
	std::vector<waiting> queue_of(const session& s, const std::map<std::string, projects::issue_work>& works, const std::map<std::string, std::string>& titles, bool all) {
		std::vector<waiting> out;
		std::set<std::string> seen;
		auto now = std::chrono::system_clock::now();
		for (const auto& [role, work] : works) {
			for (const auto& issue : work.items) {
				if (!titles.contains(issue.board) || !seen.insert(issue.id).second) continue;
				bool held = !issue.held_by.empty() && issue.held_until > now;
				bool yours = held && issue.held_by == s.who.id;
				if (!all && held && !yours) continue;
				out.push_back({issue.id, issue.title, issue.stage_title, titles.at(issue.board), projects::to_string(issue.priority),
				               held ? issue.held_by_name : "", issue.number, yours});
			}
		}
		std::ranges::sort(out, [](const waiting& a, const waiting& b) {
			return urgency(a.priority) != urgency(b.priority) ? urgency(a.priority) < urgency(b.priority) : a.number < b.number;
		});
		return out;
	}

	std::string queue_json(const std::vector<waiting>& q) {
		std::string out = "[";
		for (std::size_t i = 0; i < q.size(); ++i) {
			const auto& w = q[i];
			out += (i ? "," : "") + std::string("{\"number\":") + json(w.number) + ",\"id\":" + json(w.id) + ",\"title\":" + json(w.title) +
			       ",\"priority\":" + json(w.priority) + ",\"stage\":" + json(w.stage_title) + ",\"board\":" + json(w.board_title) +
			       ",\"held_by\":" + json(w.holder) + ",\"yours\":" + json(w.yours) + "}";
		}
		return out + "]";
	}

	void print_queue(const std::vector<waiting>& q) {
		if (q.empty()) {
			std::puts("Nothing waiting.");
			return;
		}
		for (const auto& w : q) {
			std::string who = w.yours ? "yours" : w.holder.empty() ? "free" : "held by " + w.holder;
			std::printf("#%-5.0f %-8s %-12s %-40s %s\n", w.number, w.priority.c_str(), w.stage_title.c_str(), w.title.substr(0, 40).c_str(), who.c_str());
		}
	}

	volatile std::sig_atomic_t stopping = 0;

	// The boards worked on, by id, with their titles: the one selected or named, or
	// every one of the project's.
	projects::project_page project_of(session& s);

	std::map<std::string, std::string> boards_worked(session& s) {
		auto project = project_of(s);
		std::map<std::string, std::string> titles;
		for (const auto& board : project.boards) {
			if (s.board.empty() || board.name == s.board || board.id == s.board) titles[board.id] = board.title;
		}
		if (titles.empty()) throw stop(not_found, "there's no board " + s.board + " in " + s.project);
		if (s.roles.empty()) throw stop(not_found, "you hold no role in " + s.project + ", so nothing is worked by you there");
		return titles;
	}

	int queue(const asked& a) {
		auto s = sign_in(a);
		auto titles = boards_worked(s);
		bool all = a.has("all"), as_json = a.has("json");
		if (!a.has("watch")) {
			std::map<std::string, projects::issue_work> works;
			for (const auto& role : s.roles) works[role] = s.app->issue_work(role).get();
			auto q = queue_of(s, works, titles, all);
			if (as_json) std::puts(queue_json(q).c_str());
			else print_queue(q);
			return done;
		}
		// Watching: what each role works, listened to, and the queue said again whenever
		// it changes, or a lease runs out.
		std::map<std::string, projects::issue_work> works;
		std::vector<ember::database::registration> listening;
		std::string last;
		int failed = done;
		auto say = [&] {
			if (works.size() != s.roles.size()) return;
			auto q = queue_of(s, works, titles, all);
			std::string now = queue_json(q);
			if (now == last) return;
			last = now;
			if (as_json) {
				std::puts(now.c_str());
			} else {
				if (::isatty(STDOUT_FILENO)) std::printf("\033[H\033[2J");
				std::printf("Waiting in %s, for %s:\n\n", s.project.c_str(), s.who.name.c_str());
				print_queue(q);
			}
			std::fflush(stdout);
		};
		for (const auto& role : s.roles) {
			listening.push_back(s.app->issue_work(role).listen(
			    [&, role](const projects::issue_work& work) {
				    works[role] = work;
				    say();
			    },
			    [&](const ember::error& e) {
				    std::fprintf(stderr, "neotrac: %s\n", e.message.c_str());
				    failed = e.code == 7 ? signed_out : other;
				    stopping = 1;
			    }));
		}
		std::signal(SIGINT, [](int) { stopping = 1; });
		std::signal(SIGTERM, [](int) { stopping = 1; });
		while (!stopping) {
			s.here->run_one_for(std::chrono::milliseconds(250));
			say();  // a lease that ran out frees its issue without a change to listen to
		}
		return failed;
	}

	std::string issue_of(const session& s, const asked& a) {
		if (a.words.empty()) throw stop(usage, "say which issue, like neotrac " + a.command + " 42");
		return projects::issue_id(s.project, issue_number(a.words[0]));
	}

	projects::issue_page read_issue(session& s, const std::string& id, const std::string& word) {
		auto page = s.app->issue_page(id).get();
		if (!page.exists) throw stop(not_found, "there's no issue " + word + " in " + s.project + " you may read");
		return page;
	}

	// The moves a role of yours may make from where the issue is.
	std::vector<const projects::issue_page::steps_row*> moves_of(const session& s, const projects::issue_page& page) {
		std::vector<const projects::issue_page::steps_row*> out;
		for (const auto& step : page.steps) {
			if (step.from_ == page.stage && shares(step.roles, s.roles)) out.push_back(&step);
		}
		return out;
	}

	std::string text_of(const ember::value& v) { return v.is_string() ? v.as_string() : v.is_null() ? "" : ember::to_json(v); }

	// Whether someone holds it: they claimed it, and their lease hasn't run out.
	bool held(const projects::issue_page& page) { return !page.held_by.empty() && page.held_until > std::chrono::system_clock::now(); }

	// A time today as 14:20, in this computer's zone.
	std::string clock_of(neotrac::time t) {
		std::time_t at = std::chrono::system_clock::to_time_t(std::chrono::time_point_cast<std::chrono::system_clock::duration>(t));
		std::tm local{};
		::localtime_r(&at, &local);
		char out[8];
		std::strftime(out, sizeof out, "%H:%M", &local);
		return out;
	}

	// A file's name: the last part of its path.
	std::string file_name(const std::string& path) { return path.substr(path.find_last_of('/') + 1); }

	// What a file holds, by its name, so a browser shows a picture as one.
	std::string type_of(const std::string& path) {
		static const std::map<std::string, std::string> types{
		    {"png", "image/png"},        {"jpg", "image/jpeg"},     {"jpeg", "image/jpeg"},       {"gif", "image/gif"},       {"webp", "image/webp"},
		    {"svg", "image/svg+xml"},    {"pdf", "application/pdf"}, {"txt", "text/plain"},        {"log", "text/plain"},      {"md", "text/markdown"},
		    {"csv", "text/csv"},         {"json", "application/json"}, {"srt", "application/x-subrip"}, {"vtt", "text/vtt"}, {"mp4", "video/mp4"},
		    {"mov", "video/quicktime"}, {"webm", "video/webm"},      {"mp3", "audio/mpeg"},        {"wav", "audio/wav"},       {"zip", "application/zip"},
		};
		auto dot = path.find_last_of('.');
		auto found = dot == std::string::npos ? types.end() : types.find(lower(path.substr(dot + 1)));
		return found == types.end() ? "application/octet-stream" : found->second;
	}

	// Where in a folder a file is kept under its own name, or, where that's taken,
	// a number and its name.
	std::string kept_in(const std::string& folder, const std::string& name) {
		std::string at = folder + "/" + name;
		for (int n = 1; std::filesystem::exists(at); ++n) at = folder + "/" + std::to_string(n) + "-" + name;
		return at;
	}

	void print_issue(const session& s, const projects::issue_page& page, bool history) {
		std::printf("#%.0f %s\n", page.number, page.title.c_str());
		std::printf("%s on %s, %s priority", page.stage_title.c_str(), page.board_title.c_str(), projects::to_string(page.priority));
		if (!page.assignees_name.empty()) {
			std::printf(", assigned to");
			for (std::size_t i = 0; i < page.assignees_name.size(); ++i) std::printf("%s %s", i ? "," : "", page.assignees_name[i].c_str());
		}
		if (held(page)) std::printf(", held by %s until %s", page.held_by_name.c_str(), clock_of(page.held_until).c_str());
		if (!page.labels.empty()) {
			std::printf("; labels");
			for (const auto& l : page.labels) std::printf(" %s", l.c_str());
		}
		std::printf("\n\n%s\n", page.body.empty() ? "(no description)" : page.body.c_str());
		if (!page.attachments.empty()) {
			std::printf("\nAttached:");
			for (std::size_t i = 0; i < page.attachments.size(); ++i) std::printf("%s %s", i ? "," : "", file_name(page.attachments[i]).c_str());
			std::printf("\n");
		}
		for (const auto& link : page.links) std::printf("\n%s #%.0f %s", projects::to_string(link.relation), link.to_number, link.to_title.c_str());
		if (!page.links.empty()) std::printf("\n");
		for (const auto& c : page.comments) std::printf("\n%s:\n%s\n", c.author_name.c_str(), c.body.c_str());
		if (history) {
			std::printf("\nHistory:\n");
			for (const auto& h : page.history) {
				std::printf("  %s: %s %s -> %s\n", text_of(h.created_by_name).c_str(), text_of(h.field).c_str(), text_of(h.before).c_str(), text_of(h.after).c_str());
			}
		}
		auto moves = moves_of(s, page);
		std::printf("\nMoves you can make:");
		if (moves.empty()) std::printf(" none from here");
		for (const auto* m : moves) std::printf(" %s", lower(m->to_title).c_str());
		std::printf("\n");
	}

	std::string issue_json(const session& s, const projects::issue_page& page) {
		std::string out = "{\"number\":" + json(page.number) + ",\"title\":" + json(page.title) + ",\"body\":" + json(page.body) + ",\"stage\":" +
		                  json(page.stage_title) + ",\"board\":" + json(page.board_title) + ",\"priority\":" + json(std::string(projects::to_string(page.priority))) +
		                  ",\"labels\":" + json(page.labels) + ",\"assigned\":" + json(page.assignees_name) + ",\"held_by\":" + json(held(page) ? page.held_by_name : std::string()) + ",\"milestone\":" + json(page.milestone_title) + ",\"attachments\":[";
		for (std::size_t i = 0; i < page.attachments.size(); ++i) {
			out += (i ? "," : "") + std::string("{\"name\":") + json(file_name(page.attachments[i])) + ",\"path\":" + json(page.attachments[i]) + "}";
		}
		out += "],\"links\":[";
		for (std::size_t i = 0; i < page.links.size(); ++i) {
			out += (i ? "," : "") + std::string("{\"relation\":") + json(std::string(projects::to_string(page.links[i].relation))) +
			       ",\"to\":" + json(page.links[i].to_number) + "}";
		}
		out += "],\"comments\":[";
		for (std::size_t i = 0; i < page.comments.size(); ++i) {
			out += (i ? "," : "") + std::string("{\"by\":") + json(page.comments[i].author_name) + ",\"body\":" + json(page.comments[i].body) + "}";
		}
		out += "],\"moves\":[";
		auto moves = moves_of(s, page);
		for (std::size_t i = 0; i < moves.size(); ++i) out += (i ? "," : "") + json(lower(moves[i]->to_title));
		return out + "]}";
	}

	int show(const asked& a) {
		auto s = sign_in(a);
		std::string id = issue_of(s, a);
		if (!a.has("watch")) {
			auto page = read_issue(s, id, a.words[0]);
			if (a.has("json")) std::puts(issue_json(s, page).c_str());
			else print_issue(s, page, a.has("history"));
			return done;
		}
		int failed = done;
		auto listening = s.app->issue_page(id).listen(
		    [&](const projects::issue_page& page) {
			    if (a.has("json")) {
				    std::puts(issue_json(s, page).c_str());
			    } else {
				    if (::isatty(STDOUT_FILENO)) std::printf("\033[H\033[2J");
				    print_issue(s, page, a.has("history"));
			    }
			    std::fflush(stdout);
		    },
		    [&](const ember::error& e) {
			    std::fprintf(stderr, "neotrac: %s\n", e.message.c_str());
			    failed = other;
			    stopping = 1;
		    });
		std::signal(SIGINT, [](int) { stopping = 1; });
		std::signal(SIGTERM, [](int) { stopping = 1; });
		while (!stopping) s.here->run_one_for(std::chrono::milliseconds(250));
		return failed;
	}

	int code_of(int status);

	// The last lines of what a program wrote, for a comment.
	std::string tail_of(const std::string& path, std::size_t lines) {
		std::ifstream in(path);
		std::vector<std::string> kept;
		for (std::string line; std::getline(in, line);) {
			kept.push_back(line);
			if (kept.size() > lines) kept.erase(kept.begin());
		}
		std::string out;
		for (const auto& line : kept) out += line + "\n";
		while (!out.empty() && (out.back() == '\n' || out.back() == ' ')) out.pop_back();
		return out;
	}

	// The stage of an issue's board by its name, title or id.
	std::string stage_named(session& s, const projects::issue_page& page, const std::string& word) {
		auto board = s.app->board_page(page.board).get();
		for (const auto& stage : board.stages) {
			if (lower(stage.name) == lower(word) || lower(stage.title) == lower(word) || stage.id == word) return stage.id;
		}
		throw stop(usage, "there's no stage " + word + " on " + page.board_title);
	}

	// What's said of an issue for its card: its status, since when, a note; a null
	// takes one out. Saying it is a courtesy, so a refusal is only told.
	void report(session& s, const std::string& id, std::map<std::string, ember::value> data) {
		try {
			projects::issue_report sent{.id = id};
			sent.data = ember::value(std::move(data));
			s.app->issue_report(sent);
		} catch (const refused& r) {
			std::fprintf(stderr, "neotrac: couldn't say how it's going: %s\n", r.what());
		}
	}

	std::string now_said() {
		return std::format("{:%FT%TZ}", std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()));
	}

	// Works one issue it holds: runs the program with the issue as JSON on its input,
	// in a folder of its own, WORK_DIR, keeping its claim while it runs; then says
	// what it wrote, and moves it on, or, when it fails, back or lets it go.
	// Whether it waits: the program said it isn't ready yet.
	bool work_on(session& s, const std::string& id, const std::string& program, const std::string& done_word, const std::string& failed_word) {
		auto page = read_issue(s, id, id);
		std::string number = "#" + std::to_string(static_cast<long long>(page.number));
		std::string root = (env("TMPDIR").empty() ? std::string("/tmp") : env("TMPDIR")) + "/neotrac-work-XXXXXX";
		if (!::mkdtemp(root.data())) throw stop(other, "couldn't make a folder to work in");
		std::string folder = root + "/work";
		std::filesystem::create_directories(folder + "/out");
		// Its attachments are in its folder, each under its own name.
		std::vector<std::string> local;
		for (const auto& path : page.attachments) {
			local.push_back(kept_in(folder, file_name(path)));
			s.app->download(path, local.back());
		}
		std::string out_path = root + "/stdout", err_path = root + "/stderr";
		int input[2];
		if (::pipe(input) != 0) throw stop(other, "couldn't start " + program);
		posix_spawn_file_actions_t files;
		posix_spawn_file_actions_init(&files);
		posix_spawn_file_actions_adddup2(&files, input[0], STDIN_FILENO);
		posix_spawn_file_actions_addclose(&files, input[1]);
		posix_spawn_file_actions_addopen(&files, STDOUT_FILENO, out_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
		posix_spawn_file_actions_addopen(&files, STDERR_FILENO, err_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
		::setenv("WORK_DIR", folder.c_str(), 1);
		::setenv("NEOTRAC_ISSUE", std::to_string(static_cast<long long>(page.number)).c_str(), 1);
		std::string script = program;
		char sh[] = "/bin/sh", c[] = "-c";
		char* argv[] = {sh, c, script.data(), nullptr};
		pid_t child;
		int started = ::posix_spawn(&child, "/bin/sh", &files, nullptr, argv, environ);
		posix_spawn_file_actions_destroy(&files);
		::close(input[0]);
		if (started != 0) {
			::close(input[1]);
			throw stop(other, "couldn't start " + program);
		}
		std::string given = issue_json(s, page);
		given.pop_back();
		given += ",\"files\":" + json(local) + "}\n";
		(void)!::write(input[1], given.data(), given.size());
		::close(input[1]);
		std::fprintf(stderr, "%s %s: working\n", number.c_str(), page.title.c_str());
		report(s, id, {{"status", "working"}, {"since", now_said()}});
		// Kept while it runs: renewed every five minutes, well inside any lease.
		auto renewed = std::chrono::steady_clock::now();
		int status = 0;
		bool lost = false;
		while (::waitpid(child, &status, WNOHANG) == 0) {
			std::this_thread::sleep_for(std::chrono::milliseconds(200));
			if (std::chrono::steady_clock::now() - renewed < std::chrono::minutes(5)) continue;
			try {
				s.app->issue_renew({.id = id});
				renewed = std::chrono::steady_clock::now();
			} catch (const refused& r) {
				// Someone else has it now: what this run makes is no longer wanted.
				std::fprintf(stderr, "%s: lost it: %s\n", number.c_str(), r.what());
				::kill(child, SIGTERM);
				lost = true;
			}
		}
		bool fine = !lost && WIFEXITED(status) && WEXITSTATUS(status) == 0;
		// 75, EX_TEMPFAIL: it can't be done yet, like a deploy waiting on its packages.
		// It's let go where it is, saying what it waits on, and taken again once the
		// queue changes.
		bool later = !lost && WIFEXITED(status) && WEXITSTATUS(status) == 75;
		try {
			if (lost) {
				// Nothing to say or move: it isn't this worker's any more.
			} else if (later) {
				std::string said = tail_of(out_path, 1);
				report(s, id, {{"status", "waiting"}, {"since", now_said()}, {"note", said.empty() ? ember::value() : ember::value(said)}});
				s.app->issue_release({.id = id});
				std::fprintf(stderr, "%s: waiting%s\n", number.c_str(), said.empty() ? "" : (": " + said).c_str());
			} else if (fine) {
				// What it wrote in out/ is attached to the issue.
				std::vector<std::string> made;
				std::vector<std::filesystem::path> written;
				for (const auto& entry : std::filesystem::directory_iterator(folder + "/out")) {
					if (entry.is_regular_file()) written.push_back(entry.path());
				}
				std::ranges::sort(written);
				for (const auto& path : written) made.push_back(s.app->upload(path.string(), type_of(path.string())));
				if (!made.empty()) {
					auto now = read_issue(s, id, id);
					projects::issue_attach sent{.id = id};
					sent.attachments = now.attachments;
					sent.attachments->insert(sent.attachments->end(), made.begin(), made.end());
					s.app->issue_attach(sent);
				}
				std::string said = tail_of(out_path, 40);
				if (!said.empty()) s.app->comment_create({.issue = id, .body = said});
				report(s, id, {{"status", ember::value()}, {"since", ember::value()}});
				projects::issue_move sent{.id = id};
				sent.stage = stage_named(s, page, done_word);
				s.app->issue_move(sent);
				std::fprintf(stderr, "%s: done, moved to %s\n", number.c_str(), done_word.c_str());
			} else {
				std::string why = WIFEXITED(status) ? "exited " + std::to_string(WEXITSTATUS(status)) : "was stopped";
				std::string said = tail_of(err_path, 40);
				s.app->comment_create({.issue = id, .body = program + " " + why + (said.empty() ? "." : ":\n\n```\n" + said + "\n```")});
				report(s, id, {{"status", "failed"}, {"since", now_said()}, {"note", program + " " + why}});
				if (!failed_word.empty()) {
					projects::issue_move sent{.id = id};
					sent.stage = stage_named(s, page, failed_word);
					s.app->issue_move(sent);
					std::fprintf(stderr, "%s: failed, moved to %s\n", number.c_str(), failed_word.c_str());
				} else {
					s.app->issue_release({.id = id});
					std::fprintf(stderr, "%s: failed, let go\n", number.c_str());
				}
			}
		} catch (const refused& r) {
			std::fprintf(stderr, "%s: %s\n", number.c_str(), r.what());
			try {
				s.app->issue_release({.id = id});
			} catch (const refused&) {
			}
		}
		std::error_code ignored;
		std::filesystem::remove_all(root, ignored);
		return later;
	}

	// Works the issues in stages a role of yours works, one at a time, as they come:
	// takes one, runs the program on it, and moves it on. --once stops after one.
	int work(const asked& a) {
		auto s = sign_in(a);
		std::string program = a.flag("run"), done_word = a.flag("done"), failed_word = a.flag("failed");
		if (program.empty() || done_word.empty()) throw stop(usage, "say what to run and where it goes when it's done, like neotrac work --run ./fix --done review");
		auto titles = boards_worked(s);
		bool once = a.has("once");
		std::map<std::string, projects::issue_work> works;
		std::vector<ember::database::registration> listening;
		int failed = done;
		for (const auto& role : s.roles) {
			listening.push_back(s.app->issue_work(role).listen([&, role](const projects::issue_work& work) { works[role] = work; },
			                                                     [&](const ember::error& e) {
				                                                     std::fprintf(stderr, "neotrac: %s\n", e.message.c_str());
				                                                     failed = e.code == 7 ? signed_out : other;
				                                                     stopping = 1;
			                                                     }));
		}
		std::signal(SIGINT, [](int) { stopping = 1; });
		std::signal(SIGTERM, [](int) { stopping = 1; });
		std::set<std::string> tried;  // taken by someone else as it was asked for; tried again once it changes
		std::string seen;
		// What waits, until an issue comes into the queue or leaves it, like the one it
		// waits on moving on: its own let-go changes the queue, so that doesn't count.
		std::set<std::string> parked;
		std::string parked_with;
		// What it's just worked, not worked again while the queue still shows it, as it
		// does for a moment after it's moved on; dragged back later, it's worked again.
		std::map<std::string, std::chrono::steady_clock::time_point> finished;
		std::fprintf(stderr, "Working %s as %s; waiting for issues.\n", s.project.c_str(), s.who.name.c_str());
		while (!stopping) {
			s.here->run_one_for(std::chrono::milliseconds(250));
			if (works.size() != s.roles.size()) continue;
			auto q = queue_of(s, works, titles, false);
			std::string now = queue_json(q);
			if (now != seen) tried.clear(), seen = now;
			std::string ids;
			for (const auto& w : q) ids += w.id + " ";
			if (ids != parked_with) parked.clear();
			std::erase_if(finished, [&](const auto& f) {
				return std::ranges::none_of(q, [&](const waiting& w) { return w.id == f.first; }) || std::chrono::steady_clock::now() - f.second > std::chrono::seconds(30);
			});
			auto next = std::ranges::find_if(q, [&](const waiting& w) { return !tried.contains(w.id) && !parked.contains(w.id) && !finished.contains(w.id); });
			if (next == q.end()) continue;
			std::string id = next->id;
			// Claimed even when the queue says it's held here already, since the queue
			// can be a moment behind; refused, it's this worker's only if it really is,
			// like one started again while it held it.
			try {
				s.app->issue_claim({.id = id});
			} catch (const refused& r) {
				if (code_of(r.status) != said_no) throw;
				auto page = read_issue(s, id, id);
				if (!(held(page) && page.held_by == s.who.id)) {
					tried.insert(id);
					continue;
				}
			}
			if (work_on(s, id, program, done_word, failed_word)) {
				parked.insert(id);
				parked_with = ids;
			} else {
				finished[id] = std::chrono::steady_clock::now();
			}
			tried.insert(id);  // until the queue says it's gone, or changed
			if (once) break;
		}
		return failed;
	}

	// Uploads files and attaches them to an issue, beside what's attached already.
	int attach(const asked& a) {
		auto s = sign_in(a);
		std::string id = issue_of(s, a);
		if (a.words.size() < 2) throw stop(usage, "say which files, like neotrac attach 42 screenshot.png");
		auto page = read_issue(s, id, a.words[0]);
		std::vector<std::string> made;
		for (std::size_t i = 1; i < a.words.size(); ++i) {
			if (!std::filesystem::is_regular_file(a.words[i])) throw stop(usage, a.words[i] + " isn't a file");
		}
		for (std::size_t i = 1; i < a.words.size(); ++i) made.push_back(s.app->upload(a.words[i], type_of(a.words[i])));
		projects::issue_attach sent{.id = id};
		sent.attachments = page.attachments;
		sent.attachments->insert(sent.attachments->end(), made.begin(), made.end());
		s.app->issue_attach(sent);
		if (a.has("json")) {
			std::printf("{\"id\":%s,\"attached\":%s}\n", json(id).c_str(), json(made).c_str());
		} else {
			for (const auto& path : made) std::printf("Attached %s to #%s.\n", file_name(path).c_str(), a.words[0].c_str());
		}
		return done;
	}

	// Saves an issue's attachments in a folder, the one it's run in unless it's said.
	int download(const asked& a) {
		auto s = sign_in(a);
		std::string id = issue_of(s, a);
		auto page = read_issue(s, id, a.words[0]);
		std::string folder = a.words.size() > 1 ? a.words[1] : ".";
		std::filesystem::create_directories(folder);
		std::vector<std::string> saved;
		for (const auto& path : page.attachments) {
			saved.push_back(kept_in(folder, file_name(path)));
			s.app->download(path, saved.back());
		}
		if (a.has("json")) {
			std::printf("{\"id\":%s,\"saved\":%s}\n", json(id).c_str(), json(saved).c_str());
		} else if (saved.empty()) {
			std::printf("#%s has nothing attached.\n", a.words[0].c_str());
		} else {
			for (const auto& at : saved) std::printf("Saved %s\n", at.c_str());
		}
		return done;
	}

	int claim(const asked& a, bool taking) {
		auto s = sign_in(a);
		std::string id = issue_of(s, a);
		if (taking) s.app->issue_claim({.id = id});
		else s.app->issue_release({.id = id});
		if (a.has("json")) std::printf("{\"id\":%s,\"%s\":true}\n", json(id).c_str(), taking ? "taken" : "let_go");
		else std::printf(taking ? "Took %s.\n" : "Let go of %s.\n", a.words[0].c_str());
		return done;
	}

	// Says how an issue you hold is going, for its card's dot and timer: working,
	// waiting, done, failed or inactive, since now, with --note; clear takes it out.
	int status(const asked& a) {
		auto s = sign_in(a);
		std::string id = issue_of(s, a);
		if (a.words.size() < 2) throw stop(usage, "say how it's going, like neotrac status 42 working, or waiting, done, failed, inactive or clear");
		std::string word = lower(a.words[1]);
		std::map<std::string, ember::value> data;
		if (word == "clear") {
			data = {{"status", ember::value()}, {"since", ember::value()}, {"note", ember::value()}};
		} else {
			projects::status known;
			if (!projects::from_string(word, known)) throw stop(usage, a.words[1] + " isn't how an issue goes: working, waiting, done, failed, inactive or clear");
			data = {{"status", word}, {"since", now_said()}, {"note", a.has("note") ? ember::value(a.flag("note")) : ember::value()}};
		}
		projects::issue_report sent{.id = id};
		sent.data = ember::value(std::move(data));
		s.app->issue_report(sent);
		if (a.has("json")) std::printf("{\"id\":%s,\"status\":%s}\n", json(id).c_str(), json(word).c_str());
		else std::printf("%s is %s.\n", a.words[0].c_str(), word == "clear" ? "said nothing of" : word.c_str());
		return done;
	}

	// Closes an issue from whatever stage it's in, saying why, or opens it again.
	int close(const asked& a) {
		auto s = sign_in(a);
		std::string id = issue_of(s, a);
		if (a.words.size() < 2) throw stop(usage, "say why, like neotrac close 42 done, or wont_do, or duplicate 9");
		std::string why = lower(a.words[1]);
		std::ranges::replace(why, '-', '_');
		projects::issue_close sent{.id = id};
		projects::resolution resolution{};
		if (!projects::from_string(why, resolution)) throw stop(usage, a.words[1] + " isn't why an issue closes: done, wont_do or duplicate");
		sent.closed = resolution;
		if (resolution == projects::resolution::duplicate) {
			if (a.words.size() < 3) throw stop(usage, "say which issue it duplicates, like neotrac close 42 duplicate 9");
			sent.duplicate_of = projects::issue_id(s.project, issue_number(a.words[2]));
		}
		s.app->issue_close(sent);
		if (a.has("json")) std::printf("{\"id\":%s,\"closed\":%s}\n", json(id).c_str(), json(std::string(projects::to_string(resolution))).c_str());
		else std::printf("Closed %s.\n", a.words[0].c_str());
		return done;
	}

	int reopen(const asked& a) {
		auto s = sign_in(a);
		std::string id = issue_of(s, a);
		s.app->issue_reopen({.id = id});
		if (a.has("json")) std::printf("{\"id\":%s,\"reopened\":true}\n", json(id).c_str());
		else std::printf("Reopened %s.\n", a.words[0].c_str());
		return done;
	}

	int move(const asked& a) {
		auto s = sign_in(a);
		std::string id = issue_of(s, a);
		if (a.words.size() < 2) throw stop(usage, "say where to, like neotrac move 42 review");
		auto page = read_issue(s, id, a.words[0]);
		// The stage by its name, its title, or its id, among the issue's board's.
		std::string wanted = lower(a.words[1]);
		auto board = s.app->board_page(page.board).get();
		const projects::board_page::stages_row* to = nullptr;
		for (const auto& stage : board.stages) {
			if (lower(stage.name) == wanted || lower(stage.title) == wanted || stage.id == a.words[1]) to = &stage;
		}
		auto moves = moves_of(s, page);
		std::string allowed;
		for (const auto* m : moves) allowed += (allowed.empty() ? "" : ", ") + lower(m->to_title);
		if (allowed.empty()) allowed = "none from " + page.stage_title;
		if (!to) throw stop(usage, "there's no stage " + a.words[1] + " on " + page.board_title + "; you can move it to: " + allowed);
		if (std::ranges::none_of(moves, [&](const auto* m) { return m->to == to->id; })) {
			throw stop(said_no, "your role doesn't move an issue from " + page.stage_title + " to " + to->title + "; you can move it to: " + allowed);
		}
		projects::issue_move sent{.id = id};
		sent.stage = to->id;
		s.app->issue_move(sent);
		if (a.has("json")) std::printf("{\"id\":%s,\"stage\":%s}\n", json(id).c_str(), json(to->title).c_str());
		else std::printf("Moved %s to %s.\n", a.words[0].c_str(), to->title.c_str());
		return done;
	}

	int comment(const asked& a) {
		auto s = sign_in(a);
		std::string id = issue_of(s, a);
		std::string body = a.flag("message");
		if (!a.has("message") && asking(a)) {
			std::string saved = terminal::edit("\n" + terminal::scissors + "\n# Everything from the line above down is left out. Write the comment above it, in markdown.\n");
			body = terminal::trimmed(saved.substr(0, saved.find(terminal::scissors)));
		} else if (!a.has("message")) {
			std::stringstream in;
			in << std::cin.rdbuf();
			body = in.str();
		}
		while (!body.empty() && (body.back() == '\n' || body.back() == ' ')) body.pop_back();
		if (body.empty()) throw stop(usage, "say what to comment, with -m \"...\" or on stdin");
		projects::comment_create sent;
		sent.issue = id;
		sent.body = body;
		auto made = s.app->comment_create(sent);
		if (a.has("json")) std::printf("{\"id\":%s}\n", json(made).c_str());
		else std::printf("Commented on %s.\n", a.words[0].c_str());
		return done;
	}

	// Shows the address in the person's browser: $BROWSER when it's set, or the
	// system's opener. It's printed too, for a browser that doesn't open.
	void open_browser(const std::string& url) {
		std::fprintf(stderr, "Opening your browser to authorize neotrac. If it doesn't open, go to:\n\n  %s\n\nWaiting for you there...\n", url.c_str());
		std::string opener = env("BROWSER");
		opener = opener.substr(0, opener.find(':'));
#ifdef __APPLE__
		if (opener.empty()) opener = "open";
#else
		if (opener.empty()) opener = "xdg-open";
#endif
		posix_spawn_file_actions_t quiet;
		posix_spawn_file_actions_init(&quiet);
		posix_spawn_file_actions_addopen(&quiet, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
		posix_spawn_file_actions_addopen(&quiet, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
		std::string address = url;
		char* argv[] = {opener.data(), address.data(), nullptr};
		pid_t child;
		// Not waited for: the browser comes back to this program while it runs.
		posix_spawnp(&child, opener.c_str(), &quiet, nullptr, argv, environ);
		posix_spawn_file_actions_destroy(&quiet);
	}

	int login(const asked& a) {
		std::string server = a.flag("server", env("NEOTRAC_SERVER"));
		if (server.empty()) server = "https://neotrac.org";
		ember::queue_executor here;
		client app(options_for(server), &here);
		kept k;
		k.server = server;
		if (a.has("key")) {
			std::string key = env("NEOTRAC_KEY");
			if (key.empty()) key = read_hidden("The service's key: ");
			while (!key.empty() && (key.back() == '\n' || key.back() == '\r' || key.back() == ' ')) key.pop_back();
			if (key.empty()) throw stop(usage, "no key given");
			app.sign_in_with_key(key);
			k.key = key;
		} else {
			try {
				app.sign_in_with_browser(open_browser);
			} catch (const refused& r) {
				throw stop(signed_out, r.what());
			}
			k.refresh_token = app.refresh_token();
			k.project = a.flag("project");
		}
		auto who = app.me();
		write_credentials(k);
		if (who.service) std::printf("Signed in as %s, a service, in %s.\n", who.name.c_str(), who.place.c_str());
		else std::printf("Signed in as %s.\n", who.name.c_str());
		return done;
	}

	int whoami(const asked& a) {
		auto s = sign_in(a, false);
		std::string key = !env("NEOTRAC_KEY").empty() ? env("NEOTRAC_KEY") : s.credentials.key;
		if (a.has("json")) {
			std::printf("{\"id\":%s,\"name\":%s,\"service\":%s,\"project\":%s,\"roles\":%s}\n", json(s.who.id).c_str(), json(s.who.name).c_str(), json(s.who.service).c_str(),
			            json(s.project).c_str(), json(s.roles).c_str());
			return done;
		}
		if (s.who.service) {
			std::printf("%s, a service, in %s", s.who.name.c_str(), s.project.c_str());
			if (!key.empty()) std::printf(", with key %s…", key.substr(0, 10).c_str());
			std::printf("\n");
			return done;
		}
		std::printf("%s\n", s.who.name.c_str());
		for (const auto& project : projects_in(s.mine)) {
			std::vector<std::string> titles;
			for (const auto& row : s.mine.rows) {
				if (row.project == project) titles.push_back(row.role_title);
			}
			std::printf("  %s %s, %s\n", project == s.project ? "*" : " ", project.c_str(), joined(titles).c_str());
		}
		if (s.mine.rows.empty()) std::printf("  in no project yet\n");
		else if (s.project.empty()) std::printf("Pick the project commands work in with neotrac select project.\n");
		return done;
	}

	// ---- Picking what a command acts on --------------------------------------------

	// The kinds of things the command knows, singular, from what was typed.
	std::string noun_of(std::string word) {
		static const std::map<std::string, std::string> nouns{
		    {"project", "project"}, {"projects", "project"}, {"board", "board"}, {"boards", "board"}, {"issue", "issue"},
		    {"issues", "issue"}, {"page", "page"}, {"pages", "page"}, {"wiki", "page"}, {"milestone", "milestone"},
		    {"milestones", "milestone"}, {"stage", "stage"}, {"stages", "stage"}, {"step", "step"}, {"steps", "step"}, {"service", "service"}, {"services", "service"}, {"people", "person"}, {"person", "person"},
		    {"members", "person"}};
		auto it = nouns.find(lower(word));
		return it == nouns.end() ? "" : it->second;
	}

	// One of several things, by what was typed, or picked at the terminal. choices are
	// (what names it, how it reads in the list); a typed word matches what names it,
	// or how it reads, whatever its capitals.
	std::string chosen(const asked& a, std::size_t word, const std::string& what, const std::vector<std::pair<std::string, std::string>>& choices,
	                   const std::string& current = "") {
		if (a.words.size() > word) {
			std::string typed = lower(a.words[word]);
			for (const auto& [name, reads] : choices) {
				if (lower(name) == typed || lower(reads) == typed) return name;
			}
			throw stop(not_found, "there's no " + what + " " + a.words[word] + (choices.empty() ? "" : "; neotrac list " + what + "s lists them"));
		}
		if (choices.empty()) throw stop(not_found, "there are no " + what + "s here yet");
		if (!asking(a)) {
			std::vector<std::string> names;
			for (const auto& c : choices) names.push_back(c.first);
			throw stop(usage, "say which " + what + ": " + joined(names));
		}
		std::vector<std::string> rows;
		std::size_t at = 0;
		for (std::size_t i = 0; i < choices.size(); ++i) {
			rows.push_back(choices[i].second);
			if (choices[i].first == current) at = i;
		}
		auto picked = terminal::pick("Pick a " + what, rows, at);
		if (!picked) throw stop(other, "");
		return choices[*picked].first;
	}

	projects::project_page project_of(session& s) {
		auto page = s.app->project_page(s.project).get();
		if (!page.exists) throw stop(not_found, "there's no project " + s.project + " you may read");
		// Its private boards, which its public page leaves out, for its people.
		auto hidden = s.app->private_boards(s.project).get();
		for (const auto& b : hidden.boards) page.boards.push_back({.id = b.id, .name = b.name, .title = b.title});
		return page;
	}

	std::string padded(std::string s, std::size_t width) {
		if (s.size() > width) return s.substr(0, width - 1) + "…";
		return s + std::string(width - s.size(), ' ');
	}

	std::string date_of(const neotrac::time& t) {
		if (t.time_since_epoch().count() == 0) return "";
		return std::format("{:%Y-%m-%d}", std::chrono::floor<std::chrono::days>(t));
	}

	neotrac::time date_from(const std::string& text) {
		int y = 0, m = 0, d = 0;
		if (std::sscanf(text.c_str(), "%d-%d-%d", &y, &m, &d) != 3) throw stop(usage, text + " isn't a date like 2026-11-30");
		std::chrono::year_month_day day{std::chrono::year{y}, std::chrono::month{static_cast<unsigned>(m)}, std::chrono::day{static_cast<unsigned>(d)}};
		if (!day.ok()) throw stop(usage, text + " isn't a date like 2026-11-30");
		return neotrac::time{std::chrono::sys_days{day}};
	}

	std::vector<std::string> labels_from(const std::string& text) {
		std::vector<std::string> out;
		std::stringstream in(text);
		std::string label;
		while (std::getline(in, label, ',')) {
			label = terminal::trimmed(label);
			if (!label.empty()) out.push_back(label);
		}
		return out;
	}

	// The text a flag gives, or stdin's for --body -.
	std::string text_flag(const asked& a, const std::string& name) {
		std::string value = a.flag(name);
		if (value != "-") return value;
		std::stringstream in;
		in << std::cin.rdbuf();
		return terminal::trimmed(in.str());
	}

	// A thing written: in the person's editor, from what it holds now, or from flags,
	// each a field by its name in lower case, like --title, and --body for its text.
	terminal::document written_by(const asked& a, const terminal::document& now, const std::string& help) {
		// A field's flag is its name in lower case, a space a dash: Worked by is --worked-by.
		auto flag_of = [](std::string name) {
			std::ranges::replace(name, ' ', '-');
			return lower(name);
		};
		bool flagged = a.has("body") || std::ranges::any_of(now.fields, [&](const auto& f) { return a.has(flag_of(f.first)); });
		if (flagged || !asking(a)) {
			terminal::document out = now;
			for (auto& [name, value] : out.fields) {
				if (a.has(flag_of(name))) value = a.flag(flag_of(name));
			}
			if (a.has("body")) out.text = text_flag(a, "body");
			return out;
		}
		std::string before = terminal::written(now, help);
		std::string saved = terminal::edit(before);
		if (saved == before) throw stop(other, "nothing changed, so nothing was saved");
		try {
			return terminal::read(saved, now);
		} catch (const std::runtime_error& e) {
			throw stop(usage, e.what());
		}
	}

	// ---- Projects ---------------------------------------------------------------------

	std::vector<std::pair<std::string, std::string>> project_choices(const session& s) {
		std::vector<std::pair<std::string, std::string>> out;
		for (const auto& project : projects_in(s.mine)) {
			std::string name, roles;
			for (const auto& row : s.mine.rows) {
				if (row.project != project) continue;
				name = row.project_name;
				roles += (roles.empty() ? "" : ", ") + row.role_title;
			}
			out.emplace_back(project, padded(project, 16) + " " + padded(name, 28) + " " + roles);
		}
		return out;
	}

	int list_projects(const asked& a, session& s) {
		if (a.has("json")) {
			std::string out = "[";
			for (std::size_t i = 0; i < s.mine.rows.size(); ++i) {
				const auto& r = s.mine.rows[i];
				out += (i ? "," : "") + std::string("{\"project\":") + json(r.project) + ",\"name\":" + json(r.project_name) + ",\"role\":" + json(r.role_title) +
				       ",\"selected\":" + json(r.project == s.project) + "}";
			}
			std::puts((out + "]").c_str());
			return done;
		}
		if (s.mine.rows.empty()) std::puts("You aren't in any project yet.");
		for (const auto& [project, reads] : project_choices(s)) std::printf("%s %s\n", project == s.project ? "*" : " ", reads.c_str());
		if (a.has("all")) {
			auto all = s.app->all().get();
			std::puts("\nEvery public project:");
			for (const auto& p : all.rows) std::printf("  %s %s\n", padded(p.slug, 16).c_str(), p.name.c_str());
		}
		return done;
	}

	int show_project(const asked& a, session& s) {
		if (a.words.size() > 1 || s.project.empty()) s.project = chosen(a, 1, "project", project_choices(s), s.project);
		auto page = project_of(s);
		if (a.has("json")) {
			std::string boards = "[", members = "[";
			for (std::size_t i = 0; i < page.boards.size(); ++i) boards += (i ? "," : "") + json(page.boards[i].name);
			for (std::size_t i = 0; i < page.members.size(); ++i) {
				members += (i ? "," : "") + std::string("{\"name\":") + json(page.members[i].person_name) + ",\"role\":" + json(page.members[i].role_title) + "}";
			}
			std::printf("{\"project\":%s,\"name\":%s,\"summary\":%s,\"visibility\":%s,\"boards\":%s],\"issues\":%zu,\"milestones\":%zu,\"pages\":%zu,\"people\":%s]}\n",
			            json(s.project).c_str(), json(page.name).c_str(), json(page.summary).c_str(), json(std::string(projects::to_string(page.visibility))).c_str(),
			            boards.c_str(), page.issues.size(), page.milestones.size(), page.pages.size(), members.c_str());
			return done;
		}
		std::printf("%s (%s), %s\n", page.name.c_str(), s.project.c_str(), projects::to_string(page.visibility));
		if (!page.summary.empty()) std::printf("\n%s\n", page.summary.c_str());
		std::printf("\nBoards:");
		for (const auto& b : page.boards) std::printf(" %s", b.name.c_str());
		std::printf("\n%zu issues, %zu milestones, %zu wiki pages\nPeople:", page.issues.size(), page.milestones.size(), page.pages.size());
		for (std::size_t i = 0; i < page.members.size(); ++i) {
			std::printf("%s %s (%s)", i ? "," : "", page.members[i].person_name.c_str(), page.members[i].role_title.c_str());
		}
		std::printf("\n");
		return done;
	}

	int new_project(const asked& a, session& s) {
		terminal::document now{{{"Slug", ""}, {"Name", ""}, {"Visibility", "public"}, {"Workflow", "basic"}}, ""};
		auto d = written_by(a, now,
		                    "Slug is its address, like neotrac. Visibility is public or private. Workflow is basic, software, editorial, bugs, adr "
		                    "or custom. The text is its summary.");
		projects::project_create sent;
		sent.slug = d.get("Slug");
		sent.name = d.get("Name").empty() ? d.get("Slug") : d.get("Name");
		if (!d.text.empty()) sent.summary = d.text;
		projects::project_visibility visibility{};
		if (!projects::from_string(lower(d.get("Visibility")), visibility)) throw stop(usage, "visibility is public or private");
		sent.visibility = visibility;
		// Each preset by its name, or its title's first word, like software for Software
		// development, which is stored as agile.
		std::string preset = lower(d.get("Workflow"));
		if (preset.starts_with("software")) preset = "agile";
		projects::workflow workflow{};
		if (!projects::from_string(preset, workflow)) throw stop(usage, "the workflow is basic, software, editorial, bugs, adr or custom");
		sent.workflow = workflow;
		auto made = s.app->project_create(sent);
		if (a.has("json")) std::printf("{\"project\":%s}\n", json(made).c_str());
		else std::printf("Made %s. neotrac select project %s works in it.\n", made.c_str(), made.c_str());
		return done;
	}

	int edit_project(const asked& a, session& s) {
		if (a.words.size() > 1 || s.project.empty()) s.project = chosen(a, 1, "project", project_choices(s), s.project);
		auto page = project_of(s);
		terminal::document now{{{"Name", page.name}, {"Visibility", projects::to_string(page.visibility)}}, page.summary};
		auto d = written_by(a, now, "Visibility is public or private. The text is its summary.");
		projects::project_update sent{.id = s.project};
		sent.name = d.get("Name");
		sent.summary = d.text;
		projects::project_visibility visibility{};
		if (!projects::from_string(lower(d.get("Visibility")), visibility)) throw stop(usage, "visibility is public or private");
		sent.visibility = visibility;
		s.app->project_update(sent);
		if (a.has("json")) std::printf("{\"project\":%s}\n", json(s.project).c_str());
		else std::printf("Saved %s.\n", s.project.c_str());
		return done;
	}

	// A board, with its issues, or a milestone, its issues staying, once its title is
	// typed, or given with --confirm.
	int delete_part(const asked& a, const std::string& noun) {
		auto s = sign_in(a);
		auto project = project_of(s);
		if (a.words.size() < 2) throw stop(usage, "say which, like neotrac delete " + noun + " " + (noun == "board" ? "main" : "beta"));
		std::string wanted = lower(a.words[1]), id, title;
		auto pick = [&](const auto& rows) {
			for (const auto& r : rows) {
				if (lower(r.name) == wanted || r.id == a.words[1]) id = r.id, title = r.title;
			}
			for (const auto& r : rows) {
				if (id.empty() && lower(r.title) == wanted) id = r.id, title = r.title;
			}
		};
		if (noun == "board") pick(project.boards);
		else pick(project.milestones);
		if (id.empty()) throw stop(not_found, "there's no " + noun + " " + a.words[1] + " in " + s.project + "; neotrac list " + noun + "s lists them");
		std::string typed = a.flag("confirm");
		if (!a.has("confirm")) {
			if (!asking(a)) throw stop(usage, "say --confirm \"" + title + "\" to delete it");
			std::fprintf(stderr, noun == "board" ? "Deleting %s deletes its issues with it. It can't be undone.\n" : "Deleting %s leaves its issues in no milestone.\n", title.c_str());
			typed = terminal::ask("Type its title, " + title + ", to delete it: ");
		}
		if (noun == "board") {
			projects::board_delete sent{.id = id};
			sent.typed = typed;
			s.app->board_delete(sent);
		} else {
			projects::milestone_delete sent{.id = id};
			sent.typed = typed;
			s.app->milestone_delete(sent);
		}
		if (a.has("json")) std::printf("{\"deleted\":%s}\n", json(id).c_str());
		else std::printf("Deleted %s.\n", title.c_str());
		return done;
	}

	int delete_project(const asked& a, session& s) {
		std::string project = chosen(a, 1, "project", project_choices(s), s.project);
		std::string typed = a.flag("confirm");
		if (!a.has("confirm")) {
			if (!asking(a)) throw stop(usage, "deleting a project can't be undone; say --confirm " + project + " to mean it");
			std::fprintf(stderr, "Deleting %s deletes its issues, boards, milestones, wiki and reports, and takes away everyone's place in it.\n"
			             "It can't be undone.\n", project.c_str());
			typed = terminal::ask("Type its address, " + project + ", to delete it: ");
		}
		projects::project_delete sent{.id = project};
		sent.address = typed;
		s.app->project_delete(sent);
		if (s.credentials.project == project) {
			s.credentials.project.clear();
			s.credentials.board.clear();
			write_credentials(s.credentials);
		}
		if (a.has("json")) std::printf("{\"deleted\":%s}\n", json(project).c_str());
		else std::printf("Deleted %s.\n", project.c_str());
		return done;
	}

	// ---- Boards and stages -------------------------------------------------------------

	std::vector<std::pair<std::string, std::string>> board_choices(const projects::project_page& page) {
		std::vector<std::pair<std::string, std::string>> out;
		for (const auto& b : page.boards) out.emplace_back(b.name, padded(b.name, 20) + " " + b.title);
		return out;
	}

	// The board a command works on: named, selected, the project's only one, or picked.
	std::string board_of(const asked& a, session& s, const projects::project_page& page) {
		if (!s.board.empty()) {
			for (const auto& b : page.boards) {
				if (b.name == s.board || b.id == s.board) return b.id;
			}
			throw stop(not_found, "there's no board " + s.board + " in " + s.project);
		}
		if (page.boards.size() == 1) return page.boards.front().id;
		asked none = a;
		none.words.clear();
		return projects::board_id(s.project, chosen(none, 0, "board", board_choices(page)));
	}

	int list_boards(const asked& a, session& s) {
		auto page = project_of(s);
		if (a.has("json")) {
			std::string out = "[";
			for (std::size_t i = 0; i < page.boards.size(); ++i) {
				out += (i ? "," : "") + std::string("{\"board\":") + json(page.boards[i].name) + ",\"title\":" + json(page.boards[i].title) +
				       ",\"selected\":" + json(page.boards[i].name == s.board) + "}";
			}
			std::puts((out + "]").c_str());
			return done;
		}
		for (const auto& [name, reads] : board_choices(page)) std::printf("%s %s\n", name == s.board ? "*" : " ", reads.c_str());
		return done;
	}

	int show_board(const asked& a, session& s) {
		auto project = project_of(s);
		std::string board = a.words.size() > 1 || s.board.empty() ? projects::board_id(s.project, chosen(a, 1, "board", board_choices(project), s.board))
		                                                          : board_of(a, s, project);
		auto page = s.app->board_page(board).get();
		if (!page.exists) throw stop(not_found, "there's no board " + board + " you may read");
		std::map<std::string, int> counted;
		for (const auto& i : page.issues) ++counted[i.stage];
		if (a.has("json")) {
			std::string out = "{\"board\":" + json(board) + ",\"title\":" + json(page.title) + ",\"stages\":[";
			for (std::size_t i = 0; i < page.stages.size(); ++i) {
				out += (i ? "," : "") + std::string("{\"stage\":") + json(page.stages[i].name) + ",\"title\":" + json(page.stages[i].title) +
				       ",\"issues\":" + std::to_string(counted[page.stages[i].id]) + ",\"worked_by\":" + json(page.stages[i].worked_by_title) + "}";
			}
			std::puts((out + "]}").c_str());
			return done;
		}
		std::printf("%s, in %s\n\n", page.title.c_str(), page.project_name.c_str());
		for (const auto& p : page.stages) {
			std::printf("  %s %4d issue%s", padded(p.title, 24).c_str(), counted[p.id], counted[p.id] == 1 ? " " : "s");
			if (!p.worked_by_title.empty()) std::printf("  worked by %s", joined(p.worked_by_title).c_str());
			std::printf("\n");
		}
		return done;
	}

	int list_stages(const asked& a, session& s) {
		auto project = project_of(s);
		auto page = s.app->board_page(board_of(a, s, project)).get();
		if (a.has("json")) {
			std::string out = "[";
			for (std::size_t i = 0; i < page.stages.size(); ++i) {
				out += (i ? "," : "") + std::string("{\"stage\":") + json(page.stages[i].name) + ",\"title\":" + json(page.stages[i].title) + "}";
			}
			std::puts((out + "]").c_str());
			return done;
		}
		for (const auto& p : page.stages) std::printf("  %s %s\n", padded(p.name, 20).c_str(), p.title.c_str());
		return done;
	}

	int list_people(const asked& a, session& s) {
		auto page = project_of(s);
		if (a.has("json")) {
			std::string out = "[";
			for (std::size_t i = 0; i < page.members.size(); ++i) {
				out += (i ? "," : "") + std::string("{\"name\":") + json(page.members[i].person_name) + ",\"role\":" + json(page.members[i].role_title) + "}";
			}
			std::puts((out + "]").c_str());
			return done;
		}
		for (const auto& m : page.members) std::printf("  %s %s\n", padded(m.person_name, 28).c_str(), m.role_title.c_str());
		return done;
	}

	// ---- Selecting ---------------------------------------------------------------------

	int select(const asked& a) {
		if (a.words.empty()) throw stop(usage, "say what to select: neotrac select project, or neotrac select board");
		std::string noun = noun_of(a.words[0]);
		auto s = sign_in(a, noun != "project");
		if (s.who.service) throw stop(usage, "a service works only in its own project, " + s.project);
		if (noun == "project") {
			std::string project = chosen(a, 1, "project", project_choices(s), s.project);
			if (project != s.credentials.project) s.credentials.board.clear();
			s.credentials.project = project;
			write_credentials(s.credentials);
			if (a.has("json")) std::printf("{\"project\":%s}\n", json(project).c_str());
			else std::printf("Selected %s. Commands work in it until neotrac unselect project.\n", project.c_str());
			return done;
		}
		if (noun == "board") {
			auto page = project_of(s);
			std::string board = chosen(a, 1, "board", board_choices(page), s.board);
			s.credentials.project = s.project;
			s.credentials.board = board;
			write_credentials(s.credentials);
			if (a.has("json")) std::printf("{\"project\":%s,\"board\":%s}\n", json(s.project).c_str(), json(board).c_str());
			else std::printf("Selected %s in %s, until neotrac unselect board.\n", board.c_str(), s.project.c_str());
			return done;
		}
		throw stop(usage, "a project or a board is selected, like neotrac select project");
	}

	int unselect(const asked& a) {
		std::string noun = a.words.empty() ? "project" : noun_of(a.words[0]);
		auto k = read_credentials();
		if (noun == "project") {
			k.project.clear();
			k.board.clear();
		} else if (noun == "board") {
			k.board.clear();
		} else {
			throw stop(usage, "a project or a board is unselected, like neotrac unselect project");
		}
		write_credentials(k);
		if (!a.has("json")) std::printf("Unselected the %s.\n", noun.c_str());
		return done;
	}

	// ---- Issues ------------------------------------------------------------------------

	int list_issues(const asked& a, session& s) {
		auto project = project_of(s);
		struct row {
			double number;
			std::string title, stage;
		};
		std::vector<row> rows;
		if (!s.board.empty()) {
			auto page = s.app->board_page(board_of(a, s, project)).get();
			for (const auto& i : page.issues) rows.push_back({i.number, i.title, i.stage_title});
		} else {
			for (const auto& i : project.issues) rows.push_back({i.number, i.title, i.stage_title});
		}
		std::ranges::sort(rows, [](const row& x, const row& y) { return x.number > y.number; });
		if (a.has("json")) {
			std::string out = "[";
			for (std::size_t i = 0; i < rows.size(); ++i) {
				out += (i ? "," : "") + std::string("{\"number\":") + json(rows[i].number) + ",\"title\":" + json(rows[i].title) + ",\"stage\":" + json(rows[i].stage) + "}";
			}
			std::puts((out + "]").c_str());
			return done;
		}
		if (rows.empty()) std::puts("No issues yet.");
		for (const auto& r : rows) std::printf("#%-5.0f %s %s\n", r.number, padded(r.stage, 14).c_str(), r.title.c_str());
		return done;
	}

	// An issue named by its number, or picked from the project's at the terminal.
	std::string issue_word(const asked& a, session& s, std::size_t word) {
		if (a.words.size() > word) return a.words[word];
		auto project = project_of(s);
		std::vector<std::pair<std::string, std::string>> choices;
		std::vector<const projects::project_page::issues_row*> sorted;
		for (const auto& i : project.issues) {
			if (s.board.empty() || i.board == projects::board_id(s.project, s.board)) sorted.push_back(&i);
		}
		std::ranges::sort(sorted, [](const auto* x, const auto* y) { return x->number > y->number; });
		for (const auto* i : sorted) {
			std::string number = std::format("{:.0f}", i->number);
			choices.emplace_back(number, "#" + padded(number, 5) + " " + padded(i->stage_title, 14) + " " + i->title);
		}
		return chosen(a, word, "issue", choices);
	}

	// A milestone by its name or title, as its id; empty for none.
	std::string milestone_named(const session& s, const projects::project_page& project, const std::string& word) {
		if (terminal::trimmed(word).empty()) return "";
		for (const auto& m : project.milestones) {
			if (lower(m.name) == lower(word) || lower(m.title) == lower(word)) return m.id;
		}
		throw stop(not_found, "there's no milestone " + word + " in " + s.project + "; neotrac list milestones lists them");
	}

	std::string milestone_name(const projects::project_page& project, const std::string& id) {
		for (const auto& m : project.milestones) {
			if (m.id == id) return m.name;
		}
		return "";
	}

	const char* issue_help = "Priority is critical, high, normal, low or trivial; labels are split by commas; the milestone is its name, or "
	                         "empty. The text is its description.";

	int new_issue(const asked& a, session& s) {
		auto project = project_of(s);
		std::string board = board_of(a, s, project);
		terminal::document now{{{"Title", ""}, {"Priority", "normal"}, {"Labels", ""}, {"Milestone", ""}}, ""};
		auto d = written_by(a, now, issue_help);
		if (d.get("Title").empty()) throw stop(usage, "an issue needs a title");
		projects::issue_create sent;
		sent.project = s.project;
		sent.board = board;
		sent.title = d.get("Title");
		if (!d.text.empty()) sent.body = d.text;
		projects::priority priority{};
		if (!projects::from_string(lower(d.get("Priority")), priority)) throw stop(usage, "priority is critical, high, normal, low or trivial");
		sent.priority = priority;
		if (auto labels = labels_from(d.get("Labels")); !labels.empty()) sent.labels = labels;
		if (auto milestone = milestone_named(s, project, d.get("Milestone")); !milestone.empty()) sent.milestone = milestone;
		auto made = s.app->issue_create(sent);
		if (a.has("json")) std::printf("{\"id\":%s}\n", json(made).c_str());
		else std::printf("Opened %s.\n", made.c_str());
		return done;
	}

	int edit_issue(const asked& a, session& s) {
		std::string word = issue_word(a, s, 1);
		std::string id = projects::issue_id(s.project, issue_number(word));
		auto page = read_issue(s, id, word);
		auto project = project_of(s);
		std::string labels;
		for (const auto& l : page.labels) labels += (labels.empty() ? "" : ", ") + l;
		terminal::document now{
		    {{"Title", page.title}, {"Priority", projects::to_string(page.priority)}, {"Labels", labels}, {"Milestone", milestone_name(project, page.milestone)}},
		    page.body};
		auto d = written_by(a, now, issue_help);
		if (d.get("Title").empty()) throw stop(usage, "an issue needs a title");
		projects::issue_update sent{.id = id};
		sent.title = d.get("Title");
		sent.body = d.text;
		projects::priority priority{};
		if (!projects::from_string(lower(d.get("Priority")), priority)) throw stop(usage, "priority is critical, high, normal, low or trivial");
		sent.priority = priority;
		sent.labels = labels_from(d.get("Labels"));
		sent.milestone = milestone_named(s, project, d.get("Milestone"));
		s.app->issue_update(sent);
		if (a.has("json")) std::printf("{\"id\":%s}\n", json(id).c_str());
		else std::printf("Saved #%s.\n", word.c_str());
		return done;
	}

	// ---- Wiki pages --------------------------------------------------------------------

	std::vector<std::pair<std::string, std::string>> page_choices(const projects::project_page& project) {
		std::vector<std::pair<std::string, std::string>> out;
		for (const auto& p : project.pages) out.emplace_back(p.name, padded(p.name, 24) + " " + p.title);
		return out;
	}

	int list_pages(const asked& a, session& s) {
		auto project = project_of(s);
		if (a.has("json")) {
			std::string out = "[";
			for (std::size_t i = 0; i < project.pages.size(); ++i) {
				out += (i ? "," : "") + std::string("{\"page\":") + json(project.pages[i].name) + ",\"title\":" + json(project.pages[i].title) + "}";
			}
			std::puts((out + "]").c_str());
			return done;
		}
		if (project.pages.empty()) std::puts("No wiki pages yet.");
		for (const auto& [name, reads] : page_choices(project)) std::printf("  %s\n", reads.c_str());
		return done;
	}

	int show_page(const asked& a, session& s) {
		std::string name = chosen(a, 1, "page", page_choices(project_of(s)));
		auto page = s.app->wiki_page(projects::page_id(s.project, name)).get();
		if (!page.exists) throw stop(not_found, "there's no page " + name + " you may read");
		if (a.has("json")) std::printf("{\"page\":%s,\"title\":%s,\"body\":%s}\n", json(name).c_str(), json(page.title).c_str(), json(page.body).c_str());
		else std::printf("%s\n\n%s\n", page.title.c_str(), page.body.c_str());
		return done;
	}

	int new_page(const asked& a, session& s) {
		terminal::document now{{{"Title", ""}}, ""};
		auto d = written_by(a, now, "The text is the page, in markdown.");
		if (d.get("Title").empty()) throw stop(usage, "a page needs a title");
		projects::page_create sent;
		sent.project = s.project;
		sent.title = d.get("Title");
		sent.body = d.text;
		auto made = s.app->page_create(sent);
		if (a.has("json")) std::printf("{\"id\":%s}\n", json(made).c_str());
		else std::printf("Wrote %s.\n", d.get("Title").c_str());
		return done;
	}

	int edit_page(const asked& a, session& s) {
		std::string name = chosen(a, 1, "page", page_choices(project_of(s)));
		std::string id = projects::page_id(s.project, name);
		auto page = s.app->wiki_page(id).get();
		if (!page.exists) throw stop(not_found, "there's no page " + name + " you may read");
		terminal::document now{{{"Title", page.title}}, page.body};
		auto d = written_by(a, now, "The text is the page, in markdown.");
		if (d.get("Title").empty()) throw stop(usage, "a page needs a title");
		projects::page_update sent{.id = id};
		sent.title = d.get("Title");
		sent.body = d.text;
		s.app->page_update(sent);
		if (a.has("json")) std::printf("{\"id\":%s}\n", json(id).c_str());
		else std::printf("Saved %s.\n", d.get("Title").c_str());
		return done;
	}

	// ---- Milestones --------------------------------------------------------------------

	std::vector<std::pair<std::string, std::string>> milestone_choices(const projects::project_page& project) {
		std::vector<std::pair<std::string, std::string>> out;
		for (const auto& m : project.milestones) out.emplace_back(m.name, padded(m.name, 20) + " " + padded(date_of(m.due), 11) + " " + m.title);
		return out;
	}

	int list_milestones(const asked& a, session& s) {
		auto project = project_of(s);
		if (a.has("json")) {
			std::string out = "[";
			for (std::size_t i = 0; i < project.milestones.size(); ++i) {
				const auto& m = project.milestones[i];
				out += (i ? "," : "") + std::string("{\"milestone\":") + json(m.name) + ",\"title\":" + json(m.title) + ",\"due\":" + json(date_of(m.due)) + "}";
			}
			std::puts((out + "]").c_str());
			return done;
		}
		if (project.milestones.empty()) std::puts("No milestones yet.");
		for (const auto& [name, reads] : milestone_choices(project)) std::printf("  %s\n", reads.c_str());
		return done;
	}

	int show_milestone(const asked& a, session& s) {
		std::string name = chosen(a, 1, "milestone", milestone_choices(project_of(s)));
		auto page = s.app->milestone_page(projects::milestone_id(s.project, name)).get();
		if (!page.exists) throw stop(not_found, "there's no milestone " + name + " you may read");
		if (a.has("json")) {
			std::string issues = "[";
			for (std::size_t i = 0; i < page.issues.size(); ++i) {
				issues += (i ? "," : "") + std::string("{\"number\":") + json(page.issues[i].number) + ",\"title\":" + json(page.issues[i].title) + "}";
			}
			std::printf("{\"milestone\":%s,\"title\":%s,\"due\":%s,\"description\":%s,\"issues\":%s]}\n", json(name).c_str(), json(page.title).c_str(),
			            json(date_of(page.due)).c_str(), json(page.description).c_str(), issues.c_str());
			return done;
		}
		std::printf("%s%s\n", page.title.c_str(), page.due.time_since_epoch().count() ? (", due " + date_of(page.due)).c_str() : "");
		if (!page.description.empty()) std::printf("\n%s\n", page.description.c_str());
		if (!page.issues.empty()) std::printf("\n");
		for (const auto& i : page.issues) std::printf("#%-5.0f %s %s\n", i.number, padded(i.stage_title, 14).c_str(), i.title.c_str());
		return done;
	}

	int write_milestone(const asked& a, session& s, bool making) {
		std::string name, id;
		terminal::document now{{{"Title", ""}, {"Due", ""}}, ""};
		if (!making) {
			name = chosen(a, 1, "milestone", milestone_choices(project_of(s)));
			id = projects::milestone_id(s.project, name);
			auto page = s.app->milestone_page(id).get();
			if (!page.exists) throw stop(not_found, "there's no milestone " + name + " you may read");
			now = {{{"Title", page.title}, {"Due", date_of(page.due)}}, page.description};
		}
		auto d = written_by(a, now, "Due is a date like 2026-11-30, or empty. The text is its description.");
		if (d.get("Title").empty()) throw stop(usage, "a milestone needs a title");
		std::optional<neotrac::time> due;
		if (!d.get("Due").empty()) due = date_from(d.get("Due"));
		if (making) {
			projects::milestone_create sent;
			sent.project = s.project;
			sent.title = d.get("Title");
			sent.due = due;
			if (!d.text.empty()) sent.description = d.text;
			id = s.app->milestone_create(sent);
		} else {
			projects::milestone_update sent{.id = id};
			sent.title = d.get("Title");
			sent.due = due;
			sent.description = d.text;
			s.app->milestone_update(sent);
		}
		if (a.has("json")) std::printf("{\"id\":%s}\n", json(id).c_str());
		else std::printf("%s %s.\n", making ? "Added" : "Saved", d.get("Title").c_str());
		return done;
	}

	// ---- Boards and their stages ----------------------------------------------------------

	int new_board(const asked& a, session& s) {
		terminal::document now{{{"Title", ""}, {"Visibility", "public"}, {"Workflow", "basic"}}, ""};
		auto d = written_by(a, now,
		                    "Visibility is public, or private for the project's people alone. Workflow is basic, software, editorial, bugs, "
		                    "adr or custom.");
		if (d.get("Title").empty()) throw stop(usage, "a board needs a title");
		projects::board_create sent;
		sent.project = s.project;
		sent.title = d.get("Title");
		projects::board_visibility visibility{};
		if (!projects::from_string(lower(d.get("Visibility")), visibility)) throw stop(usage, "visibility is public or private");
		sent.visibility = visibility;
		std::string preset = lower(d.get("Workflow"));
		if (preset.starts_with("software")) preset = "agile";
		projects::workflow workflow{};
		if (!projects::from_string(preset, workflow)) throw stop(usage, "the workflow is basic, software, editorial, bugs, adr or custom");
		sent.workflow = workflow;
		auto made = s.app->board_create(sent);
		auto page = s.app->board_page(made).get();
		std::string name = page.exists ? made.substr(s.project.size() + 1) : made;
		if (a.has("json")) std::printf("{\"board\":%s}\n", json(name).c_str());
		else std::printf("Made %s. neotrac select board %s works in it.\n", sent.title->c_str(), name.c_str());
		return done;
	}

	// The selected board's page, and a stage of it by its name, title or id.
	projects::board_page board_page_of(const asked& a, session& s, std::string& id) {
		id = board_of(a, s, project_of(s));
		auto page = s.app->board_page(id).get();
		if (!page.exists) throw stop(not_found, "there's no board " + id + " you may read");
		return page;
	}

	const projects::board_page::stages_row& stage_on(const projects::board_page& page, const std::string& word) {
		for (const auto& p : page.stages) {
			if (p.name == word || lower(p.title) == lower(word) || p.id == word) return p;
		}
		std::string names;
		for (const auto& p : page.stages) names += (names.empty() ? "" : ", ") + p.name;
		throw stop(not_found, "there's no stage " + word + " on " + page.title + "; its stages are " + names);
	}

	const char* stage_help = "Status is its cards' dot: inactive, waiting, working, done or failed. Notify is chime, or nothing; email and text are "
	                         "to come. Worked by names the roles whose services take its issues, like agent.";

	// A stage's fields as written: its status, and how it tells the board's watchers.
	void stage_fields(const session& s, const terminal::document& d, std::optional<projects::status>& status, std::optional<std::vector<std::string>>& notify,
	                  std::optional<std::vector<std::string>>& worked_by) {
		worked_by.emplace();
		for (const auto& role : labels_from(lower(d.get("Worked by")))) worked_by->push_back(projects::role_id(s.project, role));
		projects::status said{};
		if (!projects::from_string(lower(d.get("Status")), said)) throw stop(usage, "status is inactive, waiting, working, done or failed");
		status = said;
		notify = labels_from(lower(d.get("Notify")));
		for (const auto& n : *notify) {
			if (n != "chime" && n != "email" && n != "text") throw stop(usage, n + " isn't how a stage tells: chime, email or text");
		}
	}

	int new_stage(const asked& a, session& s) {
		std::string board;
		auto page = board_page_of(a, s, board);
		terminal::document now{{{"Title", ""}, {"Status", "inactive"}, {"Notify", ""}, {"Worked by", ""}}, ""};
		auto d = written_by(a, now, stage_help);
		if (d.get("Title").empty()) throw stop(usage, "a stage needs a title");
		projects::stage_create sent;
		sent.board = board;
		sent.title = d.get("Title");
		stage_fields(s, d, sent.status, sent.notify, sent.worked_by);
		// Where it goes among the others, like 2.5 between the second and third; after them unless said.
		if (a.has("position")) {
			char* end = nullptr;
			std::string said = a.flag("position");
			double position = std::strtod(said.c_str(), &end);
			if (said.empty() || *end != '\0') throw stop(usage, said + " isn't a position, like 2.5");
			sent.position = position;
		}
		s.app->stage_create(sent);
		if (a.has("json")) std::printf("{\"board\":%s,\"title\":%s}\n", json(board).c_str(), json(*sent.title).c_str());
		else std::printf("Added %s to %s.\n", sent.title->c_str(), page.title.c_str());
		return done;
	}

	int edit_stage(const asked& a, session& s) {
		if (a.words.size() < 2) throw stop(usage, "say which stage, like neotrac edit stage review --status waiting --notify chime");
		std::string board;
		auto page = board_page_of(a, s, board);
		const auto& stage = stage_on(page, a.words[1]);
		std::string notify;
		for (const auto& n : stage.notify) notify += (notify.empty() ? "" : ", ") + n;
		std::string status = projects::to_string(stage.status);
		std::string workers;
		for (const auto& r : stage.worked_by) workers += (workers.empty() ? "" : ", ") + r.substr(s.project.size() + 1);
		terminal::document now{{{"Title", stage.title}, {"Status", status.empty() ? "inactive" : status}, {"Notify", notify}, {"Worked by", workers}}, ""};
		auto d = written_by(a, now, stage_help);
		projects::stage_update sent{.id = stage.id};
		sent.title = d.get("Title");
		stage_fields(s, d, sent.status, sent.notify, sent.worked_by);
		s.app->stage_update(sent);
		if (a.has("json")) std::printf("{\"stage\":%s}\n", json(stage.name).c_str());
		else std::printf("Saved %s.\n", d.get("Title").c_str());
		return done;
	}

	// A service in the project, with a role made for services, and its key, shown once.
	int new_service(const asked& a, session& s) {
		terminal::document now{{{"Title", ""}, {"Role", "agent"}}, ""};
		auto d = written_by(a, now, "Role is a role made for services, like agent. Its key is shown once.");
		if (d.get("Title").empty()) throw stop(usage, "a service needs a title");
		projects::service_create sent;
		sent.project = s.project;
		sent.title = d.get("Title");
		sent.role = projects::role_id(s.project, lower(d.get("Role")));
		auto id = s.app->service_create(sent);
		std::string key = text_of(s.app->said("projects::service::key", "{\"id\":" + json(id) + "}")["key"]);
		if (a.has("json")) std::printf("{\"service\":%s,\"key\":%s}\n", json(id).c_str(), json(key).c_str());
		else std::printf("Made %s. Its key, shown this once:\n%s\n", sent.title->c_str(), key.c_str());
		return done;
	}

	// A move between two stages, for whoever moves issues on the board.
	int new_step(const asked& a, session& s) {
		std::string board;
		auto page = board_page_of(a, s, board);
		std::string from = a.flag("from"), to = a.flag("to");
		if (from.empty() || to.empty()) throw stop(usage, "say between which stages, like neotrac new step --from review --to done");
		projects::step_create sent;
		sent.board = board;
		sent.from_ = stage_on(page, from).id;
		sent.to = stage_on(page, to).id;
		if (a.has("title")) sent.title = a.flag("title");
		// Whose move it is: roles by name, like agent; the project's people unless said.
		if (a.has("roles")) {
			sent.roles.emplace();
			for (const auto& role : labels_from(lower(a.flag("roles")))) sent.roles->push_back(projects::role_id(s.project, role));
		}
		s.app->step_create(sent);
		if (a.has("json")) std::printf("{\"from\":%s,\"to\":%s}\n", json(from).c_str(), json(to).c_str());
		else std::printf("Issues move from %s to %s.\n", stage_on(page, from).title.c_str(), stage_on(page, to).title.c_str());
		return done;
	}

	// ---- The verbs ---------------------------------------------------------------------

	int list(const asked& a) {
		std::string noun = a.words.empty() ? "" : noun_of(a.words[0]);
		if (noun.empty()) throw stop(usage, "say what to list: projects, boards, issues, pages, milestones, stages or people");
		auto s = sign_in(a, noun != "project");
		if (noun == "project") return list_projects(a, s);
		if (noun == "board") return list_boards(a, s);
		if (noun == "issue") return list_issues(a, s);
		if (noun == "page") return list_pages(a, s);
		if (noun == "milestone") return list_milestones(a, s);
		if (noun == "stage") return list_stages(a, s);
		return list_people(a, s);
	}

	int show_any(const asked& a) {
		// neotrac show 42 is an issue, as it always was.
		if (a.words.empty() || noun_of(a.words[0]).empty()) {
			if (a.words.empty() && asking(a)) {
				auto s = sign_in(a);
				asked picked = a;
				picked.words = {issue_word(a, s, 0)};
				return show(picked);
			}
			return show(a);
		}
		std::string noun = noun_of(a.words[0]);
		if (noun == "issue") {
			auto s = sign_in(a);
			asked picked = a;
			picked.words = {issue_word(a, s, 1)};
			return show(picked);
		}
		auto s = sign_in(a, noun != "project");
		if (noun == "project") return show_project(a, s);
		if (noun == "board") return show_board(a, s);
		if (noun == "page") return show_page(a, s);
		if (noun == "milestone") return show_milestone(a, s);
		throw stop(usage, "a project, board, issue, page or milestone is shown");
	}

	int make(const asked& a) {
		std::string noun = a.words.empty() ? "" : noun_of(a.words[0]);
		auto s = sign_in(a, noun != "project");
		if (noun == "project") return new_project(a, s);
		if (noun == "issue") return new_issue(a, s);
		if (noun == "page") return new_page(a, s);
		if (noun == "milestone") return write_milestone(a, s, true);
		if (noun == "board") return new_board(a, s);
		if (noun == "stage") return new_stage(a, s);
		if (noun == "step") return new_step(a, s);
		if (noun == "service") return new_service(a, s);
		throw stop(usage, "say what to make: neotrac new project, board, stage, step, issue, page or milestone");
	}

	int edit(const asked& a) {
		std::string noun = a.words.empty() ? "" : noun_of(a.words[0]);
		auto s = sign_in(a, noun != "project");
		if (noun == "project") return edit_project(a, s);
		if (noun == "issue") return edit_issue(a, s);
		if (noun == "page") return edit_page(a, s);
		if (noun == "milestone") return write_milestone(a, s, false);
		if (noun == "stage") return edit_stage(a, s);
		throw stop(usage, "say what to edit: neotrac edit project, stage, issue, page or milestone");
	}

	int remove(const asked& a) {
		std::string noun = a.words.empty() ? "" : noun_of(a.words[0]);
		if (noun == "milestone" || noun == "board") return delete_part(a, noun);
		if (noun != "project") throw stop(usage, "a project, a board or a milestone is deleted; an issue goes with its board or project");
		auto s = sign_in(a, false);
		if (s.who.service) throw stop(said_no, "a service doesn't delete projects");
		return delete_project(a, s);
	}

	// claim, release, move and comment take an issue's number, or issue and its number,
	// or, at the terminal, pick it.
	asked on_issue(const asked& a) {
		asked out = a;
		if (!out.words.empty() && noun_of(out.words[0]) == "issue") out.words.erase(out.words.begin());
		if (out.words.empty() && asking(a)) {
			auto s = sign_in(a);
			out.words.push_back(issue_word(out, s, 0));
		}
		return out;
	}

	// One issue bearing on another: neotrac link 12 blocked_by 9. neotrac keeps the
	// other side too, said from there. neotrac unlink 12 blocked_by 9 takes both away.
	int link(const asked& a, bool making) {
		auto s = sign_in(a);
		asked on = on_issue(a);
		std::string verb = making ? "link" : "unlink";
		if (on.words.size() < 3) throw stop(usage, "say how, like neotrac " + verb + " 12 blocks 13, or blocked_by, relates, duplicates, replaces");
		std::string how = lower(on.words[1]);
		std::ranges::replace(how, '-', '_');
		projects::relation relation{};
		if (!projects::from_string(how, relation)) {
			throw stop(usage, on.words[1] + " isn't how issues bear on each other: blocks, blocked_by, relates, duplicates, duplicated_by, replaces or replaced_by");
		}
		std::string from = projects::issue_id(s.project, issue_number(on.words[0]));
		std::string to = projects::issue_id(s.project, issue_number(on.words[2]));
		auto page = read_issue(s, from, on.words[0]);
		for (const auto& l : page.links) {
			if (l.to == to && l.relation == relation && !making) {
				projects::link_delete gone;
				gone.id = l.id;
				s.app->link_delete(gone);
				if (a.has("json")) std::printf("{\"id\":%s,\"deleted\":true}\n", json(l.id).c_str());
				else std::printf("#%s no longer %s #%s.\n", on.words[0].c_str(), projects::to_string(relation), on.words[2].c_str());
				return done;
			}
			if (l.to == to && l.relation == relation) {
				if (a.has("json")) std::printf("{\"id\":%s,\"made\":false}\n", json(l.id).c_str());
				else std::printf("#%s already %s #%s.\n", on.words[0].c_str(), projects::to_string(relation), on.words[2].c_str());
				return done;
			}
		}
		if (!making) {
			if (a.has("json")) std::printf("{\"deleted\":false}\n");
			else std::printf("#%s isn't %s #%s.\n", on.words[0].c_str(), projects::to_string(relation), on.words[2].c_str());
			return other;
		}
		projects::link_create sent;
		sent.project = s.project;
		sent.from_ = from;
		sent.to = to;
		sent.relation = relation;
		auto made = s.app->link_create(sent);
		if (a.has("json")) std::printf("{\"id\":%s,\"made\":true}\n", json(made).c_str());
		else std::printf("#%s %s #%s.\n", on.words[0].c_str(), projects::to_string(relation), on.words[2].c_str());
		return done;
	}

	int logout(const asked& a) {
		std::error_code ignored;
		std::filesystem::remove(credentials_path(), ignored);
		if (!a.has("json")) std::puts("Signed out.");
		return done;
	}

	const char* help = R"(neotrac: neotrac's projects, boards, issues and wiki on the command line.

Signing in
  neotrac login                  as yourself, in your browser
  neotrac login --key            as a service, with the key its project gave it
  neotrac whoami, neotrac logout

What you work in, kept until you change it
  neotrac select project [p]     pick the project commands work in
  neotrac select board [b]       and a board in it
  neotrac unselect project|board

Reading
  neotrac list projects|boards|issues|pages|milestones|stages|people
  neotrac show project|board|issue|page|milestone [which]
  neotrac show 42 [--history] [--watch]
  neotrac queue [--all] [--watch]  the issues in stages your roles work

Writing, in your editor ($VISUAL, $EDITOR or vi)
  neotrac new project|board|stage|issue|page|milestone
  neotrac edit project|stage|issue|page|milestone [which]
  neotrac new step --from a --to b   lets issues move from one stage to another,
                                 for --roles agent, or the project's people
  neotrac new stage --position 2.5   puts a stage between the second and third
  neotrac new service --title Runner --role agent   and its key, shown once
  neotrac comment 42             or -m "..." or the comment on stdin
  neotrac attach 42 log.txt ...  uploads files and attaches them to the issue
  neotrac download 42 [folder]   saves what's attached to it
  neotrac delete project [p]     asks you to type its address
  neotrac delete board|milestone b   asks you to type its title

Working an issue
  neotrac claim 42, neotrac release 42
  neotrac status 42 working      or waiting, done, failed, inactive; clear takes it
                                 out. [--note "..."] Its card shows it, and the time since
  neotrac work --run ./fix --done review [--failed backlog] [--once]
                                 takes each issue a role of yours works as it comes,
                                 runs the program on it, its attachments beside it,
                                 attaches what it writes to out/, and moves it on;
                                 a program that exits 75 isn't ready, and lets it wait
  neotrac move 42 review         where the workflow lets your role
  neotrac close 42 done          or wont_do, or duplicate 9, from any stage
  neotrac reopen 42              back on its board, in the stage it was in
  neotrac link 42 blocked_by 9   or blocks, relates, duplicates, replaces
  neotrac unlink 42 relates 9    takes a link away, both sides

At a terminal, leaving out which one picks it from a list (arrows or j/k, Enter).
A program names it instead, and gives what it writes as flags: --title, --body
(- for stdin), --priority, --labels, --milestone, --due, --name, --visibility, --slug,
--workflow, --status, --notify, and --confirm <address> to delete a project. --project
and --board work in another than the selected one, for one command.

Every command takes --json, and --server to use another neotrac. A service's key may
also come from NEOTRAC_KEY. Exit codes: 0 done, 1 left or nothing changed, 2 wrong
usage, 3 not signed in or refused, 4 not found, 5 the workflow said no, 6 no
connection, 7 too many requests.
)";

	int run(const asked& a) {
		if (a.has("version")) {
			std::printf("neotrac %s\n", version);
			return done;
		}
		if (a.command.empty() || a.command == "help" || a.has("help")) {
			std::fputs(help, a.command.empty() && !a.has("help") ? stderr : stdout);
			return a.command.empty() && !a.has("help") ? usage : done;
		}
		if (a.command == "login") return login(a);
		if (a.command == "logout") return logout(a);
		if (a.command == "whoami") return whoami(a);
		if (a.command == "select") return select(a);
		if (a.command == "unselect") return unselect(a);
		if (a.command == "list") return list(a);
		if (a.command == "queue") return queue(a);
		if (a.command == "show") return show_any(a);
		if (a.command == "new") return make(a);
		if (a.command == "edit") return edit(a);
		if (a.command == "delete") return remove(a);
		if (a.command == "claim") return claim(on_issue(a), true);
		if (a.command == "release") return claim(on_issue(a), false);
		if (a.command == "move") return move(on_issue(a));
		if (a.command == "status") return status(on_issue(a));
		if (a.command == "close") return close(on_issue(a));
		if (a.command == "reopen") return reopen(on_issue(a));
		if (a.command == "comment") return comment(on_issue(a));
		if (a.command == "work") return work(a);
		if (a.command == "attach") return attach(on_issue(a));
		if (a.command == "download") return download(on_issue(a));
		if (a.command == "link") return link(a, true);
		if (a.command == "unlink") return link(a, false);
		throw stop(usage, "there's no command " + a.command + "; neotrac help lists them");
	}

	// The exit code for what the app said.
	int code_of(int status) {
		if (status == 401) return signed_out;
		if (status == 404) return not_found;
		if (status == 429) return too_many;
		if (status == 400 || status == 403) return said_no;
		return other;
	}

} // namespace

int main(int count, char** given) {
	bool as_json = false;
	try {
		auto a = read_arguments(count, given);
		as_json = a.has("json");
		return run(a);
	} catch (const stop& s) {
		if (std::string(s.what()).empty()) return s.code;
		if (as_json) std::fprintf(stderr, "{\"error\":%s,\"code\":%d}\n", json(std::string(s.what())).c_str(), s.code);
		else std::fprintf(stderr, "neotrac: %s\n", s.what());
		return s.code;
	} catch (const refused& r) {
		int code = code_of(r.status);
		if (as_json) std::fprintf(stderr, "{\"error\":%s,\"status\":%d,\"code\":%d}\n", json(std::string(r.what())).c_str(), r.status, code);
		else std::fprintf(stderr, "neotrac: %s\n", r.what());
		return code;
	} catch (const ember::connection_error& e) {
		std::fprintf(stderr, as_json ? "{\"error\":%s,\"code\":6}\n" : "neotrac: couldn't reach neotrac: %s\n", as_json ? json(std::string(e.what())).c_str() : e.what());
		return network;
	} catch (const ember::failure& f) {
		std::fprintf(stderr, as_json ? "{\"error\":%s,\"code\":3}\n" : "neotrac: signing in failed: %s\n", as_json ? json(std::string(f.what())).c_str() : f.what());
		return signed_out;
	} catch (const std::exception& e) {
		std::fprintf(stderr, "neotrac: %s\n", e.what());
		return other;
	}
}
