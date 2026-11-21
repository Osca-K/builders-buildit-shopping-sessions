"""Synthetic long-row regression: quoting, aliases and invalid-earliest dedup."""
import csv
import json
from pathlib import Path
import sys

root = Path(sys.argv[1]) / 'long_rows'
data = root / 'input'


def write(path, rows):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('w', newline='', encoding='utf-8') as stream:
        csv.writer(stream, lineterminator='\n', quoting=csv.QUOTE_ALL).writerows(rows)


user = 'u' + 'x' * 250
alias = 'z' + 'y' * 270
start = '2026-10-03T00:00:00Z'
write(data / 'links/a.csv', [('user_id_a', 'user_id_b'), (user, alias)])
write(data / 'products.csv', [('product_id', 'category'), ('p', 'category')])
write(data / 'campaigns.csv', [('campaign_id', 'channel'), ('c', 'channel')])
write(data / 'events/a.csv', [
    ('event_id', 'user_id', 'ts', 'ingest_ts', 'event_type', 'product_id', 'price_cents'),
    ('first', user, start, start, 'view', 'p', 'ignored'),
    ('second', alias, '2026-10-03T00:01:00Z', start, 'purchase', 'p', '100'),
    ('d'*300, user, 'invalid', start, 'purchase', 'p', '200'),
    ('d'*300, alias, start, '2026-10-03T00:02:00Z', 'purchase', 'p', '200'),
])
write(data / 'clicks/a.csv', [
    ('click_id', 'user_id', 'campaign_id', 'ts'),
    ('click'*70, alias, 'c', start), ('click'*70, alias, 'c', start),
])
(data / 'manifest.json').write_text(json.dumps({
    'window_start': '2026-10-01T00:00:00Z', 'window_end': '2026-10-15T00:00:00Z'}))
write(root / 'expected/user_daily.csv', [
    ('user_id', 'day', 'sessions', 'events', 'revenue_cents', 'top_category'),
    (user, '2026-10-03', 1, 2, 100, 'category'),
])
write(root / 'expected/campaign_daily.csv', [
    ('campaign_id', 'channel', 'day', 'sessions', 'purchasing_sessions', 'revenue_cents', 'assisted_sessions'),
    ('c', 'channel', '2026-10-03', 1, 1, 100, 1),
])
