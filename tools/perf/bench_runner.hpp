#pragma once

// Shared command line for Sub0 family benchmark executables (nanobench based).
//
//   <bench> [--filter <text>] [--json <file>] [--soak <seconds>] [--list]
//
//   --filter  run only benchmarks whose name contains <text> (case-insensitive)
//   --json    also write nanobench JSON results (input to tools/perf/bench_check.py)
//   --soak    instead of measuring, loop each selected benchmark for <seconds>; use under a profiler
//             (VTune, Advisor, perf) so the hot path dominates the samples
//   --list    print benchmark names and exit
//
// Include after defining ANKERL_NANOBENCH_IMPLEMENT in exactly one translation unit.

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <nanobench.h>
#include <string>
#include <string_view>
#include <vector>

namespace sub0bench
{

class BenchRunner
{
public:
    BenchRunner(int argc, char** argv, const char* title)
    {
        for (int i = 1; i < argc; ++i)
        {
            const std::string_view arg = argv[i];
            const bool hasValue = i + 1 < argc;
            if (arg == "--filter" && hasValue)
            {
                filter_ = argv[++i];
            }
            else if (arg == "--json" && hasValue)
            {
                json_ = argv[++i];
            }
            else if (arg == "--soak" && hasValue)
            {
                soakSeconds_ = std::atof(argv[++i]);
            }
            else if (arg == "--list")
            {
                list_ = true;
            }
            else
            {
                std::fprintf(stderr, "usage: %s [--filter text] [--json file] [--soak seconds] [--list]\n", argv[0]);
                std::exit(2);
            }
        }
        bench_.title(title).warmup(100).minEpochIterations(10).performanceCounters(true);
    }

    // The underlying nanobench configuration, for per-benchmark settings (units, batch, epochs).
    ankerl::nanobench::Bench& bench() noexcept
    {
        return bench_;
    }

    template<typename Op>
    void run(const char* name, Op&& op)
    {
        if (!matches(name))
        {
            return;
        }
        if (list_)
        {
            std::printf("%s\n", name);
            return;
        }
        ++selected_;
        if (soakSeconds_ > 0.0)
        {
            std::printf("soaking %s for %.1f s\n", name, soakSeconds_);
            const auto end = std::chrono::steady_clock::now() + std::chrono::duration<double>(soakSeconds_);
            std::uint64_t iterations = 0;
            while (std::chrono::steady_clock::now() < end)
            {
                for (int i = 0; i < 64; ++i)
                {
                    op();
                }
                iterations += 64U;
            }
            std::printf("  %llu iterations\n", static_cast<unsigned long long>(iterations));
            return;
        }
        bench_.run(name, op);
        // Changing units or titles clears nanobench's own list, so keep every result here.
        results_.push_back(bench_.results().back());
    }

    // Writes JSON when requested; returns the process exit code.
    int finish()
    {
        if (list_)
        {
            return 0;
        }
        if (selected_ == 0)
        {
            std::fprintf(stderr, "no benchmark matches the filter\n");
            return 1;
        }
        if (!json_.empty() && soakSeconds_ <= 0.0)
        {
            std::ofstream out(json_);
            ankerl::nanobench::render(ankerl::nanobench::templates::json(), results_, out);
        }
        return 0;
    }

private:
    // Case-insensitive substring match; an empty filter selects everything.
    [[nodiscard]] bool matches(std::string_view name) const
    {
        const auto lower = [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); };
        return filter_.empty()
            || std::search(name.begin(), name.end(), filter_.begin(), filter_.end(),
                           [&](char a, char b) { return lower(a) == lower(b); }) != name.end();
    }

    ankerl::nanobench::Bench bench_;
    std::vector<ankerl::nanobench::Result> results_;
    std::string filter_;
    std::string json_;
    double soakSeconds_ = 0.0;
    bool list_ = false;
    int selected_ = 0;
};

} // namespace sub0bench
