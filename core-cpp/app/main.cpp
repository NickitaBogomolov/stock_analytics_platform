// Консольный интерфейс C++ модуля.
//   market_cli analyze    <quotes.csv> [--ticker T] [--fast N] [--slow N] [--rsi N] [--rf PCT] [--out F] [--summary]
//   market_cli backtest   <quotes.csv> [--ticker T] [--commission PCT] [--train SHARE] [--fast-min/max/step N] [--slow-min/max/step N] [--rf PCT]
//   market_cli montecarlo <quotes.csv> [--ticker T] [--paths N] [--horizon DAYS] [--seed N]
//   market_cli portfolio  <a.csv> <b.csv> ... [--tickers A,B,...] [--samples N] [--seed N] [--rf PCT]
// Старый вызов «market_cli <quotes.csv> ...» равносилен analyze. Результат — JSON в stdout.
#include <chrono>
#include <cstdio>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "market/Analyzer.hpp"
#include "market/CsvLoader.hpp"
#include "market/Json.hpp"
#include "market/Quant.hpp"

namespace {

constexpr int kExitDataError = 1;
constexpr int kExitUsage = 2;

struct Args {
    std::string command;
    std::vector<std::string> files;
    std::map<std::string, std::string> opts;
    bool flag(const std::string& k) const { return opts.count(k) > 0; }
    std::string get(const std::string& k, const std::string& def = "") const {
        auto it = opts.find(k);
        return it == opts.end() ? def : it->second;
    }
};

void usage() {
    std::cerr << "Usage:\n"
                 "  market_cli analyze    <quotes.csv> [--ticker T] [--fast N] [--slow N] [--rsi N] [--rf PCT] [--out F] [--summary]\n"
                 "  market_cli backtest   <quotes.csv> [--ticker T] [--commission PCT] [--train 0.7]\n"
                 "                        [--fast-min N --fast-max N --fast-step N --slow-min N --slow-max N --slow-step N] [--rf PCT]\n"
                 "  market_cli montecarlo <quotes.csv> [--ticker T] [--paths N] [--horizon DAYS] [--seed N]\n"
                 "  market_cli portfolio  <a.csv> <b.csv> ... [--tickers A,B,...] [--samples N] [--seed N] [--rf PCT]\n";
}

std::size_t toSize(const std::string& s) {
    std::size_t pos = 0;
    long long v = std::stoll(s, &pos);
    if (pos != s.size() || v <= 0) throw std::invalid_argument("positive integer expected: " + s);
    return static_cast<std::size_t>(v);
}

double toDouble(const std::string& s) {
    std::size_t pos = 0;
    double v = std::stod(s, &pos);
    if (pos != s.size()) throw std::invalid_argument("number expected: " + s);
    return v;
}

std::string baseName(const std::string& path) {
    auto slash = path.find_last_of("/\\");
    std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    auto dot = name.find_last_of('.');
    return dot == std::string::npos ? name : name.substr(0, dot);
}

Args parse(int argc, char** argv) {
    static const std::map<std::string, bool> known = {  // опция -> есть ли значение
        {"--ticker", true}, {"--fast", true}, {"--slow", true}, {"--rsi", true}, {"--rf", true}, {"--out", true},
        {"--summary", false}, {"--commission", true}, {"--train", true}, {"--fast-min", true}, {"--fast-max", true},
        {"--fast-step", true}, {"--slow-min", true}, {"--slow-max", true}, {"--slow-step", true}, {"--paths", true},
        {"--horizon", true}, {"--seed", true}, {"--tickers", true}, {"--samples", true}};
    Args a;
    int i = 1;
    std::string first = argc > 1 ? argv[1] : "";
    if (first == "analyze" || first == "backtest" || first == "montecarlo" || first == "portfolio") {
        a.command = first;
        i = 2;
    } else {
        a.command = "analyze";  // обратная совместимость
    }
    for (; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg.rfind("--", 0) != 0) {
            a.files.push_back(arg);
            continue;
        }
        auto k = known.find(arg);
        if (k == known.end()) throw std::invalid_argument("unknown option: " + arg);
        if (!k->second) {
            a.opts[arg] = "1";
            continue;
        }
        if (i + 1 >= argc) throw std::invalid_argument("missing value for " + arg);
        a.opts[arg] = argv[++i];
    }
    if (a.files.empty()) throw std::invalid_argument("quotes file is required");
    if (a.command != "portfolio" && a.files.size() != 1) throw std::invalid_argument("exactly one quotes file expected");
    return a;
}

