#include <gtest/gtest.h>

#include <sstream>

#include "market/CsvLoader.hpp"

using namespace market;

namespace {
std::vector<Candle> parse(const std::string& text) {
    std::istringstream in(text);
    return loadCsv(in);
}

std::size_t errorLine(const std::string& text) {
    try {
        parse(text);
    } catch (const MarketDataError& e) {
        return e.line();
    }
    return static_cast<std::size_t>(-1);
}

const std::string kHeader = "Date,Open,High,Low,Close,Volume\n";
}  // namespace

TEST(CsvLoader, ParsesValidFile) {
    auto c = parse(kHeader + "2024-01-02,10,11,9,10.5,1000\n2024-01-03,10.5,12,10,11.5,2000\n");
    ASSERT_EQ(c.size(), 2u);
    EXPECT_EQ(c[0].date, "2024-01-02");
    EXPECT_DOUBLE_EQ(c[1].close, 11.5);
    EXPECT_EQ(c[1].volume, 2000);
}

TEST(CsvLoader, ToleratesWhitespaceCrlfAndBlankLines) {
    auto c = parse("date, open, high, low, close, volume\r\n\r\n 2024-01-02 , 10 , 11 , 9 , 10 , 5 \r\n\n");
    ASSERT_EQ(c.size(), 1u);
    EXPECT_DOUBLE_EQ(c[0].high, 11);
}

TEST(CsvLoader, EmptyInputIsError) {
    EXPECT_THROW(parse(""), MarketDataError);
    EXPECT_THROW(parse(kHeader), MarketDataError);
}

TEST(CsvLoader, MissingHeaderReportsLine1) {
    EXPECT_EQ(errorLine("2024-01-02,10,11,9,10,1\n"), 1u);
}

TEST(CsvLoader, WrongColumnCount) {
    EXPECT_EQ(errorLine(kHeader + "2024-01-02,10,11,9,10\n"), 2u);
    EXPECT_EQ(errorLine(kHeader + "2024-01-02,10,11,9,10,1,\n"), 2u);
}

TEST(CsvLoader, NonNumericValue) {
    EXPECT_EQ(errorLine(kHeader + "2024-01-02,10,abc,9,10,1\n"), 2u);
    EXPECT_EQ(errorLine(kHeader + "2024-01-02,10,11x,9,10,1\n"), 2u);
    EXPECT_EQ(errorLine(kHeader + "2024-01-02,10,11,9,nan,1\n"), 2u);
    EXPECT_EQ(errorLine(kHeader + "2024-01-02,0x0A,11,9,10,1\n"), 2u);  // дефект D-02
    EXPECT_EQ(errorLine(kHeader + "2024-01-02,10,,9,10,1\n"), 2u);
}

TEST(CsvLoader, InvalidDate) {
    EXPECT_EQ(errorLine(kHeader + "02.01.2024,10,11,9,10,1\n"), 2u);
    EXPECT_EQ(errorLine(kHeader + "2024-13-02,10,11,9,10,1\n"), 2u);
    EXPECT_EQ(errorLine(kHeader + "2023-02-29,10,11,9,10,1\n"), 2u);  // не високосный год
    EXPECT_EQ(errorLine(kHeader + "2024-04-31,10,11,9,10,1\n"), 2u);
    EXPECT_NO_THROW(parse(kHeader + "2024-02-29,10,11,9,10,1\n"));    // високосный год
}

TEST(CsvLoader, NonPositivePrice) {
    EXPECT_EQ(errorLine(kHeader + "2024-01-02,0,11,9,10,1\n"), 2u);
    EXPECT_EQ(errorLine(kHeader + "2024-01-02,10,11,-9,10,1\n"), 2u);
}

TEST(CsvLoader, InconsistentOhlc) {
    EXPECT_EQ(errorLine(kHeader + "2024-01-02,10,9.5,9,10,1\n"), 2u);   // high < open
    EXPECT_EQ(errorLine(kHeader + "2024-01-02,10,11,10.5,10,1\n"), 2u); // low > close
}

TEST(CsvLoader, NegativeOrFractionalVolume) {
    EXPECT_EQ(errorLine(kHeader + "2024-01-02,10,11,9,10,-1\n"), 2u);
    EXPECT_EQ(errorLine(kHeader + "2024-01-02,10,11,9,10,1.5\n"), 2u);
}

TEST(CsvLoader, DatesMustBeStrictlyAscending) {
    EXPECT_EQ(errorLine(kHeader + "2024-01-03,10,11,9,10,1\n2024-01-02,10,11,9,10,1\n"), 3u);
    EXPECT_EQ(errorLine(kHeader + "2024-01-03,10,11,9,10,1\n2024-01-03,10,11,9,10,1\n"), 3u);
}

TEST(CsvLoader, MissingFile) {
    EXPECT_THROW(loadCsvFile("/nonexistent/file.csv"), MarketDataError);
}

TEST(CsvLoader, LoadsDemoDataFile) {
    auto c = loadCsvFile(std::string(MARKET_DATA_DIR) + "/DEMO_TECH.csv");
    EXPECT_EQ(c.size(), 500u);
}
