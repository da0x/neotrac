// Generated from neotrac/ by one. Do not edit.

// neotrac's C++ client: its records, the ids they're stored under, its commands,
// and its views, read live. Built with libember (github.com/da0x/libember):
//
//   g++ -std=c++23 program.cpp $(pkg-config --cflags --libs botan-3)

#pragma once

#include <cctype>
#include <chrono>
#include <cstdio>
#include <format>
#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <ember.hpp>

namespace neotrac {

	// When something happened, to the nanosecond.
	using time = std::chrono::sys_time<std::chrono::nanoseconds>;

	// What the app said when it refused a command: why, and its HTTP status.
	struct refused : std::runtime_error {
		int status;
		refused(int status, const std::string& message) : std::runtime_error(message), status(status) {}
	};

	namespace detail {
		inline std::string text(const ember::value& v) { return v.is_string() ? v.as_string() : std::string(); }
		inline double number(const ember::value& v) { return v.is_integer() ? double(v.as_integer()) : v.is_double() ? v.as_double() : 0; }
		inline bool flag(const ember::value& v) { return v.is_bool() && v.as_bool(); }
		inline time when(const ember::value& v) { return v.is_time() ? v.as_time() : time{}; }
		inline std::vector<std::string> texts(const ember::value& v) {
			std::vector<std::string> out;
			if (v.is_array()) {
				for (const auto& item : v.as_array()) out.push_back(text(item));
			}
			return out;
		}
		template <class Choice> Choice choice(const ember::value& v) {
			Choice out{};
			from_string(text(v), out);
			return out;
		}
		// A string as JSON.
		inline std::string json(const std::string& s) {
			std::string out = "\"";
			for (unsigned char c : s) {
				if (c == '"' || c == '\\') out += '\\', out += char(c);
				else if (c < 0x20) { char hex[8]; std::snprintf(hex, sizeof hex, "\\u%04x", c); out += hex; }
				else out += char(c);
			}
			return out + "\"";
		}
		inline std::string json(double n) { char s[32]; std::snprintf(s, sizeof s, "%.17g", n); return s; }
		inline std::string json(bool b) { return b ? "true" : "false"; }
		inline std::string json(const std::vector<std::string>& list) {
			std::string out = "[";
			for (std::size_t i = 0; i < list.size(); ++i) out += (i ? "," : "") + json(list[i]);
			return out + "]";
		}
		inline std::string json(time t) {
			return json(std::format("{:%FT%TZ}", std::chrono::floor<std::chrono::milliseconds>(t)));
		}
	} // namespace detail

	// The id an entity is stored under, from its keys in order, as the app makes it:
	// each part escaped as in a path, and a dash after the first part escaped too, so
	// project engine and person x-1 is engine-x%2D1.
	inline std::string key(std::initializer_list<std::string> parts) {
		std::string out;
		bool first = true;
		for (const auto& part : parts) {
			if (!first) out += '-';
			for (unsigned char c : part) {
				bool plain = std::isalnum(c) || c == '_' || c == '.' || c == '~' || c == '$' || c == '&' || c == '+' || c == ':' || c == '=' || c == '@';
				if (plain || (c == '-' && first)) { out += char(c); continue; }
				char hex[4];
				std::snprintf(hex, sizeof hex, "%%%02X", c);
				out += hex;
			}
			first = false;
		}
		return out;
	}

	// A number as a key holds it, 42 rather than 42.000000.
	inline std::string key_part(double n) {
		char s[32];
		std::snprintf(s, sizeof s, "%.15g", n);
		return s;
	}
	inline std::string key_part(const std::string& s) { return s; }

	namespace about {

	} // namespace about

	namespace projects {

		enum class priority { critical, high, normal, low, trivial };
		inline const char* to_string(priority c) {
			switch (c) {
				case priority::critical: return "critical";
				case priority::high: return "high";
				case priority::normal: return "normal";
				case priority::low: return "low";
				case priority::trivial: return "trivial";
			}
			return "";
		}
		// Reads a choice by its name; one that isn't, like a choice added since, leaves it.
		inline bool from_string(const std::string& text, priority& c) {
			if (text == "critical") return c = priority::critical, true;
			if (text == "high") return c = priority::high, true;
			if (text == "normal") return c = priority::normal, true;
			if (text == "low") return c = priority::low, true;
			if (text == "trivial") return c = priority::trivial, true;
			return false;
		}

		enum class relation { blocks, blocked_by, relates, duplicates, duplicated_by, replaces, replaced_by };
		inline const char* to_string(relation c) {
			switch (c) {
				case relation::blocks: return "blocks";
				case relation::blocked_by: return "blocked_by";
				case relation::relates: return "relates";
				case relation::duplicates: return "duplicates";
				case relation::duplicated_by: return "duplicated_by";
				case relation::replaces: return "replaces";
				case relation::replaced_by: return "replaced_by";
			}
			return "";
		}
		// Reads a choice by its name; one that isn't, like a choice added since, leaves it.
		inline bool from_string(const std::string& text, relation& c) {
			if (text == "blocks") return c = relation::blocks, true;
			if (text == "blocked_by") return c = relation::blocked_by, true;
			if (text == "relates") return c = relation::relates, true;
			if (text == "duplicates") return c = relation::duplicates, true;
			if (text == "duplicated_by") return c = relation::duplicated_by, true;
			if (text == "replaces") return c = relation::replaces, true;
			if (text == "replaced_by") return c = relation::replaced_by, true;
			return false;
		}

		enum class workflow { basic, agile, editorial, bugs, adr, custom };
		inline const char* to_string(workflow c) {
			switch (c) {
				case workflow::basic: return "basic";
				case workflow::agile: return "agile";
				case workflow::editorial: return "editorial";
				case workflow::bugs: return "bugs";
				case workflow::adr: return "adr";
				case workflow::custom: return "custom";
			}
			return "";
		}
		// Reads a choice by its name; one that isn't, like a choice added since, leaves it.
		inline bool from_string(const std::string& text, workflow& c) {
			if (text == "basic") return c = workflow::basic, true;
			if (text == "agile") return c = workflow::agile, true;
			if (text == "editorial") return c = workflow::editorial, true;
			if (text == "bugs") return c = workflow::bugs, true;
			if (text == "adr") return c = workflow::adr, true;
			if (text == "custom") return c = workflow::custom, true;
			return false;
		}

		enum class project_visibility { public_, private_ };
		inline const char* to_string(project_visibility c) {
			switch (c) {
				case project_visibility::public_: return "public";
				case project_visibility::private_: return "private";
			}
			return "";
		}
		// Reads a choice by its name; one that isn't, like a choice added since, leaves it.
		inline bool from_string(const std::string& text, project_visibility& c) {
			if (text == "public") return c = project_visibility::public_, true;
			if (text == "private") return c = project_visibility::private_, true;
			return false;
		}

		enum class report_status { open, resolved };
		inline const char* to_string(report_status c) {
			switch (c) {
				case report_status::open: return "open";
				case report_status::resolved: return "resolved";
			}
			return "";
		}
		// Reads a choice by its name; one that isn't, like a choice added since, leaves it.
		inline bool from_string(const std::string& text, report_status& c) {
			if (text == "open") return c = report_status::open, true;
			if (text == "resolved") return c = report_status::resolved, true;
			return false;
		}

		struct board {
			std::string id{};
			time created_at{};
			std::string created_by{};
			time updated_at{};
			std::string updated_by{};
			std::string project{};
			std::string name{};
			std::string title{};
			::neotrac::projects::workflow workflow{};
			std::string start{};
			double position{};
			std::vector<std::string> followers{};

			static board from(const ember::value& v) {
				board out;
				out.id = detail::text(v["id"]);
				out.created_at = detail::when(v["created_at"]);
				out.created_by = detail::text(v["created_by"]);
				out.updated_at = detail::when(v["updated_at"]);
				out.updated_by = detail::text(v["updated_by"]);
				out.project = detail::text(v["project"]);
				out.name = detail::text(v["name"]);
				out.title = detail::text(v["title"]);
				out.workflow = detail::choice<::neotrac::projects::workflow>(v["workflow"]);
				out.start = detail::text(v["start"]);
				out.position = detail::number(v["position"]);
				out.followers = detail::texts(v["followers"]);
				return out;
			}
		};
		inline std::string board_id(const std::string& project, const std::string& name) { return key({key_part(project), key_part(name)}); }

		struct issue {
			std::string id{};
			time created_at{};
			std::string created_by{};
			time updated_at{};
			std::string updated_by{};
			std::string project{};
			double number{};
			std::string board{};
			std::string title{};
			std::string body{};
			std::vector<std::string> labels{};
			std::string author{};
			std::string phase{};
			::neotrac::projects::priority priority{};
			std::vector<std::string> assignees{};
			std::string milestone{};

			static issue from(const ember::value& v) {
				issue out;
				out.id = detail::text(v["id"]);
				out.created_at = detail::when(v["created_at"]);
				out.created_by = detail::text(v["created_by"]);
				out.updated_at = detail::when(v["updated_at"]);
				out.updated_by = detail::text(v["updated_by"]);
				out.project = detail::text(v["project"]);
				out.number = detail::number(v["number"]);
				out.board = detail::text(v["board"]);
				out.title = detail::text(v["title"]);
				out.body = detail::text(v["body"]);
				out.labels = detail::texts(v["labels"]);
				out.author = detail::text(v["author"]);
				out.phase = detail::text(v["phase"]);
				out.priority = detail::choice<::neotrac::projects::priority>(v["priority"]);
				out.assignees = detail::texts(v["assignees"]);
				out.milestone = detail::text(v["milestone"]);
				return out;
			}
		};
		inline std::string issue_id(const std::string& project, double number) { return key({key_part(project), key_part(number)}); }

		struct link {
			std::string id{};
			time created_at{};
			std::string created_by{};
			time updated_at{};
			std::string updated_by{};
			std::string project{};
			std::string from_{};
			std::string to{};
			::neotrac::projects::relation relation{};
			std::string pair{};

			static link from(const ember::value& v) {
				link out;
				out.id = detail::text(v["id"]);
				out.created_at = detail::when(v["created_at"]);
				out.created_by = detail::text(v["created_by"]);
				out.updated_at = detail::when(v["updated_at"]);
				out.updated_by = detail::text(v["updated_by"]);
				out.project = detail::text(v["project"]);
				out.from_ = detail::text(v["from"]);
				out.to = detail::text(v["to"]);
				out.relation = detail::choice<::neotrac::projects::relation>(v["relation"]);
				out.pair = detail::text(v["pair"]);
				return out;
			}
		};

		struct comment {
			std::string id{};
			time created_at{};
			std::string created_by{};
			time updated_at{};
			std::string updated_by{};
			std::string issue{};
			std::string body{};
			std::string author{};
			std::vector<std::string> mentioned{};

			static comment from(const ember::value& v) {
				comment out;
				out.id = detail::text(v["id"]);
				out.created_at = detail::when(v["created_at"]);
				out.created_by = detail::text(v["created_by"]);
				out.updated_at = detail::when(v["updated_at"]);
				out.updated_by = detail::text(v["updated_by"]);
				out.issue = detail::text(v["issue"]);
				out.body = detail::text(v["body"]);
				out.author = detail::text(v["author"]);
				out.mentioned = detail::texts(v["mentioned"]);
				return out;
			}
		};

		struct milestone {
			std::string id{};
			time created_at{};
			std::string created_by{};
			time updated_at{};
			std::string updated_by{};
			std::string project{};
			std::string name{};
			std::string title{};
			time due{};
			std::string description{};

			static milestone from(const ember::value& v) {
				milestone out;
				out.id = detail::text(v["id"]);
				out.created_at = detail::when(v["created_at"]);
				out.created_by = detail::text(v["created_by"]);
				out.updated_at = detail::when(v["updated_at"]);
				out.updated_by = detail::text(v["updated_by"]);
				out.project = detail::text(v["project"]);
				out.name = detail::text(v["name"]);
				out.title = detail::text(v["title"]);
				out.due = detail::when(v["due"]);
				out.description = detail::text(v["description"]);
				return out;
			}
		};
		inline std::string milestone_id(const std::string& project, const std::string& name) { return key({key_part(project), key_part(name)}); }

		struct project {
			std::string id{};
			time created_at{};
			std::string created_by{};
			time updated_at{};
			std::string updated_by{};
			std::string slug{};
			std::string name{};
			std::string summary{};
			::neotrac::projects::project_visibility visibility{};
			bool takes_reports{};
			::neotrac::projects::workflow workflow{};
			std::string start{};

