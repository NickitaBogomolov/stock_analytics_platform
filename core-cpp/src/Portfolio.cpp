#include <algorithm>
#include <cmath>
#include <map>
#include <random>
#include <iterator>
#include <set>

#include "market/Json.hpp"
#include "market/Quant.hpp"

namespace market {

PortfolioResult optimizePortfolio(const std::vector<std::string>& tickers, const std::vector<std::vector<Candle>>& series,
                                  const PortfolioParams& p) {
    const std::size_t k = tickers.size();
    if (k < 2 || k > 20) throw MarketDataError("portfolio needs from 2 to 20 instruments");
    if (series.size() != k) throw MarketDataError("tickers and series size mismatch");
    if (p.samples < 100 || p.samples > 2000000) throw MarketDataError("samples must be within 100..2000000");

    // выравнивание по общим датам
    std::vector<std::map<std::string, double>> byDate(k);
    std::set<std::string> common;
    for (std::size_t a = 0; a < k; ++a) {
        for (const auto& c : series[a]) byDate[a][c.date] = c.close;
        std::set<std::string> dates;
        for (const auto& kv : byDate[a]) dates.insert(kv.first);
        if (a == 0) common = dates;
        else {
            std::set<std::string> keep;
            std::set_intersection(common.begin(), common.end(), dates.begin(), dates.end(), std::inserter(keep, keep.end()));
            common.swap(keep);
        }
    }
    if (common.size() < 31) throw MarketDataError("instruments have fewer than 31 common trading days");
    std::vector<std::string> dates(common.begin(), common.end());

    // дневные доходности: rets[a][t]
    const std::size_t n = dates.size() - 1;
    std::vector<std::vector<double>> rets(k, std::vector<double>(n));
    for (std::size_t a = 0; a < k; ++a)
        for (std::size_t t = 0; t < n; ++t) rets[a][t] = byDate[a][dates[t + 1]] / byDate[a][dates[t]] - 1;

    // годовые ожидаемые доходности и ковариационная матрица
    std::vector<double> mu(k, 0);
    for (std::size_t a = 0; a < k; ++a) {
        for (double r : rets[a]) mu[a] += r;
        mu[a] /= n;
    }
    std::vector<std::vector<double>> cov(k, std::vector<double>(k, 0));
    for (std::size_t a = 0; a < k; ++a)
        for (std::size_t b = a; b < k; ++b) {
            double s = 0;
            for (std::size_t t = 0; t < n; ++t) s += (rets[a][t] - mu[a]) * (rets[b][t] - mu[b]);
            cov[a][b] = cov[b][a] = s / (n - 1) * 252;
        }
    for (double& m : mu) m *= 252;

    PortfolioResult r;
    r.tickers = tickers;
    r.days = dates.size();
    r.samples = p.samples;
    r.firstDate = dates.front();
    r.lastDate = dates.back();
    r.correlation.assign(k, std::vector<double>(k, 0));
    for (std::size_t a = 0; a < k; ++a) {
        r.assetReturnPct.push_back(mu[a] * 100);
        r.assetVolatilityPct.push_back(std::sqrt(cov[a][a]) * 100);
        for (std::size_t b = 0; b < k; ++b) {
            double d = std::sqrt(cov[a][a] * cov[b][b]);
            r.correlation[a][b] = d > 0 ? cov[a][b] / d : 0;
        }
    }

    const double rf = p.riskFreeRatePct / 100;
    auto evaluate = [&](const std::vector<double>& w) {
        PortfolioPoint pt;
        double ret = 0, var = 0;
        for (std::size_t a = 0; a < k; ++a) {
            ret += w[a] * mu[a];
            for (std::size_t b = 0; b < k; ++b) var += w[a] * w[b] * cov[a][b];
        }
        double vol = std::sqrt(std::max(var, 0.0));
        pt.returnPct = ret * 100;
        pt.volatilityPct = vol * 100;
        pt.sharpe = vol > 0 ? (ret - rf) / vol : 0;
        pt.weights = w;
        return pt;
    };

    // равномерная выборка по симплексу весов: w_i = E_i / sum(E), E ~ Exp(1) (распределение Дирихле(1,…,1))
    std::mt19937_64 rng(p.seed);
    std::exponential_distribution<double> expo(1.0);
    std::vector<double> w(k);
    r.equalWeight = evaluate(std::vector<double>(k, 1.0 / k));
    r.maxSharpe = r.minVolatility = r.equalWeight;
    const std::size_t stride = std::max<std::size_t>(1, p.samples / std::max<std::size_t>(1, p.cloudPoints));
    for (std::size_t s = 0; s < p.samples + k; ++s) {
        if (s < k) {  // «угловые» портфели — по одной бумаге
            std::fill(w.begin(), w.end(), 0.0);
            w[s] = 1;
        } else {
            double sum = 0;
            for (double& x : w) sum += (x = expo(rng));
            for (double& x : w) x /= sum;
        }
        PortfolioPoint pt = evaluate(w);
        if (pt.sharpe > r.maxSharpe.sharpe) r.maxSharpe = pt;
        if (pt.volatilityPct < r.minVolatility.volatilityPct) r.minVolatility = pt;
        if (s % stride == 0) {
            pt.weights.clear();
            r.cloud.push_back(pt);
        }
    }
    return r;
}

namespace {
std::string pointJson(const PortfolioPoint& p) {
    using namespace json;
    std::vector<std::pair<std::string, std::string>> f{
        {"returnPct", num(p.returnPct)}, {"volatilityPct", num(p.volatilityPct)}, {"sharpe", num(p.sharpe)}};
    if (!p.weights.empty()) f.push_back({"weights", nums(p.weights)});
    return object(f);
}
}  // namespace

std::string toJson(const PortfolioResult& r) {
    using namespace json;
    return object({{"kind", str("portfolio")}, {"tickers", array(r.tickers, str)}, {"days", std::to_string(r.days)},
                   {"samples", std::to_string(r.samples)}, {"firstDate", str(r.firstDate)}, {"lastDate", str(r.lastDate)},
                   {"assetReturnPct", nums(r.assetReturnPct)}, {"assetVolatilityPct", nums(r.assetVolatilityPct)},
                   {"correlation", array(r.correlation, nums)}, {"maxSharpe", pointJson(r.maxSharpe)},
                   {"minVolatility", pointJson(r.minVolatility)}, {"equalWeight", pointJson(r.equalWeight)},
                   {"cloud", array(r.cloud, pointJson)}});
}

}  // namespace market
