#include "market/Indicators.hpp"

#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace market {
namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kTradingDays = 252.0;

void checkPeriod(std::size_t period, std::size_t size, std::size_t minExtra = 0) {
    if (period == 0) throw std::invalid_argument("period must be positive");
    if (size < period + minExtra) throw std::invalid_argument("not enough data for period " + std::to_string(period));
}

double mean(const std::vector<double>& v) { return std::accumulate(v.begin(), v.end(), 0.0) / v.size(); }

double sampleStd(const std::vector<double>& v) {
    if (v.size() < 2) throw std::invalid_argument("need at least 2 returns");
    double m = mean(v), acc = 0;
    for (double x : v) acc += (x - m) * (x - m);
    return std::sqrt(acc / (v.size() - 1));
}

}  // namespace

std::vector<double> sma(const std::vector<double>& values, std::size_t period) {
    checkPeriod(period, values.size());
    std::vector<double> out(values.size(), kNaN);
    double sum = 0;
    for (std::size_t i = 0; i < values.size(); ++i) {
        sum += values[i];
        if (i >= period) sum -= values[i - period];
        if (i + 1 >= period) out[i] = sum / period;
    }
    return out;
}

std::vector<double> ema(const std::vector<double>& values, std::size_t period) {
    checkPeriod(period, values.size());
    std::vector<double> out(values.size(), kNaN);
    double k = 2.0 / (period + 1);
    double prev = std::accumulate(values.begin(), values.begin() + period, 0.0) / period;
    out[period - 1] = prev;
    for (std::size_t i = period; i < values.size(); ++i) out[i] = prev = values[i] * k + prev * (1 - k);
    return out;
}

std::vector<double> rsi(const std::vector<double>& values, std::size_t period) {
    checkPeriod(period, values.size(), 1);
    std::vector<double> out(values.size(), kNaN);
    double gain = 0, loss = 0;
    auto value = [](double g, double l) { return l == 0 ? (g == 0 ? 50.0 : 100.0) : 100.0 - 100.0 / (1 + g / l); };
    for (std::size_t i = 1; i <= period; ++i) {
        double d = values[i] - values[i - 1];
        (d > 0 ? gain : loss) += std::fabs(d);
    }
    gain /= period;
    loss /= period;
    out[period] = value(gain, loss);
    for (std::size_t i = period + 1; i < values.size(); ++i) {
        double d = values[i] - values[i - 1];
        gain = (gain * (period - 1) + (d > 0 ? d : 0)) / period;
        loss = (loss * (period - 1) + (d < 0 ? -d : 0)) / period;
        out[i] = value(gain, loss);
    }
    return out;
}

std::vector<double> dailyReturns(const std::vector<double>& closes) {
    std::vector<double> out;
    for (std::size_t i = 1; i < closes.size(); ++i) {
        if (closes[i - 1] <= 0) throw std::invalid_argument("price must be positive");
        out.push_back(closes[i] / closes[i - 1] - 1.0);
    }
    return out;
}

double annualizedVolatility(const std::vector<double>& returns) {
    return sampleStd(returns) * std::sqrt(kTradingDays);
}

double maxDrawdown(const std::vector<double>& closes) {
    double peak = 0, worst = 0;
    for (double c : closes) {
        peak = std::max(peak, c);
        if (peak > 0) worst = std::max(worst, (peak - c) / peak);
    }
    return worst;
}

double sharpeRatio(const std::vector<double>& returns, double riskFreeAnnual) {
    double sd = sampleStd(returns);
    if (sd == 0) return 0.0;
    return (mean(returns) * kTradingDays - riskFreeAnnual) / (sd * std::sqrt(kTradingDays));
}

std::vector<TradeSignal> crossoverSignals(const std::vector<std::string>& dates, const std::vector<double>& closes,
                                          std::size_t fast, std::size_t slow) {
    if (fast >= slow) throw std::invalid_argument("fast period must be less than slow period");
    if (dates.size() != closes.size()) throw std::invalid_argument("dates and closes size mismatch");
    auto f = sma(closes, fast), s = sma(closes, slow);
    std::vector<TradeSignal> out;
    for (std::size_t i = slow; i < closes.size(); ++i) {
        double prevDiff = f[i - 1] - s[i - 1], diff = f[i] - s[i];
        if (prevDiff <= 0 && diff > 0) out.push_back({dates[i], SignalType::Buy, closes[i]});
        else if (prevDiff >= 0 && diff < 0) out.push_back({dates[i], SignalType::Sell, closes[i]});
    }
    return out;
}

}  // namespace market