			static project from(const ember::value& v) {
				project out;
				out.id = detail::text(v["id"]);
				out.created_at = detail::when(v["created_at"]);
				out.created_by = detail::text(v["created_by"]);
				out.updated_at = detail::when(v["updated_at"]);
				out.updated_by = detail::text(v["updated_by"]);
				out.slug = detail::text(v["slug"]);
				out.name = detail::text(v["name"]);
				out.summary = detail::text(v["summary"]);
				out.visibility = detail::choice<::neotrac::projects::project_visibility>(v["visibility"]);
				out.takes_reports = detail::flag(v["takes_reports"]);
				out.workflow = detail::choice<::neotrac::projects::workflow>(v["workflow"]);
				out.start = detail::text(v["start"]);
				return out;
			}
		};
		inline std::string project_id(const std::string& slug) { return key({key_part(slug)}); }

		struct reader {
			std::string id{};
			time created_at{};
			std::string created_by{};
			time updated_at{};
			std::string updated_by{};
			std::string person{};
			time seen_at{};

			static reader from(const ember::value& v) {
				reader out;
				out.id = detail::text(v["id"]);
				out.created_at = detail::when(v["created_at"]);
				out.created_by = detail::text(v["created_by"]);
				out.updated_at = detail::when(v["updated_at"]);
				out.updated_by = detail::text(v["updated_by"]);
				out.person = detail::text(v["person"]);
				out.seen_at = detail::when(v["seen_at"]);
				return out;
			}
		};
		inline std::string reader_id(const std::string& person) { return key({key_part(person)}); }

		struct invitation {
			std::string id{};
			time created_at{};
			std::string created_by{};
			time updated_at{};
			std::string updated_by{};
			std::string project{};
			std::string email{};
			std::string role{};

			static invitation from(const ember::value& v) {
				invitation out;
				out.id = detail::text(v["id"]);
				out.created_at = detail::when(v["created_at"]);
				out.created_by = detail::text(v["created_by"]);
				out.updated_at = detail::when(v["updated_at"]);
				out.updated_by = detail::text(v["updated_by"]);
				out.project = detail::text(v["project"]);
				out.email = detail::text(v["email"]);
				out.role = detail::text(v["role"]);
				return out;
			}
		};
		inline std::string invitation_id(const std::string& project, const std::string& email) { return key({key_part(project), key_part(email)}); }

		struct service {
			std::string id{};
			time created_at{};
			std::string created_by{};
			time updated_at{};
			std::string updated_by{};
			std::string project{};
			std::string name{};
			std::string title{};
			std::string role{};
			std::string key_start{};
			time key_made{};
			time key_used{};

			static service from(const ember::value& v) {
				service out;
				out.id = detail::text(v["id"]);
				out.created_at = detail::when(v["created_at"]);
				out.created_by = detail::text(v["created_by"]);
				out.updated_at = detail::when(v["updated_at"]);
				out.updated_by = detail::text(v["updated_by"]);
				out.project = detail::text(v["project"]);
				out.name = detail::text(v["name"]);
				out.title = detail::text(v["title"]);
				out.role = detail::text(v["role"]);
				out.key_start = detail::text(v["key_start"]);
				out.key_made = detail::when(v["key_made"]);
				out.key_used = detail::when(v["key_used"]);
				return out;
			}
		};
		inline std::string service_id(const std::string& project, const std::string& name) { return key({key_part(project), key_part(name)}); }

		struct role {
			std::string id{};
			time created_at{};
			std::string created_by{};
			time updated_at{};
			std::string updated_by{};
			std::string project{};
			std::string name{};
			std::string title{};
			std::vector<std::string> may{};

			static role from(const ember::value& v) {
				role out;
				out.id = detail::text(v["id"]);
				out.created_at = detail::when(v["created_at"]);
				out.created_by = detail::text(v["created_by"]);
				out.updated_at = detail::when(v["updated_at"]);
				out.updated_by = detail::text(v["updated_by"]);
				out.project = detail::text(v["project"]);
				out.name = detail::text(v["name"]);
				out.title = detail::text(v["title"]);
				out.may = detail::texts(v["may"]);
				return out;
			}
		};
		inline std::string role_id(const std::string& project, const std::string& name) { return key({key_part(project), key_part(name)}); }

		struct member {
			std::string id{};
			time created_at{};
			std::string created_by{};
			time updated_at{};
			std::string updated_by{};
			std::string project{};
			std::string person{};
			std::string role{};

			static member from(const ember::value& v) {
				member out;
				out.id = detail::text(v["id"]);
				out.created_at = detail::when(v["created_at"]);
				out.created_by = detail::text(v["created_by"]);
				out.updated_at = detail::when(v["updated_at"]);
				out.updated_by = detail::text(v["updated_by"]);
				out.project = detail::text(v["project"]);
				out.person = detail::text(v["person"]);
				out.role = detail::text(v["role"]);
				return out;
			}
		};
		inline std::string member_id(const std::string& project, const std::string& person, const std::string& role) { return key({key_part(project), key_part(person), key_part(role)}); }

		struct report {
			std::string id{};
			time created_at{};
			std::string created_by{};
			time updated_at{};
			std::string updated_by{};
			std::string project{};
			std::string title{};
			std::string body{};
			std::string author{};
			::neotrac::projects::report_status status{};

			static report from(const ember::value& v) {
				report out;
				out.id = detail::text(v["id"]);
				out.created_at = detail::when(v["created_at"]);
				out.created_by = detail::text(v["created_by"]);
				out.updated_at = detail::when(v["updated_at"]);
				out.updated_by = detail::text(v["updated_by"]);
				out.project = detail::text(v["project"]);
				out.title = detail::text(v["title"]);
				out.body = detail::text(v["body"]);
				out.author = detail::text(v["author"]);
				out.status = detail::choice<::neotrac::projects::report_status>(v["status"]);
				return out;
			}
		};

		struct reply {
			std::string id{};
			time created_at{};
			std::string created_by{};
			time updated_at{};
			std::string updated_by{};
			std::string report{};
			std::string body{};
			std::string author{};

			static reply from(const ember::value& v) {
				reply out;
				out.id = detail::text(v["id"]);
				out.created_at = detail::when(v["created_at"]);
				out.created_by = detail::text(v["created_by"]);
				out.updated_at = detail::when(v["updated_at"]);
				out.updated_by = detail::text(v["updated_by"]);
				out.report = detail::text(v["report"]);
				out.body = detail::text(v["body"]);
				out.author = detail::text(v["author"]);
				return out;
			}
		};

		struct page {
			std::string id{};
			time created_at{};
			std::string created_by{};
			time updated_at{};
			std::string updated_by{};
			std::string project{};
			std::string name{};
			std::string title{};
			std::string body{};
			double position{};

			static page from(const ember::value& v) {
				page out;
				out.id = detail::text(v["id"]);
				out.created_at = detail::when(v["created_at"]);
				out.created_by = detail::text(v["created_by"]);
				out.updated_at = detail::when(v["updated_at"]);
				out.updated_by = detail::text(v["updated_by"]);
				out.project = detail::text(v["project"]);
				out.name = detail::text(v["name"]);
				out.title = detail::text(v["title"]);
				out.body = detail::text(v["body"]);
				out.position = detail::number(v["position"]);
				return out;
			}
		};
		inline std::string page_id(const std::string& project, const std::string& name) { return key({key_part(project), key_part(name)}); }

		struct phase {
			std::string id{};
			time created_at{};
			std::string created_by{};
			time updated_at{};
			std::string updated_by{};
			std::string board{};
			std::string name{};
			std::string project{};
			std::string title{};
			double position{};
			std::vector<std::string> worked_by{};

			static phase from(const ember::value& v) {
				phase out;
				out.id = detail::text(v["id"]);
				out.created_at = detail::when(v["created_at"]);
				out.created_by = detail::text(v["created_by"]);
				out.updated_at = detail::when(v["updated_at"]);
				out.updated_by = detail::text(v["updated_by"]);
				out.board = detail::text(v["board"]);
				out.name = detail::text(v["name"]);
				out.project = detail::text(v["project"]);
				out.title = detail::text(v["title"]);
				out.position = detail::number(v["position"]);
				out.worked_by = detail::texts(v["worked_by"]);
				return out;
			}
		};
		inline std::string phase_id(const std::string& board, const std::string& name) { return key({key_part(board), key_part(name)}); }

		struct step {
			std::string id{};
			time created_at{};
			std::string created_by{};
			time updated_at{};
			std::string updated_by{};
			std::string board{};
			std::string from_{};
			std::string to{};
			std::string project{};
			std::string title{};
			std::vector<std::string> roles{};

			static step from(const ember::value& v) {
				step out;
				out.id = detail::text(v["id"]);
				out.created_at = detail::when(v["created_at"]);
				out.created_by = detail::text(v["created_by"]);
				out.updated_at = detail::when(v["updated_at"]);
				out.updated_by = detail::text(v["updated_by"]);
				out.board = detail::text(v["board"]);
				out.from_ = detail::text(v["from"]);
				out.to = detail::text(v["to"]);
				out.project = detail::text(v["project"]);
				out.title = detail::text(v["title"]);
				out.roles = detail::texts(v["roles"]);
				return out;
			}
		};
		inline std::string step_id(const std::string& board, const std::string& from_, const std::string& to) { return key({key_part(board), key_part(from_), key_part(to)}); }

		// What board::update is sent.
		struct board_update {
			std::string id{};
			std::optional<std::string> project{};
			std::optional<std::string> name{};
			std::optional<std::string> title{};
			std::optional<::neotrac::projects::workflow> workflow{};
			std::optional<std::string> start{};
			std::optional<double> position{};
			std::optional<std::vector<std::string>> followers{};

			std::string json() const {
				std::string out = "{";
				auto add = [&](const char* name, const std::string& value) { out += (out.size() > 1 ? "," : "") + detail::json(std::string(name)) + ":" + value; };
				add("id", detail::json(id));
				if (project) add("project", detail::json(*project));
				if (name) add("name", detail::json(*name));
				if (title) add("title", detail::json(*title));
				if (workflow) add("workflow", detail::json(std::string(to_string(*workflow))));
				if (start) add("start", detail::json(*start));
				if (position) add("position", detail::json(*position));
				if (followers) add("followers", detail::json(*followers));
				return out + "}";
			}
		};

		// What board::follow is sent.
		struct board_follow {
			std::string id{};
			std::optional<std::string> project{};
			std::optional<std::string> name{};
			std::optional<std::string> title{};
			std::optional<::neotrac::projects::workflow> workflow{};
			std::optional<std::string> start{};
			std::optional<double> position{};
			std::optional<std::vector<std::string>> followers{};

			std::string json() const {
				std::string out = "{";
				auto add = [&](const char* name, const std::string& value) { out += (out.size() > 1 ? "," : "") + detail::json(std::string(name)) + ":" + value; };
				add("id", detail::json(id));
				if (project) add("project", detail::json(*project));
				if (name) add("name", detail::json(*name));
				if (title) add("title", detail::json(*title));
				if (workflow) add("workflow", detail::json(std::string(to_string(*workflow))));
				if (start) add("start", detail::json(*start));
				if (position) add("position", detail::json(*position));
				if (followers) add("followers", detail::json(*followers));
				return out + "}";
			}
		};

		// What board::unfollow is sent.
		struct board_unfollow {
			std::string id{};
			std::optional<std::string> project{};
			std::optional<std::string> name{};
			std::optional<std::string> title{};
			std::optional<::neotrac::projects::workflow> workflow{};
			std::optional<std::string> start{};
			std::optional<double> position{};
			std::optional<std::vector<std::string>> followers{};

			std::string json() const {
				std::string out = "{";
				auto add = [&](const char* name, const std::string& value) { out += (out.size() > 1 ? "," : "") + detail::json(std::string(name)) + ":" + value; };
				add("id", detail::json(id));
				if (project) add("project", detail::json(*project));
				if (name) add("name", detail::json(*name));
				if (title) add("title", detail::json(*title));
				if (workflow) add("workflow", detail::json(std::string(to_string(*workflow))));
				if (start) add("start", detail::json(*start));
				if (position) add("position", detail::json(*position));
				if (followers) add("followers", detail::json(*followers));
				return out + "}";
			}
		};

		// What issue::create is sent.
		struct issue_create {
			std::optional<std::string> project{};
			std::optional<double> number{};
			std::optional<std::string> board{};
			std::optional<std::string> title{};
			std::optional<std::string> body{};
			std::optional<std::vector<std::string>> labels{};
			std::optional<std::string> author{};
			std::optional<std::string> phase{};
			std::optional<::neotrac::projects::priority> priority{};
			std::optional<std::vector<std::string>> assignees{};
			std::optional<std::string> milestone{};

