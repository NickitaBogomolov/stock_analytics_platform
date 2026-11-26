#include "market/CsvLoader.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <sstream>

namespace market {
namespace {

std::string trim(const std::string& s) {
    auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    return s.substr(b, s.find_last_not_of(" \t\r\n") - b + 1);
}

std::vector<std::string> split(const std::string& line, char sep) {
    std::vector<std::string> out;
    std::stringstream ss(line);
    std::string item;
    while (std::getline(ss, item, sep)) out.push_back(trim(item));
    if (!line.empty() && line.back() == sep) out.emplace_back();
    return out;
}

double parseNumber(const std::string& s, const char* field, std::size_t line) {
    try {
        if (s.empty() || s.find_first_not_of("0123456789.eE+-") != std::string::npos)
            throw std::invalid_argument(s);  // stod принял бы "0x1A", "inf" и т.п.
        std::size_t pos = 0;
        double v = std::stod(s, &pos);
        if (pos != s.size() || !std::isfinite(v)) throw std::invalid_argument(s);
        return v;
    } catch (const std::exception&) {
        throw MarketDataError(std::string("invalid number in ") + field + ": '" + s + "'", line);
    }
}

bool isIsoDate(const std::string& d) {
    if (d.size() != 10 || d[4] != '-' || d[7] != '-') return false;
    for (std::size_t i : {0, 1, 2, 3, 5, 6, 8, 9})
        if (!std::isdigit(static_cast<unsigned char>(d[i]))) return false;
    int year = std::stoi(d.substr(0, 4)), month = std::stoi(d.substr(5, 2)), day = std::stoi(d.substr(8, 2));
    if (month < 1 || month > 12 || day < 1) return false;
    static const int kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    return day <= kDays[month - 1] + (month == 2 && leap ? 1 : 0);
}

}  // namespace

void validateCandle(const Candle& c, std::size_t line) {
    if (!isIsoDate(c.date)) throw MarketDataError("invalid date '" + c.date + "', expected YYYY-MM-DD", line);
    if (c.open <= 0 || c.high <= 0 || c.low <= 0 || c.close <= 0)
        throw MarketDataError("prices must be positive", line);
    if (c.high < std::max(c.open, c.close) || c.low > std::min(c.open, c.close) || c.low > c.high)
        throw MarketDataError("inconsistent OHLC: low <= open,close <= high violated", line);
    if (c.volume < 0) throw MarketDataError("volume must be non-negative", line);
}

std::vector<Candle> loadCsv(std::istream& in) {
    std::vector<Candle> candles;
    std::string raw;
    std::size_t lineNo = 0;
    bool headerSeen = false;
    while (std::getline(in, raw)) {
        ++lineNo;
        std::string line = trim(raw);
        if (line.empty()) continue;
        if (!headerSeen) {
            std::string lower = line;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            if (lower.rfind("date", 0) != 0) throw MarketDataError("missing header 'Date,Open,High,Low,Close,Volume'", lineNo);
            headerSeen = true;
            continue;
        }
        auto f = split(line, ',');
        if (f.size() != 6) throw MarketDataError("expected 6 columns, got " + std::to_string(f.size()), lineNo);
        Candle c;
        c.date = f[0];
        c.open = parseNumber(f[1], "Open", lineNo);
        c.high = parseNumber(f[2], "High", lineNo);
        c.low = parseNumber(f[3], "Low", lineNo);
        c.close = parseNumber(f[4], "Close", lineNo);
        double vol = parseNumber(f[5], "Volume", lineNo);
        if (vol != std::floor(vol)) throw MarketDataError("volume must be an integer", lineNo);
        c.volume = static_cast<long long>(vol);
        validateCandle(c, lineNo);
        if (!candles.empty() && c.date <= candles.back().date)
            throw MarketDataError("dates must be strictly ascending", lineNo);
        candles.push_back(c);
    }
    if (candles.empty()) throw MarketDataError("file contains no quotes");
    return candles;
}

std::vector<Candle> loadCsvFile(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw MarketDataError("cannot open file: " + path);
    return loadCsv(in);
}

}  // namespace market
