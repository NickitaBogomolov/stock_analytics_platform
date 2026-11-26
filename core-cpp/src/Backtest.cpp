#include <algorithm>
#include <cmath>
#include <limits>

#include "market/Json.hpp"
#include "market/Quant.hpp"

namespace market {
namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kDays = 252.0;

/** Статистика по дневным доходностям стратегии и кривой капитала. */
StrategyStats stats(const std::vector<double>& rets, const std::vector<double>& eq, int trades, std::size_t inMarket,
                    double rfPct) {
    StrategyStats s;
    s.days = rets.size();
    s.trades = trades;
    if (rets.empty()) return s;
    double mean = 0;
    for (double r : rets) mean += r;
    mean /= rets.size();
    double var = 0;
    for (double r : rets) var += (r - mean) * (r - mean);
    double sd = rets.size() > 1 ? std::sqrt(var / (rets.size() - 1)) : 0;
    double total = eq.back() / eq.front();
    s.totalReturnPct = (total - 1) * 100;
    s.annualReturnPct = (std::pow(total, kDays / rets.size()) - 1) * 100;
    s.annualVolatilityPct = sd * std::sqrt(kDays) * 100;
    s.sharpe = sd > 0 ? (mean * kDays - rfPct / 100) / (sd * std::sqrt(kDays)) : 0;
    double peak = eq.front(), mdd = 0;
    for (double v : eq) {
        peak = std::max(peak, v);
        mdd = std::max(mdd, (peak - v) / peak);
    }
    s.maxDrawdownPct = mdd * 100;
    s.exposurePct = 100.0 * inMarket / rets.size();
    return s;
}

/** Префиксные суммы — SMA любого периода за O(1). */
std::vector<double> prefix(const std::vector<double>& v) {
    std::vector<double> p(v.size() + 1, 0);
    for (std::size_t i = 0; i < v.size(); ++i) p[i + 1] = p[i] + v[i];
    return p;
}

double smaAt(const std::vector<double>& pre, std::size_t i, std::size_t n) { return (pre[i + 1] - pre[i + 1 - n]) / n; }

StrategyStats run(const std::vector<double>& closes, const std::vector<double>& pre, std::size_t fast, std::size_t slow,
                  std::size_t from, std::size_t to, double commission, double rfPct, std::vector<double>* equity) {
    std::vector<double> rets, eq{100.0};
    int trades = 0;
    std::size_t inMarket = 0;
    int pos = 0;
    for (std::size_t t = from; t + 1 < to; ++t) {
        int want = fast == 0 ? 1 : (t + 1 >= slow && smaAt(pre, t, fast) > smaAt(pre, t, slow) ? 1 : 0);
        double r = 0;
        if (want != pos) {
            r -= commission;  // комиссия при входе/выходе
            ++trades;
            pos = want;
        }
        if (pos) {
            r += closes[t + 1] / closes[t] - 1;
            ++inMarket;
        }
        rets.push_back(r);
        eq.push_back(eq.back() * (1 + r));
    }
    if (equity) *equity = eq;
    return stats(rets, eq, trades, inMarket, rfPct);
}

}  // namespace

StrategyStats simulateSma(const std::vector<double>& closes, std::size_t fast, std::size_t slow, std::size_t from,
                          std::size_t to, double commissionPct, double riskFreePct, std::vector<double>* equity) {
    if (fast == 0 || fast >= slow) throw MarketDataError("fast period must be positive and less than slow period");
    if (to > closes.size() || from + 2 > to) throw MarketDataError("invalid backtest range");
    return run(closes, prefix(closes), fast, slow, from, to, commissionPct / 100, riskFreePct, equity);
}

StrategyStats buyAndHold(const std::vector<double>& closes, std::size_t from, std::size_t to, double commissionPct,
                         double riskFreePct, std::vector<double>* equity) {
    if (to > closes.size() || from + 2 > to) throw MarketDataError("invalid backtest range");
    return run(closes, prefix(closes), 0, 0, from, to, commissionPct / 100, riskFreePct, equity);
}

