#pragma once
// Минимальная сериализация в JSON (без внешних зависимостей). NaN/Inf -> null.
#include <cmath>
#include <sstream>
#include <string>
#include <vector>

namespace market::json {

inline std::string num(double v) {
    if (!std::isfinite(v)) return "null";
    std::ostringstream os;
    os.precision(10);
    os << v;
    return os.str();
}

inline std::string str(const std::string& s) {
    std::string out = "\"";
    for (char ch : s) {
        if (ch == '"' || ch == '\\') out += '\\';
        out += ch;
    }
    return out + "\"";
}

template <class T, class F>
std::string array(const std::vector<T>& v, F fmt) {
    std::string out = "[";
    for (std::size_t i = 0; i < v.size(); ++i) out += (i ? "," : "") + fmt(v[i]);
    return out + "]";
}

inline std::string nums(const std::vector<double>& v) { return array(v, num); }

/** Пары "ключ":значение (значения уже сериализованы) -> объект. */
inline std::string object(const std::vector<std::pair<std::string, std::string>>& fields) {
    std::string out = "{";
    for (std::size_t i = 0; i < fields.size(); ++i) out += (i ? "," : "") + str(fields[i].first) + ":" + fields[i].second;
    return out + "}";
}

}  // namespace market::json
