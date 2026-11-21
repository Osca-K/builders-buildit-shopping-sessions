"""Compare aggregate multisets across two independently implemented pipelines."""
from pathlib import Path
import sys
import duckdb

a,b=map(Path,sys.argv[1:])
con=duckdb.connect()
con.execute("SET memory_limit='1GB'")
for table in ('user_daily','campaign_daily'):
    for view,folder in (('a',a),('b',b)):
        path=str(folder/table/'*.parquet').replace("'","''")
        con.execute(f"CREATE OR REPLACE VIEW {view} AS SELECT * FROM read_parquet('{path}')")
    result=con.execute('SELECT (SELECT count(*) FROM a),(SELECT count(*) FROM b), (SELECT count(*) FROM (SELECT * FROM a EXCEPT ALL SELECT * FROM b)), (SELECT count(*) FROM (SELECT * FROM b EXCEPT ALL SELECT * FROM a))').fetchone()
    print(table,result,flush=True)
    if result[2] or result[3]:raise SystemExit(1)
print('MATCH',flush=True)