void printSummary(const market::AnalysisReport& r) {
    std::printf("Ticker:            %s\n", r.ticker.c_str());
    std::printf("Period:            %s .. %s (%zu sessions)\n", r.firstDate.c_str(), r.lastDate.c_str(), r.count);
    std::printf("Last close:        %.2f\n", r.lastClose);
    std::printf("Total return:      %.2f %%\n", r.totalReturnPct);
    std::printf("Volatility (ann.): %.2f %%\n", r.annualVolatilityPct);
    std::printf("Max drawdown:      %.2f %%\n", r.maxDrawdownPct);
    std::printf("Sharpe ratio:      %.3f\n", r.sharpeRatio);
    std::printf("RSI(%zu):           %.2f (%s)\n", r.params.rsiPeriod, r.rsi, r.rsiZone.c_str());
    std::printf("Trend:             %s\n", r.trend.c_str());
    std::printf("Signals:           %zu\n", r.signals.size());
}

std::vector<double> closesOf(const std::vector<market::Candle>& c) {
    std::vector<double> v;
    for (const auto& x : c) v.push_back(x.close);
    return v;
}

/** Добавляет время расчёта в JSON-объект. */
std::string withTiming(const std::string& json, double ms) {
    return json.substr(0, json.size() - 1) + ",\"computeMs\":" + market::json::num(ms) + "}";
}

std::string run(const Args& a, bool& printed) {
    using namespace market;
    std::string ticker = a.get("--ticker", baseName(a.files[0]));
    if (a.command == "analyze") {
        AnalysisParams p;
        if (a.flag("--fast")) p.fastPeriod = toSize(a.get("--fast"));
        if (a.flag("--slow")) p.slowPeriod = toSize(a.get("--slow"));
        if (a.flag("--rsi")) p.rsiPeriod = toSize(a.get("--rsi"));
        if (a.flag("--rf")) p.riskFreeRatePct = toDouble(a.get("--rf"));
        auto report = analyze(ticker, loadCsvFile(a.files[0]), p);
        if (a.flag("--out")) writeReport(report, a.get("--out"));
        if (a.flag("--summary")) {
            printSummary(report);
            printed = true;
        }
        return a.flag("--out") || printed ? "" : toJson(report);
    }
    auto t0 = std::chrono::steady_clock::now();
    auto elapsed = [&] { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(); };
    if (a.command == "backtest") {
        auto candles = loadCsvFile(a.files[0]);
        BacktestParams p;
        auto sz = [&](const char* k, std::size_t& v) { if (a.flag(k)) v = toSize(a.get(k)); };
        sz("--fast-min", p.fastMin), sz("--fast-max", p.fastMax), sz("--fast-step", p.fastStep);
        sz("--slow-min", p.slowMin), sz("--slow-max", p.slowMax), sz("--slow-step", p.slowStep);
        if (a.flag("--commission")) p.commissionPct = toDouble(a.get("--commission"));
        if (a.flag("--train")) p.trainShare = toDouble(a.get("--train"));
        if (a.flag("--rf")) p.riskFreeRatePct = toDouble(a.get("--rf"));
        t0 = std::chrono::steady_clock::now();
        auto r = backtestSmaGrid(closesOf(candles), p);
        double ms = elapsed();
        std::vector<std::string> dates;
        for (const auto& c : candles) dates.push_back(c.date);
        return withTiming(toJson(r, dates, ticker), ms);
    }
    if (a.command == "montecarlo") {
        auto candles = loadCsvFile(a.files[0]);
        MonteCarloParams p;
        if (a.flag("--paths")) p.paths = toSize(a.get("--paths"));
        if (a.flag("--horizon")) p.horizon = toSize(a.get("--horizon"));
        if (a.flag("--seed")) p.seed = toSize(a.get("--seed"));
        t0 = std::chrono::steady_clock::now();
        auto r = monteCarlo(closesOf(candles), p);
        return withTiming(toJson(r, ticker, candles.back().date), elapsed());
    }
    // portfolio
    std::vector<std::string> tickers;
    if (a.flag("--tickers")) {
        std::stringstream ss(a.get("--tickers"));
        for (std::string t; std::getline(ss, t, ',');) tickers.push_back(t);
    } else {
        for (const auto& f : a.files) tickers.push_back(baseName(f));
    }
    if (tickers.size() != a.files.size()) throw std::invalid_argument("--tickers must list one name per file");
    std::vector<std::vector<Candle>> series;
    for (const auto& f : a.files) series.push_back(loadCsvFile(f));
    PortfolioParams p;
    if (a.flag("--samples")) p.samples = toSize(a.get("--samples"));
    if (a.flag("--seed")) p.seed = toSize(a.get("--seed"));
    if (a.flag("--rf")) p.riskFreeRatePct = toDouble(a.get("--rf"));
    t0 = std::chrono::steady_clock::now();
    auto r = optimizePortfolio(tickers, series, p);
    return withTiming(toJson(r), elapsed());
}

}  // namespace

int main(int argc, char** argv) {
    Args args;
    try {
        args = parse(argc, argv);
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
        usage();
        return kExitUsage;
    }
    try {
        bool printed = false;
        std::string out = run(args, printed);
        if (!out.empty()) std::cout << out << '\n';
    } catch (const std::invalid_argument& e) {
        std::cerr << "error: " << e.what() << '\n';
        usage();
        return kExitUsage;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
        return kExitDataError;
    }
    return 0;
}
