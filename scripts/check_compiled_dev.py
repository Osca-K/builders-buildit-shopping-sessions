"""Exact aggregate output comparison, including duplicates, via DuckDB."""
from pathlib import Path
import sys
import duckdb

actual,expected=map(Path,sys.argv[1:])
schemas={
 'user_daily':{'user_id':'VARCHAR','day':'DATE','sessions':'BIGINT','events':'BIGINT','revenue_cents':'BIGINT','top_category':'VARCHAR'},
 'campaign_daily':{'campaign_id':'VARCHAR','channel':'VARCHAR','day':'DATE','sessions':'BIGINT','purchasing_sessions':'BIGINT','revenue_cents':'BIGINT','assisted_sessions':'BIGINT'}}
def q(v):return "'"+str(v).replace("'","''")+"'"
con=duckdb.connect()
for table,cols in schemas.items():
    schema='{'+','.join(q(k)+':'+q(v) for k,v in cols.items())+'}'
    columns=','.join('"'+k+'"' for k in cols)
    con.execute(f'CREATE OR REPLACE VIEW a AS SELECT {columns} FROM read_parquet({q(actual/table/"*.parquet")})')
    con.execute(f'CREATE OR REPLACE VIEW b AS SELECT {columns} FROM read_csv({q(expected/(table+".csv"))},header=true,columns={schema})')
    counts=con.execute('SELECT (SELECT count(*) FROM a),(SELECT count(*) FROM b), (SELECT count(*) FROM (SELECT * FROM a EXCEPT ALL SELECT * FROM b)), (SELECT count(*) FROM (SELECT * FROM b EXCEPT ALL SELECT * FROM a))').fetchone()
    print(table,counts,flush=True)
    if counts[2] or counts[3]:raise SystemExit(1)
print('MATCH',flush=True)
