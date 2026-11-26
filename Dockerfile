# syntax=docker/dockerfile:1
# Многоэтапная сборка: C++ ядро (market_cli) + Kotlin веб-сервис в одном образе.
# Базовые образы на Ubuntu 24.04, чтобы бинарник C++ и среда выполнения имели одинаковые glibc/libstdc++.

# ---------- 1. C++ ядро ----------
FROM ubuntu:24.04 AS cpp-build
RUN apt-get update \
 && apt-get install -y --no-install-recommends g++ cmake make git ca-certificates \
 && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY core-cpp/ core-cpp/
COPY data/ data/
ARG MARKET_BUILD_TESTS=OFF
RUN cmake -S core-cpp -B build -DCMAKE_BUILD_TYPE=Release -DMARKET_BUILD_TESTS=${MARKET_BUILD_TESTS} \
 && cmake --build build -j"$(nproc)"

# ---------- 2. Kotlin веб-сервис ----------
FROM eclipse-temurin:21-jdk-noble AS kotlin-build
WORKDIR /src/web-kotlin
# сначала только файлы сборки — зависимости кэшируются отдельным слоем
COPY web-kotlin/gradlew web-kotlin/settings.gradle.kts web-kotlin/build.gradle.kts ./
COPY web-kotlin/gradle/ gradle/
# sed: если репозиторий получен на Windows (core.autocrlf), убираем CRLF — иначе "./gradlew: not found"
RUN sed -i 's/\r$//' gradlew && chmod +x gradlew && ./gradlew --no-daemon -q dependencies > /dev/null
COPY web-kotlin/src/ src/
RUN ./gradlew --no-daemon -q installDist

# ---------- 3. Тесты: C++ (Google Test) + Kotlin (JUnit, в т.ч. с PostgreSQL из compose) ----------
FROM kotlin-build AS test
RUN apt-get update \
 && apt-get install -y --no-install-recommends g++ cmake make git \
 && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY core-cpp/ core-cpp/
COPY data/ data/
RUN cmake -S core-cpp -B core-cpp/build -DCMAKE_BUILD_TYPE=Release && cmake --build core-cpp/build -j"$(nproc)"
WORKDIR /src/web-kotlin
CMD ["sh", "-c", "ctest --test-dir /src/core-cpp/build --output-on-failure && ./gradlew --no-daemon test"]

# ---------- 4. Итоговый образ ----------
FROM eclipse-temurin:21-jre-noble AS runtime
RUN apt-get update \
 && apt-get install -y --no-install-recommends curl \
 && rm -rf /var/lib/apt/lists/* \
 && useradd --system --uid 10001 --create-home market
WORKDIR /app
COPY --from=kotlin-build /src/web-kotlin/build/install/market-web/ /app/
COPY --from=cpp-build /src/build/market_cli /app/bin/market_cli
COPY data/ /app/data/
# MARKET_DB_URL / MARKET_DB_USER / MARKET_DB_PASSWORD задаются в compose.yaml;
# без них приложение работает с хранилищем в памяти.
ENV PORT=8080 \
    MARKET_DATA_DIR=/app/data \
    MARKET_CLI=/app/bin/market_cli
USER market
EXPOSE 8080
HEALTHCHECK --interval=15s --timeout=3s --start-period=20s --retries=3 \
  CMD curl -fs http://localhost:8080/api/health || exit 1
ENTRYPOINT ["/app/bin/market-web"]