			std::string json() const {
				std::string out = "{";
				auto add = [&](const char* name, const std::string& value) { out += (out.size() > 1 ? "," : "") + detail::json(std::string(name)) + ":" + value; };
				if (project) add("project", detail::json(*project));
				if (number) add("number", detail::json(*number));
				if (board) add("board", detail::json(*board));
				if (title) add("title", detail::json(*title));
				if (body) add("body", detail::json(*body));
				if (labels) add("labels", detail::json(*labels));
				if (author) add("author", detail::json(*author));
				if (phase) add("phase", detail::json(*phase));
				if (priority) add("priority", detail::json(std::string(to_string(*priority))));
				if (assignees) add("assignees", detail::json(*assignees));
				if (milestone) add("milestone", detail::json(*milestone));
				return out + "}";
			}
		};

		// What issue::update is sent.
		struct issue_update {
			std::string id{};
			std::optional<std::string> project{};
			std::optional<double> number{};
			std::optional<std::string> board{};
			std::optional<std::string> title{};
			std::optional<std::string> body{};
			std::optional<std::vector<std::string>> labels{};
			std::optional<std::string> author{};
			std::optional<std::string> phase{};
			std::optional<::neotrac::projects::priority> priority{};
			std::optional<std::vector<std::string>> assignees{};
			std::optional<std::string> milestone{};

			std::string json() const {
				std::string out = "{";
				auto add = [&](const char* name, const std::string& value) { out += (out.size() > 1 ? "," : "") + detail::json(std::string(name)) + ":" + value; };
				add("id", detail::json(id));
				if (project) add("project", detail::json(*project));
				if (number) add("number", detail::json(*number));
				if (board) add("board", detail::json(*board));
				if (title) add("title", detail::json(*title));
				if (body) add("body", detail::json(*body));
				if (labels) add("labels", detail::json(*labels));
				if (author) add("author", detail::json(*author));
				if (phase) add("phase", detail::json(*phase));
				if (priority) add("priority", detail::json(std::string(to_string(*priority))));
				if (assignees) add("assignees", detail::json(*assignees));
				if (milestone) add("milestone", detail::json(*milestone));
				return out + "}";
			}
		};

		// What issue::transfer is sent.
		struct issue_transfer {
			std::string id{};
			std::optional<std::string> project{};
			std::optional<double> number{};
			std::optional<std::string> board{};
			std::optional<std::string> title{};
			std::optional<std::string> body{};
			std::optional<std::vector<std::string>> labels{};
			std::optional<std::string> author{};
			std::optional<std::string> phase{};
			std::optional<::neotrac::projects::priority> priority{};
			std::optional<std::vector<std::string>> assignees{};
			std::optional<std::string> milestone{};

			std::string json() const {
				std::string out = "{";
				auto add = [&](const char* name, const std::string& value) { out += (out.size() > 1 ? "," : "") + detail::json(std::string(name)) + ":" + value; };
				add("id", detail::json(id));
				if (project) add("project", detail::json(*project));
				if (number) add("number", detail::json(*number));
				if (board) add("board", detail::json(*board));
				if (title) add("title", detail::json(*title));
				if (body) add("body", detail::json(*body));
				if (labels) add("labels", detail::json(*labels));
				if (author) add("author", detail::json(*author));
				if (phase) add("phase", detail::json(*phase));
				if (priority) add("priority", detail::json(std::string(to_string(*priority))));
				if (assignees) add("assignees", detail::json(*assignees));
				if (milestone) add("milestone", detail::json(*milestone));
				return out + "}";
			}
		};

		// What link::create is sent.
		struct link_create {
			std::optional<std::string> project{};
			std::optional<std::string> from_{};
			std::optional<std::string> to{};
			std::optional<::neotrac::projects::relation> relation{};
			std::optional<std::string> pair{};

			std::string json() const {
				std::string out = "{";
				auto add = [&](const char* name, const std::string& value) { out += (out.size() > 1 ? "," : "") + detail::json(std::string(name)) + ":" + value; };
				if (project) add("project", detail::json(*project));
				if (from_) add("from", detail::json(*from_));
				if (to) add("to", detail::json(*to));
				if (relation) add("relation", detail::json(std::string(to_string(*relation))));
				if (pair) add("pair", detail::json(*pair));
				return out + "}";
			}
		};

		// What link::delete is sent.
		struct link_delete {
			std::string id{};
			std::optional<std::string> project{};
			std::optional<std::string> from_{};
			std::optional<std::string> to{};
			std::optional<::neotrac::projects::relation> relation{};
			std::optional<std::string> pair{};

			std::string json() const {
				std::string out = "{";
				auto add = [&](const char* name, const std::string& value) { out += (out.size() > 1 ? "," : "") + detail::json(std::string(name)) + ":" + value; };
				add("id", detail::json(id));
				if (project) add("project", detail::json(*project));
				if (from_) add("from", detail::json(*from_));
				if (to) add("to", detail::json(*to));
				if (relation) add("relation", detail::json(std::string(to_string(*relation))));
				if (pair) add("pair", detail::json(*pair));
				return out + "}";
			}
		};

		// What issue::move is sent.
		struct issue_move {
			std::string id{};
			std::optional<std::string> project{};
			std::optional<double> number{};
			std::optional<std::string> board{};
			std::optional<std::string> title{};
			std::optional<std::string> body{};
			std::optional<std::vector<std::string>> labels{};
			std::optional<std::string> author{};
			std::optional<std::string> phase{};
			std::optional<::neotrac::projects::priority> priority{};
			std::optional<std::vector<std::string>> assignees{};
			std::optional<std::string> milestone{};

			std::string json() const {
				std::string out = "{";
				auto add = [&](const char* name, const std::string& value) { out += (out.size() > 1 ? "," : "") + detail::json(std::string(name)) + ":" + value; };
				add("id", detail::json(id));
				if (project) add("project", detail::json(*project));
				if (number) add("number", detail::json(*number));
				if (board) add("board", detail::json(*board));
				if (title) add("title", detail::json(*title));
				if (body) add("body", detail::json(*body));
				if (labels) add("labels", detail::json(*labels));
				if (author) add("author", detail::json(*author));
				if (phase) add("phase", detail::json(*phase));
				if (priority) add("priority", detail::json(std::string(to_string(*priority))));
				if (assignees) add("assignees", detail::json(*assignees));
				if (milestone) add("milestone", detail::json(*milestone));
				return out + "}";
			}
		};

		// What issue::claim is sent.
		struct issue_claim {
			std::string id{};
			std::optional<std::string> project{};
			std::optional<double> number{};
			std::optional<std::string> board{};
			std::optional<std::string> title{};
			std::optional<std::string> body{};
			std::optional<std::vector<std::string>> labels{};
			std::optional<std::string> author{};
			std::optional<std::string> phase{};
			std::optional<::neotrac::projects::priority> priority{};
			std::optional<std::vector<std::string>> assignees{};
			std::optional<std::string> milestone{};

			std::string json() const {
				std::string out = "{";
				auto add = [&](const char* name, const std::string& value) { out += (out.size() > 1 ? "," : "") + detail::json(std::string(name)) + ":" + value; };
				add("id", detail::json(id));
				if (project) add("project", detail::json(*project));
				if (number) add("number", detail::json(*number));
				if (board) add("board", detail::json(*board));
				if (title) add("title", detail::json(*title));
				if (body) add("body", detail::json(*body));
				if (labels) add("labels", detail::json(*labels));
				if (author) add("author", detail::json(*author));
				if (phase) add("phase", detail::json(*phase));
				if (priority) add("priority", detail::json(std::string(to_string(*priority))));
				if (assignees) add("assignees", detail::json(*assignees));
				if (milestone) add("milestone", detail::json(*milestone));
				return out + "}";
			}
		};

		// What issue::release is sent.
		struct issue_release {
			std::string id{};
			std::optional<std::string> project{};
			std::optional<double> number{};
			std::optional<std::string> board{};
			std::optional<std::string> title{};
			std::optional<std::string> body{};
			std::optional<std::vector<std::string>> labels{};
			std::optional<std::string> author{};
			std::optional<std::string> phase{};
			std::optional<::neotrac::projects::priority> priority{};
			std::optional<std::vector<std::string>> assignees{};
			std::optional<std::string> milestone{};

			std::string json() const {
				std::string out = "{";
				auto add = [&](const char* name, const std::string& value) { out += (out.size() > 1 ? "," : "") + detail::json(std::string(name)) + ":" + value; };
				add("id", detail::json(id));
				if (project) add("project", detail::json(*project));
				if (number) add("number", detail::json(*number));
				if (board) add("board", detail::json(*board));
				if (title) add("title", detail::json(*title));
				if (body) add("body", detail::json(*body));
				if (labels) add("labels", detail::json(*labels));
				if (author) add("author", detail::json(*author));
				if (phase) add("phase", detail::json(*phase));
				if (priority) add("priority", detail::json(std::string(to_string(*priority))));
				if (assignees) add("assignees", detail::json(*assignees));
				if (milestone) add("milestone", detail::json(*milestone));
				return out + "}";
			}
		};

		// What comment::create is sent.
		struct comment_create {
			std::optional<std::string> issue{};
			std::optional<std::string> body{};
			std::optional<std::string> author{};
			std::optional<std::vector<std::string>> mentioned{};

			std::string json() const {
				std::string out = "{";
				auto add = [&](const char* name, const std::string& value) { out += (out.size() > 1 ? "," : "") + detail::json(std::string(name)) + ":" + value; };
				if (issue) add("issue", detail::json(*issue));
				if (body) add("body", detail::json(*body));
				if (author) add("author", detail::json(*author));
				if (mentioned) add("mentioned", detail::json(*mentioned));
				return out + "}";
			}
		};

		// What milestone::create is sent.
		struct milestone_create {
			std::optional<std::string> project{};
			std::optional<std::string> name{};
			std::optional<std::string> title{};
			std::optional<time> due{};
			std::optional<std::string> description{};

			std::string json() const {
				std::string out = "{";
				auto add = [&](const char* name, const std::string& value) { out += (out.size() > 1 ? "," : "") + detail::json(std::string(name)) + ":" + value; };
				if (project) add("project", detail::json(*project));
				if (name) add("name", detail::json(*name));
				if (title) add("title", detail::json(*title));
				if (due) add("due", detail::json(*due));
				if (description) add("description", detail::json(*description));
				return out + "}";
			}
		};

		// What milestone::update is sent.
		struct milestone_update {
			std::string id{};
			std::optional<std::string> project{};
			std::optional<std::string> name{};
			std::optional<std::string> title{};
			std::optional<time> due{};
			std::optional<std::string> description{};

			std::string json() const {
				std::string out = "{";
				auto add = [&](const char* name, const std::string& value) { out += (out.size() > 1 ? "," : "") + detail::json(std::string(name)) + ":" + value; };
				add("id", detail::json(id));
				if (project) add("project", detail::json(*project));
				if (name) add("name", detail::json(*name));
				if (title) add("title", detail::json(*title));
				if (due) add("due", detail::json(*due));
				if (description) add("description", detail::json(*description));
				return out + "}";
			}
		};

		// What project::create is sent.
		struct project_create {
			std::optional<std::string> slug{};
			std::optional<std::string> name{};
			std::optional<std::string> summary{};
			std::optional<::neotrac::projects::project_visibility> visibility{};
			std::optional<bool> takes_reports{};
			std::optional<::neotrac::projects::workflow> workflow{};
			std::optional<std::string> start{};

			std::string json() const {
				std::string out = "{";
				auto add = [&](const char* name, const std::string& value) { out += (out.size() > 1 ? "," : "") + detail::json(std::string(name)) + ":" + value; };
				if (slug) add("slug", detail::json(*slug));
				if (name) add("name", detail::json(*name));
				if (summary) add("summary", detail::json(*summary));
				if (visibility) add("visibility", detail::json(std::string(to_string(*visibility))));
				if (takes_reports) add("takes_reports", detail::json(*takes_reports));
				if (workflow) add("workflow", detail::json(std::string(to_string(*workflow))));
				if (start) add("start", detail::json(*start));
				return out + "}";
			}
		};

		// What project::update is sent.
		struct project_update {
			std::string id{};
			std::optional<std::string> slug{};
			std::optional<std::string> name{};
			std::optional<std::string> summary{};
			std::optional<::neotrac::projects::project_visibility> visibility{};
			std::optional<bool> takes_reports{};
			std::optional<::neotrac::projects::workflow> workflow{};
			std::optional<std::string> start{};

