#pragma once
// Квант-лаборатория: вычислительно тяжёлые задачи, ради которых модуль написан на C++.
//  * бэктест стратегии пересечения SMA с перебором параметров и проверкой вне выборки;
//  * Монте-Карло (бутстреп доходностей) для оценки риска: VaR, CVaR, веер сценариев;
//  * оптимизация портфеля случайным поиском по симплексу весов (эффективная граница).
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "market/Models.hpp"

namespace market {

// ---------------------------------------------------------------- бэктест

/** Показатели стратегии на отрезке истории. */
struct StrategyStats {
    double totalReturnPct = 0, annualReturnPct = 0, annualVolatilityPct = 0;
    double sharpe = 0, maxDrawdownPct = 0, exposurePct = 0;
    int trades = 0;
    std::size_t days = 0;
};

struct BacktestParams {
    std::size_t fastMin = 2, fastMax = 50, fastStep = 2;
    std::size_t slowMin = 10, slowMax = 250, slowStep = 10;
    double commissionPct = 0.05;  // комиссия за сделку, % от суммы
    double trainShare = 0.7;      // доля истории для подбора параметров
    double riskFreeRatePct = 0.0;
};

struct BacktestResult {
    std::size_t bestFast = 0, bestSlow = 0, combos = 0, trainSize = 0;
    std::vector<std::size_t> fasts, slows;
    std::vector<std::vector<double>> sharpeGrid;  // [fast][slow], NaN — недопустимая пара
    StrategyStats inSample, outSample, buyHoldIn, buyHoldOut;
    std::vector<double> equityStrategy, equityBuyHold;  // вся история, старт = 100
};

/**
 * Стратегия «в позиции, пока SMA(fast) > SMA(slow)», только длинные позиции, сделка по цене закрытия.
 * Решение на дне t исполняется доходностью дня t+1 — заглядывания в будущее нет.
 */
StrategyStats simulateSma(const std::vector<double>& closes, std::size_t fast, std::size_t slow,
                          std::size_t from, std::size_t to, double commissionPct, double riskFreePct,
                          std::vector<double>* equity = nullptr);
StrategyStats buyAndHold(const std::vector<double>& closes, std::size_t from, std::size_t to,
                         double commissionPct, double riskFreePct, std::vector<double>* equity = nullptr);
/** Подбор (fast, slow) по Шарпу на первых trainShare истории, проверка на оставшейся части. */
BacktestResult backtestSmaGrid(const std::vector<double>& closes, const BacktestParams& p);

// ---------------------------------------------------------------- Монте-Карло

struct MonteCarloParams {
    std::size_t paths = 10000;
    std::size_t horizon = 21;  // торговых дней (~ месяц)
    std::uint64_t seed = 42;
};

struct MonteCarloResult {
    std::size_t paths = 0, horizon = 0, sample = 0;
    double lastClose = 0, meanReturnPct = 0, probLossPct = 0;
    double var95 = 0, var99 = 0, cvar95 = 0, cvar99 = 0;  // в % от текущей цены, положительные = потери
    std::vector<double> levels;                           // перцентили веера: 5, 25, 50, 75, 95
    std::vector<std::vector<double>> fan;                 // [уровень][день 0..horizon] — цена
    std::vector<double> histEdges, histCounts;            // распределение итоговой доходности, %
};

/** Бутстреп исторических дневных лог-доходностей: paths сценариев длиной horizon дней. */
MonteCarloResult monteCarlo(const std::vector<double>& closes, const MonteCarloParams& p);

// ---------------------------------------------------------------- портфель

struct PortfolioParams {
    std::size_t samples = 20000;
    std::uint64_t seed = 7;
    double riskFreeRatePct = 0.0;
    std::size_t cloudPoints = 1500;  // сколько точек облака отдавать для графика
};

struct PortfolioPoint {
    double returnPct = 0, volatilityPct = 0, sharpe = 0;
    std::vector<double> weights;
};

struct PortfolioResult {
    std::vector<std::string> tickers;
    std::size_t days = 0, samples = 0;
    std::string firstDate, lastDate;
    std::vector<double> assetReturnPct, assetVolatilityPct;
    std::vector<std::vector<double>> correlation;
    PortfolioPoint maxSharpe, minVolatility, equalWeight;
    std::vector<PortfolioPoint> cloud;  // без весов
};

/** Выравнивает ряды по общим датам и ищет портфели с макс. Шарпом и мин. риском (только покупка). */
PortfolioResult optimizePortfolio(const std::vector<std::string>& tickers,
                                  const std::vector<std::vector<Candle>>& series, const PortfolioParams& p);

// ---------------------------------------------------------------- JSON

std::string toJson(const BacktestResult& r, const std::vector<std::string>& dates, const std::string& ticker);
std::string toJson(const MonteCarloResult& r, const std::string& ticker, const std::string& lastDate);
std::string toJson(const PortfolioResult& r);

}  // namespace market
