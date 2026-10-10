// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: GPL-3.0-only

// neotrac: the issues waiting for you, or for a service, on the command line. A
// service, like an AI agent, signs in with the key a project gave it; a person signs
// in with GitHub. Then it lists the issues in the phases a role of theirs works,
// takes one, comments, and moves it on where the workflow lets it.
//
// Everything it knows about neotrac comes from the client one build writes,
// generated/neotrac.hpp, so a change to neotrac's .one files reaches it at the next
// build.

#include <neotrac.hpp>

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <termios.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

	using namespace neotrac;

	constexpr const char* version = "0.1.0";

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
	const std::set<std::string> valued{"board", "project", "server", "message", "m"};

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

	// JSON, written by hand: strings, numbers and lists of them.
	std::string json(const std::string& s) { return detail::json(s); }
	std::string json(double n) { return detail::json(n); }
	std::string json(bool b) { return b ? "true" : "false"; }
	std::string json(const std::vector<std::string>& list) { return detail::json(list); }

	// What a sign-in keeps between runs, in $XDG_CONFIG_HOME/neotrac/credentials, which
	// only its owner may read.
	struct kept {
		std::string server, key, refresh_token, project;
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
		return out;
	}

	void write_credentials(const kept& k) {
		auto path = credentials_path();
		std::filesystem::create_directories(path.parent_path());
		std::string text = "{\"server\":" + json(k.server) + ",\"key\":" + json(k.key) + ",\"refresh_token\":" + json(k.refresh_token) +
		                   ",\"project\":" + json(k.project) + "}\n";
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
		std::vector<std::string> roles;  // the ids of the roles held in the project
	};

	options options_for(const std::string& server) {
		options o;
		o.server = server;
		o.project = env("NEOTRAC_FIREBASE_PROJECT");
		o.firestore_host = env("NEOTRAC_FIRESTORE_HOST");
		o.auth_host = env("NEOTRAC_AUTH_HOST");
		return o;
	}

	session sign_in(const asked& a) {
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
		auto mine = s.app->mine().get();
		if (s.project.empty()) {
			std::set<std::string> projects;
			for (const auto& row : mine.rows) projects.insert(row.project);
			if (projects.size() != 1) throw stop(usage, "say which project with --project, one of yours");
			s.project = *projects.begin();
		}
		for (const auto& row : mine.rows) {
			if (row.project == s.project) s.roles.push_back(row.role);
		}
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

	// One issue waiting in a phase a role of yours works.
	struct waiting {
		std::string id, title, phase_title, board_title, priority;
		double number = 0;
		std::vector<std::string> assigned;
		bool yours = false;
	};

	// The order issues are worked in: most urgent first, then oldest.
	int urgency(const std::string& priority) {
		static const std::vector<std::string> order{"critical", "high", "normal", "low", "trivial"};
		auto it = std::ranges::find(order, priority);
		return it == order.end() ? 2 : static_cast<int>(it - order.begin());
	}

	std::vector<waiting> queue_of(const session& s, const std::map<std::string, projects::board_page>& boards, const std::map<std::string, std::string>& titles, bool all) {
		std::vector<waiting> out;
		for (const auto& [board, page] : boards) {
			std::map<std::string, const projects::board_page::phases_row*> worked;
			for (const auto& phase : page.phases) {
				if (shares(phase.worked_by, s.roles)) worked[phase.id] = &phase;
			}
			for (const auto& issue : page.issues) {
				auto phase = worked.find(issue.phase);
				if (phase == worked.end()) continue;
				bool yours = std::ranges::find(issue.assignees, s.who.id) != issue.assignees.end();
				if (!all && !issue.assignees.empty() && !yours) continue;
				out.push_back({issue.id, issue.title, phase->second->title, titles.at(board), projects::to_string(issue.priority), issue.number, issue.assignees_name, yours});
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
			       ",\"priority\":" + json(w.priority) + ",\"phase\":" + json(w.phase_title) + ",\"board\":" + json(w.board_title) +
			       ",\"assigned\":" + json(w.assigned) + ",\"yours\":" + json(w.yours) + "}";
		}
		return out + "]";
	}

	void print_queue(const std::vector<waiting>& q) {
		if (q.empty()) {
			std::puts("Nothing waiting.");
			return;
		}
		for (const auto& w : q) {
			std::string who = w.yours ? "yours" : w.assigned.empty() ? "free" : "taken by " + w.assigned.front();
			std::printf("#%-5.0f %-8s %-12s %-40s %s\n", w.number, w.priority.c_str(), w.phase_title.c_str(), w.title.substr(0, 40).c_str(), who.c_str());
		}
	}

	volatile std::sig_atomic_t stopping = 0;

	int queue(const asked& a) {
		auto s = sign_in(a);
		auto project = s.app->project_page(s.project).get();
		if (!project.exists) throw stop(not_found, "there's no project " + s.project + " you may read");
		std::map<std::string, std::string> titles;
		for (const auto& board : project.boards) {
			if (!a.has("board") || board.name == a.flag("board") || board.id == a.flag("board")) titles[board.id] = board.title;
		}
		if (titles.empty()) throw stop(not_found, "there's no board " + a.flag("board") + " in " + s.project);
		bool all = a.has("all"), as_json = a.has("json");
		if (!a.has("watch")) {
			std::map<std::string, projects::board_page> boards;
			for (const auto& [board, _] : titles) boards[board] = s.app->board_page(board).get();
			auto q = queue_of(s, boards, titles, all);
			if (as_json) std::puts(queue_json(q).c_str());
			else print_queue(q);
			return done;
		}
		// Watching: each board listened to, and the queue said again whenever one changes.
		std::map<std::string, projects::board_page> boards;
		std::vector<ember::database::registration> listening;
		std::string last;
		int failed = done;
		auto say = [&] {
			if (boards.size() != titles.size()) return;
			auto q = queue_of(s, boards, titles, all);
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
		for (const auto& [board, _] : titles) {
			listening.push_back(s.app->board_page(board).listen(
			    [&, board](const projects::board_page& page) {
				    boards[board] = page;
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
		while (!stopping) s.here->run_one_for(std::chrono::milliseconds(250));
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
			if (step.from_ == page.phase && shares(step.roles, s.roles)) out.push_back(&step);
		}
		return out;
	}

	std::string text_of(const ember::value& v) { return v.is_string() ? v.as_string() : v.is_null() ? "" : ember::to_json(v); }

	void print_issue(const session& s, const projects::issue_page& page, bool history) {
		std::printf("#%.0f %s\n", page.number, page.title.c_str());
		std::printf("%s on %s, %s priority", page.phase_title.c_str(), page.board_title.c_str(), projects::to_string(page.priority));
		if (!page.assigned.empty()) {
			std::printf(", taken by");
			for (std::size_t i = 0; i < page.assigned.size(); ++i) std::printf("%s %s", i ? "," : "", page.assigned[i].c_str());
		}
		if (!page.labels.empty()) {
			std::printf("; labels");
			for (const auto& l : page.labels) std::printf(" %s", l.c_str());
		}
		std::printf("\n\n%s\n", page.body.empty() ? "(no description)" : page.body.c_str());
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
		std::string out = "{\"number\":" + json(page.number) + ",\"title\":" + json(page.title) + ",\"body\":" + json(page.body) + ",\"phase\":" +
		                  json(page.phase_title) + ",\"board\":" + json(page.board_title) + ",\"priority\":" + json(std::string(projects::to_string(page.priority))) +
		                  ",\"labels\":" + json(page.labels) + ",\"assigned\":" + json(page.assigned) + ",\"comments\":[";
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

	int claim(const asked& a, bool taking) {
		auto s = sign_in(a);
		std::string id = issue_of(s, a);
		if (taking) s.app->issue_claim({.id = id});
		else s.app->issue_release({.id = id});
		if (a.has("json")) std::printf("{\"id\":%s,\"%s\":true}\n", json(id).c_str(), taking ? "taken" : "let_go");
		else std::printf(taking ? "Took %s.\n" : "Let go of %s.\n", a.words[0].c_str());
		return done;
	}

	int move(const asked& a) {
		auto s = sign_in(a);
		std::string id = issue_of(s, a);
		if (a.words.size() < 2) throw stop(usage, "say where to, like neotrac move 42 review");
		auto page = read_issue(s, id, a.words[0]);
		// The phase by its name, its title, or its id, among the issue's board's.
		std::string wanted = lower(a.words[1]);
		auto board = s.app->board_page(page.board).get();
		const projects::board_page::phases_row* to = nullptr;
		for (const auto& phase : board.phases) {
			if (lower(phase.name) == wanted || lower(phase.title) == wanted || phase.id == a.words[1]) to = &phase;
		}
		auto moves = moves_of(s, page);
		std::string allowed;
		for (const auto* m : moves) allowed += (allowed.empty() ? "" : ", ") + lower(m->to_title);
		if (allowed.empty()) allowed = "none from " + page.phase_title;
		if (!to) throw stop(usage, "there's no phase " + a.words[1] + " on " + page.board_title + "; you can move it to: " + allowed);
		if (std::ranges::none_of(moves, [&](const auto* m) { return m->to == to->id; })) {
			throw stop(said_no, "your role doesn't move an issue from " + page.phase_title + " to " + to->title + "; you can move it to: " + allowed);
		}
		projects::issue_move sent{.id = id};
		sent.phase = to->id;
		s.app->issue_move(sent);
		if (a.has("json")) std::printf("{\"id\":%s,\"phase\":%s}\n", json(id).c_str(), json(to->title).c_str());
		else std::printf("Moved %s to %s.\n", a.words[0].c_str(), to->title.c_str());
		return done;
	}

	int comment(const asked& a) {
		auto s = sign_in(a);
		std::string id = issue_of(s, a);
		std::string body = a.flag("message");
		if (!a.has("message")) {
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

	// Signs in with GitHub's device flow: a code to type at github.com, then the
	// access GitHub gives traded for a neotrac sign-in.
	std::string github_device_sign_in(const std::string& client_id) {
		auto form = [](const std::string& s) {
			std::string out;
			for (unsigned char c : s) {
				if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out += static_cast<char>(c);
				else {
					char hex[4];
					std::snprintf(hex, sizeof hex, "%%%02X", c);
					out += hex;
				}
			}
			return out;
		};
		std::vector<std::pair<std::string, std::string>> headers{{"Accept", "application/json"}, {"Content-Type", "application/x-www-form-urlencoded"}};
		auto code = ember::https_post("https://github.com/login/device/code", headers, "client_id=" + form(client_id) + "&scope=" + form("read:user user:email"));
		auto v = ember::parse_json(code.body);
		if (code.status != 200 || !v["device_code"].is_string()) throw stop(signed_out, "GitHub didn't start a sign-in: " + code.body);
		std::fprintf(stderr, "Open %s and type %s\n", detail::text(v["verification_uri"]).c_str(), detail::text(v["user_code"]).c_str());
		int every = std::max(5, static_cast<int>(detail::number(v["interval"])));
		for (;;) {
			std::this_thread::sleep_for(std::chrono::seconds(every));
			auto answer = ember::https_post("https://github.com/login/oauth/access_token", headers,
			                                "client_id=" + form(client_id) + "&device_code=" + form(detail::text(v["device_code"])) +
			                                    "&grant_type=" + form("urn:ietf:params:oauth:grant-type:device_code"));
			auto got = ember::parse_json(answer.body);
			if (got["access_token"].is_string()) return got["access_token"].as_string();
			std::string why = detail::text(got["error"]);
			if (why == "authorization_pending") continue;
			if (why == "slow_down") {
				every += 5;
				continue;
			}
			throw stop(signed_out, "GitHub's sign-in ended: " + (why.empty() ? answer.body : why));
		}
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
			std::string client_id = env("NEOTRAC_GITHUB_CLIENT_ID");
			if (client_id.empty()) throw stop(usage, "signing in as a person needs NEOTRAC_GITHUB_CLIENT_ID, the app's GitHub client id, for now");
			app.sign_in_with_provider("github.com", github_device_sign_in(client_id));
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
		auto s = sign_in(a);
		std::string key = !env("NEOTRAC_KEY").empty() ? env("NEOTRAC_KEY") : s.credentials.key;
		if (a.has("json")) {
			std::printf("{\"id\":%s,\"name\":%s,\"service\":%s,\"project\":%s,\"roles\":%s}\n", json(s.who.id).c_str(), json(s.who.name).c_str(), json(s.who.service).c_str(),
			            json(s.project).c_str(), json(s.roles).c_str());
			return done;
		}
		std::printf("%s%s in %s", s.who.name.c_str(), s.who.service ? ", a service," : "", s.project.c_str());
		if (!key.empty()) std::printf(", with key %s…", key.substr(0, 10).c_str());
		std::printf("\n");
		return done;
	}

	const char* help = R"(neotrac: the issues waiting for you, or for a service, on the command line.

  neotrac login --key          sign in as a service, with the key its project gave it
  neotrac login                sign in as yourself, with GitHub
  neotrac whoami               who's signed in, and where
  neotrac queue                the issues in phases your roles work, free or yours
          [--board main] [--all] [--watch]
  neotrac show 42 [--history] [--watch]
                               an issue, its comments, and the moves you can make
  neotrac claim 42             take an issue, so nobody else works it
  neotrac release 42           let it go
  neotrac move 42 review       move it, where the workflow lets your role
  neotrac comment 42 -m "..."  comment, or with the comment on stdin

Every command takes --json, and --server to use another neotrac. A service's key may
also come from NEOTRAC_KEY. Exit codes: 0 done, 2 wrong usage, 3 not signed in or
refused, 4 not found, 5 the workflow said no, 6 no connection, 7 too many requests.
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
		if (a.command == "whoami") return whoami(a);
		if (a.command == "queue") return queue(a);
		if (a.command == "show") return show(a);
		if (a.command == "claim") return claim(a, true);
		if (a.command == "release") return claim(a, false);
		if (a.command == "move") return move(a);
		if (a.command == "comment") return comment(a);
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
