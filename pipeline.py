"""Exact shopping-session pipeline, using vectorized DuckDB SQL.

Raw lines are parsed before CSV field decoding so malformed quotes and CR bytes
obey the challenge dialect. Connected components use union-find, including IDs
with no events. Assists merge per-person/campaign intervals before joining sessions.
"""
import json
import mmap
import os
from pathlib import Path
import re
import sys
import time

# Workspace development only; ignored when independently published.
for ancestor in Path(__file__).resolve().parents:
    local_runtime = ancestor / '.runtime'
    if local_runtime.exists():
        sys.path.insert(0, str(local_runtime))
        break
import duckdb


def quote(value):
    return "'" + str(value).replace("'", "''") + "'"


class StageConnection:
    """Optional stage timings for reproducible profiling on public datasets."""
    def __init__(self, connection):
        self.connection = connection
        self.profile = os.environ.get('PROFILE_PIPELINE') == '1'

    def execute(self, sql, parameters=None):
        started = time.perf_counter()
        result = self.connection.execute(sql) if parameters is None else self.connection.execute(sql, parameters)
        label = re.match(r'(?:CREATE (?:TABLE|MACRO)|INSERT INTO|DROP TABLE|COPY)\s+\w+', sql)
        if self.profile and label:
            print(f'Stage {label[0]}: {time.perf_counter()-started:.3f}s', file=sys.stderr)
        return result


def raw_rows(folder, count):
    field = r'(?:"[^",\n]*"|[^",\n]*)'
    valid = '^' + ','.join([field] * count) + '$'
    paths = sorted(Path(folder).glob('*.csv'))
    if not paths:
        return 'SELECT NULL::VARCHAR[] AS f WHERE false'
    fast, strict = [], []
    for path in paths:
        if path.stat().st_size == 0:
            continue
        with path.open('rb') as handle, mmap.mmap(handle.fileno(),0,access=mmap.ACCESS_READ) as content:
            # DuckDB's CSV reader treats CR as a line ending, unlike the rules.
            # Exceptional files retain the exact-byte text parser. The delimiter
            # must also be absent from values before the parallel line fast path.
            (strict if content.find(b'\r') >= 0 or content.find(b'\x1f') >= 0 else fast).append(path)
    def file_list(items):
        return '[' + ','.join(quote(p.as_posix()) for p in items) + ']'
    sources=[]
    if fast:
        sources.append(f"SELECT line FROM read_csv({file_list(fast)},columns={{'line':'VARCHAR'}},"
                       "delim=chr(31),quote='',escape='',header=false,skip=1,new_line='\\n',"
                       "strict_mode=false,parallel=true)")
    if strict:
        sources.append(f"SELECT unnest(string_split(content, chr(10))[2:]) AS line FROM read_text({file_list(strict)})")
    if not sources:
        return 'SELECT NULL::VARCHAR[] AS f WHERE false'
    source = ' UNION ALL '.join(sources)
    return f"""SELECT list_transform(string_split(line, ','),
        x -> CASE WHEN starts_with(x, '"') THEN x[2:-2] ELSE x END) AS f
        FROM ({source})
        WHERE regexp_full_match(line, {quote(valid)})"""


def link_map(folder):
    parent, size, minimum = {}, {}, {}

    def find(x):
        while parent[x] != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x

    field = re.compile(r'^(?:"[^",\n]*"|[^",\n]*),(?:"[^",\n]*"|[^",\n]*)$')
    for file in sorted(Path(folder).glob('*.csv')):
        with file.open(encoding='utf-8', newline='') as stream:
            next(stream, None)
            for line in stream:
                line = line.removesuffix('\n')
                if not field.fullmatch(line):
                    continue
                a, b = [x[1:-1] if x.startswith('"') else x for x in line.split(',')]
                if not a or not b:
                    continue
                for x in (a, b):
                    if x not in parent:
                        parent[x], size[x], minimum[x] = x, 1, x
                a, b = find(a), find(b)
                if a != b:
                    if size[a] < size[b]:
                        a, b = b, a
                    parent[b] = a
                    size[a] += size[b]
                    minimum[a] = min(minimum[a], minimum[b])
    return [(x, minimum[find(x)]) for x in parent]


