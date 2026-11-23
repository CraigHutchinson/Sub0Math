#pragma once

// Shared command line for Sub0 family benchmark executables (nanobench based).
//
//   <bench> [--filter <text>] [--exact <name>] [--epochs <n>] [--json <file>] [--soak <seconds>]
//           [--list] [--calibration] [--metrics-only] [--smoke]
//
//   --filter       run only benchmarks whose name contains <text> (case-insensitive)
//   --exact        run only the benchmark with exactly this name (bench_check.py interleaves with it)
//   --epochs       override every benchmark's epoch count (fewer = quicker, noisier)
//   --json         also write nanobench JSON results
//   --soak         instead of measuring, loop each selected benchmark for <seconds>; use under a profiler
//                  (VTune, Advisor, perf) so the hot path dominates the samples
//   --list         print benchmark names and exit
//   --calibration  measure only the built-in calibration kernel: a fixed dependent arithmetic chain whose
//                  time tracks the core's clock. bench_check.py uses it to correct for frequency and
//                  thermal changes within and between runs
//                  With --soak <seconds>: run the kernel continuously for that long in this one process and
//                  report the median of its final quarter, the SUSTAINED speed. Some machines (a laptop in
//                  Windows' "Best power efficiency" mode) hold boost for about 3 s and then fall to ~0.4x, and
//                  only within one long-lived process: separate short processes each start at boost.
//   --metrics-only run each selected workload once (no timing, no nanobench) and write the deterministic metrics
//                  registered with BenchRunner::metric() as {"metrics":[{"name","value","unit","kind"}]} to the
//                  --json file, or to stdout without one. `bench_check.py metrics` judges them against a spec.
//   --smoke        run each selected workload once (one iteration, no timing) and report only through the exit
//                  status: 0 when every workload ran, 1 when none matched. Never a timing verdict.
//
// Metrics. runner.metric("sizeof Vec3<double>", sizeof(Vec3<double>), "B") registers a value that does not depend on the
// machine: kind exact (default; it must equal the spec's expected value) or max (a ceiling, such as iterations or bytes).
// Call it before the runs, or inside a workload (which --metrics-only and --smoke execute once); the last value of a
// name wins. Metrics are only written by --metrics-only.
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
#include <utility>
#include <vector>