			std::string json() const {
				std::string out = "{";
				auto add = [&](const char* name, const std::string& value) { out += (out.size() > 1 ? "," : "") + detail::json(std::string(name)) + ":" + value; };
				add("id", detail::json(id));
				if (slug) add("slug", detail::json(*slug));
				if (name) add("name", detail::json(*name));
				if (summary) add("summary", detail::json(*summary));
				if (visibility) add("visibility", detail::json(std::string(to_string(*visibility))));
				if (takes_reports) add("takes_reports", detail::json(*takes_reports));
				if (workflow) add("workflow", detail::json(std::string(to_string(*workflow))));
				if (start) add("start", detail::json(*start));
				return out + "}";
			}
		};

		// What member::create is sent.
		struct member_create {
			std::optional<std::string> project{};
			std::optional<std::string> person{};
			std::optional<std::string> role{};

			std::string json() const {
				std::string out = "{";
				auto add = [&](const char* name, const std::string& value) { out += (out.size() > 1 ? "," : "") + detail::json(std::string(name)) + ":" + value; };
				if (project) add("project", detail::json(*project));
				if (person) add("person", detail::json(*person));
				if (role) add("role", detail::json(*role));
				return out + "}";
			}
		};

		// What member::delete is sent.
		struct member_delete {
			std::string id{};
			std::optional<std::string> project{};
			std::optional<std::string> person{};
			std::optional<std::string> role{};

			std::string json() const {
				std::string out = "{";
				auto add = [&](const char* name, const std::string& value) { out += (out.size() > 1 ? "," : "") + detail::json(std::string(name)) + ":" + value; };
				add("id", detail::json(id));
				if (project) add("project", detail::json(*project));
				if (person) add("person", detail::json(*person));
				if (role) add("role", detail::json(*role));
				return out + "}";
			}
		};

		// What role::create is sent.
		struct role_create {
			std::optional<std::string> project{};
			std::optional<std::string> name{};
			std::optional<std::string> title{};
			std::optional<std::vector<std::string>> may{};

			std::string json() const {
				std::string out = "{";
				auto add = [&](const char* name, const std::string& value) { out += (out.size() > 1 ? "," : "") + detail::json(std::string(name)) + ":" + value; };
				if (project) add("project", detail::json(*project));
				if (name) add("name", detail::json(*name));
				if (title) add("title", detail::json(*title));
				if (may) add("may", detail::json(*may));
				return out + "}";
			}
		};

		// What role::update is sent.
		struct role_update {
			std::string id{};
			std::optional<std::string> project{};
			std::optional<std::string> name{};
			std::optional<std::string> title{};
			std::optional<std::vector<std::string>> may{};

			std::string json() const {
				std::string out = "{";
				auto add = [&](const char* name, const std::string& value) { out += (out.size() > 1 ? "," : "") + detail::json(std::string(name)) + ":" + value; };
				add("id", detail::json(id));
				if (project) add("project", detail::json(*project));
				if (name) add("name", detail::json(*name));
				if (title) add("title", detail::json(*title));
				if (may) add("may", detail::json(*may));
				return out + "}";
			}
		};

		// What role::delete is sent.
		struct role_delete {
			std::string id{};
			std::optional<std::string> project{};
			std::optional<std::string> name{};
			std::optional<std::string> title{};
			std::optional<std::vector<std::string>> may{};

			std::string json() const {
				std::string out = "{";
				auto add = [&](const char* name, const std::string& value) { out += (out.size() > 1 ? "," : "") + detail::json(std::string(name)) + ":" + value; };
				add("id", detail::json(id));
				if (project) add("project", detail::json(*project));
				if (name) add("name", detail::json(*name));
				if (title) add("title", detail::json(*title));
				if (may) add("may", detail::json(*may));
				return out + "}";
			}
		};

		// What reader::create is sent.
		struct reader_create {
			std::optional<std::string> person{};
			std::optional<time> seen_at{};

			std::string json() const {
				std::string out = "{";
				auto add = [&](const char* name, const std::string& value) { out += (out.size() > 1 ? "," : "") + detail::json(std::string(name)) + ":" + value; };
				if (person) add("person", detail::json(*person));
				if (seen_at) add("seen_at", detail::json(*seen_at));
				return out + "}";
			}
		};

		// What invitation::create is sent.
		struct invitation_create {
			std::optional<std::string> project{};
			std::optional<std::string> email{};
			std::optional<std::string> role{};

			std::string json() const {
				std::string out = "{";
				auto add = [&](const char* name, const std::string& value) { out += (out.size() > 1 ? "," : "") + detail::json(std::string(name)) + ":" + value; };
				if (project) add("project", detail::json(*project));
				if (email) add("email", detail::json(*email));
				if (role) add("role", detail::json(*role));
				return out + "}";
			}
		};

		// What invitation::delete is sent.
		struct invitation_delete {
			std::string id{};
			std::optional<std::string> project{};
			std::optional<std::string> email{};
			std::optional<std::string> role{};

			std::string json() const {
				std::string out = "{";
				auto add = [&](const char* name, const std::string& value) { out += (out.size() > 1 ? "," : "") + detail::json(std::string(name)) + ":" + value; };
				add("id", detail::json(id));
				if (project) add("project", detail::json(*project));
				if (email) add("email", detail::json(*email));
				if (role) add("role", detail::json(*role));
				return out + "}";
			}
		};

		// What report::create is sent.
		struct report_create {
			std::optional<std::string> project{};
			std::optional<std::string> title{};
			std::optional<std::string> body{};
			std::optional<std::string> author{};
			std::optional<::neotrac::projects::report_status> status{};

			std::string json() const {
				std::string out = "{";
				auto add = [&](const char* name, const std::string& value) { out += (out.size() > 1 ? "," : "") + detail::json(std::string(name)) + ":" + value; };
				if (project) add("project", detail::json(*project));
				if (title) add("title", detail::json(*title));
				if (body) add("body", detail::json(*body));
				if (author) add("author", detail::json(*author));
				if (status) add("status", detail::json(std::string(to_string(*status))));
				return out + "}";
			}
		};

		// What report::resolve is sent.
		struct report_resolve {
			std::string id{};
			std::optional<std::string> project{};
			std::optional<std::string> title{};
			std::optional<std::string> body{};
			std::optional<std::string> author{};
			std::optional<::neotrac::projects::report_status> status{};

			std::string json() const {
				std::string out = "{";
				auto add = [&](const char* name, const std::string& value) { out += (out.size() > 1 ? "," : "") + detail::json(std::string(name)) + ":" + value; };
				add("id", detail::json(id));
				if (project) add("project", detail::json(*project));
				if (title) add("title", detail::json(*title));
				if (body) add("body", detail::json(*body));
				if (author) add("author", detail::json(*author));
				if (status) add("status", detail::json(std::string(to_string(*status))));
				return out + "}";
			}
		};

		// What reply::create is sent.
		struct reply_create {
			std::optional<std::string> report{};
			std::optional<std::string> body{};
			std::optional<std::string> author{};

			std::string json() const {
				std::string out = "{";
				auto add = [&](const char* name, const std::string& value) { out += (out.size() > 1 ? "," : "") + detail::json(std::string(name)) + ":" + value; };
				if (report) add("report", detail::json(*report));
				if (body) add("body", detail::json(*body));
				if (author) add("author", detail::json(*author));
				return out + "}";
			}
		};

		// What service::create is sent.
		struct service_create {
			std::optional<std::string> project{};
			std::optional<std::string> name{};
			std::optional<std::string> title{};
			std::optional<std::string> role{};
			std::optional<std::string> key_start{};
			std::optional<time> key_made{};
			std::optional<time> key_used{};

			std::string json() const {
				std::string out = "{";
				auto add = [&](const char* name, const std::string& value) { out += (out.size() > 1 ? "," : "") + detail::json(std::string(name)) + ":" + value; };
				if (project) add("project", detail::json(*project));
				if (name) add("name", detail::json(*name));
				if (title) add("title", detail::json(*title));
				if (role) add("role", detail::json(*role));
				if (key_start) add("key_start", detail::json(*key_start));
				if (key_made) add("key_made", detail::json(*key_made));
				if (key_used) add("key_used", detail::json(*key_used));
				return out + "}";
			}
		};

		// What service::update is sent.
		struct service_update {
			std::string id{};
			std::optional<std::string> project{};
			std::optional<std::string> name{};
			std::optional<std::string> title{};
			std::optional<std::string> role{};
			std::optional<std::string> key_start{};
			std::optional<time> key_made{};
			std::optional<time> key_used{};

			std::string json() const {
				std::string out = "{";
				auto add = [&](const char* name, const std::string& value) { out += (out.size() > 1 ? "," : "") + detail::json(std::string(name)) + ":" + value; };
				add("id", detail::json(id));
				if (project) add("project", detail::json(*project));
				if (name) add("name", detail::json(*name));
				if (title) add("title", detail::json(*title));
				if (role) add("role", detail::json(*role));
				if (key_start) add("key_start", detail::json(*key_start));
				if (key_made) add("key_made", detail::json(*key_made));
				if (key_used) add("key_used", detail::json(*key_used));
				return out + "}";
			}
		};

		// What service::delete is sent.
		struct service_delete {
			std::string id{};
			std::optional<std::string> project{};
			std::optional<std::string> name{};
			std::optional<std::string> title{};
			std::optional<std::string> role{};
			std::optional<std::string> key_start{};
			std::optional<time> key_made{};
			std::optional<time> key_used{};

			std::string json() const {
				std::string out = "{";
				auto add = [&](const char* name, const std::string& value) { out += (out.size() > 1 ? "," : "") + detail::json(std::string(name)) + ":" + value; };
				add("id", detail::json(id));
				if (project) add("project", detail::json(*project));
				if (name) add("name", detail::json(*name));
				if (title) add("title", detail::json(*title));
				if (role) add("role", detail::json(*role));
				if (key_start) add("key_start", detail::json(*key_start));
				if (key_made) add("key_made", detail::json(*key_made));
				if (key_used) add("key_used", detail::json(*key_used));
				return out + "}";
			}
		};

		// What service::key is sent.
		struct service_key {
			std::string id{};
			std::optional<std::string> project{};
			std::optional<std::string> name{};
			std::optional<std::string> title{};
			std::optional<std::string> role{};
			std::optional<std::string> key_start{};
			std::optional<time> key_made{};
			std::optional<time> key_used{};

			std::string json() const {
				std::string out = "{";
				auto add = [&](const char* name, const std::string& value) { out += (out.size() > 1 ? "," : "") + detail::json(std::string(name)) + ":" + value; };
				add("id", detail::json(id));
				if (project) add("project", detail::json(*project));
				if (name) add("name", detail::json(*name));
				if (title) add("title", detail::json(*title));
				if (role) add("role", detail::json(*role));
				if (key_start) add("key_start", detail::json(*key_start));
				if (key_made) add("key_made", detail::json(*key_made));
				if (key_used) add("key_used", detail::json(*key_used));
				return out + "}";
			}
		};

		// What service::revoke is sent.
		struct service_revoke {
			std::string id{};
			std::optional<std::string> project{};
			std::optional<std::string> name{};
			std::optional<std::string> title{};
			std::optional<std::string> role{};
			std::optional<std::string> key_start{};
			std::optional<time> key_made{};
			std::optional<time> key_used{};

			std::string json() const {
				std::string out = "{";
				auto add = [&](const char* name, const std::string& value) { out += (out.size() > 1 ? "," : "") + detail::json(std::string(name)) + ":" + value; };
				add("id", detail::json(id));
				if (project) add("project", detail::json(*project));
				if (name) add("name", detail::json(*name));
				if (title) add("title", detail::json(*title));
				if (role) add("role", detail::json(*role));
				if (key_start) add("key_start", detail::json(*key_start));
				if (key_made) add("key_made", detail::json(*key_made));
				if (key_used) add("key_used", detail::json(*key_used));
				return out + "}";
			}
		};

		// What page::create is sent.
		struct page_create {
			std::optional<std::string> project{};
			std::optional<std::string> name{};
			std::optional<std::string> title{};
			std::optional<std::string> body{};
			std::optional<double> position{};

			std::string json() const {
				std::string out = "{";
				auto add = [&](const char* name, const std::string& value) { out += (out.size() > 1 ? "," : "") + detail::json(std::string(name)) + ":" + value; };
				if (project) add("project", detail::json(*project));
				if (name) add("name", detail::json(*name));
				if (title) add("title", detail::json(*title));
				if (body) add("body", detail::json(*body));
				if (position) add("position", detail::json(*position));
				return out + "}";
			}
		};

