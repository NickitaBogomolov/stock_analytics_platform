#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

#include "market/Analyzer.hpp"

using namespace market;

namespace {
std::vector<Candle> series(const std::vector<double>& closes) {
    std::vector<Candle> out;
    for (std::size_t i = 0; i < closes.size(); ++i) {
        char date[32];
        std::snprintf(date, sizeof date, "2024-%02zu-%02zu", i / 28 + 1, i % 28 + 1);
        out.push_back({date, closes[i], closes[i], closes[i], closes[i], 100});
    }
    return out;
}

std::vector<double> linear(std::size_t n, double start, double step) {
    std::vector<double> v;
    for (std::size_t i = 0; i < n; ++i) v.push_back(start + step * i);
    return v;
}
}  // namespace

TEST(Analyzer, RisingMarketIsUptrendAndOverbought) {
    auto r = analyze("UP", series(linear(40, 100, 1)), {5, 20, 14, 0});
    EXPECT_EQ(r.count, 40u);
    EXPECT_EQ(r.trend, "UPTREND");
    EXPECT_EQ(r.rsiZone, "OVERBOUGHT");
    EXPECT_NEAR(r.totalReturnPct, 39.0, 1e-9);
    EXPECT_DOUBLE_EQ(r.maxDrawdownPct, 0.0);
    EXPECT_GT(r.sharpeRatio, 0);
    EXPECT_EQ(r.firstDate, "2024-01-01");
}

TEST(Analyzer, FallingMarketIsDowntrendAndOversold) {
    auto r = analyze("DOWN", series(linear(40, 200, -2)), {5, 20, 14, 0});
    EXPECT_EQ(r.trend, "DOWNTREND");
    EXPECT_EQ(r.rsiZone, "OVERSOLD");
    EXPECT_NEAR(r.maxDrawdownPct, (200.0 - 122.0) / 200.0 * 100, 1e-9);
}

TEST(Analyzer, FlatMarketIsSideways) {
    auto r = analyze("FLAT", series(std::vector<double>(40, 50)), {5, 20, 14, 0});
    EXPECT_EQ(r.trend, "SIDEWAYS");
    EXPECT_EQ(r.rsiZone, "NEUTRAL");
    EXPECT_DOUBLE_EQ(r.annualVolatilityPct, 0.0);
    EXPECT_DOUBLE_EQ(r.sharpeRatio, 0.0);
}

TEST(Analyzer, MinimumLengthBoundary) {
    AnalysisParams p{5, 20, 14, 0};
    EXPECT_NO_THROW(analyze("T", series(linear(21, 10, 0.1)), p));
    EXPECT_THROW(analyze("T", series(linear(20, 10, 0.1)), p), MarketDataError);
}

TEST(Analyzer, ValidatesParams) {
    auto data = series(linear(50, 10, 0.1));
    EXPECT_THROW(analyze("", data), MarketDataError);
    EXPECT_THROW(analyze("T", data, {20, 20, 14, 0}), MarketDataError);
    EXPECT_THROW(analyze("T", data, {0, 20, 14, 0}), MarketDataError);
    EXPECT_THROW(analyze("T", data, {5, 20, 0, 0}), MarketDataError);
}

TEST(Report, JsonContainsMetricsAndNullsForWarmup) {
    auto r = analyze("A\"B", series(linear(30, 10, 1)), {2, 5, 3, 0});
    std::string json = toJson(r);
    EXPECT_NE(json.find("\"ticker\":\"A\\\"B\""), std::string::npos);
    EXPECT_NE(json.find("\"smaSlow\":[null,null,null,null,"), std::string::npos);
    EXPECT_NE(json.find("\"trend\":\"UPTREND\""), std::string::npos);
    EXPECT_EQ(json.find("nan"), std::string::npos);
}

TEST(Report, WritesFileAndFailsOnBadPath) {
    auto r = analyze("T", series(linear(30, 10, 1)), {2, 5, 3, 0});
    std::string path = ::testing::TempDir() + "report_test.json";
    writeReport(r, path);
    std::ifstream in(path);
    std::stringstream buf;
    buf << in.rdbuf();
    EXPECT_EQ(buf.str(), toJson(r) + "\n");
    std::remove(path.c_str());
    EXPECT_THROW(writeReport(r, "/nonexistent-dir/x.json"), MarketDataError);
}
