FROM python:3.13-slim@sha256:e48f59cdba69a3cbd9ed2cf0f348d90d45a8d350d98134cf1070bc106ee9ed90 AS build
RUN apt-get update && apt-get install -y --no-install-recommends g++ curl ca-certificates && rm -rf /var/lib/apt/lists/*
COPY engine.cpp parallel_event_dedup.hpp parallel_event_validate.hpp parallel_clicks.hpp person_sort.hpp /src/
COPY unordered_dense.h stl.h /src/
COPY tests/ /src/tests/
RUN g++ -std=c++20 -O3 -DNDEBUG -pthread -march=x86-64-v3 -flto /src/engine.cpp -o /src/engine
RUN g++ -std=c++20 -O2 -pthread -march=x86-64-v3 /src/tests/engine_checks.cpp -o /tmp/engine_checks && /tmp/engine_checks
RUN curl -fsSL https://github.com/duckdb/duckdb/releases/download/v1.5.5/duckdb_cli-linux-amd64.gz -o /tmp/duckdb.gz \
 && echo 'c61f21485e6e41d3a0c28ce9904ea18346309cf427b4cf9479bc3564348dc885  /tmp/duckdb.gz' | sha256sum -c - \
 && gzip -d /tmp/duckdb.gz && chmod 755 /tmp/duckdb
FROM python:3.13-slim@sha256:e48f59cdba69a3cbd9ed2cf0f348d90d45a8d350d98134cf1070bc106ee9ed90
COPY --from=build /usr/lib/x86_64-linux-gnu/libstdc++.so.6 /usr/lib/x86_64-linux-gnu/libstdc++.so.6
COPY --from=build /usr/lib/x86_64-linux-gnu/libgcc_s.so.1 /usr/lib/x86_64-linux-gnu/libgcc_s.so.1
COPY --from=build /src/engine /app/engine
COPY --from=build /tmp/duckdb /app/duckdb
COPY run_compiled.sh /app/run_compiled.sh
COPY LICENSE.unordered_dense /app/licenses/unordered_dense.txt
WORKDIR /app
CMD ["/bin/sh","/app/run_compiled.sh"]

