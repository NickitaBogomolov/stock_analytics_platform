#include <gtest/gtest.h>

#include <cmath>

#include "market/CsvLoader.hpp"
#include "market/Quant.hpp"

using namespace market;

namespace {
std::vector<double> demoCloses(const char* name) {
    std::vector<double> v;
    for (const auto& c : loadCsvFile(std::string(MARKET_DATA_DIR) + "/" + name + ".csv")) v.push_back(c.close);
    return v;
}
std::vector<double> geometric(std::size_t n, double start, double dailyGrowth) {
    std::vector<double> v{start};
    while (v.size() < n) v.push_back(v.back() * (1 + dailyGrowth));
    return v;
}
}  // namespace

// ---------- бэктест

TEST(Backtest, BuyAndHoldMatchesPriceChange) {
    std::vector<double> c = {100, 110, 99, 120};
    std::vector<double> eq;
    auto s = buyAndHold(c, 0, c.size(), 0.0, 0.0, &eq);
    EXPECT_NEAR(s.totalReturnPct, 20.0, 1e-9);
    EXPECT_EQ(s.trades, 1);
    EXPECT_DOUBLE_EQ(s.exposurePct, 100.0);
    EXPECT_NEAR(eq.back(), 120.0, 1e-9);
    EXPECT_NEAR(s.maxDrawdownPct, 10.0, 1e-9);
}

TEST(Backtest, CommissionIsChargedPerTrade) {
    std::vector<double> c = {100, 100, 100};
    auto s = buyAndHold(c, 0, c.size(), 1.0, 0.0);  // 1% за вход
    EXPECT_NEAR(s.totalReturnPct, -1.0, 1e-9);
}

TEST(Backtest, NoLookAheadSignalUsesOnlyPastPrices) {
    // рост начинается на дне 10; сигнал может появиться только после того, как он виден в SMA
    std::vector<double> c(10, 100.0);
    for (int i = 0; i < 10; ++i) c.push_back(100.0 + 10 * (i + 1));
    std::vector<double> eq;
    auto s = simulateSma(c, 1, 2, 0, c.size(), 0.0, 0.0, &eq);
    // первый рост (100 -> 110) пропущен: на закрытии дня 9 цена ещё не выросла
    EXPECT_NEAR(eq[10], 100.0, 1e-9);
    EXPECT_GT(s.totalReturnPct, 0.0);
    EXPECT_LT(s.totalReturnPct, (200.0 / 100.0 - 1) * 100);
}

TEST(Backtest, StrategyStaysOutOfFallingMarket) {
    auto c = geometric(300, 100, -0.002);
    auto s = simulateSma(c, 5, 20, 0, c.size(), 0.0, 0.0);
    EXPECT_EQ(s.trades, 0);
    EXPECT_DOUBLE_EQ(s.totalReturnPct, 0.0);
}

TEST(Backtest, GridFindsBestPairAndSplitsSample) {
    auto c = demoCloses("DEMO_TECH");
    BacktestParams p;
    p.slowMax = 100;
    auto r = backtestSmaGrid(c, p);
    EXPECT_GT(r.combos, 100u);
    EXPECT_LT(r.bestFast, r.bestSlow);
    EXPECT_EQ(r.trainSize, static_cast<std::size_t>(c.size() * 0.7));
    EXPECT_EQ(r.equityStrategy.size(), c.size());
    EXPECT_EQ(r.equityBuyHold.size(), c.size());
    EXPECT_EQ(r.inSample.days + r.outSample.days, c.size() - 1);
    // лучший Шарп в сетке равен Шарпу выбранной пары
    double best = -1e9;
    for (auto& row : r.sharpeGrid)
        for (double v : row)
            if (!std::isnan(v)) best = std::max(best, v);
    EXPECT_NEAR(best, r.inSample.sharpe, 1e-12);
}

TEST(Backtest, RejectsShortSeriesAndBadGrid) {
    EXPECT_THROW(backtestSmaGrid(geometric(30, 100, 0.001), BacktestParams{}), MarketDataError);
    BacktestParams bad;
    bad.fastMin = 0;
    EXPECT_THROW(backtestSmaGrid(geometric(500, 100, 0.001), bad), MarketDataError);
    bad = BacktestParams{};
    bad.trainShare = 0.99;
    EXPECT_THROW(backtestSmaGrid(geometric(500, 100, 0.001), bad), MarketDataError);
    EXPECT_THROW(simulateSma(geometric(50, 100, 0.001), 10, 5, 0, 50, 0, 0), MarketDataError);
}

// ---------- Монте-Карло

