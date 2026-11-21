# Shopping Sessions

Exact C++20 pipeline with strict raw-line CSV validation, transitive identity
components, deduplication before event validation, session attribution, and rolling
campaign counts. The standalone DuckDB CLI writes the final aggregate tables as
Parquet without Python startup overhead. Inputs and
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

Small person histories now share work claims in batches of 64; only large
histories are sorted by size for scheduling. All 36 fixtures still pass and both
aggregate tables match the submitted version exactly on 1M and 20M events.
A warmed 20M comparison on 2 CPUs/3GB measured 47.81s before and 47.29s after.
This small, load-sensitive improvement does not establish a new leaderboard score.

Identity and product lookups now use the MIT-licensed ankerl::unordered_dense
4.8.1, pinned at 3234af2c03549bc85656bfd3a86993bf1cd8aef1 from
https://github.com/martinus/unordered_dense. Headers and license are vendored.
Event rows retain validated CSV field boundaries in their existing 24-byte record;
rows longer than 255 bytes use the original general parser. Deduplication still
precedes validation, and hash collisions compare complete decoded identifiers.

Two alternating 20M-event/5M-click runs on 2 CPUs and 3GB measured median engine
time 38.183s versus 52.207s for the submitted version (26.9% lower). Both output
tables matched exactly. This is a local benchmark, not a hidden leaderboard score.
