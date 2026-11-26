#include <algorithm>
#include <cmath>
#include <random>

#include "market/Json.hpp"
#include "market/Quant.hpp"

namespace market {

MonteCarloResult monteCarlo(const std::vector<double>& closes, const MonteCarloParams& p) {
    if (closes.size() < 61) throw MarketDataError("not enough quotes for Monte Carlo: need at least 61");
    if (p.paths < 100 || p.paths > 1000000) throw MarketDataError("paths must be within 100..1000000");
    if (p.horizon < 1 || p.horizon > 756) throw MarketDataError("horizon must be within 1..756 trading days");

    std::vector<double> logRets;
    for (std::size_t i = 1; i < closes.size(); ++i) logRets.push_back(std::log(closes[i] / closes[i - 1]));

    MonteCarloResult r;
    r.paths = p.paths;
    r.horizon = p.horizon;
    r.sample = logRets.size();
    r.lastClose = closes.back();
    r.levels = {5, 25, 50, 75, 95};

    // paths x (horizon + 1) накопленных лог-доходностей; столбец 0 = 0 (сегодня)
    std::mt19937_64 rng(p.seed);
    std::uniform_int_distribution<std::size_t> pick(0, logRets.size() - 1);
    std::vector<std::vector<double>> byDay(p.horizon + 1, std::vector<double>(p.paths, 0.0));
    for (std::size_t k = 0; k < p.paths; ++k) {
        double acc = 0;
        for (std::size_t d = 1; d <= p.horizon; ++d) {
            acc += logRets[pick(rng)];
            byDay[d][k] = acc;
        }
    }

    auto quantile = [](std::vector<double>& v, double q) {
        std::size_t idx = std::min(v.size() - 1, static_cast<std::size_t>(q * (v.size() - 1) + 0.5));
        std::nth_element(v.begin(), v.begin() + idx, v.end());
        return v[idx];
    };
    r.fan.assign(r.levels.size(), std::vector<double>(p.horizon + 1, r.lastClose));
    for (std::size_t d = 1; d <= p.horizon; ++d)
        for (std::size_t l = 0; l < r.levels.size(); ++l)
            r.fan[l][d] = r.lastClose * std::exp(quantile(byDay[d], r.levels[l] / 100));

    // распределение итоговой простой доходности, %
    std::vector<double> fin(p.paths);
    for (std::size_t k = 0; k < p.paths; ++k) fin[k] = (std::exp(byDay[p.horizon][k]) - 1) * 100;
    std::sort(fin.begin(), fin.end());
    double sum = 0, losses = 0;
    for (double x : fin) {
        sum += x;
        if (x < 0) ++losses;
    }
    r.meanReturnPct = sum / fin.size();
    r.probLossPct = 100 * losses / fin.size();
    auto tail = [&](double alpha, double& var, double& cvar) {
        std::size_t cut = std::max<std::size_t>(1, static_cast<std::size_t>(fin.size() * alpha));
        var = -fin[cut - 1];
        double s = 0;
        for (std::size_t i = 0; i < cut; ++i) s += fin[i];
        cvar = -s / cut;  // ожидаемые потери в худших alpha случаев (Expected Shortfall)
    };
    tail(0.05, r.var95, r.cvar95);
    tail(0.01, r.var99, r.cvar99);

    const std::size_t bins = 40;
    double lo = fin[static_cast<std::size_t>(fin.size() * 0.005)], hi = fin[static_cast<std::size_t>(fin.size() * 0.995)];
    if (hi <= lo) hi = lo + 1;
    double w = (hi - lo) / bins;
    r.histCounts.assign(bins, 0);
    for (std::size_t b = 0; b <= bins; ++b) r.histEdges.push_back(lo + b * w);
    for (double x : fin) {
        auto b = static_cast<long>((x - lo) / w);
        r.histCounts[static_cast<std::size_t>(std::clamp<long>(b, 0, bins - 1))] += 1;
    }
    return r;
}

std::string toJson(const MonteCarloResult& r, const std::string& ticker, const std::string& lastDate) {
    using namespace json;
    return object({{"kind", str("montecarlo")}, {"ticker", str(ticker)}, {"lastDate", str(lastDate)},
                   {"paths", std::to_string(r.paths)}, {"horizon", std::to_string(r.horizon)},
                   {"sample", std::to_string(r.sample)}, {"lastClose", num(r.lastClose)},
                   {"meanReturnPct", num(r.meanReturnPct)}, {"probLossPct", num(r.probLossPct)},
                   {"var95", num(r.var95)}, {"var99", num(r.var99)}, {"cvar95", num(r.cvar95)}, {"cvar99", num(r.cvar99)},
                   {"levels", nums(r.levels)}, {"fan", array(r.fan, nums)},
                   {"histEdges", nums(r.histEdges)}, {"histCounts", nums(r.histCounts)}});
}

}  // namespace market