TEST(MonteCarlo, ConstantGrowthIsDeterministic) {
    auto c = geometric(100, 100, 0.01);  // все дневные доходности одинаковы
    MonteCarloParams p;
    p.paths = 1000;
    p.horizon = 10;
    auto r = monteCarlo(c, p);
    double expected = (std::pow(1.01, 10) - 1) * 100;
    EXPECT_NEAR(r.meanReturnPct, expected, 1e-6);
    EXPECT_NEAR(r.fan[2].back(), c.back() * std::pow(1.01, 10), 1e-6);
    EXPECT_DOUBLE_EQ(r.probLossPct, 0.0);
    EXPECT_NEAR(r.var95, -expected, 1e-6);  // «потеря» отрицательна — это прибыль
}

TEST(MonteCarlo, RiskMeasuresAreConsistent) {
    auto c = demoCloses("DEMO_ENERGY");
    MonteCarloParams p;
    p.paths = 20000;
    p.horizon = 21;
    auto r = monteCarlo(c, p);
    EXPECT_GE(r.var99, r.var95);    // 99% хвост глубже
    EXPECT_GE(r.cvar95, r.var95);   // средняя потеря в хвосте не меньше порога
    EXPECT_GE(r.cvar99, r.cvar95);
    for (std::size_t d = 1; d <= p.horizon; ++d)
        for (std::size_t l = 1; l < r.fan.size(); ++l) EXPECT_LE(r.fan[l - 1][d], r.fan[l][d]);
    double total = 0;
    for (double x : r.histCounts) total += x;
    EXPECT_DOUBLE_EQ(total, 20000.0);
    EXPECT_GT(r.probLossPct, 0.0);
    EXPECT_LT(r.probLossPct, 100.0);
}

TEST(MonteCarlo, SameSeedSameResult) {
    auto c = demoCloses("DEMO_BANK");
    MonteCarloParams p;
    p.paths = 2000;
    EXPECT_DOUBLE_EQ(monteCarlo(c, p).var95, monteCarlo(c, p).var95);
    p.seed = 1;
    MonteCarloParams q = p;
    q.seed = 2;
    EXPECT_NE(monteCarlo(c, p).meanReturnPct, monteCarlo(c, q).meanReturnPct);
}

TEST(MonteCarlo, ValidatesInput) {
    MonteCarloParams p;
    EXPECT_THROW(monteCarlo(geometric(30, 100, 0.01), p), MarketDataError);
    p.paths = 10;
    EXPECT_THROW(monteCarlo(geometric(100, 100, 0.01), p), MarketDataError);
}

// ---------- портфель

TEST(Portfolio, OptimumDominatesEqualWeight) {
    std::vector<std::string> t = {"DEMO_TECH", "DEMO_BANK", "DEMO_ENERGY", "DEMO_RETAIL"};
    std::vector<std::vector<Candle>> s;
    for (const auto& name : t) s.push_back(loadCsvFile(std::string(MARKET_DATA_DIR) + "/" + name + ".csv"));
    PortfolioParams p;
    p.samples = 20000;
    auto r = optimizePortfolio(t, s, p);
    EXPECT_EQ(r.days, 500u);
    EXPECT_GE(r.maxSharpe.sharpe, r.equalWeight.sharpe);
    EXPECT_LE(r.minVolatility.volatilityPct, r.equalWeight.volatilityPct);
    // диверсификация: минимальный риск ниже риска самой спокойной бумаги
    double minAsset = *std::min_element(r.assetVolatilityPct.begin(), r.assetVolatilityPct.end());
    EXPECT_LT(r.minVolatility.volatilityPct, minAsset);
    double sum = 0;
    for (double w : r.maxSharpe.weights) {
        EXPECT_GE(w, 0.0);
        sum += w;
    }
    EXPECT_NEAR(sum, 1.0, 1e-9);
    EXPECT_NEAR(r.correlation[0][0], 1.0, 1e-12);
    EXPECT_FALSE(r.cloud.empty());
}

TEST(Portfolio, AlignsByCommonDates) {
    std::vector<Candle> a, b;
    for (int d = 1; d <= 80; ++d) {
        char date[16];
        std::snprintf(date, sizeof date, "2024-%02d-%02d", 1 + (d - 1) / 28, 1 + (d - 1) % 28);
        double pa = 100 + d, pb = 50 + (d % 7);
        a.push_back({date, pa, pa, pa, pa, 1});
        if (d % 2 == 0) b.push_back({date, pb, pb, pb, pb, 1});  // у второй бумаги — только чётные дни
    }
    auto r = optimizePortfolio({"A", "B"}, {a, b}, PortfolioParams{});
    EXPECT_EQ(r.days, 40u);
}

TEST(Portfolio, ValidatesInput) {
    std::vector<Candle> a{{"2024-01-02", 1, 1, 1, 1, 1}};
    EXPECT_THROW(optimizePortfolio({"A"}, {a}, PortfolioParams{}), MarketDataError);
    EXPECT_THROW(optimizePortfolio({"A", "B"}, {a, a}, PortfolioParams{}), MarketDataError);
}
