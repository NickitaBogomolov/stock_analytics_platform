-- Отдельная база для интеграционных тестов (docker compose --profile test run --rm tests).
-- Тесты очищают таблицы, поэтому основная база market для них не используется.
CREATE DATABASE market_test OWNER market;
