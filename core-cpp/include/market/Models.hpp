#pragma once
// Доменная модель модуля анализа рынка ценных бумаг.
#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

namespace market {

/// Одна торговая сессия (дневная свеча) по инструменту.
struct Candle {
    std::string date;  // YYYY-MM-DD
    double open = 0, high = 0, low = 0, close = 0;
    long long volume = 0;
};

enum class SignalType { Buy, Sell };

/// Торговый сигнал пересечения скользящих средних.
struct TradeSignal {
    std::string date;
    SignalType type;
    double price;
};

/// Параметры анализа, задаваемые пользователем.
struct AnalysisParams {
    std::size_t fastPeriod = 10;
    std::size_t slowPeriod = 30;
    std::size_t rsiPeriod = 14;
    double riskFreeRatePct = 0.0;  // годовая безрисковая ставка, %
};

/// Итоговый отчёт по инструменту.
struct AnalysisReport {
    std::string ticker;
    AnalysisParams params;
    std::size_t count = 0;
    std::string firstDate, lastDate;
    double lastClose = 0;
    double totalReturnPct = 0;
    double annualVolatilityPct = 0;
    double maxDrawdownPct = 0;
    double sharpeRatio = 0;
    double rsi = 0;
    std::string trend;    // UPTREND | DOWNTREND | SIDEWAYS
    std::string rsiZone;  // OVERBOUGHT | OVERSOLD | NEUTRAL
    std::vector<std::string> dates;
    std::vector<double> close, smaFast, smaSlow, rsiSeries;  // NaN = нет значения
    std::vector<TradeSignal> signals;
};

/// Ошибка входных данных (с номером строки файла, если известен).
class MarketDataError : public std::runtime_error {
public:
    explicit MarketDataError(const std::string& msg, std::size_t line = 0)
        : std::runtime_error(line ? "line " + std::to_string(line) + ": " + msg : msg), line_(line) {}
    std::size_t line() const noexcept { return line_; }

private:
    std::size_t line_;
};

}  // namespace market
