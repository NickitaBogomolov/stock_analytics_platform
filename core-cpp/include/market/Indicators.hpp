#pragma once
#include <cstddef>
#include <string>
#include <vector>

#include "market/Models.hpp"

namespace market {

/// Простая скользящая средняя. Первые period-1 значений = NaN.
std::vector<double> sma(const std::vector<double>& values, std::size_t period);
/// Экспоненциальная скользящая средняя (затравка — SMA).
std::vector<double> ema(const std::vector<double>& values, std::size_t period);
/// Индекс относительной силы (сглаживание Уайлдера). Первые period значений = NaN.
std::vector<double> rsi(const std::vector<double>& values, std::size_t period);
/// Простые дневные доходности, размер n-1.
std::vector<double> dailyReturns(const std::vector<double>& closes);
/// Годовая волатильность (выборочное СКО * sqrt(252)), доля.
double annualizedVolatility(const std::vector<double>& returns);
/// Максимальная просадка, доля от пика.
double maxDrawdown(const std::vector<double>& closes);
/// Коэффициент Шарпа (годовой).
double sharpeRatio(const std::vector<double>& returns, double riskFreeAnnual);
/// Сигналы пересечения быстрой и медленной SMA.
std::vector<TradeSignal> crossoverSignals(const std::vector<std::string>& dates,
                                          const std::vector<double>& closes,
                                          std::size_t fast, std::size_t slow);

}  // namespace market