		// What page::update is sent.
		struct page_update {
			std::string id{};
			std::optional<std::string> project{};
			std::optional<std::string> name{};
			std::optional<std::string> title{};
			std::optional<std::string> body{};
			std::optional<double> position{};

			std::string json() const {
				std::string out = "{";
				auto add = [&](const char* name, const std::string& value) { out += (out.size() > 1 ? "," : "") + detail::json(std::string(name)) + ":" + value; };
				add("id", detail::json(id));
				if (project) add("project", detail::json(*project));
				if (name) add("name", detail::json(*name));
				if (title) add("title", detail::json(*title));
				if (body) add("body", detail::json(*body));
				if (position) add("position", detail::json(*position));
				return out + "}";
			}
		};

		// What phase::create is sent.
		struct phase_create {
			std::optional<std::string> board{};
			std::optional<std::string> name{};
			std::optional<std::string> project{};
			std::optional<std::string> title{};
			std::optional<double> position{};
			std::optional<std::vector<std::string>> worked_by{};

			std::string json() const {
				std::string out = "{";
				auto add = [&](const char* name, const std::string& value) { out += (out.size() > 1 ? "," : "") + detail::json(std::string(name)) + ":" + value; };
				if (board) add("board", detail::json(*board));
				if (name) add("name", detail::json(*name));
				if (project) add("project", detail::json(*project));
				if (title) add("title", detail::json(*title));
				if (position) add("position", detail::json(*position));
				if (worked_by) add("worked_by", detail::json(*worked_by));
				return out + "}";
			}
		};

		// What phase::update is sent.
		struct phase_update {
			std::string id{};
			std::optional<std::string> board{};
			std::optional<std::string> name{};
			std::optional<std::string> project{};
			std::optional<std::string> title{};
			std::optional<double> position{};
			std::optional<std::vector<std::string>> worked_by{};

			std::string json() const {
				std::string out = "{";
				auto add = [&](const char* name, const std::string& value) { out += (out.size() > 1 ? "," : "") + detail::json(std::string(name)) + ":" + value; };
				add("id", detail::json(id));
				if (board) add("board", detail::json(*board));
				if (name) add("name", detail::json(*name));
				if (project) add("project", detail::json(*project));
				if (title) add("title", detail::json(*title));
				if (position) add("position", detail::json(*position));
				if (worked_by) add("worked_by", detail::json(*worked_by));
				return out + "}";
			}
		};

		// What phase::delete is sent.
		struct phase_delete {
			std::string id{};
			std::optional<std::string> board{};
			std::optional<std::string> name{};
			std::optional<std::string> project{};
			std::optional<std::string> title{};
			std::optional<double> position{};
			std::optional<std::vector<std::string>> worked_by{};
			std::optional<std::string> into{};

			std::string json() const {
				std::string out = "{";
				auto add = [&](const char* name, const std::string& value) { out += (out.size() > 1 ? "," : "") + detail::json(std::string(name)) + ":" + value; };
				add("id", detail::json(id));
				if (board) add("board", detail::json(*board));
				if (name) add("name", detail::json(*name));
				if (project) add("project", detail::json(*project));
				if (title) add("title", detail::json(*title));
				if (position) add("position", detail::json(*position));
				if (worked_by) add("worked_by", detail::json(*worked_by));
				if (into) add("into", detail::json(*into));
				return out + "}";
			}
		};

		// What step::create is sent.
		struct step_create {
			std::optional<std::string> board{};
			std::optional<std::string> from_{};
			std::optional<std::string> to{};
			std::optional<std::string> project{};
			std::optional<std::string> title{};
			std::optional<std::vector<std::string>> roles{};

			std::string json() const {
				std::string out = "{";
				auto add = [&](const char* name, const std::string& value) { out += (out.size() > 1 ? "," : "") + detail::json(std::string(name)) + ":" + value; };
				if (board) add("board", detail::json(*board));
				if (from_) add("from", detail::json(*from_));
				if (to) add("to", detail::json(*to));
				if (project) add("project", detail::json(*project));
				if (title) add("title", detail::json(*title));
				if (roles) add("roles", detail::json(*roles));
				return out + "}";
			}
		};

		// What step::update is sent.
		struct step_update {
			std::string id{};
			std::optional<std::string> board{};
			std::optional<std::string> from_{};
			std::optional<std::string> to{};
			std::optional<std::string> project{};
			std::optional<std::string> title{};
			std::optional<std::vector<std::string>> roles{};

			std::string json() const {
				std::string out = "{";
				auto add = [&](const char* name, const std::string& value) { out += (out.size() > 1 ? "," : "") + detail::json(std::string(name)) + ":" + value; };
				add("id", detail::json(id));
				if (board) add("board", detail::json(*board));
				if (from_) add("from", detail::json(*from_));
				if (to) add("to", detail::json(*to));
				if (project) add("project", detail::json(*project));
				if (title) add("title", detail::json(*title));
				if (roles) add("roles", detail::json(*roles));
				return out + "}";
			}
		};

		// What step::delete is sent.
		struct step_delete {
			std::string id{};
			std::optional<std::string> board{};
			std::optional<std::string> from_{};
			std::optional<std::string> to{};
			std::optional<std::string> project{};
			std::optional<std::string> title{};
			std::optional<std::vector<std::string>> roles{};

			std::string json() const {
				std::string out = "{";
				auto add = [&](const char* name, const std::string& value) { out += (out.size() > 1 ? "," : "") + detail::json(std::string(name)) + ":" + value; };
				add("id", detail::json(id));
				if (board) add("board", detail::json(*board));
				if (from_) add("from", detail::json(*from_));
				if (to) add("to", detail::json(*to));
				if (project) add("project", detail::json(*project));
				if (title) add("title", detail::json(*title));
				if (roles) add("roles", detail::json(*roles));
				return out + "}";
			}
		};

		// What board::create is sent.
		struct board_create {
			std::optional<std::string> project{};
			std::optional<std::string> name{};
			std::optional<std::string> title{};
			std::optional<::neotrac::projects::workflow> workflow{};
			std::optional<std::string> start{};
			std::optional<double> position{};
			std::optional<std::vector<std::string>> followers{};

			std::string json() const {
				std::string out = "{";
				auto add = [&](const char* name, const std::string& value) { out += (out.size() > 1 ? "," : "") + detail::json(std::string(name)) + ":" + value; };
				if (project) add("project", detail::json(*project));
				if (name) add("name", detail::json(*name));
				if (title) add("title", detail::json(*title));
				if (workflow) add("workflow", detail::json(std::string(to_string(*workflow))));
				if (start) add("start", detail::json(*start));
				if (position) add("position", detail::json(*position));
				if (followers) add("followers", detail::json(*followers));
				return out + "}";
			}
		};

		struct board_page {
			bool exists = false;  // whether it's been made, and may be read
			std::string title{};
			std::string start{};
			std::vector<std::string> followers{};
			std::string project_name{};
			struct issues_row {
				std::string id{};
				double number{};
				std::string title{};
				std::vector<std::string> labels{};
				std::string phase{};
				std::string phase_title{};
				::neotrac::projects::priority priority{};
				time updated_at{};
				std::string author{};
				std::string author_picture{};
				std::string author_name{};
				std::vector<std::string> assignees{};
				std::vector<std::string> assignees_name{};
				static issues_row from(const ember::value& v) {
					issues_row out;
					out.id = detail::text(v["id"]);
					out.number = detail::number(v["number"]);
					out.title = detail::text(v["title"]);
					out.labels = detail::texts(v["labels"]);
					out.phase = detail::text(v["phase"]);
					out.phase_title = detail::text(v["phase.title"]);
					out.priority = detail::choice<::neotrac::projects::priority>(v["priority"]);
					out.updated_at = detail::when(v["updated_at"]);
					out.author = detail::text(v["author"]);
					out.author_picture = detail::text(v["author.picture"]);
					out.author_name = detail::text(v["author.name"]);
					out.assignees = detail::texts(v["assignees"]);
					out.assignees_name = detail::texts(v["assignees.name"]);
					return out;
				}
			};
			std::vector<issues_row> issues{};
			struct phases_row {
				std::string id{};
				std::string name{};
				std::string title{};
				double position{};
				std::vector<std::string> worked_by{};
				std::vector<std::string> worked_by_title{};
				static phases_row from(const ember::value& v) {
					phases_row out;
					out.id = detail::text(v["id"]);
					out.name = detail::text(v["name"]);
					out.title = detail::text(v["title"]);
					out.position = detail::number(v["position"]);
					out.worked_by = detail::texts(v["worked_by"]);
					out.worked_by_title = detail::texts(v["worked_by.title"]);
					return out;
				}
			};
			std::vector<phases_row> phases{};
			struct steps_row {
				std::string id{};
				std::string from_{};
				std::string to{};
				std::string from_title{};
				double from_position{};
				std::string to_title{};
				double to_position{};
				std::string title{};
				std::vector<std::string> roles{};
				std::vector<std::string> roles_title{};
				static steps_row from(const ember::value& v) {
					steps_row out;
					out.id = detail::text(v["id"]);
					out.from_ = detail::text(v["from"]);
					out.to = detail::text(v["to"]);
					out.from_title = detail::text(v["from.title"]);
					out.from_position = detail::number(v["from.position"]);
					out.to_title = detail::text(v["to.title"]);
					out.to_position = detail::number(v["to.position"]);
					out.title = detail::text(v["title"]);
					out.roles = detail::texts(v["roles"]);
					out.roles_title = detail::texts(v["roles.title"]);
					return out;
				}
			};
			std::vector<steps_row> steps{};

			static struct board_page from(const ember::snapshot& s) {
				struct board_page out;
				out.exists = s.exists;
				const ember::value& v = s.data;
				out.title = detail::text(v["title"]);
				out.start = detail::text(v["start"]);
				out.followers = detail::texts(v["followers"]);
				out.project_name = detail::text(v["project_name"]);
				if (v["issues"].is_array()) {
					for (const auto& row : v["issues"].as_array()) out.issues.push_back(issues_row::from(row));
				}
				if (v["phases"].is_array()) {
					for (const auto& row : v["phases"].as_array()) out.phases.push_back(phases_row::from(row));
				}
				if (v["steps"].is_array()) {
					for (const auto& row : v["steps"].as_array()) out.steps.push_back(steps_row::from(row));
				}
				return out;
			}
			static std::string path(const std::string& of) { return "views/projects::board_page:" + of; }
		};

		struct issue_page {
			bool exists = false;  // whether it's been made, and may be read
			double number{};
			std::string title{};
			std::string body{};
			std::vector<std::string> labels{};
			std::string phase{};
			std::string phase_title{};
			::neotrac::projects::priority priority{};
			std::vector<std::string> assignees{};
			std::vector<std::string> assigned{};
			std::string milestone{};
			std::string milestone_title{};
			std::string board{};
			std::string board_title{};
			struct links_row {
				std::string id{};
				::neotrac::projects::relation relation{};
				std::string to{};
				double to_number{};
				std::string to_title{};
				static links_row from(const ember::value& v) {
					links_row out;
					out.id = detail::text(v["id"]);
					out.relation = detail::choice<::neotrac::projects::relation>(v["relation"]);
					out.to = detail::text(v["to"]);
					out.to_number = detail::number(v["to.number"]);
					out.to_title = detail::text(v["to.title"]);
					return out;
				}
			};
			std::vector<links_row> links{};
			struct steps_row {
				std::string id{};
				std::string from_{};
				std::string to{};
				std::string from_title{};
				std::string to_title{};
				std::string title{};
				std::vector<std::string> roles{};
				static steps_row from(const ember::value& v) {
					steps_row out;
					out.id = detail::text(v["id"]);
					out.from_ = detail::text(v["from"]);
					out.to = detail::text(v["to"]);
					out.from_title = detail::text(v["from.title"]);
					out.to_title = detail::text(v["to.title"]);
					out.title = detail::text(v["title"]);
					out.roles = detail::texts(v["roles"]);
					return out;
				}
			};
			std::vector<steps_row> steps{};
			struct comments_row {
				std::string id{};
				std::string author_picture{};
				std::string author_name{};
				std::string body{};
				time created_at{};
				static comments_row from(const ember::value& v) {
					comments_row out;
					out.id = detail::text(v["id"]);
					out.author_picture = detail::text(v["author.picture"]);
					out.author_name = detail::text(v["author.name"]);
					out.body = detail::text(v["body"]);
					out.created_at = detail::when(v["created_at"]);
					return out;
				}
			};
			std::vector<comments_row> comments{};
			struct history_row {
				std::string id{};
				ember::value field{};
				ember::value before{};
				ember::value after{};
				ember::value action{};
				ember::value created_by_name{};
				ember::value created_at{};
				static history_row from(const ember::value& v) {
					history_row out;
					out.id = detail::text(v["id"]);
					out.field = v["field"];
					out.before = v["before"];
					out.after = v["after"];
					out.action = v["action"];
					out.created_by_name = v["created_by.name"];
					out.created_at = v["created_at"];
					return out;
				}
			};
			std::vector<history_row> history{};

