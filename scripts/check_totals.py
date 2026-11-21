import duckdb

con=duckdb.connect()
con.execute("CREATE VIEW u AS SELECT * FROM read_parquet('/output/user_daily/*.parquet')")
con.execute("CREATE VIEW c AS SELECT * FROM read_parquet('/output/campaign_daily/*.parquet')")
u=con.execute('SELECT sum(sessions),sum(revenue_cents) FROM u').fetchone()
c=con.execute('SELECT sum(sessions),sum(revenue_cents) FROM c').fetchone()
assert u==c, (u,c)
assert not con.execute('SELECT 1 FROM u GROUP BY user_id,day HAVING count(*)>1 LIMIT 1').fetchall()
assert not con.execute('SELECT 1 FROM c GROUP BY campaign_id,day HAVING count(*)>1 LIMIT 1').fetchall()
assert con.execute("SELECT coalesce(sum(assisted_sessions),0) FROM c WHERE campaign_id='organic'").fetchone()[0]==0
assisted,credited=con.execute("SELECT sum(assisted_sessions),sum(CASE WHEN campaign_id!='organic' THEN sessions ELSE 0 END) FROM c").fetchone()
assert assisted>=credited
print('Cross-table totals and unique-key checks passed. This is not a reference-answer comparison.')
