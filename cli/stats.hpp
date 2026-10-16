// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: GPL-3.0-only

// What an agent is using, read as btop reads it, from /proc and the GPU's own
// library: the CPU and memory of what it runs, its whole process tree, and its
// machine's CPU, memory, GPU, GPU memory and network. A sample a second, the last
// two minutes kept, newest last.

#pragma once

#include <dirent.h>
#include <dlfcn.h>
#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace stats {

	// A sample of each, at a moment; what can't be read is left out.
	struct sample {
		std::map<std::string, double> values;
	};

	namespace detail {

		inline std::string read_file(const std::string& path) {
			std::ifstream in(path);
			std::stringstream all;
			all << in.rdbuf();
			return all.str();
		}

		// The machine's CPU time, busy and all, in ticks, from /proc/stat's first line.
		inline std::pair<std::uint64_t, std::uint64_t> machine_ticks() {
			std::ifstream in("/proc/stat");
			std::string cpu;
			std::uint64_t user = 0, nice = 0, system = 0, idle = 0, iowait = 0, irq = 0, softirq = 0, steal = 0;
			in >> cpu >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal;
			std::uint64_t all = user + nice + system + idle + iowait + irq + softirq + steal;
			return {all - idle - iowait, all};
		}

		// Memory in use: all of it less what's available, in bytes.
		inline std::pair<double, double> machine_memory() {
			std::ifstream in("/proc/meminfo");
			std::string name;
			double total = 0, available = 0, kb = 0;
			std::string unit;
			while (in >> name >> kb) {
				std::getline(in, unit);
				if (name == "MemTotal:") total = kb * 1024;
				if (name == "MemAvailable:") available = kb * 1024;
			}
			return {total - available, total};
		}

		// Bytes received and sent by every interface but the loopback.
		inline std::pair<std::uint64_t, std::uint64_t> network_bytes() {
			std::ifstream in("/proc/net/dev");
			std::string line;
			std::uint64_t received = 0, sent = 0;
			while (std::getline(in, line)) {
				auto colon = line.find(':');
				if (colon == std::string::npos) continue;
				std::string name = line.substr(0, colon);
				name.erase(0, name.find_first_not_of(' '));
				if (name == "lo") continue;
				std::istringstream rest(line.substr(colon + 1));
				std::uint64_t f[16] = {};
				for (auto& v : f) rest >> v;
				received += f[0], sent += f[8];
			}
			return {received, sent};
		}

		// Each process's parent, its CPU ticks and its resident memory in pages.
		struct process {
			int parent = 0;
			std::uint64_t ticks = 0;
			std::uint64_t pages = 0;
		};

		inline std::map<int, process> processes() {
			std::map<int, process> out;
			DIR* dir = ::opendir("/proc");
			if (!dir) return out;
			while (dirent* entry = ::readdir(dir)) {
				char* end = nullptr;
				long pid = std::strtol(entry->d_name, &end, 10);
				if (*end != '\0' || pid <= 0) continue;
				std::string stat = read_file("/proc/" + std::string(entry->d_name) + "/stat");
				auto close = stat.rfind(')');
				if (close == std::string::npos) continue;
				std::istringstream fields(stat.substr(close + 2));
				std::string state;
				process p;
				std::uint64_t skip = 0, utime = 0, stime = 0;
				fields >> state >> p.parent;
				for (int i = 0; i < 9; ++i) fields >> skip;  // pgrp .. cmajflt
				fields >> utime >> stime;
				p.ticks = utime + stime;
				std::istringstream statm(read_file("/proc/" + std::string(entry->d_name) + "/statm"));
				std::uint64_t size = 0;
				statm >> size >> p.pages;
				out[static_cast<int>(pid)] = p;
			}
			::closedir(dir);
			return out;
		}

		// NVIDIA's library, loaded if it's there: how busy each GPU is, and its memory in use.
		struct nvml {
			using init_t = int (*)();
			using count_t = int (*)(unsigned*);
			using handle_t = int (*)(unsigned, void**);
			struct utilization {
				unsigned gpu, memory;
			};
			struct memory {
				unsigned long long total, free, used;
			};
			using utilization_t = int (*)(void*, utilization*);
			using memory_t = int (*)(void*, memory*);
			void* library = nullptr;
			count_t count = nullptr;
			handle_t handle = nullptr;
			utilization_t busy = nullptr;
			memory_t used = nullptr;
			bool ready = false;

			nvml() {
				library = ::dlopen("libnvidia-ml.so.1", RTLD_LAZY);
				if (!library) return;
				auto init = reinterpret_cast<init_t>(::dlsym(library, "nvmlInit_v2"));
				count = reinterpret_cast<count_t>(::dlsym(library, "nvmlDeviceGetCount_v2"));
				handle = reinterpret_cast<handle_t>(::dlsym(library, "nvmlDeviceGetHandleByIndex_v2"));
				busy = reinterpret_cast<utilization_t>(::dlsym(library, "nvmlDeviceGetUtilizationRates"));
				used = reinterpret_cast<memory_t>(::dlsym(library, "nvmlDeviceGetMemoryInfo"));
				ready = init && count && handle && busy && used && init() == 0;
			}
			// The GPUs' busy share, averaged, and their memory in use, added up.
			std::optional<std::pair<double, double>> read() const {
				if (!ready) return std::nullopt;
				unsigned n = 0;
				if (count(&n) != 0 || n == 0) return std::nullopt;
				double share = 0, bytes = 0;
				for (unsigned i = 0; i < n; ++i) {
					void* device = nullptr;
					utilization u{};
					memory m{};
					if (handle(i, &device) != 0) continue;
					if (busy(device, &u) == 0) share += u.gpu;
					if (used(device, &m) == 0) bytes += static_cast<double>(m.used);
				}
				return std::pair{share / n, bytes};
			}
		};

		// An AMD or Intel GPU, as the kernel tells it, where there's no NVIDIA one.
		inline std::optional<std::pair<double, double>> kernel_gpu() {
			for (int card = 0; card < 8; ++card) {
				std::string at = "/sys/class/drm/card" + std::to_string(card) + "/device/";
				std::ifstream busy(at + "gpu_busy_percent");
				double share = 0;
				if (!(busy >> share)) continue;
				std::ifstream vram(at + "mem_info_vram_used");
				double bytes = 0;
				vram >> bytes;
				return std::pair{share, bytes};
			}
			return std::nullopt;
		}

	}  // namespace detail

	// Samples once a second, keeping the last two minutes of each, for whatever runs
	// under the process it's told of, and for the machine.
	class sampler {
	public:
		static constexpr std::size_t kept = 120;

		// The processes whose trees are what it runs now: each program a stage of its
		// runs, added as it starts and taken away as it ends.
		void watch(int pid) { watched_.insert(pid); }
		void unwatch(int pid) { watched_.erase(pid); }

		// Samples when a second has passed since the last; whether it did.
		bool tick() {
			auto now = std::chrono::steady_clock::now();
			if (sampled_ && now - last_ < std::chrono::seconds(1)) return false;
			double seconds = sampled_ ? std::chrono::duration<double>(now - last_).count() : 0;
			last_ = now;
			auto [busy, all] = detail::machine_ticks();
			auto [received, sent] = detail::network_bytes();
			auto tree = detail::processes();
			std::uint64_t mine_ticks = 0, mine_pages = 0;
			// Its descendants: each process whose parents lead back to one it watches.
			std::set<int> under(watched_.begin(), watched_.end());
			for (bool grew = !under.empty(); grew;) {
				grew = false;
				for (const auto& [pid, p] : tree) {
					if (!under.contains(pid) && under.contains(p.parent)) under.insert(pid), grew = true;
				}
			}
			for (int pid : under) {
				if (auto found = tree.find(pid); found != tree.end()) mine_ticks += found->second.ticks, mine_pages += found->second.pages;
			}
			if (sampled_ && all > all_) {
				double span = static_cast<double>(all - all_);
				push("machine_cpu", 100.0 * static_cast<double>(busy - busy_) / span);
				// Its share of the whole machine, as the machine's own is: ticks over all of them.
				std::uint64_t mine_was = 0;
				for (const auto& [pid, t] : ticks_) {
					if (tree.contains(pid)) mine_was += t;
				}
				push("cpu", !watched_.empty() ? std::min(100.0, 100.0 * static_cast<double>(mine_ticks > mine_was ? mine_ticks - mine_was : 0) / span) : 0.0);
				if (seconds > 0) {
					push("received", static_cast<double>(received - received_) / seconds);
					push("sent", static_cast<double>(sent - sent_) / seconds);
				}
			}
			push("memory", static_cast<double>(mine_pages) * static_cast<double>(::sysconf(_SC_PAGESIZE)));
			auto [used, total] = detail::machine_memory();
			push("machine_memory", used);
			auto gpu = gpu_.read();
			if (!gpu) gpu = detail::kernel_gpu();
			if (gpu) push("gpu", gpu->first), push("gpu_memory", gpu->second);
			busy_ = busy, all_ = all, received_ = received, sent_ = sent, sampled_ = true;
			// Only its trees' are kept: what's counted next is how much they grew.
			ticks_.clear();
			for (int pid : under) {
				if (auto found = tree.find(pid); found != tree.end()) ticks_[pid] = found->second.ticks;
			}
			return true;
		}

		// Each run of numbers, newest last, rounded so what's sent stays small.
		std::map<std::string, std::vector<double>> runs() const {
			std::map<std::string, std::vector<double>> out;
			for (const auto& [name, values] : runs_) {
				auto& shown = out[name];
				for (double v : values) shown.push_back(v >= 1000 ? static_cast<double>(static_cast<long long>(v)) : static_cast<double>(static_cast<long long>(v * 10)) / 10);
			}
			return out;
		}

	private:
		void push(const std::string& name, double value) {
			auto& run = runs_[name];
			run.push_back(value);
			while (run.size() > kept) run.pop_front();
		}

		std::set<int> watched_;
		bool sampled_ = false;
		std::chrono::steady_clock::time_point last_{};
		std::uint64_t busy_ = 0, all_ = 0, received_ = 0, sent_ = 0;
		std::map<int, std::uint64_t> ticks_;
		std::map<std::string, std::deque<double>> runs_;
		detail::nvml gpu_;
	};

}  // namespace stats