			static struct issue_page from(const ember::snapshot& s) {
				struct issue_page out;
				out.exists = s.exists;
				const ember::value& v = s.data;
				out.number = detail::number(v["number"]);
				out.title = detail::text(v["title"]);
				out.body = detail::text(v["body"]);
				out.labels = detail::texts(v["labels"]);
				out.phase = detail::text(v["phase"]);
				out.phase_title = detail::text(v["phase_title"]);
				out.priority = detail::choice<::neotrac::projects::priority>(v["priority"]);
				out.assignees = detail::texts(v["assignees"]);
				out.assigned = detail::texts(v["assigned"]);
				out.milestone = detail::text(v["milestone"]);
				out.milestone_title = detail::text(v["milestone_title"]);
				out.board = detail::text(v["board"]);
				out.board_title = detail::text(v["board_title"]);
				if (v["links"].is_array()) {
					for (const auto& row : v["links"].as_array()) out.links.push_back(links_row::from(row));
				}
				if (v["steps"].is_array()) {
					for (const auto& row : v["steps"].as_array()) out.steps.push_back(steps_row::from(row));
				}
				if (v["comments"].is_array()) {
					for (const auto& row : v["comments"].as_array()) out.comments.push_back(comments_row::from(row));
				}
				if (v["history"].is_array()) {
					for (const auto& row : v["history"].as_array()) out.history.push_back(history_row::from(row));
				}
				return out;
			}
			static std::string path(const std::string& of) { return "views/projects::issue_page:" + of; }
		};

		struct milestone_page {
			bool exists = false;  // whether it's been made, and may be read
			std::string title{};
			time due{};
			std::string description{};
			struct issues_row {
				std::string id{};
				double number{};
				std::string title{};
				std::string phase_title{};
				::neotrac::projects::priority priority{};
				std::vector<std::string> assignees_name{};
				static issues_row from(const ember::value& v) {
					issues_row out;
					out.id = detail::text(v["id"]);
					out.number = detail::number(v["number"]);
					out.title = detail::text(v["title"]);
					out.phase_title = detail::text(v["phase.title"]);
					out.priority = detail::choice<::neotrac::projects::priority>(v["priority"]);
					out.assignees_name = detail::texts(v["assignees.name"]);
					return out;
				}
			};
			std::vector<issues_row> issues{};

			static struct milestone_page from(const ember::snapshot& s) {
				struct milestone_page out;
				out.exists = s.exists;
				const ember::value& v = s.data;
				out.title = detail::text(v["title"]);
				out.due = detail::when(v["due"]);
				out.description = detail::text(v["description"]);
				if (v["issues"].is_array()) {
					for (const auto& row : v["issues"].as_array()) out.issues.push_back(issues_row::from(row));
				}
				return out;
			}
			static std::string path(const std::string& of) { return "views/projects::milestone_page:" + of; }
		};

		struct all {
			bool exists = false;  // whether it's been made, and may be read
			struct rows_row {
				std::string id{};
				std::string slug{};
				std::string name{};
				static rows_row from(const ember::value& v) {
					rows_row out;
					out.id = detail::text(v["id"]);
					out.slug = detail::text(v["slug"]);
					out.name = detail::text(v["name"]);
					return out;
				}
			};
			std::vector<rows_row> rows{};

			static struct all from(const ember::snapshot& s) {
				struct all out;
				out.exists = s.exists;
				const ember::value& v = s.data;
				if (v["rows"].is_array()) {
					for (const auto& row : v["rows"].as_array()) out.rows.push_back(rows_row::from(row));
				}
				return out;
			}
			static std::string path() { return "views/projects::all"; }
		};

		struct assigned {
			bool exists = false;  // whether it's been made, and may be read
			struct rows_row {
				std::string id{};
				std::string project{};
				double number{};
				std::string title{};
				std::string project_name{};
				std::string phase_title{};
				static rows_row from(const ember::value& v) {
					rows_row out;
					out.id = detail::text(v["id"]);
					out.project = detail::text(v["project"]);
					out.number = detail::number(v["number"]);
					out.title = detail::text(v["title"]);
					out.project_name = detail::text(v["project.name"]);
					out.phase_title = detail::text(v["phase.title"]);
					return out;
				}
			};
			std::vector<rows_row> rows{};

			static struct assigned from(const ember::snapshot& s) {
				struct assigned out;
				out.exists = s.exists;
				const ember::value& v = s.data;
				if (v["rows"].is_array()) {
					for (const auto& row : v["rows"].as_array()) out.rows.push_back(rows_row::from(row));
				}
				return out;
			}
			static std::string path(const std::string& of) { return "views/projects::assigned:" + of; }
		};

		struct news {
			bool exists = false;  // whether it's been made, and may be read
			ember::value seen{};
			ember::value unread{};
			struct changes_row {
				std::string id{};
				ember::value project{};
				ember::value project_name{};
				ember::value issue{};
				ember::value issue_number{};
				ember::value issue_title{};
				ember::value field{};
				ember::value before{};
				ember::value after{};
				ember::value action{};
				ember::value created_by_name{};
				ember::value created_at{};
				static changes_row from(const ember::value& v) {
					changes_row out;
					out.id = detail::text(v["id"]);
					out.project = v["project"];
					out.project_name = v["project.name"];
					out.issue = v["issue"];
					out.issue_number = v["issue.number"];
					out.issue_title = v["issue.title"];
					out.field = v["field"];
					out.before = v["before"];
					out.after = v["after"];
					out.action = v["action"];
					out.created_by_name = v["created_by.name"];
					out.created_at = v["created_at"];
					return out;
				}
			};
			std::vector<changes_row> changes{};

			static struct news from(const ember::snapshot& s) {
				struct news out;
				out.exists = s.exists;
				const ember::value& v = s.data;
				out.seen = v["seen"];
				out.unread = v["unread"];
				if (v["changes"].is_array()) {
					for (const auto& row : v["changes"].as_array()) out.changes.push_back(changes_row::from(row));
				}
				return out;
			}
			static std::string path(const std::string& of) { return "views/projects::news:" + of; }
		};

		struct followed {
			bool exists = false;  // whether it's been made, and may be read
			struct rows_row {
				std::string id{};
				std::string project{};
				std::string title{};
				std::string project_name{};
				static rows_row from(const ember::value& v) {
					rows_row out;
					out.id = detail::text(v["id"]);
					out.project = detail::text(v["project"]);
					out.title = detail::text(v["title"]);
					out.project_name = detail::text(v["project.name"]);
					return out;
				}
			};
			std::vector<rows_row> rows{};

			static struct followed from(const ember::snapshot& s) {
				struct followed out;
				out.exists = s.exists;
				const ember::value& v = s.data;
				if (v["rows"].is_array()) {
					for (const auto& row : v["rows"].as_array()) out.rows.push_back(rows_row::from(row));
				}
				return out;
			}
			static std::string path(const std::string& of) { return "views/projects::followed:" + of; }
		};

		struct mine {
			bool exists = false;  // whether it's been made, and may be read
			struct rows_row {
				std::string id{};
				std::string project{};
				std::string project_name{};
				std::string role{};
				std::string role_title{};
				static rows_row from(const ember::value& v) {
					rows_row out;
					out.id = detail::text(v["id"]);
					out.project = detail::text(v["project"]);
					out.project_name = detail::text(v["project.name"]);
					out.role = detail::text(v["role"]);
					out.role_title = detail::text(v["role.title"]);
					return out;
				}
			};
			std::vector<rows_row> rows{};

			static struct mine from(const ember::snapshot& s) {
				struct mine out;
				out.exists = s.exists;
				const ember::value& v = s.data;
				if (v["rows"].is_array()) {
					for (const auto& row : v["rows"].as_array()) out.rows.push_back(rows_row::from(row));
				}
				return out;
			}
			static std::string path(const std::string& of) { return "views/projects::mine:" + of; }
		};

		struct project_page {
			bool exists = false;  // whether it's been made, and may be read
			std::string name{};
			std::string summary{};
			::neotrac::projects::project_visibility visibility{};
			bool takes_reports{};
			struct boards_row {
				std::string id{};
				std::string name{};
				std::string title{};
				static boards_row from(const ember::value& v) {
					boards_row out;
					out.id = detail::text(v["id"]);
					out.name = detail::text(v["name"]);
					out.title = detail::text(v["title"]);
					return out;
				}
			};
			std::vector<boards_row> boards{};
			struct issues_row {
				std::string id{};
				double number{};
				std::string title{};
				std::vector<std::string> labels{};
				std::string board{};
				std::string milestone{};
				std::string phase{};
				std::string phase_title{};
				double phase_position{};
				static issues_row from(const ember::value& v) {
					issues_row out;
					out.id = detail::text(v["id"]);
					out.number = detail::number(v["number"]);
					out.title = detail::text(v["title"]);
					out.labels = detail::texts(v["labels"]);
					out.board = detail::text(v["board"]);
					out.milestone = detail::text(v["milestone"]);
					out.phase = detail::text(v["phase"]);
					out.phase_title = detail::text(v["phase.title"]);
					out.phase_position = detail::number(v["phase.position"]);
					return out;
				}
			};
			std::vector<issues_row> issues{};
			struct milestones_row {
				std::string id{};
				std::string name{};
				std::string title{};
				time due{};
				static milestones_row from(const ember::value& v) {
					milestones_row out;
					out.id = detail::text(v["id"]);
					out.name = detail::text(v["name"]);
					out.title = detail::text(v["title"]);
					out.due = detail::when(v["due"]);
					return out;
				}
			};
			std::vector<milestones_row> milestones{};
			struct pages_row {
				std::string id{};
				std::string name{};
				std::string title{};
				double position{};
				time updated_at{};
				static pages_row from(const ember::value& v) {
					pages_row out;
					out.id = detail::text(v["id"]);
					out.name = detail::text(v["name"]);
					out.title = detail::text(v["title"]);
					out.position = detail::number(v["position"]);
					out.updated_at = detail::when(v["updated_at"]);
					return out;
				}
			};
			std::vector<pages_row> pages{};
			struct members_row {
				std::string id{};
				std::string person{};
				std::string person_picture{};
				std::string person_name{};
				std::string role_title{};
				static members_row from(const ember::value& v) {
					members_row out;
					out.id = detail::text(v["id"]);
					out.person = detail::text(v["person"]);
					out.person_picture = detail::text(v["person.picture"]);
					out.person_name = detail::text(v["person.name"]);
					out.role_title = detail::text(v["role.title"]);
					return out;
				}
			};
			std::vector<members_row> members{};
			struct roles_row {
				std::string id{};
				std::string name{};
				std::string title{};
				std::vector<std::string> may{};
				static roles_row from(const ember::value& v) {
					roles_row out;
					out.id = detail::text(v["id"]);
					out.name = detail::text(v["name"]);
					out.title = detail::text(v["title"]);
					out.may = detail::texts(v["may"]);
					return out;
				}
			};
			std::vector<roles_row> roles{};
			struct timeline_row {
				std::string id{};
				ember::value issue{};
				ember::value issue_number{};
				ember::value issue_title{};
				ember::value field{};
				ember::value before{};
				ember::value after{};
				ember::value action{};
				ember::value created_by_name{};
				ember::value created_at{};
				static timeline_row from(const ember::value& v) {
					timeline_row out;
					out.id = detail::text(v["id"]);
					out.issue = v["issue"];
					out.issue_number = v["issue.number"];
					out.issue_title = v["issue.title"];
					out.field = v["field"];
					out.before = v["before"];
					out.after = v["after"];
					out.action = v["action"];
					out.created_by_name = v["created_by.name"];
					out.created_at = v["created_at"];
					return out;
				}
			};
			std::vector<timeline_row> timeline{};
			struct recent_row {
				std::string id{};
				ember::value issue{};
				ember::value issue_number{};
				ember::value issue_title{};
				ember::value field{};
				ember::value before{};
				ember::value after{};
				ember::value action{};
				ember::value created_by_name{};
				ember::value created_at{};
				static recent_row from(const ember::value& v) {
					recent_row out;
					out.id = detail::text(v["id"]);
					out.issue = v["issue"];
					out.issue_number = v["issue.number"];
					out.issue_title = v["issue.title"];
					out.field = v["field"];
					out.before = v["before"];
					out.after = v["after"];
					out.action = v["action"];
					out.created_by_name = v["created_by.name"];
					out.created_at = v["created_at"];
					return out;
				}
			};
			std::vector<recent_row> recent{};

