#pragma once
#include <istream>
#include <string>
#include <vector>

#include "market/Models.hpp"

namespace market {

/// Читает котировки в формате CSV: Date,Open,High,Low,Close,Volume.
/// Бросает MarketDataError при любой ошибке формата или валидации.
std::vector<Candle> loadCsv(std::istream& in);
std::vector<Candle> loadCsvFile(const std::string& path);

/// Проверяет одну свечу; при ошибке бросает MarketDataError.
void validateCandle(const Candle& c, std::size_t line = 0);

}  // namespace market
