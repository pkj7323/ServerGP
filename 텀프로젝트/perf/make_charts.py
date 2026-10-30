"""측정 CSV에서 성능 비교 패널 PNG를 만든다. 색과 구성은 덱의 VSM 패널에 맞춘다.

사용: python make_charts.py [출력 폴더]
Pillow만 쓴다. 값은 전부 results, results_smoke의 CSV에서 다시 계산한다.
"""
import os
import re
import sys

from PIL import Image, ImageDraw, ImageFont

from summarize import mean, rows, settled_index, stable, traffic_index, weighted

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_OUT = r'C:\Users\PARK\Desktop\포트폴리오\02_포트폴리오\포트폴리오_통합_겜서버_PIP\성능비교\그래프'

SURFACE = '#1E1E1E'
RULE = '#4A4A4A'
INK = '#FFFFFF'
MUTED = '#B0B0B0'
CURRENT = '#1B7FA8'  # 현재 구조
COMPARE = '#E97132'  # 비교 구조

WIDTH, HEIGHT = 1600, 1000
DRAW_SCALE = 3   # 3배로 그린다
OUTPUT_SCALE = 1.5  # 2400x1500으로 줄여 저장한다
MARGIN = 60
PLOT_TOP, PLOT_BOTTOM = 330, 810
BIG_BASELINE = 186

FONT_REGULAR = r'C:\Windows\Fonts\malgun.ttf'
FONT_BOLD = r'C:\Windows\Fonts\malgunbd.ttf'


def font(size, bold=False):
    return ImageFont.truetype(FONT_BOLD if bold else FONT_REGULAR, size * DRAW_SCALE)


class Canvas:
    def __init__(self):
        self.image = Image.new('RGB', (WIDTH * DRAW_SCALE, HEIGHT * DRAW_SCALE), SURFACE)
        self.draw = ImageDraw.Draw(self.image)

    def text(self, x, y, value, size, color=MUTED, bold=False, anchor='la'):
        self.draw.text((x * DRAW_SCALE, y * DRAW_SCALE), value, fill=color, font=font(size, bold), anchor=anchor)

    def width(self, value, size, bold=False):
        return self.draw.textlength(value, font=font(size, bold)) / DRAW_SCALE

    def rect(self, x0, y0, x1, y1, color):
        self.draw.rectangle([x0 * DRAW_SCALE, y0 * DRAW_SCALE, x1 * DRAW_SCALE, y1 * DRAW_SCALE], fill=color)

    def bar(self, x0, y0, x1, y1, color):
        # 위쪽만 둥글게 하고 바닥은 기준선에 붙인다.
        radius = min(4, (y1 - y0) / 2, (x1 - x0) / 2)
        self.draw.rounded_rectangle([x0 * DRAW_SCALE, y0 * DRAW_SCALE, x1 * DRAW_SCALE, y1 * DRAW_SCALE],
                                    radius=radius * DRAW_SCALE, fill=color,
                                    corners=(True, True, False, False))

    def save(self, path):
        size = (int(WIDTH * OUTPUT_SCALE), int(HEIGHT * OUTPUT_SCALE))
        self.image.resize(size, Image.LANCZOS).save(path, optimize=True)


def draw_zone(c, x0, x1, zone):
    if zone.get('title'):
        c.text(x0, PLOT_TOP - 84, zone['title'], 25, MUTED)
    groups = zone['groups']
    top = max(value for group in groups for value, _, _ in group['bars'])
    slot = (x1 - x0) / len(groups)
    bar_w = min(112, (slot - 44) / max(len(g['bars']) for g in groups))
    for i, group in enumerate(groups):
        center = x0 + slot * (i + 0.5)
        left = center - bar_w * len(group['bars']) / 2
        for j, (value, color, label) in enumerate(group['bars']):
            bx0 = left + bar_w * j + 2  # 막대 사이 간격
            bx1 = left + bar_w * (j + 1) - 2
            h = max(4, (PLOT_BOTTOM - PLOT_TOP) * value / top)
            c.bar(bx0, PLOT_BOTTOM - h, bx1, PLOT_BOTTOM, color)
            c.text((bx0 + bx1) / 2, PLOT_BOTTOM - h - 8, label, 27, INK, bold=True, anchor='ms')
        c.text(center, PLOT_BOTTOM + 14, group['label'], 26, MUTED, anchor='ma')
    c.rect(x0, PLOT_BOTTOM, x1, PLOT_BOTTOM + 1, RULE)


