import csv
import json
import sys
from pathlib import Path

root=Path(sys.argv[1])/'parallel_long_ids'
data=root/'input';expected=root/'expected'
for directory in (data/'events',data/'clicks',data/'links',expected):directory.mkdir(parents=True,exist_ok=True)
(data/'manifest.json').write_text(json.dumps({'window_start':'2026-10-01T00:00:00Z','window_end':'2026-10-15T00:00:00Z'}))
(data/'products.csv').write_bytes(b'product_id,category\np,A\n')
(data/'campaigns.csv').write_bytes(b'campaign_id,channel\nc_1,paid\n')
(data/'links/a.csv').write_bytes('user_id_a,user_id_b\nlong_user_00_é,AAA_unused\nalias_one,long_user_00_é\n'.encode('utf-8'))
(data/'clicks/a.csv').write_bytes('click_id,user_id,campaign_id,ts\nclick_ignored,discarded_user_00_é,c_1,2026-10-01T09:00:00Z\n'.encode('utf-8'))
counts=[0]*37;revenue=[0]*37
with (data/'events/a.csv').open('w',newline='',encoding='utf-8') as stream:
 writer=csv.writer(stream,lineterminator='\n');writer.writerow(['event_id','user_id','ts','ingest_ts','event_type','product_id','price_cents'])
 for copy in range(3):
  for i in range(90000):
   # All IDs use the long/non-ASCII fallback. Earliest invalid copies must
   # suppress valid later copies, including those arriving in another chunk.
   user=i%37;invalid=i%13==0
   timestamp='not-a-date' if copy==0 and invalid else '2026-10-01T10:00:00Z'
   event_type='purchase' if i%97==0 else 'view'
   identity=f'long_user_{user:02d}_é'
   if user==0 and i%2==0:identity='alias_one'
   if copy and i%43==0:identity=f'discarded_user_{i%29:02d}_é'
   elif copy:identity=f'long_user_{(user+17)%37:02d}_é'
   writer.writerow([f'long_event_{i:012d}_é',identity,timestamp,f'2026-10-01T10:00:0{copy}Z',event_type,'p','100'])
   if copy==0 and not invalid:
    counts[user]+=1;revenue[user]+=100*(event_type=='purchase')
with (expected/'user_daily.csv').open('w',newline='',encoding='utf-8') as stream:
 writer=csv.writer(stream,lineterminator='\n');writer.writerow(['user_id','day','sessions','events','revenue_cents','top_category'])
 for user in range(37):writer.writerow(['AAA_unused' if user==0 else f'long_user_{user:02d}_é','2026-10-01',1,counts[user],revenue[user],'A' if revenue[user] else None])
with (expected/'campaign_daily.csv').open('w',newline='',encoding='utf-8') as stream:
 writer=csv.writer(stream,lineterminator='\n');writer.writerow(['campaign_id','channel','day','sessions','purchasing_sessions','revenue_cents','assisted_sessions'])
 writer.writerow(['organic','organic','2026-10-01',37,sum(bool(v) for v in revenue),sum(revenue),0])
print(root)