			static struct project_page from(const ember::snapshot& s) {
				struct project_page out;
				out.exists = s.exists;
				const ember::value& v = s.data;
				out.name = detail::text(v["name"]);
				out.summary = detail::text(v["summary"]);
				out.visibility = detail::choice<::neotrac::projects::project_visibility>(v["visibility"]);
				out.takes_reports = detail::flag(v["takes_reports"]);
				if (v["boards"].is_array()) {
					for (const auto& row : v["boards"].as_array()) out.boards.push_back(boards_row::from(row));
				}
				if (v["issues"].is_array()) {
					for (const auto& row : v["issues"].as_array()) out.issues.push_back(issues_row::from(row));
				}
				if (v["milestones"].is_array()) {
					for (const auto& row : v["milestones"].as_array()) out.milestones.push_back(milestones_row::from(row));
				}
				if (v["pages"].is_array()) {
					for (const auto& row : v["pages"].as_array()) out.pages.push_back(pages_row::from(row));
				}
				if (v["members"].is_array()) {
					for (const auto& row : v["members"].as_array()) out.members.push_back(members_row::from(row));
				}
				if (v["roles"].is_array()) {
					for (const auto& row : v["roles"].as_array()) out.roles.push_back(roles_row::from(row));
				}
				if (v["timeline"].is_array()) {
					for (const auto& row : v["timeline"].as_array()) out.timeline.push_back(timeline_row::from(row));
				}
				if (v["recent"].is_array()) {
					for (const auto& row : v["recent"].as_array()) out.recent.push_back(recent_row::from(row));
				}
				return out;
			}
			static std::string path(const std::string& of) { return "views/projects::project_page:" + of; }
		};

		struct invitations {
			bool exists = false;  // whether it's been made, and may be read
			struct invited_row {
				std::string id{};
				std::string email{};
				std::string role_title{};
				static invited_row from(const ember::value& v) {
					invited_row out;
					out.id = detail::text(v["id"]);
					out.email = detail::text(v["email"]);
					out.role_title = detail::text(v["role.title"]);
					return out;
				}
			};
			std::vector<invited_row> invited{};

			static struct invitations from(const ember::snapshot& s) {
				struct invitations out;
				out.exists = s.exists;
				const ember::value& v = s.data;
				if (v["invited"].is_array()) {
					for (const auto& row : v["invited"].as_array()) out.invited.push_back(invited_row::from(row));
				}
				return out;
			}
			static std::string path(const std::string& of) { return "views/projects::invitations:" + of; }
		};

		struct role_page {
			bool exists = false;  // whether it's been made, and may be read
			std::string name{};
			std::string title{};
			std::vector<std::string> may{};

			static struct role_page from(const ember::snapshot& s) {
				struct role_page out;
				out.exists = s.exists;
				const ember::value& v = s.data;
				out.name = detail::text(v["name"]);
				out.title = detail::text(v["title"]);
				out.may = detail::texts(v["may"]);
				return out;
			}
			static std::string path(const std::string& of) { return "views/projects::role_page:" + of; }
		};

		struct report_page {
			bool exists = false;  // whether it's been made, and may be read
			std::string title{};
			std::string body{};
			::neotrac::projects::report_status status{};
			struct replies_row {
				std::string id{};
				std::string author_picture{};
				std::string author_name{};
				std::string body{};
				time created_at{};
				static replies_row from(const ember::value& v) {
					replies_row out;
					out.id = detail::text(v["id"]);
					out.author_picture = detail::text(v["author.picture"]);
					out.author_name = detail::text(v["author.name"]);
					out.body = detail::text(v["body"]);
					out.created_at = detail::when(v["created_at"]);
					return out;
				}
			};
			std::vector<replies_row> replies{};

			static struct report_page from(const ember::snapshot& s) {
				struct report_page out;
				out.exists = s.exists;
				const ember::value& v = s.data;
				out.title = detail::text(v["title"]);
				out.body = detail::text(v["body"]);
				out.status = detail::choice<::neotrac::projects::report_status>(v["status"]);
				if (v["replies"].is_array()) {
					for (const auto& row : v["replies"].as_array()) out.replies.push_back(replies_row::from(row));
				}
				return out;
			}
			static std::string path(const std::string& of) { return "views/projects::report_page:" + of; }
		};

		struct project_reports {
			bool exists = false;  // whether it's been made, and may be read
			struct rows_row {
				std::string id{};
				std::string title{};
				::neotrac::projects::report_status status{};
				std::string author_picture{};
				std::string author_name{};
				time created_at{};
				static rows_row from(const ember::value& v) {
					rows_row out;
					out.id = detail::text(v["id"]);
					out.title = detail::text(v["title"]);
					out.status = detail::choice<::neotrac::projects::report_status>(v["status"]);
					out.author_picture = detail::text(v["author.picture"]);
					out.author_name = detail::text(v["author.name"]);
					out.created_at = detail::when(v["created_at"]);
					return out;
				}
			};
			std::vector<rows_row> rows{};

			static struct project_reports from(const ember::snapshot& s) {
				struct project_reports out;
				out.exists = s.exists;
				const ember::value& v = s.data;
				if (v["rows"].is_array()) {
					for (const auto& row : v["rows"].as_array()) out.rows.push_back(rows_row::from(row));
				}
				return out;
			}
			static std::string path(const std::string& of) { return "views/projects::project_reports:" + of; }
		};

		struct my_reports {
			bool exists = false;  // whether it's been made, and may be read
			struct rows_row {
				std::string id{};
				std::string project{};
				std::string title{};
				::neotrac::projects::report_status status{};
				static rows_row from(const ember::value& v) {
					rows_row out;
					out.id = detail::text(v["id"]);
					out.project = detail::text(v["project"]);
					out.title = detail::text(v["title"]);
					out.status = detail::choice<::neotrac::projects::report_status>(v["status"]);
					return out;
				}
			};
			std::vector<rows_row> rows{};

			static struct my_reports from(const ember::snapshot& s) {
				struct my_reports out;
				out.exists = s.exists;
				const ember::value& v = s.data;
				if (v["rows"].is_array()) {
					for (const auto& row : v["rows"].as_array()) out.rows.push_back(rows_row::from(row));
				}
				return out;
			}
			static std::string path(const std::string& of) { return "views/projects::my_reports:" + of; }
		};

		struct services {
			bool exists = false;  // whether it's been made, and may be read
			struct services_row {
				std::string id{};
				std::string title{};
				std::string role{};
				std::string role_title{};
				std::string key_start{};
				time key_made{};
				time key_used{};
				static services_row from(const ember::value& v) {
					services_row out;
					out.id = detail::text(v["id"]);
					out.title = detail::text(v["title"]);
					out.role = detail::text(v["role"]);
					out.role_title = detail::text(v["role.title"]);
					out.key_start = detail::text(v["key_start"]);
					out.key_made = detail::when(v["key_made"]);
					out.key_used = detail::when(v["key_used"]);
					return out;
				}
			};
			std::vector<services_row> services{};

			static struct services from(const ember::snapshot& s) {
				struct services out;
				out.exists = s.exists;
				const ember::value& v = s.data;
				if (v["services"].is_array()) {
					for (const auto& row : v["services"].as_array()) out.services.push_back(services_row::from(row));
				}
				return out;
			}
			static std::string path(const std::string& of) { return "views/projects::services:" + of; }
		};

		struct wiki_page {
			bool exists = false;  // whether it's been made, and may be read
			std::string title{};
			std::string body{};
			struct history_row {
				std::string id{};
				ember::value field{};
				ember::value before{};
				ember::value after{};
				ember::value action{};
				ember::value created_by_name{};
				ember::value created_at{};
				static history_row from(const ember::value& v) {
					history_row out;
					out.id = detail::text(v["id"]);
					out.field = v["field"];
					out.before = v["before"];
					out.after = v["after"];
					out.action = v["action"];
					out.created_by_name = v["created_by.name"];
					out.created_at = v["created_at"];
					return out;
				}
			};
			std::vector<history_row> history{};

			static struct wiki_page from(const ember::snapshot& s) {
				struct wiki_page out;
				out.exists = s.exists;
				const ember::value& v = s.data;
				out.title = detail::text(v["title"]);
				out.body = detail::text(v["body"]);
				if (v["history"].is_array()) {
					for (const auto& row : v["history"].as_array()) out.history.push_back(history_row::from(row));
				}
				return out;
			}
			static std::string path(const std::string& of) { return "views/projects::wiki_page:" + of; }
		};

		struct member_roles {
			bool exists = false;  // whether it's been made, and may be read
			struct rows_row {
				std::string id{};
				std::string project{};
				std::string role{};
				std::vector<std::string> role_may{};
				static rows_row from(const ember::value& v) {
					rows_row out;
					out.id = detail::text(v["id"]);
					out.project = detail::text(v["project"]);
					out.role = detail::text(v["role"]);
					out.role_may = detail::texts(v["role.may"]);
					return out;
				}
			};
			std::vector<rows_row> rows{};

			static struct member_roles from(const ember::snapshot& s) {
				struct member_roles out;
				out.exists = s.exists;
				const ember::value& v = s.data;
				if (v["rows"].is_array()) {
					for (const auto& row : v["rows"].as_array()) out.rows.push_back(rows_row::from(row));
				}
				return out;
			}
			static std::string path(const std::string& of) { return "views/projects::member_roles:" + of; }
		};

	} // namespace projects

	// A view to read once, or to listen to as it changes.
	template <class View> class live {
		public:
		live(ember::database& store, std::string path) : store_(store), path_(std::move(path)) {}
		View get() { return View::from(store_.get(path_)); }
		ember::database::registration listen(std::function<void(const View&)> on_change, std::function<void(const ember::error&)> on_error = {}) {
			return store_.listen(path_, [on_change](const ember::snapshot& s) { on_change(View::from(s)); }, std::move(on_error));
		}
		private:
		ember::database& store_;
		std::string path_;
	};

	// Who's asking: a person, or a service and the project and role it holds.
	struct caller {
		std::string id{}, name{}, place{}, role{};
		bool service = false;
	};

	// Where the app is: its address, and, for the emulators, where they listen.
	struct options {
		std::string server{};          // like https://neotrac.org
		std::string project{};         // its Firebase project; asked of the server when empty
		std::string api_key{};         // asked of the server with the project when empty
		std::string firestore_host{};  // the Firestore emulator, like localhost:8080
		std::string auth_host{};       // the Auth emulator, like localhost:9099
	};

	// The app: sign in as a person or with a service's key, run its commands, and read
	// its views live. Callbacks run on the executor given, or on libember's own thread.
	class client {
		public:
		explicit client(options o, ember::executor* deliver_on = nullptr) : options_(std::move(o)), deliver_on_(deliver_on) {}

		// Signs in as a service: its key runs commands, and trades for a sign-in of
		// the service's own that reads its views.
		void sign_in_with_key(const std::string& key) {
			key_ = key;
			auto token = post("/api/token", "{}");
			connect();
			auth_->sign_in_with_custom_token(detail::text(ember::parse_json(token)["token"]));
		}

		// Signs in as a person, with the refresh token a sign-in kept.
		void sign_in_with_refresh_token(const std::string& token) {
			connect();
			auth_->sign_in_with_refresh_token(token);
		}

		// Signs in as a person, with a GitHub or Google access token, like one a device's
		// sign-in gave: github.com or google.com.
		void sign_in_with_provider(const std::string& provider, const std::string& access_token) {
			connect();
			auth_->sign_in_with_idp(provider, access_token);
		}

		std::string refresh_token() { connect(); return auth_->refresh_token(); }

		caller me() {
			auto v = ember::parse_json(get("/api/me"));
			caller out{detail::text(v["id"]), detail::text(v["name"]), "", detail::text(v["role"]), detail::flag(v["service"])};
			for (const auto& [name, value] : v.as_map()) if (name != "id" && name != "name" && name != "role" && name != "service") out.place = detail::text(value);
			return out;
		}