def panel(path, header, big, big_unit, legend, zones, captions):
    c = Canvas()
    c.text(MARGIN, 42, header, 31)
    # 숫자만 크게 쓰고 단위와 설명은 같은 기준선에 작게 붙인다.
    number, suffix = re.match(r'([\d.,]+)\s*(.*)', big).groups()
    c.text(MARGIN, BIG_BASELINE, number, 94, INK, bold=True, anchor='ls')
    x = MARGIN + c.width(number, 94, True) + 10
    if suffix:
        c.text(x, BIG_BASELINE, suffix, 56, INK, bold=True, anchor='ls')
        x += c.width(suffix, 56, True) + 10
    if big_unit:
        c.text(x + 14, BIG_BASELINE, big_unit, 31, INK, bold=True, anchor='ls')

    x = WIDTH - MARGIN
    for label, color in reversed(legend):
        w = c.width(label, 25)
        c.text(x - w, 46, label, 25)
        c.rect(x - w - 32, 53, x - w - 12, 73, color)
        x -= w + 62

    gap = 70
    zone_w = (WIDTH - 2 * MARGIN - gap * (len(zones) - 1)) / len(zones)
    for i, zone in enumerate(zones):
        draw_zone(c, MARGIN + i * (zone_w + gap), MARGIN + i * (zone_w + gap) + zone_w, zone)

    for i, line in enumerate(captions):
        c.text(MARGIN, 882 + i * 36, line, 23)
    c.save(path)
    print(os.path.basename(path))


# ---- 측정값 ---------------------------------------------------------------

