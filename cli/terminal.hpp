// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: GPL-3.0-only

// What the neotrac command does at a person's terminal: a list to pick from, moved
// through with the arrows or j and k, as git-select's is, and a document opened in
// their editor, its fields first and its text after. A program, like an agent, is
// never asked: it names what it means, and gives what it writes as flags.

#pragma once

#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

extern char** environ;

namespace terminal {

	// Whether there's a person to ask: input and output both a terminal.
	inline bool present() { return ::isatty(STDIN_FILENO) && ::isatty(STDOUT_FILENO); }

	// Picks one of rows: ↑/↓ or j/k to move, g and G to the first and the last, Enter to
	// pick, q or Esc to leave without. The list is drawn where the cursor is and wiped
	// after, so what's printed next reads on from the command.
	inline std::optional<std::size_t> pick(const std::string& title, const std::vector<std::string>& rows, std::size_t at = 0) {
		if (rows.empty()) return std::nullopt;
		int tty = ::open("/dev/tty", O_RDWR);
		if (tty < 0) return std::nullopt;
		termios before{};
		::tcgetattr(tty, &before);
		termios raw = before;
		raw.c_lflag &= ~static_cast<tcflag_t>(ICANON | ECHO | ISIG);
		raw.c_cc[VMIN] = 1;
		raw.c_cc[VTIME] = 0;
		::tcsetattr(tty, TCSANOW, &raw);
		winsize size{};
		int height = ::ioctl(tty, TIOCGWINSZ, &size) == 0 && size.ws_row > 4 ? size.ws_row : 24;
		int width = size.ws_col > 10 ? size.ws_col : 80;
		std::size_t shown = std::min<std::size_t>(rows.size(), static_cast<std::size_t>(height - 2));
		std::size_t top = 0;
		at = std::min(at, rows.size() - 1);
		bool drawn = false;
		auto out = [tty](const std::string& s) { (void)!::write(tty, s.data(), s.size()); };
		auto draw = [&] {
			if (at < top) top = at;
			if (at >= top + shown) top = at - shown + 1;
			std::string screen;
			if (drawn) screen += "\033[" + std::to_string(shown + 1) + "A";
			screen += "\r\033[K\033[1m" + title + "\033[0m  (↑/↓ j/k, Enter to pick, q to leave)\n";
			for (std::size_t i = top; i < top + shown; ++i) {
				std::string row = rows[i].substr(0, static_cast<std::size_t>(width - 3));
				screen += "\r\033[K" + (i == at ? "\033[7m> " + row + "\033[0m" : "  " + row) + "\n";
			}
			out(screen);
			drawn = true;
		};
		out("\033[?25l");
		draw();
		std::optional<std::size_t> picked;
		for (;;) {
			char c = 0;
			if (::read(tty, &c, 1) != 1) break;
			if (c == '\033') {
				pollfd more{tty, POLLIN, 0};
				if (::poll(&more, 1, 50) <= 0) break;  // Esc alone
				char seq[2] = {};
				if (::read(tty, seq, 1) != 1 || seq[0] != '[' || ::read(tty, seq + 1, 1) != 1) continue;
				if (seq[1] == 'A' && at > 0) --at;
				else if (seq[1] == 'B' && at + 1 < rows.size()) ++at;
			} else if (c == 'k' && at > 0) {
				--at;
			} else if (c == 'j' && at + 1 < rows.size()) {
				++at;
			} else if (c == 'g') {
				at = 0;
			} else if (c == 'G') {
				at = rows.size() - 1;
			} else if (c == '\r' || c == '\n') {
				picked = at;
				break;
			} else if (c == 'q' || c == 3 || c == 4) {
				break;
			}
			draw();
		}
		out("\033[" + std::to_string(shown + 1) + "A\r\033[J\033[?25h");
		::tcsetattr(tty, TCSANOW, &before);
		::close(tty);
		return picked;
	}