		// board::update: the id of what it changed, or refused.
		std::string board_update(const projects::board_update& sent) {
			return detail::text(ember::parse_json(post("/api/projects/board/update", sent.json()))["id"]);
		}
		// board::follow: the id of what it changed, or refused.
		std::string board_follow(const projects::board_follow& sent) {
			return detail::text(ember::parse_json(post("/api/projects/board/follow", sent.json()))["id"]);
		}
		// board::unfollow: the id of what it changed, or refused.
		std::string board_unfollow(const projects::board_unfollow& sent) {
			return detail::text(ember::parse_json(post("/api/projects/board/unfollow", sent.json()))["id"]);
		}
		// issue::create: the id of what it changed, or refused.
		std::string issue_create(const projects::issue_create& sent) {
			return detail::text(ember::parse_json(post("/api/projects/issue/create", sent.json()))["id"]);
		}
		// issue::update: the id of what it changed, or refused.
		std::string issue_update(const projects::issue_update& sent) {
			return detail::text(ember::parse_json(post("/api/projects/issue/update", sent.json()))["id"]);
		}
		// issue::transfer: the id of what it changed, or refused.
		std::string issue_transfer(const projects::issue_transfer& sent) {
			return detail::text(ember::parse_json(post("/api/projects/issue/transfer", sent.json()))["id"]);
		}
		// link::create: the id of what it changed, or refused.
		std::string link_create(const projects::link_create& sent) {
			return detail::text(ember::parse_json(post("/api/projects/link/create", sent.json()))["id"]);
		}
		// link::delete: the id of what it changed, or refused.
		std::string link_delete(const projects::link_delete& sent) {
			return detail::text(ember::parse_json(post("/api/projects/link/delete", sent.json()))["id"]);
		}
		// issue::move: the id of what it changed, or refused.
		std::string issue_move(const projects::issue_move& sent) {
			return detail::text(ember::parse_json(post("/api/projects/issue/move", sent.json()))["id"]);
		}
		// issue::claim: the id of what it changed, or refused.
		std::string issue_claim(const projects::issue_claim& sent) {
			return detail::text(ember::parse_json(post("/api/projects/issue/claim", sent.json()))["id"]);
		}
		// issue::release: the id of what it changed, or refused.
		std::string issue_release(const projects::issue_release& sent) {
			return detail::text(ember::parse_json(post("/api/projects/issue/release", sent.json()))["id"]);
		}
		// comment::create: the id of what it changed, or refused.
		std::string comment_create(const projects::comment_create& sent) {
			return detail::text(ember::parse_json(post("/api/projects/comment/create", sent.json()))["id"]);
		}
		// milestone::create: the id of what it changed, or refused.
		std::string milestone_create(const projects::milestone_create& sent) {
			return detail::text(ember::parse_json(post("/api/projects/milestone/create", sent.json()))["id"]);
		}
		// milestone::update: the id of what it changed, or refused.
		std::string milestone_update(const projects::milestone_update& sent) {
			return detail::text(ember::parse_json(post("/api/projects/milestone/update", sent.json()))["id"]);
		}
		// project::create: the id of what it changed, or refused.
		std::string project_create(const projects::project_create& sent) {
			return detail::text(ember::parse_json(post("/api/projects/project/create", sent.json()))["id"]);
		}
		// project::update: the id of what it changed, or refused.
		std::string project_update(const projects::project_update& sent) {
			return detail::text(ember::parse_json(post("/api/projects/project/update", sent.json()))["id"]);
		}
		// member::create: the id of what it changed, or refused.
		std::string member_create(const projects::member_create& sent) {
			return detail::text(ember::parse_json(post("/api/projects/member/create", sent.json()))["id"]);
		}
		// member::delete: the id of what it changed, or refused.
		std::string member_delete(const projects::member_delete& sent) {
			return detail::text(ember::parse_json(post("/api/projects/member/delete", sent.json()))["id"]);
		}
		// role::create: the id of what it changed, or refused.
		std::string role_create(const projects::role_create& sent) {
			return detail::text(ember::parse_json(post("/api/projects/role/create", sent.json()))["id"]);
		}
		// role::update: the id of what it changed, or refused.
		std::string role_update(const projects::role_update& sent) {
			return detail::text(ember::parse_json(post("/api/projects/role/update", sent.json()))["id"]);
		}
		// role::delete: the id of what it changed, or refused.
		std::string role_delete(const projects::role_delete& sent) {
			return detail::text(ember::parse_json(post("/api/projects/role/delete", sent.json()))["id"]);
		}
		// reader::create: the id of what it changed, or refused.
		std::string reader_create(const projects::reader_create& sent) {
			return detail::text(ember::parse_json(post("/api/projects/reader/create", sent.json()))["id"]);
		}
		// invitation::create: the id of what it changed, or refused.
		std::string invitation_create(const projects::invitation_create& sent) {
			return detail::text(ember::parse_json(post("/api/projects/invitation/create", sent.json()))["id"]);
		}
		// invitation::delete: the id of what it changed, or refused.
		std::string invitation_delete(const projects::invitation_delete& sent) {
			return detail::text(ember::parse_json(post("/api/projects/invitation/delete", sent.json()))["id"]);
		}
		// report::create: the id of what it changed, or refused.
		std::string report_create(const projects::report_create& sent) {
			return detail::text(ember::parse_json(post("/api/projects/report/create", sent.json()))["id"]);
		}
		// report::resolve: the id of what it changed, or refused.
		std::string report_resolve(const projects::report_resolve& sent) {
			return detail::text(ember::parse_json(post("/api/projects/report/resolve", sent.json()))["id"]);
		}
		// reply::create: the id of what it changed, or refused.
		std::string reply_create(const projects::reply_create& sent) {
			return detail::text(ember::parse_json(post("/api/projects/reply/create", sent.json()))["id"]);
		}
		// service::create: the id of what it changed, or refused.
		std::string service_create(const projects::service_create& sent) {
			return detail::text(ember::parse_json(post("/api/projects/service/create", sent.json()))["id"]);
		}
		// service::update: the id of what it changed, or refused.
		std::string service_update(const projects::service_update& sent) {
			return detail::text(ember::parse_json(post("/api/projects/service/update", sent.json()))["id"]);
		}
		// service::delete: the id of what it changed, or refused.
		std::string service_delete(const projects::service_delete& sent) {
			return detail::text(ember::parse_json(post("/api/projects/service/delete", sent.json()))["id"]);
		}
		// service::key: the id of what it changed, or refused.
		std::string service_key(const projects::service_key& sent) {
			return detail::text(ember::parse_json(post("/api/projects/service/key", sent.json()))["id"]);
		}
		// service::revoke: the id of what it changed, or refused.
		std::string service_revoke(const projects::service_revoke& sent) {
			return detail::text(ember::parse_json(post("/api/projects/service/revoke", sent.json()))["id"]);
		}
		// page::create: the id of what it changed, or refused.
		std::string page_create(const projects::page_create& sent) {
			return detail::text(ember::parse_json(post("/api/projects/page/create", sent.json()))["id"]);
		}
		// page::update: the id of what it changed, or refused.
		std::string page_update(const projects::page_update& sent) {
			return detail::text(ember::parse_json(post("/api/projects/page/update", sent.json()))["id"]);
		}
		// phase::create: the id of what it changed, or refused.
		std::string phase_create(const projects::phase_create& sent) {
			return detail::text(ember::parse_json(post("/api/projects/phase/create", sent.json()))["id"]);
		}
		// phase::update: the id of what it changed, or refused.
		std::string phase_update(const projects::phase_update& sent) {
			return detail::text(ember::parse_json(post("/api/projects/phase/update", sent.json()))["id"]);
		}
		// phase::delete: the id of what it changed, or refused.
		std::string phase_delete(const projects::phase_delete& sent) {
			return detail::text(ember::parse_json(post("/api/projects/phase/delete", sent.json()))["id"]);
		}
		// step::create: the id of what it changed, or refused.
		std::string step_create(const projects::step_create& sent) {
			return detail::text(ember::parse_json(post("/api/projects/step/create", sent.json()))["id"]);
		}
		// step::update: the id of what it changed, or refused.
		std::string step_update(const projects::step_update& sent) {
			return detail::text(ember::parse_json(post("/api/projects/step/update", sent.json()))["id"]);
		}
		// step::delete: the id of what it changed, or refused.
		std::string step_delete(const projects::step_delete& sent) {
			return detail::text(ember::parse_json(post("/api/projects/step/delete", sent.json()))["id"]);
		}
		// board::create: the id of what it changed, or refused.
		std::string board_create(const projects::board_create& sent) {
			return detail::text(ember::parse_json(post("/api/projects/board/create", sent.json()))["id"]);
		}
		live<projects::board_page> board_page(const std::string& board) { connect(); return {*store_, projects::board_page::path(board)}; }
		live<projects::issue_page> issue_page(const std::string& issue) { connect(); return {*store_, projects::issue_page::path(issue)}; }
		live<projects::milestone_page> milestone_page(const std::string& milestone) { connect(); return {*store_, projects::milestone_page::path(milestone)}; }
		live<projects::all> all() { connect(); return {*store_, projects::all::path()}; }
		// What it shows the one signed in.
		live<projects::assigned> assigned() { connect(); return {*store_, projects::assigned::path(auth_->current_user().value().uid)}; }
		// What it shows the one signed in.
		live<projects::news> news() { connect(); return {*store_, projects::news::path(auth_->current_user().value().uid)}; }
		// What it shows the one signed in.
		live<projects::followed> followed() { connect(); return {*store_, projects::followed::path(auth_->current_user().value().uid)}; }
		// What it shows the one signed in.
		live<projects::mine> mine() { connect(); return {*store_, projects::mine::path(auth_->current_user().value().uid)}; }
		live<projects::project_page> project_page(const std::string& project) { connect(); return {*store_, projects::project_page::path(project)}; }
		live<projects::invitations> invitations(const std::string& project) { connect(); return {*store_, projects::invitations::path(project)}; }
		live<projects::role_page> role_page(const std::string& role) { connect(); return {*store_, projects::role_page::path(role)}; }
		live<projects::report_page> report_page(const std::string& report) { connect(); return {*store_, projects::report_page::path(report)}; }
		live<projects::project_reports> project_reports(const std::string& project) { connect(); return {*store_, projects::project_reports::path(project)}; }
		// What it shows the one signed in.
		live<projects::my_reports> my_reports() { connect(); return {*store_, projects::my_reports::path(auth_->current_user().value().uid)}; }
		live<projects::services> services(const std::string& project) { connect(); return {*store_, projects::services::path(project)}; }
		live<projects::wiki_page> wiki_page(const std::string& page) { connect(); return {*store_, projects::wiki_page::path(page)}; }
		// What it shows the one signed in.
		live<projects::member_roles> member_roles() { connect(); return {*store_, projects::member_roles::path(auth_->current_user().value().uid)}; }

		private:
		options options_;
		ember::executor* deliver_on_;
		std::string key_;
		std::unique_ptr<ember::auth> auth_;
		std::unique_ptr<ember::database> store_;

		// Finds the app's Firebase project and key, from what Firebase Hosting serves at
		// /__/firebase/init.json, once, then makes its sign-in and its store.
		void connect() {
			if (store_) return;
			ember::options o = ember::options::for_project(options_.project);
			o.api_key = options_.api_key;
			o.firestore_host = options_.firestore_host;
			o.auth_host = options_.auth_host;
			if (o.project.empty()) {
				auto found = ember::https_get(options_.server + "/__/firebase/init.json", {});
				if (found.status != 200) throw refused(found.status, "the app's settings weren't found at " + options_.server);
				auto settings = ember::parse_json(found.body);
				o.project = detail::text(settings["projectId"]);
				if (o.api_key.empty()) o.api_key = detail::text(settings["apiKey"]);
			}
			auth_ = std::make_unique<ember::auth>(o);
			store_ = std::make_unique<ember::database>(o, auth_.get(), deliver_on_);
		}

		// Who's asking, for a command: a service's key, or a person's sign-in.
		std::vector<std::pair<std::string, std::string>> credentials() {
			std::vector<std::pair<std::string, std::string>> headers{{"Content-Type", "application/json"}};
			if (!key_.empty()) headers.emplace_back("Authorization", "Bearer " + key_);
			else if (auth_ && auth_->current_user()) headers.emplace_back("Authorization", "Bearer " + auth_->id_token());
			return headers;
		}

		std::string post(const std::string& route, const std::string& body) {
			auto answer = ember::https_post(options_.server + route, credentials(), body);
			if (answer.status != 200) throw refused(answer.status, why(answer));
			return answer.body;
		}

		std::string get(const std::string& route) {
			auto answer = ember::https_get(options_.server + route, credentials());
			if (answer.status != 200) throw refused(answer.status, why(answer));
			return answer.body;
		}

		static std::string why(const ember::response& answer) {
			try {
				auto said = detail::text(ember::parse_json(answer.body)["error"]);
				if (!said.empty()) return said;
			} catch (...) {}
			return "the app answered " + std::to_string(answer.status);
		}
	};

} // namespace neotrac
