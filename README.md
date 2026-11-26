# Market Analytics — анализ рынка ценных бумаг

Индивидуальный проект по курсу «Программная инженерия» (4 курс): **C++ + Kotlin, полный цикл разработки**.

Веб-приложение загружает **реальные котировки Московской биржи** (индекс IMOEX и крупнейшие акции, дневные
свечи за 3 года через бесплатный ISS API), хранит их в PostgreSQL, регулярно обновляет и рассчитывает
доходность, волатильность, максимальную просадку, коэффициент Шарпа, индикаторы SMA и RSI, сигналы
пересечения скользящих средних, а также сравнивает бумаги между собой (динамика к 100 + матрица корреляций).
Любую акцию или индекс Мосбиржи можно добавить по тикеру, свои данные — загрузить из CSV.

- **Kotlin** (`web-kotlin/`) — **сервис**: загрузка котировок с Мосбиржи, PostgreSQL, REST API на Ktor, веб-интерфейс и интерактивный анализ (индикаторы, метрики, сравнение бумаг, история); тесты JUnit 5 + Kotest.
- **C++17** (`core-cpp/`) — **квант-лаборатория** для тяжёлых расчётов: бэктест стратегии с перебором параметров и проверкой вне выборки, Монте-Карло (VaR / CVaR), оптимизация портфеля. Плюс ядро индикаторов (этап 3), консольная утилита `market_cli`; тесты Google Test.

Почему так: Kotlin удобен для сервиса (сеть, БД, API), а C++ — для вычислений на сотни тысяч сценариев без
пауз GC. Kotlin выгружает котировки из БД, вызывает `market_cli` и сохраняет результат. Подробно — в
[docs/03-cpp-vs-kotlin.md](docs/03-cpp-vs-kotlin.md).
- **Московская биржа (ISS API)** — источник котировок: загрузка при старте, автообновление каждые 6 часов, добавление по тикеру.
- **PostgreSQL** — хранение инструментов, котировок, истории анализов и сравнений (JDBC + HikariCP, миграции Flyway).
- **Docker Compose** — запуск всего стека (веб-приложение + база) одной командой.

![Интерфейс](docs/img/ui-analysis.png)

## Соответствие этапам проекта

| Этап | Результат | Где |
|---|---|---|
| 1. Требования | SRS: пользовательские истории, критерии приёмки, ТЗ | [docs/01-requirements.md](docs/01-requirements.md) |
| 2. Проектирование | UML (Use Case, Class, Sequence, Activity, Component), ERD схемы PostgreSQL, описание архитектуры, API | [docs/02-architecture.md](docs/02-architecture.md), [docs/uml](docs/uml) (PlantUML) |
| 3. Реализация ядра | C++ модуль (~265 строк логики ядра + квант-модуль) | [core-cpp](core-cpp) |
| 4. Портирование / расширение | Kotlin-порт ядра + сервис (Мосбиржа, PostgreSQL, веб) + C++ квант-лаборатория + сравнение | [web-kotlin](web-kotlin), [docs/03-cpp-vs-kotlin.md](docs/03-cpp-vs-kotlin.md) |
| 5. Тестирование | 51 тест Google Test, 119 тестов JUnit/Kotest (в т.ч. на реальной PostgreSQL, заглушке ISS API и настоящем C++ модуле), отчёт о дефектах | [docs/04-test-report.md](docs/04-test-report.md) |
| 6. Git + защита | Репозиторий, ветки, PR, README, презентация | [docs/presentation/Market_Analytics.pptx](docs/presentation/Market_Analytics.pptx), [план доклада](docs/05-defense-talk.md) |

## Быстрый старт: Docker Compose

Нужен только Docker (с Compose v2).

```bash
docker compose up --build -d
```

Откройте <http://localhost:8080>. Поднимаются два контейнера: `db` (PostgreSQL 16) и `web`
(Kotlin-сервис с собранным внутри C++ `market_cli`). При первом старте приложение создаёт схему БД
(Flyway) и в фоне загружает с Мосбиржи историю IMOEX, SBER, GAZP, LKOH, ROSN, NVTK, GMKN, PLZL, TATN, YDEX,
MGNT, VTBR (несколько секунд; интерфейс показывает «Загружаем котировки…»). Данные хранятся в томе `pgdata`
и переживают перезапуск; свежие свечи догружаются при каждом старте и каждые 6 часов.
Если биржа недоступна (нет интернета), а база пуста, подгружаются синтетические демо-данные из `data/` с меткой «демо».

```bash
docker compose --profile test run --rm tests
```

Прогон всех тестов (Google Test + JUnit, включая тесты на PostgreSQL) в контейнере.

```bash
docker compose down
```