	// A line typed at the terminal, after a prompt.
	inline std::string ask(const std::string& prompt) {
		std::fprintf(stderr, "%s", prompt.c_str());
		std::fflush(stderr);
		std::string line;
		int c;
		while ((c = std::fgetc(stdin)) != EOF && c != '\n') line += static_cast<char>(c);
		return line;
	}

	// Opens text in the person's editor, $VISUAL, $EDITOR or vi, and gives back what
	// they saved. The file ends in .md, so the editor colours its markdown.
	inline std::string edit(const std::string& text) {
		const char* dir = std::getenv("TMPDIR");
		std::string path = std::string(dir && *dir ? dir : "/tmp") + "/neotrac-XXXXXX.md";
		int fd = ::mkstemps(path.data(), 3);
		if (fd < 0) throw std::runtime_error("couldn't make a file to edit in");
		(void)!::write(fd, text.data(), text.size());
		::close(fd);
		const char* visual = std::getenv("VISUAL");
		const char* editor = std::getenv("EDITOR");
		std::string command = visual && *visual ? visual : editor && *editor ? editor : "vi";
		// Through the shell, so an editor given with its own words, like "code -w", works.
		std::string script = command + " \"$1\"";
		char sh[] = "/bin/sh", c[] = "-c", name[] = "neotrac";
		char* argv[] = {sh, c, script.data(), name, path.data(), nullptr};
		pid_t child;
		if (::posix_spawn(&child, "/bin/sh", nullptr, nullptr, argv, environ) != 0) {
			::unlink(path.c_str());
			throw std::runtime_error("couldn't start the editor, " + command);
		}
		int status = 0;
		while (::waitpid(child, &status, 0) < 0 && errno == EINTR) {
		}
		std::ifstream in(path);
		std::stringstream saved;
		saved << in.rdbuf();
		::unlink(path.c_str());
		if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) throw std::runtime_error("the editor, " + command + ", stopped without saving");
		return saved.str();
	}

	// A thing to edit, as a file: its fields, one a line as Name: value, then a blank
	// line and its text, like an issue's description, in markdown.
	struct document {
		std::vector<std::pair<std::string, std::string>> fields;
		std::string text;

		std::string get(const std::string& name) const {
			for (const auto& [n, v] : fields) {
				if (n == name) return v;
			}
			return {};
		}
		void set(const std::string& name, const std::string& value) {
			for (auto& [n, v] : fields) {
				if (n == name) {
					v = value;
					return;
				}
			}
			fields.emplace_back(name, value);
		}
	};

	inline const std::string scissors = "# ------------------------ >8 ------------------------";

	inline std::string written(const document& d, const std::string& help) {
		std::string out;
		for (const auto& [name, value] : d.fields) out += name + ": " + value + "\n";
		out += "\n" + d.text;
		if (!d.text.empty() && d.text.back() != '\n') out += "\n";
		out += "\n" + scissors + "\n# Everything from the line above down is left out. " + help + "\n";
		return out;
	}

	inline std::string trimmed(std::string s) {
		auto blank = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; };
		while (!s.empty() && blank(s.back())) s.pop_back();
		std::size_t start = 0;
		while (start < s.size() && blank(s[start])) ++start;
		return s.substr(start);
	}

	// Reads a document back: the fields it started with, by name, and its text.
	inline document read(const std::string& saved, const document& started) {
		std::string kept = saved.substr(0, saved.find(scissors));
		document out;
		std::istringstream lines(kept);
		std::string line;
		bool in_text = false;
		std::string text;
		while (std::getline(lines, line)) {
			if (in_text) {
				text += line + "\n";
				continue;
			}
			if (trimmed(line).empty()) {
				in_text = true;
				continue;
			}
			auto colon = line.find(':');
			std::string name = colon == std::string::npos ? "" : trimmed(line.substr(0, colon));
			bool known = std::ranges::any_of(started.fields, [&](const auto& f) { return f.first == name; });
			if (!known) throw std::runtime_error("\"" + line + "\" isn't one of the fields at the top, like " + started.fields.front().first + ": ...");
			out.set(name, trimmed(line.substr(colon + 1)));
		}
		out.text = trimmed(text);
		return out;
	}

} // namespace terminal
