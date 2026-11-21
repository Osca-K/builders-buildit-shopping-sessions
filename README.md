# Shopping Sessions

Exact C++20 pipeline with strict raw-line CSV validation, transitive identity
components, deduplication before event validation, session attribution, and rolling
campaign counts. DuckDB writes the final aggregate tables as Parquet. Inputs and
outputs use the official environment variables. Event deduplication, validation,
and click parsing run in parallel; complete IDs resolve every hash collision.

Identical click copies can be retained: each enters and leaves the rolling count
once, while assists depend only on campaign presence. Attribution remains latest
timestamp, then byte-wise smallest click ID. No data-dependent answers are stored.

Build: `docker build -t shopping-sessions .`

Local checks: all 36 official fixtures passed. A generated 20M-event / 5M-click
seed-17 dataset matched both output multisets from the independent SQL baseline:
2,249,563 user-day rows and 28,014 campaign-day rows, no differences. Local runs
with 4 CPUs took 50.25 seconds compiled versus 201.51 seconds SQL. Hardware and
background load affect these measurements; hidden-data performance is separate.
CI also generates the official 1M-event seed-1 dev set and compares its answer.

`Dockerfile.sql` preserves the previous independent SQL implementation.
`Dockerfile.generator` builds the official public generator for local benchmarks.

The next revision replaces global event/click sorting with counting partitioning
by canonical person, followed by parallel sorting of independent histories.
All36 official fixtures and the exact20M independent SQL comparison pass.
Local20M run:29.450 seconds; performance still depends on hardware and load.
