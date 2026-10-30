"""results 폴더의 CSV를 태그별 한 줄로 요약한다.

봇이 목표 인원에 도달한 뒤 예열 구간을 더 빼고, 끝 부분도 뺀 안정 구간만 집계한다.
사용: python summarize.py [results 폴더] [--warmup 60] [--tail 5]
"""
import argparse
import csv
import glob
import os


def rows(path):
    with open(path, newline='') as f:
        return [{k: float(v) for k, v in r.items()} for r in csv.DictReader(f)]


def stable(data, warmup, tail):
    return data[warmup:len(data) - tail] if len(data) > warmup + tail else []


def settled_index(bot):
    """봇이 목표 인원에 처음 도달한 행."""
    top = max((r['active'] for r in bot), default=0)
    return next((i for i, r in enumerate(bot) if r['active'] >= top), 0)


def traffic_index(srv):
    """서버 CSV는 봇 접속 전 대기 구간부터 찍히므로 첫 트래픽 행을 찾아 봇과 맞춘다."""
    return next((i for i, r in enumerate(srv) if r['send_packets'] > 0), 0)


def mean(data, key):
    return sum(r[key] for r in data) / len(data) if data else 0.0


def weighted(data, key, weight):
    total = sum(r[weight] for r in data)
    return sum(r[key] * r[weight] for r in data) / total if total else 0.0


def worst(data, key):
    return max((r[key] for r in data), default=0.0)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('dir', nargs='?', default=os.path.join(os.path.dirname(os.path.abspath(__file__)), 'results'))
    ap.add_argument('--warmup', type=int, default=60)
    ap.add_argument('--tail', type=int, default=5)
    args = ap.parse_args()

    header = ['tag', 'secs', 'bots', 'rtt_p50', 'rtt_p95', 'rtt_p99', 'rtt_max', 'cpu%', 'mem_mb',
              'wsasend/s', 'pkt/call', 'sector_ns', 'lua/s', 'lua_ns', 'lua_wait_ns', 'lua_states',
              'active_npc', 'timer_lag_ms', 'db/s', 'db_ms', 'db_pending_max',
              'login/s', 'login_fail', 'login_timeout', 'login_p50', 'login_p99']
    print(','.join(header))
    for bot_path in sorted(glob.glob(os.path.join(args.dir, 'bot_*.csv'))):
        tag = os.path.basename(bot_path)[4:-4]
        bot_rows = rows(bot_path)
        start = settled_index(bot_rows) + args.warmup
        bot = stable(bot_rows, start, args.tail)
        server_path = os.path.join(args.dir, f'server_{tag}.csv')
        srv = []
        if os.path.exists(server_path):
            srv_rows = rows(server_path)
            first = traffic_index(srv_rows)
            srv = stable(srv_rows[first:first + len(bot_rows)], start, args.tail)
        calls = sum(r['wsasend_calls'] for r in srv)
        packets = sum(r['send_packets'] for r in srv)
        out = [tag, len(bot), mean(bot, 'active'),
               weighted(bot, 'rtt_p50_ms', 'rtt_samples'), weighted(bot, 'rtt_p95_ms', 'rtt_samples'),
               weighted(bot, 'rtt_p99_ms', 'rtt_samples'), worst(bot, 'rtt_max_ms'),
               mean(srv, 'cpu_pct'), mean(srv, 'working_set_mb'), mean(srv, 'wsasend_calls'),
               packets / calls if calls else 0.0,
               weighted(srv, 'sector_avg_ns', 'sector_queries'), mean(srv, 'lua_calls'),
               weighted(srv, 'lua_avg_ns', 'lua_calls'), weighted(srv, 'lua_wait_avg_ns', 'lua_calls'),
               worst(srv, 'lua_states'), mean(srv, 'active_npcs'),
               weighted(srv, 'timer_lag_avg_us', 'timer_events') / 1000.0, mean(srv, 'db_tasks'),
               weighted(srv, 'db_avg_ms', 'db_tasks'), worst(srv, 'db_pending'),
               mean(bot, 'login_ok'), sum(r['login_fail'] for r in bot), sum(r['login_timeout'] for r in bot),
               weighted(bot, 'login_p50_ms', 'login_ok'), weighted(bot, 'login_p99_ms', 'login_ok')]
        print(','.join(f'{v:.2f}' if isinstance(v, float) else str(v) for v in out))


if __name__ == '__main__':
    main()