BacktestResult backtestSmaGrid(const std::vector<double>& closes, const BacktestParams& p) {
    if (p.fastMin == 0 || p.fastStep == 0 || p.slowStep == 0 || p.fastMin > p.fastMax || p.slowMin > p.slowMax)
        throw MarketDataError("invalid parameter grid");
    if (p.trainShare < 0.3 || p.trainShare > 0.95) throw MarketDataError("train share must be within 0.3..0.95");
    BacktestResult r;
    const std::size_t n = closes.size();
    r.trainSize = static_cast<std::size_t>(n * p.trainShare);
    if (r.trainSize < p.slowMin + 20 || n - r.trainSize < 20)
        throw MarketDataError("not enough quotes for a backtest: need at least " + std::to_string((p.slowMin + 20) * 10 / 7 + 20));
    for (std::size_t f = p.fastMin; f <= p.fastMax; f += p.fastStep) r.fasts.push_back(f);
    // медленная SMA должна успеть «прогреться» на обучающей части
    for (std::size_t s = p.slowMin; s <= p.slowMax && s + 20 <= r.trainSize; s += p.slowStep) r.slows.push_back(s);

    const auto pre = prefix(closes);
    const double c = p.commissionPct / 100;
    double best = -std::numeric_limits<double>::infinity();
    r.sharpeGrid.assign(r.fasts.size(), std::vector<double>(r.slows.size(), kNaN));
    for (std::size_t i = 0; i < r.fasts.size(); ++i)
        for (std::size_t j = 0; j < r.slows.size(); ++j) {
            if (r.fasts[i] >= r.slows[j]) continue;
            auto s = run(closes, pre, r.fasts[i], r.slows[j], 0, r.trainSize, c, p.riskFreeRatePct, nullptr);
            r.sharpeGrid[i][j] = s.sharpe;
            ++r.combos;
            if (s.sharpe > best) {
                best = s.sharpe;
                r.bestFast = r.fasts[i];
                r.bestSlow = r.slows[j];
            }
        }
    if (r.combos == 0) throw MarketDataError("no valid (fast, slow) pairs in the grid");

    r.inSample = run(closes, pre, r.bestFast, r.bestSlow, 0, r.trainSize, c, p.riskFreeRatePct, nullptr);
    r.outSample = run(closes, pre, r.bestFast, r.bestSlow, r.trainSize - 1, n, c, p.riskFreeRatePct, nullptr);
    r.buyHoldIn = run(closes, pre, 0, 0, 0, r.trainSize, c, p.riskFreeRatePct, nullptr);
    r.buyHoldOut = run(closes, pre, 0, 0, r.trainSize - 1, n, c, p.riskFreeRatePct, nullptr);
    run(closes, pre, r.bestFast, r.bestSlow, 0, n, c, p.riskFreeRatePct, &r.equityStrategy);
    run(closes, pre, 0, 0, 0, n, c, p.riskFreeRatePct, &r.equityBuyHold);
    return r;
}

namespace {
std::string statsJson(const StrategyStats& s) {
    using namespace json;
    return object({{"totalReturnPct", num(s.totalReturnPct)}, {"annualReturnPct", num(s.annualReturnPct)},
                   {"annualVolatilityPct", num(s.annualVolatilityPct)}, {"sharpe", num(s.sharpe)},
                   {"maxDrawdownPct", num(s.maxDrawdownPct)}, {"exposurePct", num(s.exposurePct)},
                   {"trades", std::to_string(s.trades)}, {"days", std::to_string(s.days)}});
}
}  // namespace

std::string toJson(const BacktestResult& r, const std::vector<std::string>& dates, const std::string& ticker) {
    using namespace json;
    auto sizes = [](const std::vector<std::size_t>& v) { return array(v, [](std::size_t x) { return std::to_string(x); }); };
    return object({{"kind", str("backtest")}, {"ticker", str(ticker)},
                   {"bestFast", std::to_string(r.bestFast)}, {"bestSlow", std::to_string(r.bestSlow)},
                   {"combos", std::to_string(r.combos)}, {"splitDate", str(dates.at(r.trainSize - 1))},
                   {"fasts", sizes(r.fasts)}, {"slows", sizes(r.slows)}, {"sharpeGrid", array(r.sharpeGrid, nums)},
                   {"inSample", statsJson(r.inSample)}, {"outSample", statsJson(r.outSample)},
                   {"buyHoldIn", statsJson(r.buyHoldIn)}, {"buyHoldOut", statsJson(r.buyHoldOut)},
                   {"dates", array(dates, str)}, {"equityStrategy", nums(r.equityStrategy)},
                   {"equityBuyHold", nums(r.equityBuyHold)}});
}

}  // namespace market
