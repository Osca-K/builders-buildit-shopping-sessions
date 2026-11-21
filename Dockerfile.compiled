FROM python:3.13-slim@sha256:e48f59cdba69a3cbd9ed2cf0f348d90d45a8d350d98134cf1070bc106ee9ed90 AS build
RUN apt-get update && apt-get install -y --no-install-recommends g++ && rm -rf /var/lib/apt/lists/*
COPY engine.cpp parallel_event_dedup.hpp parallel_event_validate.hpp parallel_clicks.hpp /src/
RUN g++ -std=c++20 -O3 -DNDEBUG -pthread /src/engine.cpp -o /src/engine
FROM python:3.13-slim@sha256:e48f59cdba69a3cbd9ed2cf0f348d90d45a8d350d98134cf1070bc106ee9ed90
COPY --from=build /usr/lib/x86_64-linux-gnu/libstdc++.so.6 /usr/lib/x86_64-linux-gnu/libstdc++.so.6
COPY --from=build /usr/lib/x86_64-linux-gnu/libgcc_s.so.1 /usr/lib/x86_64-linux-gnu/libgcc_s.so.1
COPY requirements.txt /app/requirements.txt
RUN pip install --no-cache-dir -r /app/requirements.txt
COPY --from=build /src/engine /app/engine
COPY compiled_pipeline.py /app/compiled_pipeline.py
WORKDIR /app
CMD ["python","/app/compiled_pipeline.py"]
