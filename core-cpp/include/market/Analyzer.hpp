#pragma once
#include <string>
#include <vector>

#include "market/Models.hpp"

namespace market {

/// Строит полный отчёт по ряду котировок.
AnalysisReport analyze(const std::string& ticker, const std::vector<Candle>& candles,
                       const AnalysisParams& params = {});

/// Сериализация отчёта в JSON (NaN -> null).
std::string toJson(const AnalysisReport& report);
/// Запись отчёта в файл; бросает MarketDataError, если файл не открыть.
void writeReport(const AnalysisReport& report, const std::string& path);

}  // namespace market