Остановить (данные сохранятся); `docker compose down -v` — удалить и данные.
Порты меняются переменными: `PORT=9090 DB_PORT=25432 docker compose up -d` (в PowerShell: `$env:DB_PORT=25432`).
База снаружи доступна на `localhost:15432`: `psql -h localhost -p 15432 -U market market` (пароль `market`,
меняется переменной `DB_PASSWORD`). Приложение ходит в базу по внутренней сети Docker, этот порт ему не нужен.

### Если вы на Windows

`Dockerfile` сам убирает окончания строк Windows (CRLF) у `gradlew`, а `.gitattributes` хранит скрипты с LF,
поэтому `docker compose up --build` работает при любой настройке `core.autocrlf`. Если проект был
клонирован до появления `.gitattributes` и `./gradlew` не запускается в WSL/Git Bash, удалите файл
`web-kotlin/gradlew` и выполните `git checkout -- web-kotlin/gradlew` — Git выпишет его заново с LF.

## Запуск без Docker

Требования: CMake ≥ 3.16, компилятор C++17 (GCC 9+/Clang 10+/MSVC 2019+), JDK 21. Git для загрузки GoogleTest.
PostgreSQL — по желанию: без него данные хранятся в памяти процесса.

**1. Собрать C++ ядро и запустить тесты**

```bash
cmake -S core-cpp -B core-cpp/build -DCMAKE_BUILD_TYPE=Release
```

```bash
cmake --build core-cpp/build -j
```

```bash
ctest --test-dir core-cpp/build --output-on-failure
```

**2. Консольный отчёт**

```bash
core-cpp/build/market_cli data/DEMO_TECH.csv --fast 10 --slow 30 --summary
```

```bash
core-cpp/build/market_cli data/DEMO_BANK.csv --out report.json
```

```bash
core-cpp/build/market_cli backtest data/DEMO_TECH.csv --commission 0.05 --train 0.7
```

```bash
core-cpp/build/market_cli montecarlo data/DEMO_TECH.csv --paths 100000 --horizon 21
```

```bash
core-cpp/build/market_cli portfolio data/DEMO_TECH.csv data/DEMO_BANK.csv data/DEMO_ENERGY.csv --samples 200000
```

Коды выхода: `0` — успех, `1` — ошибка данных, `2` — ошибка аргументов.

**3. Веб-приложение**

```bash
cd web-kotlin && ./gradlew run
```

Откройте <http://localhost:8080>. Вкладка «Лаборатория C++» работает, если C++ собран (шаг 1) — сервер
ищет `../core-cpp/build/market_cli` или путь из `MARKET_CLI`. Переменные окружения: `PORT` (8080), `MARKET_DATA_DIR` (`../data`),
`MARKET_CLI` (путь к `market_cli`), `MARKET_DB_URL` / `MARKET_DB_USER` / `MARKET_DB_PASSWORD` — подключение к PostgreSQL:

```bash
MARKET_DB_URL=jdbc:postgresql://localhost:5432/market MARKET_DB_USER=market MARKET_DB_PASSWORD=market ./gradlew run
```

**4. Тесты Kotlin**

```bash
cd web-kotlin && ./gradlew test
```

Тесты с PostgreSQL выполняются, если задана тестовая база (иначе помечаются skipped):

```bash
MARKET_TEST_DB_URL=jdbc:postgresql://localhost:5432/market_test MARKET_TEST_DB_USER=market MARKET_TEST_DB_PASSWORD=market ./gradlew test
```

## Возможности интерфейса

- каталог инструментов с последней ценой и изменением за период;
- метрики: доходность, годовая волатильность, макс. просадка, Шарп, RSI и зона, тренд;
- график цены с SMA и маркерами сигналов ▲/▼, график RSI с уровнями 30/70, таблица сигналов;
- настройка периодов SMA/RSI и безрисковой ставки;
- **лаборатория C++**: бэктест с тепловой картой Шарпа по парам SMA и сравнением с «купить и держать» вне выборки;
  Монте-Карло с веером сценариев, VaR / CVaR и распределением доходности; оптимизация портфеля с облаком
  риск–доходность и долями бумаг; история всех расчётов в БД;
- загрузка собственного CSV в базу (ошибки показываются с номером строки), удаление своих инструментов;
- история анализов по инструменту: каждый запуск сохраняется в PostgreSQL и открывается одним кликом;
- сравнение 2–8 бумаг: динамика к 100, таблица метрик, тепловая карта корреляций;
- светлая/тёмная тема, адаптивная вёрстка, работа без интернета.

## Источник данных: Московская биржа

