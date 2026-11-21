#!/bin/sh
set -eu

source_dir=${DATA_DIR:-/data}
output_dir=${OUTPUT_DIR:-/output}
scratch_dir=${SCRATCH_DIR:-/scratch}/compiled
threads=${NCPU:-8}
mkdir -p "$scratch_dir" "$output_dir/user_daily" "$output_dir/campaign_daily"

/app/engine "$source_dir" "$scratch_dir"

/app/duckdb :memory: -batch -c "
SET threads=$threads;
COPY (
  SELECT user_id, DATE '1970-01-01' + day AS day, sessions, events,
         revenue_cents, top_category
  FROM read_csv('$scratch_dir/user_daily.csv', header=true,
    columns={'user_id':'VARCHAR','day':'INTEGER','sessions':'BIGINT',
             'events':'BIGINT','revenue_cents':'BIGINT','top_category':'VARCHAR'},
    delim=',', quote='\"', escape='\"', new_line='\\n', allow_quoted_nulls=false)
) TO '$output_dir/user_daily/part-0.parquet' (FORMAT PARQUET, COMPRESSION ZSTD);
COPY (
  SELECT campaign_id, channel, DATE '1970-01-01' + day AS day, sessions,
         purchasing_sessions, revenue_cents, assisted_sessions
  FROM read_csv('$scratch_dir/campaign_daily.csv', header=true,
    columns={'campaign_id':'VARCHAR','channel':'VARCHAR','day':'INTEGER',
             'sessions':'BIGINT','purchasing_sessions':'BIGINT',
             'revenue_cents':'BIGINT','assisted_sessions':'BIGINT'},
    delim=',', quote='\"', escape='\"', new_line='\\n', allow_quoted_nulls=false)
) TO '$output_dir/campaign_daily/part-0.parquet' (FORMAT PARQUET, COMPRESSION ZSTD);
"