def sector_values(results, tag):
    """PC 2대 측정: 마지막 180초 (끝 5초 제외)."""
    bot_rows = rows(os.path.join(results, f'bot_{tag}.csv'))
    srv_rows = rows(os.path.join(results, f'server_{tag}.csv'))
    first = traffic_index(srv_rows)
    srv = srv_rows[first:first + len(bot_rows)][-185:-5]
    bot = [r for r in bot_rows[-185:-5] if r['rtt_samples'] > 0]
    p99 = sorted(r['rtt_p99_ms'] for r in bot)
    return {
        'p50': weighted(bot, 'rtt_p50_ms', 'rtt_samples'),
        'p99': p99[len(p99) // 2],
        'cpu': mean(srv, 'cpu_pct'),
        'query_us': weighted(srv, 'sector_avg_ns', 'sector_queries') / 1000,
        'active': bot_rows[-6]['active'],
    }


def smoke_values(results, tag, warmup=3, tail=1):
    """한 PC 예비 측정: 목표 인원 도달 후 안정 구간."""
    bot_rows = rows(os.path.join(results, f'bot_{tag}.csv'))
    srv_rows = rows(os.path.join(results, f'server_{tag}.csv'))
    first = traffic_index(srv_rows)
    start = settled_index(bot_rows) + warmup
    bot = stable(bot_rows, start, tail)
    srv = stable(srv_rows[first:first + len(bot_rows)], start, tail)
    return {
        'p99': weighted(bot, 'rtt_p99_ms', 'rtt_samples'),
        'login_p50': weighted(bot, 'login_p50_ms', 'login_ok'),
        'wsasend': mean(srv, 'wsasend_calls'),
        'pkt_per_call': sum(r['send_packets'] for r in srv) / sum(r['wsasend_calls'] for r in srv),
        'lua_us': weighted(srv, 'lua_avg_ns', 'lua_calls') / 1000,
        'lua_wait_us': weighted(srv, 'lua_wait_avg_ns', 'lua_calls') / 1000,
        'mem': mean(srv, 'working_set_mb'),
        'states': max(r['lua_states'] for r in srv),
    }


def num(value):
    if value >= 100:
        return f'{value:,.0f}'
    if value >= 10:
        return f'{value:.1f}'
    return f'{value:.2f}'


def sector_charts(out):
    results = os.path.join(HERE, 'results')
    counts = [500, 1000, 2000, 4000]
    base = {n: sector_values(results, f'base_n{n}_r1') for n in counts}
    brute = {n: sector_values(results, f'sector_brute_n{n}_r1') for n in counts}
    legend = [('격자 없음 (전체 탐색)', COMPARE), ('Sector Grid (현재)', CURRENT)]
    reached = int(brute[4000]['active'])
    common = 'PC 2대 유선 LAN, 조건당 1회, 마지막 180초 집계.'
    partial = f'* 격자 없음은 {reached:,}명까지만 접속됨.'

    def label(n):
        return f'{n:,}명*' if n == 4000 else f'{n:,}명'

    def groups(key, ns, unit=''):
        return [{'label': label(n), 'bars': [(base[n][key], CURRENT, num(base[n][key]) + unit),
                                             (brute[n][key], COMPARE, num(brute[n][key]) + unit)]} for n in ns]

    def two_scale(key):
        return [{'title': '500·1,000명 (ms)', 'groups': groups(key, [500, 1000])},
                {'title': '2,000·4,000명 (ms, 눈금 다름)', 'groups': groups(key, [2000, 4000])}]

    ratio = brute[2000]['query_us'] / base[2000]['query_us']
    panel(os.path.join(out, 'sector_1_조회시간.png'), 'Sector 조회 1회 시간 (µs)', f'{ratio:.0f}배', '봇 2,000명 기준',
          legend, [{'groups': groups('query_us', counts)}], [common, partial])

    ratio = brute[2000]['cpu'] / base[2000]['cpu']
    panel(os.path.join(out, 'sector_2_서버CPU.png'), '서버 CPU 사용률 (%)', f'{ratio:.1f}배', '봇 2,000명 기준',
          legend, [{'groups': groups('cpu', counts)}], [common, partial])

    panel(os.path.join(out, 'sector_3_RTT_p50.png'), '이동 패킷 RTT p50', f"{base[2000]['p50']:.2f} ms",
          f"봇 2,000명, 격자 없음은 {brute[2000]['p50']:,.0f} ms", legend, two_scale('p50'),
          [common + ' 초별 p50의 샘플 가중 평균.', partial + ' 2,000명 이상에서 격자 없음은 RTT가 계속 늘어 안정값이 아님.'])

    panel(os.path.join(out, 'sector_4_RTT_p99.png'), '이동 패킷 RTT p99', f"{base[2000]['p99']:.2f} ms",
          f"봇 2,000명, 격자 없음은 {brute[2000]['p99']:,.0f} ms", legend, two_scale('p99'),
          [common + ' 초별 p99의 중앙값.', partial + ' 2,000명 이상에서 격자 없음은 RTT가 계속 늘어 안정값이 아님.'])


def smoke_charts(out):
    results = os.path.join(HERE, 'results_smoke')
    v = {t: smoke_values(results, t) for t in
         ['base', 'send_per_packet', 'lua_npc_lock', 'lua_global_lock', 'db_thread', 'db_sync']}
    note = '예비 측정: 한 PC, 봇 200명, 약 18초. 부하가 낮아 최종 비교값이 아님.'

    def single(path, header, big, unit, bars, extra):
        panel(os.path.join(out, path), header, big, unit, [],
              [{'groups': [{'label': name, 'bars': [(value, color, text)]} for name, value, color, text in bars]}],
              [note, extra])

    diff = (v['send_per_packet']['wsasend'] / v['base']['wsasend'] - 1) * 100
    single('예비_wsasend_호출수.png', '초당 WSASend 호출 수', f'{diff:.0f}%', '합치기를 끄면 늘어나는 호출',
           [('전송 중 합치기 (현재)', v['base']['wsasend'], CURRENT, num(v['base']['wsasend'])),
            ('패킷마다 전송', v['send_per_packet']['wsasend'], COMPARE, num(v['send_per_packet']['wsasend']))],
           f"호출당 패킷 수 {v['base']['pkt_per_call']:.2f}개 / {v['send_per_packet']['pkt_per_call']:.2f}개.")

    ratio = v['lua_global_lock']['lua_us'] / v['base']['lua_us']
    single('예비_lua_호출시간.png', 'Lua 호출 1회 시간 (µs, 락 대기 포함)', f'{ratio:.1f}배', '전역 락 / 스레드별 state',
           [('스레드별 state (현재)', v['base']['lua_us'], CURRENT, num(v['base']['lua_us'])),
            ('NPC별 state + 락', v['lua_npc_lock']['lua_us'], COMPARE, num(v['lua_npc_lock']['lua_us'])),
            ('타입별 state + 전역 락', v['lua_global_lock']['lua_us'], COMPARE, num(v['lua_global_lock']['lua_us']))],
           f"전역 락의 락 대기 {v['lua_global_lock']['lua_wait_us']:.1f} µs. NPC별 state는 생성 비용이 포함됨.")

    single('예비_lua_메모리.png', '서버 메모리 (MB)', f"{v['lua_npc_lock']['mem'] - v['base']['mem']:.0f} MB",
           'NPC별 state가 더 쓰는 메모리',
           [('스레드별 state (현재)', v['base']['mem'], CURRENT, num(v['base']['mem'])),
            ('NPC별 state + 락', v['lua_npc_lock']['mem'], COMPARE, num(v['lua_npc_lock']['mem'])),
            ('타입별 state + 전역 락', v['lua_global_lock']['mem'], COMPARE, num(v['lua_global_lock']['mem']))],
           f"Lua state 수 {v['base']['states']:.0f}개 / {v['lua_npc_lock']['states']:.0f}개 / {v['lua_global_lock']['states']:.0f}개.")

    def db_group(label, key):
        return {'label': label, 'bars': [(v['db_thread'][key], CURRENT, num(v['db_thread'][key])),
                                         (v['db_sync'][key], COMPARE, num(v['db_sync'][key]))]}

    panel(os.path.join(out, '예비_db_thread.png'), 'DB Thread 유무 (ms)', f"{v['db_thread']['p99']:.2f} ms",
          f"게임 RTT p99, DB 스레드 없음은 {v['db_sync']['p99']:.2f} ms",
          [('워커에서 동기 실행', COMPARE), ('DB Thread (현재)', CURRENT)],
          [{'title': '게임 RTT p99 (ms)', 'groups': [db_group('로그인 초당 20회', 'p99')]},
           {'title': '로그인 p50 (ms, 눈금 다름)', 'groups': [db_group('로그인 초당 20회', 'login_p50')]}],
          [note, '쿼리가 2~4 ms로 짧아 고부하 차이는 아직 드러나지 않음. DB 스레드는 큐가 비면 10 ms씩 잠듦.'])


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_OUT
    os.makedirs(out, exist_ok=True)
    sector_charts(out)
    smoke_charts(out)


if __name__ == '__main__':
    main()
