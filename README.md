# Shopping Sessions

Exact DuckDB pipeline with strict raw-line CSV validation, transitive identity
components, deduplication before event validation, session attribution, and merged
campaign-assist intervals. Inputs and outputs use the official environment variables.

Build: `docker build -t shopping-sessions .`

Local checks: all 36 official fixtures passed. CI generates the official 1M-event
seed-1 dev set and compares both tables exactly. Large hidden-data performance has
not yet been measured. No dataset-specific or answer lookup logic is used.