- API: `https://iss.moex.com/iss/engines/stock/markets/{shares|index}/.../securities/{TICKER}/candles.json?interval=24` — дневные свечи, без ключа и регистрации.
- Акции берутся из основного режима торгов TQBR, индексы (IMOEX, RTSI, отраслевые) — с рынка index.
- Свеча текущего дня в торговые часы ещё не закрыта; при обновлении последние 7 дней перезаписываются.
- Настройки (переменные окружения): `MARKET_MOEX_TICKERS` (список тикеров для автозагрузки), `MARKET_MOEX_YEARS` (глубина истории, 3),
  `MARKET_REFRESH_HOURS` (период обновления, 6), `MARKET_MOEX_ENABLED=false` — отключить биржу (только CSV/демо).
- API приложения: `POST /api/instruments/{t}/moex` — загрузить бумагу, `POST /api/instruments/{t}/refresh` — обновить, `GET /api/sync` — состояние синхронизации.

## Формат данных

```csv
Date,Open,High,Low,Close,Volume
2024-01-02,120.36,121.79,115.32,116.71,2463919
```

Файлы в `data/` — **синтетические** котировки (используются только без доступа к бирже) (`tools/generate_demo_data.py`, детерминированная модель
геометрического броуновского движения с общим рыночным фактором). Это не реальные рыночные данные.
Чтобы добавить свой инструмент, загрузите файл через интерфейс (он сохранится в БД) или положите
`<TICKER>.csv` в `data/` — он будет импортирован при следующем старте.

## Хранение данных

PostgreSQL 16, схема — миграция Flyway [`V1__init_schema.sql`](web-kotlin/src/main/resources/db/migration/V1__init_schema.sql),
ER-диаграмма — [docs/img/erd.png](docs/img/erd.png). Таблицы: `instrument`, `quote`, `analysis_run`, `metrics_report`,
`indicator_point`, `trade_signal`, `comparison`, `comparison_item`, `correlation_cell`. Целостность обеспечивают
внешние ключи (каскадное удаление) и `CHECK`-ограничения; импорт и сохранение анализа выполняются в транзакциях.
Подробности — в [docs/02-architecture.md](docs/02-architecture.md#26-модель-данных-erd-и-хранение-в-postgresql).

## REST API (кратко)

| Метод | Путь | Описание |
|---|---|---|
| GET | `/api/instruments` | список инструментов |
| POST | `/api/instruments/{t}` (тело — CSV) | сохранить / заменить котировки в БД |
| DELETE | `/api/instruments/{t}` | удалить инструмент и его историю |
| GET | `/api/instruments/{t}/analysis?fast=10&slow=30&rsi=14&rf=0` | отчёт по инструменту (сохраняется в историю) |
| POST | `/api/lab/backtest/{t}?commission=0.05&train=0.7` | C++: бэктест с перебором параметров |
| POST | `/api/lab/montecarlo/{t}?horizon=21&paths=50000` | C++: Монте-Карло, VaR / CVaR |
| POST | `/api/lab/portfolio?tickers=A,B,C&samples=100000` | C++: оптимизация портфеля |
| GET | `/api/lab/runs`, `/api/lab/runs/{id}` | история расчётов лаборатории |
| GET | `/api/runs?ticker=T`, `/api/runs/{id}` | история анализов, сохранённый отчёт |
| POST | `/api/analysis?ticker=T` (тело — CSV) | разовый отчёт без сохранения |
| GET | `/api/compare?tickers=A,B,C` | сравнение и корреляции |
| GET | `/api/health`, `/api/lab/status` | состояние приложения и БД; доступность C++ модуля |

Подробно — в [docs/02-architecture.md](docs/02-architecture.md#27-rest-api).

## Структура репозитория

```
core-cpp/      C++17 ядро: include/, src/, app/main.cpp (CLI), tests/ (Google Test)
web-kotlin/    Kotlin: market.core (порт), market.web (Ktor), market.web.storage (PostgreSQL), db/migration, static (UI), src/test
data/          демо-котировки (CSV), импортируются в БД при старте
deploy/        init-скрипт PostgreSQL
Dockerfile     многоэтапная сборка C++ + Kotlin; compose.yaml — web + PostgreSQL
docs/          требования, архитектура, UML/ERD, сравнение, тестирование, презентация
tools/         генератор демо-данных
.github/       CI (GitHub Actions)
```

## Работа с Git

- `main` — стабильная ветка; разработка ведётся в feature-ветках, изменения вливаются через Pull Request;
- каждый PR проходит CI: сборка C++ и Google Test, сборка Kotlin и JUnit (включая тест паритета C++ ↔ Kotlin
  и интеграционные тесты на PostgreSQL в сервис-контейнере), сборка Docker-образа.

> Результаты анализа носят учебный характер и не являются инвестиционной рекомендацией.