def main():
    started = time.perf_counter()
    data = Path(os.environ.get('DATA_DIR', sys.argv[1] if len(sys.argv) > 1 else '/data'))
    output = Path(os.environ.get('OUTPUT_DIR', sys.argv[2] if len(sys.argv) > 2 else '/output'))
    scratch = Path(os.environ.get('SCRATCH_DIR', str(output / 'scratch')))
    scratch.mkdir(parents=True, exist_ok=True)
    con = StageConnection(duckdb.connect())
    con.execute(f"SET threads={int(os.environ.get('NCPU', '8'))}")
    con.execute(f"SET memory_limit='{float(os.environ.get('MEM_GB','24'))*.75}GB'")
    con.execute(f"SET temp_directory={quote(scratch.as_posix())}")
    con.execute('SET preserve_insertion_order=false')
    manifest = json.loads((data / 'manifest.json').read_text())
    con.execute("CREATE MACRO ms(s) AS CASE "
        "WHEN regexp_full_match(s, '[0-9]+') THEN try_cast(s AS BIGINT) "
        "WHEN regexp_full_match(s, '[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}Z') "
        "THEN epoch_ms(try_strptime(s, '%Y-%m-%dT%H:%M:%SZ')) "
        "WHEN regexp_full_match(s, '[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}[+-]([01][0-9]|2[0-3]):[0-5][0-9]') "
        "THEN epoch_ms(try_strptime(s[:19], '%Y-%m-%dT%H:%M:%S')) - "
        "(CASE WHEN s[20]='-' THEN -1 ELSE 1 END) * "
        "(try_cast(s[21:22] AS BIGINT)*60+try_cast(s[24:25] AS BIGINT))*60000 "
        "ELSE NULL END")
    start, end = con.execute('SELECT ms(?),ms(?)', [manifest['window_start'], manifest['window_end']]).fetchone()
    con.execute('CREATE TABLE people(user_id VARCHAR, person VARCHAR)')
    mapping = link_map(data / 'links')
    # Batched inserts keep the union-find independent of optional data libraries.
    if mapping:
        con.execute('INSERT INTO people SELECT unnest(?::VARCHAR[]),unnest(?::VARCHAR[])',
                    [[x[0] for x in mapping],[x[1] for x in mapping]])
    con.execute(f"CREATE TABLE products AS SELECT * FROM read_csv({quote((data/'products.csv').as_posix())}, all_varchar=true)")
    con.execute(f"CREATE TABLE campaigns AS SELECT * FROM read_csv({quote((data/'campaigns.csv').as_posix())}, all_varchar=true)")
    con.execute(f"""CREATE TABLE events AS
        WITH raw AS ({raw_rows(data/'events',7)}),
        dedup AS (SELECT f FROM raw WHERE f[1] != ''
            QUALIFY row_number() OVER (PARTITION BY f[1] ORDER BY f[4])=1),
        parsed AS (SELECT f[2] user_id, ms(f[3]) ts, ms(f[4]) ingest,
          f[5] kind, f[6] product_id,
          CASE WHEN regexp_full_match(f[7], '[0-9]+') THEN try_cast(f[7] AS BIGINT) END price FROM dedup)
        SELECT coalesce(p.person,e.user_id) person, ts, kind,
          CASE WHEN kind='purchase' THEN price ELSE 0 END revenue,
          coalesce(pr.category,'unknown') category
        FROM parsed e LEFT JOIN people p USING(user_id) LEFT JOIN products pr USING(product_id)
        WHERE e.user_id != '' AND ts >= {start} AND ts < {end}
          AND ingest-ts <= 86400000 AND product_id != ''
          AND kind IN ('view','cart','purchase')
          AND (kind != 'purchase' OR price BETWEEN 1 AND 10000000)""")
    con.execute(f"""CREATE TABLE clicks AS
        WITH raw AS ({raw_rows(data/'clicks',4)}),
        dedup AS (SELECT f FROM raw WHERE f[1] != '' QUALIFY row_number() OVER(PARTITION BY f[1])=1),
        parsed AS (SELECT f[1] click_id,f[2] user_id,f[3] campaign_id,ms(f[4]) ts FROM dedup)
        SELECT click_id,coalesce(p.person,c.user_id) person,campaign_id,ts
        FROM parsed c JOIN campaigns USING(campaign_id) LEFT JOIN people p USING(user_id)
        WHERE c.user_id != '' AND ts >= {start-604800000} AND ts < {end}""")
    con.execute("""CREATE TABLE session_events AS
        WITH gaps AS (SELECT *, ts-lag(ts) OVER(PARTITION BY person ORDER BY ts) gap FROM events),
        numbered AS (SELECT *,sum(CASE WHEN gap IS NULL OR gap>1800000 THEN 1 ELSE 0 END)
            OVER(PARTITION BY person ORDER BY ts RANGE BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW) sid FROM gaps)
        SELECT person,ts,kind,revenue,category,sid,
          min(ts) OVER(PARTITION BY person,sid) session_start FROM numbered""")
    con.execute('DROP TABLE events')
    con.execute("""CREATE TABLE sessions AS SELECT person,sid, min(session_start) ts,
        epoch_ms(min(session_start))::DATE AS day, count(*) events,
        sum(revenue)::BIGINT revenue, bool_or(kind='purchase') purchasing
        FROM session_events GROUP BY person,sid""")
    # Latest click is an ASOF lookup after exact timestamp ties are resolved.
    con.execute("""CREATE TABLE last_click AS SELECT person,ts,campaign_id FROM clicks
        QUALIFY row_number() OVER(PARTITION BY person,ts ORDER BY click_id)=1""")
    con.execute("""CREATE TABLE credited AS SELECT s.*,
        CASE WHEN s.ts-c.ts<=604800000 THEN c.campaign_id ELSE 'organic' END campaign_id
        FROM sessions s ASOF LEFT JOIN last_click c ON s.person=c.person AND s.ts>=c.ts""")
    # Unions of [click.ts,click.ts+7days] avoid a whale-sized many-to-many join.
    con.execute("""CREATE TABLE intervals AS
        WITH distinct_clicks AS (SELECT DISTINCT person,campaign_id,ts FROM clicks),
        gaps AS (SELECT *,ts-lag(ts) OVER(PARTITION BY person,campaign_id ORDER BY ts) gap FROM distinct_clicks),
        numbered AS (SELECT *,sum(CASE WHEN gap IS NULL OR gap>604800000 THEN 1 ELSE 0 END)
          OVER(PARTITION BY person,campaign_id ORDER BY ts) grp FROM gaps)
        SELECT person,campaign_id,min(ts) lo,max(ts)+604800000 hi
        FROM numbered GROUP BY person,campaign_id,grp""")
    con.execute("""CREATE TABLE assists AS SELECT i.campaign_id,s.day,count(*)::BIGINT assisted_sessions
        FROM sessions s JOIN intervals i ON s.person=i.person AND s.ts>=i.lo AND s.ts<=i.hi
        GROUP BY i.campaign_id,s.day""")
    con.execute("""CREATE TABLE user_result AS
        WITH totals AS (SELECT person user_id,day,count(*)::BIGINT sessions,
            sum(events)::BIGINT events,sum(revenue)::BIGINT revenue_cents FROM sessions GROUP BY person,day),
        categories AS (SELECT person user_id,epoch_ms(session_start)::DATE AS day,category,sum(revenue) revenue
            FROM session_events WHERE kind='purchase' GROUP BY person,day,category),
        top AS (SELECT user_id,day,category top_category FROM categories
            QUALIFY row_number() OVER(PARTITION BY user_id,day ORDER BY revenue DESC,category)=1)
        SELECT * FROM totals LEFT JOIN top USING(user_id,day)""")
    con.execute("""CREATE TABLE campaign_result AS
        WITH credit AS (SELECT campaign_id,day,count(*)::BIGINT sessions,
            count(*) FILTER(WHERE purchasing)::BIGINT purchasing_sessions,
            sum(revenue)::BIGINT revenue_cents FROM credited GROUP BY campaign_id,day),
        combined AS (SELECT coalesce(c.campaign_id,a.campaign_id) campaign_id,coalesce(c.day,a.day) AS day,
            coalesce(sessions,0)::BIGINT sessions,coalesce(purchasing_sessions,0)::BIGINT purchasing_sessions,
            coalesce(revenue_cents,0)::BIGINT revenue_cents,coalesce(assisted_sessions,0)::BIGINT assisted_sessions
            FROM credit c FULL OUTER JOIN assists a USING(campaign_id,day))
        SELECT c.campaign_id,CASE WHEN c.campaign_id='organic' THEN 'organic' ELSE p.channel END channel,
            day,sessions,purchasing_sessions,revenue_cents,assisted_sessions
        FROM combined c LEFT JOIN campaigns p USING(campaign_id)""")
    for table, result in [('user_daily','user_result'),('campaign_daily','campaign_result')]:
        folder = output / table
        folder.mkdir(parents=True, exist_ok=True)
        con.execute(f"COPY {result} TO {quote((folder/'part-0.parquet').as_posix())} (FORMAT PARQUET,COMPRESSION ZSTD)")
    print(f'Completed in {time.perf_counter()-started:.3f}s', file=sys.stderr)


if __name__ == '__main__':
    main()
