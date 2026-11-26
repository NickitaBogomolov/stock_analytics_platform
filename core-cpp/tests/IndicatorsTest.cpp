#include <gtest/gtest.h>

#include <cmath>
#include <stdexcept>

#include "market/Indicators.hpp"

using namespace market;

TEST(Sma, ComputesRollingMeanWithWarmup) {
    auto r = sma({1, 2, 3, 4, 5}, 3);
    ASSERT_EQ(r.size(), 5u);
    EXPECT_TRUE(std::isnan(r[0]));
    EXPECT_TRUE(std::isnan(r[1]));
    EXPECT_DOUBLE_EQ(r[2], 2.0);
    EXPECT_DOUBLE_EQ(r[3], 3.0);
    EXPECT_DOUBLE_EQ(r[4], 4.0);
}

TEST(Sma, PeriodOneReturnsInput) {
    auto r = sma({7, 8, 9}, 1);
    EXPECT_DOUBLE_EQ(r[0], 7);
    EXPECT_DOUBLE_EQ(r[2], 9);
}

TEST(Sma, PeriodEqualToSizeGivesSingleValue) {
    auto r = sma({2, 4, 6}, 3);
    EXPECT_DOUBLE_EQ(r[2], 4.0);
}

TEST(Sma, RejectsZeroOrTooLargePeriod) {
    EXPECT_THROW(sma({1, 2, 3}, 0), std::invalid_argument);
    EXPECT_THROW(sma({1, 2, 3}, 4), std::invalid_argument);
    EXPECT_THROW(sma({}, 1), std::invalid_argument);
}

TEST(Ema, SeedsWithSmaAndSmooths) {
    auto r = ema({1, 2, 3, 4}, 3);  // k = 0.5, seed = 2
    EXPECT_TRUE(std::isnan(r[1]));
    EXPECT_DOUBLE_EQ(r[2], 2.0);
    EXPECT_DOUBLE_EQ(r[3], 3.0);  // 4*0.5 + 2*0.5
}

TEST(Rsi, AllGainsGives100) {
    auto r = rsi({1, 2, 3, 4, 5, 6}, 3);
    EXPECT_TRUE(std::isnan(r[2]));
    EXPECT_DOUBLE_EQ(r[3], 100.0);
    EXPECT_DOUBLE_EQ(r[5], 100.0);
}

TEST(Rsi, AllLossesGivesZero) {
    auto r = rsi({6, 5, 4, 3, 2}, 2);
    EXPECT_DOUBLE_EQ(r[4], 0.0);
}

TEST(Rsi, FlatPricesGiveNeutral50) {
    auto r = rsi({5, 5, 5, 5}, 2);
    EXPECT_DOUBLE_EQ(r[3], 50.0);
}

TEST(Rsi, KnownValue) {
    // gains: 1, 0; losses: 0, 1 -> avgGain = avgLoss -> RSI = 50
    auto r = rsi({10, 11, 10}, 2);
    EXPECT_DOUBLE_EQ(r[2], 50.0);
}

TEST(Rsi, NeedsPeriodPlusOneValues) {
    EXPECT_THROW(rsi({1, 2, 3}, 3), std::invalid_argument);
}

TEST(Returns, SimpleDailyReturns) {
    auto r = dailyReturns({100, 110, 99});
    ASSERT_EQ(r.size(), 2u);
    EXPECT_NEAR(r[0], 0.10, 1e-12);
    EXPECT_NEAR(r[1], -0.10, 1e-12);
    EXPECT_TRUE(dailyReturns({100}).empty());
}

TEST(Volatility, ConstantReturnsHaveZeroVolatility) {
    EXPECT_DOUBLE_EQ(annualizedVolatility({0.01, 0.01, 0.01}), 0.0);
}

TEST(Volatility, KnownSampleStd) {
    // sample std of {0.01, -0.01} = sqrt(2*0.0001/1) = 0.0141421...
    EXPECT_NEAR(annualizedVolatility({0.01, -0.01}), std::sqrt(0.0002) * std::sqrt(252.0), 1e-12);
    EXPECT_THROW(annualizedVolatility({0.01}), std::invalid_argument);
}

TEST(Drawdown, FindsLargestPeakToTroughDrop) {
    EXPECT_NEAR(maxDrawdown({100, 120, 90, 110, 60, 130}), 0.5, 1e-12);
    EXPECT_DOUBLE_EQ(maxDrawdown({1, 2, 3}), 0.0);
    EXPECT_DOUBLE_EQ(maxDrawdown({}), 0.0);
}

TEST(Sharpe, ZeroVolatilityGivesZero) {
    EXPECT_DOUBLE_EQ(sharpeRatio({0.01, 0.01, 0.01}, 0.0), 0.0);
}

TEST(Sharpe, PositiveForPositiveMeanReturn) {
    EXPECT_GT(sharpeRatio({0.02, -0.01, 0.015, 0.005}, 0.0), 0.0);
    EXPECT_LT(sharpeRatio({-0.02, 0.01, -0.015, -0.005}, 0.0), 0.0);
}

TEST(Crossover, DetectsBuyAndSell) {
    std::vector<double> closes = {10, 10, 10, 10, 12, 14, 16, 14, 10, 6, 4};
    std::vector<std::string> dates;
    for (std::size_t i = 0; i < closes.size(); ++i) dates.push_back("2024-01-" + std::to_string(10 + i));
    auto s = crossoverSignals(dates, closes, 2, 4);
    ASSERT_EQ(s.size(), 2u);
    EXPECT_EQ(s[0].type, SignalType::Buy);
    EXPECT_EQ(s[0].date, "2024-01-14");
    EXPECT_EQ(s[1].type, SignalType::Sell);
    EXPECT_DOUBLE_EQ(s[1].price, closes[std::stoi(s[1].date.substr(8)) - 10]);
}

TEST(Crossover, ValidatesArguments) {
    std::vector<double> c = {1, 2, 3, 4};
    std::vector<std::string> d = {"a", "b", "c", "d"};
    EXPECT_THROW(crossoverSignals(d, c, 3, 3), std::invalid_argument);
    EXPECT_THROW(crossoverSignals({"a"}, c, 1, 2), std::invalid_argument);
}