namespace sub0bench
{

enum class MetricKind
{
    exact,
    max,
};

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
            else if (arg == "--exact" && hasValue)
            {
                exact_ = argv[++i];
            }
            else if (arg == "--epochs" && hasValue)
            {
                epochs_ = std::atoi(argv[++i]);
            }
            else if (arg == "--list")
            {
                list_ = true;
            }
            else if (arg == "--calibration")
            {
                calibration_ = true;
            }
            else if (arg == "--metrics-only")
            {
                metricsOnly_ = true;
            }
            else if (arg == "--smoke")
            {
                smoke_ = true;
            }
            else
            {
                std::fprintf(stderr, "usage: %s [--filter text] [--exact name] [--epochs n] [--json file] [--soak seconds] [--list] [--calibration] [--metrics-only] [--smoke]\n", argv[0]);
                std::exit(2);
            }
        }
        bench_.title(title).warmup(100).minEpochIterations(10).performanceCounters(true);
        if (calibration_)
        {
            runCalibration();
        }
    }

    // The underlying nanobench configuration, for per-benchmark settings (units, batch, epochs).
    ankerl::nanobench::Bench& bench() noexcept
    {
        return bench_;
    }

    // Registers a deterministic metric (see the header comment). Re-registering a name replaces its value.
    void metric(const char* name, double value, const char* unit, MetricKind kind = MetricKind::exact)
    {
        for (auto& m : metrics_)
        {
            if (m.name == name)
            {
                m.value = value;
                m.unit = unit;
                m.kind = kind;
                return;
            }
        }
        metrics_.push_back(Metric{name, unit, value, kind});
    }

    template<typename Op>
    void run(const char* name, Op&& op)
    {
        if (calibration_ || !matches(name))
        {
            return;
        }
        if (list_)
        {
            std::printf("%s\n", name);
            return;
        }
        ++selected_;
        if (metricsOnly_ || smoke_)  // the workload once: its metric() calls register, a failure shows in the exit status
        {
            op();
            return;
        }
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
        if (epochs_ > 0)
        {
            bench_.epochs(static_cast<std::size_t>(epochs_));
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
        if (selected_ == 0 && !calibration_)
        {
            std::fprintf(stderr, "no benchmark matches the filter\n");
            return 1;
        }
        if (metricsOnly_)
        {
            writeMetrics();
            return 0;
        }
        if (smoke_)
        {
            return 0;
        }
        if (!json_.empty() && calibration_ && sustainedSeconds_ > 0.0)
        {
            std::ofstream out(json_);  // the shape bench_check.py reads (name, batch, median(elapsed))
            out << "{\"results\":[{\"name\":\"__calibration\",\"batch\":1,\"median(elapsed)\":" << sustainedSeconds_ << "}]}\n";
        }
        else if (!json_.empty() && soakSeconds_ <= 0.0)
        {
            std::ofstream out(json_);
            ankerl::nanobench::render(ankerl::nanobench::templates::json(), results_, out);
        }
        return 0;
    }

private:
    struct Metric
    {
        std::string name;
        std::string unit;
        double value;
        MetricKind kind;
    };

    static std::string jsonText(const std::string& text)
    {
        std::string out = "\"";
        for (const char c : text)
        {
            if (c == '"' || c == '\\')
            {
                out += '\\';
                out += c;
            }
            else if (static_cast<unsigned char>(c) < 0x20)
            {
                char buffer[8];
                std::snprintf(buffer, sizeof buffer, "\\u%04x", static_cast<unsigned>(c));
                out += buffer;
            }
            else
            {
                out += c;
            }
        }
        return out + "\"";
    }

    void writeMetrics() const
    {
        std::string text = "{\"metrics\":[";
        for (std::size_t i = 0; i < metrics_.size(); ++i)
        {
            char number[40];
            std::snprintf(number, sizeof number, "%.17g", metrics_[i].value);
            text += (i ? "," : "");
            text += "{\"name\":" + jsonText(metrics_[i].name) + ",\"value\":" + number + ",\"unit\":" + jsonText(metrics_[i].unit)
                  + ",\"kind\":" + (metrics_[i].kind == MetricKind::max ? "\"max\"" : "\"exact\"") + "}";
        }
        text += "]}\n";
        if (json_.empty())
        {
            std::fputs(text.c_str(), stdout);
        }
        else
        {
            std::ofstream(json_) << text;
        }
    }

    // A dependent chain: each step waits for the last, so its time tracks the clock, not memory or
    // vectorisation. The "__" prefix keeps it apart from real benchmarks.
    void runCalibration()
    {
        if (soakSeconds_ > 0.0)
        {
            runSustainedCalibration();
            return;
        }
        ankerl::nanobench::Bench cal;
        cal.title("calibration").warmup(100).minEpochIterations(2000).epochs(epochs_ > 0 ? static_cast<std::size_t>(epochs_) : 11U);
        // Coefficients read through volatile, so the compiler cannot fold the chain at compile time.
        static volatile double scale = 0.9999999;
        static volatile double offset = 1e-7;
        cal.run("__calibration", [] {
            const double k = scale;
            const double c = offset;
            double x = 1.0;
            for (int i = 0; i < 4096; ++i)
            {
                x = x * k + c;
            }
            ankerl::nanobench::doNotOptimizeAway(x);
        });
        results_.push_back(cal.results().back());
    }

    // The same chain as above, timed in slices of 2000 iterations back to back for soakSeconds_; the result is the median slice of
    // the final quarter, in seconds per iteration like nanobench's median(elapsed).
    void runSustainedCalibration()
    {
        static volatile double scale = 0.9999999;
        static volatile double offset = 1e-7;
        using Clock = std::chrono::steady_clock;
        const auto begin = Clock::now();
        const auto end = begin + std::chrono::duration<double>(soakSeconds_);
        std::vector<std::pair<double, double>> slices;  // (seconds since begin, seconds per iteration)
        double sink = 0.0;
        for (auto now = begin; now < end; now = Clock::now())
        {
            const double k = scale;
            const double c = offset;
            double x = 1.0;
            const auto t0 = Clock::now();
            for (int n = 0; n < 2000; ++n)
            {
                for (int i = 0; i < 4096; ++i)
                {
                    x = x * k + c;
                }
                ankerl::nanobench::doNotOptimizeAway(x);
            }
            const auto t1 = Clock::now();
            sink += x;
            slices.emplace_back(std::chrono::duration<double>(t0 - begin).count(), std::chrono::duration<double>(t1 - t0).count() / 2000.0);
        }
        std::vector<double> tail;
        for (const auto& [at, perIteration] : slices)
        {
            if (at >= soakSeconds_ * 0.75)
            {
                tail.push_back(perIteration);
            }
        }
        if (tail.empty() && !slices.empty())
        {
            tail.push_back(slices.back().second);
        }
        std::sort(tail.begin(), tail.end());
        sustainedSeconds_ = tail.empty() ? 0.0 : tail[tail.size() / 2];
        std::printf("sustained calibration over %.1f s: %.0f ns per iteration (%zu slices, sink %g)\n", soakSeconds_,
                    sustainedSeconds_ * 1e9, slices.size(), sink);
    }

    // The exact name when --exact is given; otherwise a case-insensitive substring (empty selects all).
    [[nodiscard]] bool matches(std::string_view name) const
    {
        if (!exact_.empty())
        {
            return name == exact_;
        }
        const auto lower = [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); };
        return filter_.empty()
            || std::search(name.begin(), name.end(), filter_.begin(), filter_.end(),
                           [&](char a, char b) { return lower(a) == lower(b); }) != name.end();
    }

    ankerl::nanobench::Bench bench_;
    std::vector<ankerl::nanobench::Result> results_;
    std::vector<Metric> metrics_;
    std::string filter_;
    std::string json_;
    std::string exact_;
    double soakSeconds_ = 0.0;
    double sustainedSeconds_ = 0.0;
    int epochs_ = 0;
    bool list_ = false;
    bool calibration_ = false;
    bool metricsOnly_ = false;
    bool smoke_ = false;
    int selected_ = 0;
};

} // namespace sub0bench
