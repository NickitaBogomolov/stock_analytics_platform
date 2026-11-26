#include "market/Analyzer.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

#include "market/Indicators.hpp"
#include "market/Json.hpp"

namespace market {

AnalysisReport analyze(const std::string& ticker, const std::vector<Candle>& candles, const AnalysisParams& p) {
    if (ticker.empty()) throw MarketDataError("ticker must not be empty");
    if (p.fastPeriod == 0 || p.fastPeriod >= p.slowPeriod)
        throw MarketDataError("fast period must be positive and less than slow period");
    if (p.rsiPeriod == 0) throw MarketDataError("RSI period must be positive");
    std::size_t need = std::max(p.slowPeriod, p.rsiPeriod) + 1;
    if (candles.size() < need)
        throw MarketDataError("not enough quotes: need at least " + std::to_string(need) + ", got " +
                              std::to_string(candles.size()));

    AnalysisReport r;
    r.ticker = ticker;
    r.params = p;
    r.count = candles.size();
    for (const auto& c : candles) {
        r.dates.push_back(c.date);
        r.close.push_back(c.close);
    }
    r.firstDate = r.dates.front();
    r.lastDate = r.dates.back();
    r.lastClose = r.close.back();

    auto returns = dailyReturns(r.close);
    r.totalReturnPct = (r.close.back() / r.close.front() - 1.0) * 100.0;
    r.annualVolatilityPct = annualizedVolatility(returns) * 100.0;
    r.maxDrawdownPct = maxDrawdown(r.close) * 100.0;
    r.sharpeRatio = sharpeRatio(returns, p.riskFreeRatePct / 100.0);

    r.smaFast = sma(r.close, p.fastPeriod);
    r.smaSlow = sma(r.close, p.slowPeriod);
    r.rsiSeries = rsi(r.close, p.rsiPeriod);
    r.rsi = r.rsiSeries.back();
    r.signals = crossoverSignals(r.dates, r.close, p.fastPeriod, p.slowPeriod);

    double fast = r.smaFast.back(), slow = r.smaSlow.back();
    if (fast > slow && r.lastClose > slow) r.trend = "UPTREND";
    else if (fast < slow && r.lastClose < slow) r.trend = "DOWNTREND";
    else r.trend = "SIDEWAYS";
    r.rsiZone = r.rsi >= 70 ? "OVERBOUGHT" : (r.rsi <= 30 ? "OVERSOLD" : "NEUTRAL");
    return r;
}

using json::array;
using json::num;
using json::str;

std::string toJson(const AnalysisReport& r) {
    std::ostringstream os;
    os << "{\"engine\":\"cpp\",\"ticker\":" << str(r.ticker) << ",\"params\":{\"fastPeriod\":" << r.params.fastPeriod
       << ",\"slowPeriod\":" << r.params.slowPeriod << ",\"rsiPeriod\":" << r.params.rsiPeriod
       << ",\"riskFreeRatePct\":" << num(r.params.riskFreeRatePct) << "},\"count\":" << r.count
       << ",\"firstDate\":" << str(r.firstDate) << ",\"lastDate\":" << str(r.lastDate)
       << ",\"lastClose\":" << num(r.lastClose) << ",\"totalReturnPct\":" << num(r.totalReturnPct)
       << ",\"annualVolatilityPct\":" << num(r.annualVolatilityPct) << ",\"maxDrawdownPct\":" << num(r.maxDrawdownPct)
       << ",\"sharpeRatio\":" << num(r.sharpeRatio) << ",\"rsi\":" << num(r.rsi) << ",\"trend\":" << str(r.trend)
       << ",\"rsiZone\":" << str(r.rsiZone) << ",\"series\":{\"dates\":" << array(r.dates, str)
       << ",\"close\":" << array(r.close, num) << ",\"smaFast\":" << array(r.smaFast, num)
       << ",\"smaSlow\":" << array(r.smaSlow, num) << ",\"rsi\":" << array(r.rsiSeries, num) << "},\"signals\":"
       << array(r.signals, [](const TradeSignal& s) {
              return "{\"date\":" + str(s.date) + ",\"type\":\"" + (s.type == SignalType::Buy ? "BUY" : "SELL") +
                     "\",\"price\":" + num(s.price) + "}";
          })
       << "}";
    return os.str();
}

void writeReport(const AnalysisReport& report, const std::string& path) {
    std::ofstream out(path);
    if (!out) throw MarketDataError("cannot write report to: " + path);
    out << toJson(report) << '\n';
    if (!out) throw MarketDataError("write failed: " + path);
}

}  // namespace market
