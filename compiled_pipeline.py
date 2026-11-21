"""Run the exact compiled core and serialize its aggregate rows as Parquet."""
import os
from pathlib import Path
import subprocess
import time
import duckdb


def quote(value):
    return "'"+str(value).replace("'","''")+"'"


def main():
    started=time.perf_counter()
    source=Path(os.environ.get('DATA_DIR','/data'))
    output=Path(os.environ.get('OUTPUT_DIR','/output'))
    scratch=Path(os.environ.get('SCRATCH_DIR','/scratch'))/'compiled'
    scratch.mkdir(parents=True,exist_ok=True)
    subprocess.run([str(Path(__file__).with_name('engine')),str(source),str(scratch)],check=True)
    con=duckdb.connect()
    con.execute(f"SET threads={max(1,int(os.environ.get('NCPU','8')))}")
    schemas={
      'user_daily':{'user_id':'VARCHAR','day':'INTEGER','sessions':'BIGINT','events':'BIGINT','revenue_cents':'BIGINT','top_category':'VARCHAR'},
      'campaign_daily':{'campaign_id':'VARCHAR','channel':'VARCHAR','day':'INTEGER','sessions':'BIGINT','purchasing_sessions':'BIGINT','revenue_cents':'BIGINT','assisted_sessions':'BIGINT'}}
    for table,columns in schemas.items():
        dest=output/table;dest.mkdir(parents=True,exist_ok=True)
        schema='{'+','.join(quote(k)+':'+quote(v) for k,v in columns.items())+'}'
        select=','.join("DATE '1970-01-01'+day AS day" if c=='day' else '"'+c+'"' for c in columns)
        con.execute(f"COPY (SELECT {select} FROM read_csv({quote(scratch/(table+'.csv'))},header=true,columns={schema},delim=',',quote='\"',escape='\"',new_line='\\n',allow_quoted_nulls=false)) TO {quote(dest/'part-0.parquet')} (FORMAT PARQUET,COMPRESSION ZSTD)")
    print(f'Compiled pipeline total: {time.perf_counter()-started:.3f}s',flush=True)


if __name__=='__main__':main()
