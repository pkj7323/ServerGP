"""봇 PC에서 이것 하나만 실행한다. 봇, 네트워크 기록, CPU 기록을 함께 돌리고 끝나면 결과를 zip으로 묶는다.

사용: python bot_run.py
조건을 바꿀 때: python bot_run.py --modes base --counts 8000 --reps 13
Python 표준 라이브러리만 쓴다. 서버 PC에는 matrix_server.ps1을 같은 조건으로 띄운다.
"""
import argparse
import glob
import hashlib
import os
import subprocess
import sys
import time
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
RESULTS = os.path.join(HERE, 'results')
# 전체 평균과 가장 바쁜 코어를 기록한다. 스레드 하나의 포화는 평균으로는 안 보인다.
# PowerShell을 매번 새로 띄우면 그 부하가 기록에 섞이므로 한 프로세스에서 반복한다.
CPU_LOOP = (
    "while ($true) { "
    "$c = Get-CimInstance Win32_PerfFormattedData_PerfOS_Processor; "
    "$t = ($c | Where-Object Name -eq '_Total').PercentProcessorTime; "
    "$m = ($c | Where-Object Name -ne '_Total' | Measure-Object PercentProcessorTime -Maximum).Maximum; "
    "\"$(Get-Date -Format HH:mm:ss),$t,$m\" | Add-Content -LiteralPath $env:BOT_CPU_CSV; "
    "Start-Sleep 5 }"
)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--server', default='172.30.1.10')
    ap.add_argument('--modes', default='send_per_packet,base')
    ap.add_argument('--counts', default='8000')
    ap.add_argument('--reps', default='12')
    ap.add_argument('--warmup', type=int, default=120)
    ap.add_argument('--stable', type=int, default=180)
    ap.add_argument('--init', type=int, default=20, help='포트가 열린 뒤 NPC 초기화를 기다리는 초')
    ap.add_argument('--adaptive', action='store_true', help='기존 자동 조절로 접속 수를 정한다. --counts는 상한')
    ap.add_argument('--stop-gain', type=int, default=0, help='60초 동안 늘어난 접속이 이 값 미만이면 조건 종료')
    args = ap.parse_args()

    os.makedirs(RESULTS, exist_ok=True)
    tag = 'r' + args.reps.replace(',', '_')
    net_csv = os.path.join(RESULTS, f'net_bot_{tag}.csv')
    cpu_csv = os.path.join(RESULTS, f'bot_cpu_{tag}.csv')
    started = time.time()

    with open(os.path.join(HERE, 'STRESS_TEST.exe'), 'rb') as f:
        print('봇 exe SHA256:', hashlib.sha256(f.read()).hexdigest()[:12].upper())
    print(f'조건: {args.modes} / {args.counts}명 / {tag}. 서버를 기다립니다...', flush=True)

    # 측정보다 충분히 길게 잡고, 봇이 끝나면 직접 끈다.
    net = subprocess.Popen([sys.executable, os.path.join(HERE, 'net_watch.py'),
                            '--out', net_csv, '--seconds', '86400'])
    with open(cpu_csv, 'w') as f:
        f.write('time,total_pct,busiest_core_pct\n')
    cpu = subprocess.Popen(['powershell', '-NoProfile', '-Command', CPU_LOOP],
                           env={**os.environ, 'BOT_CPU_CSV': cpu_csv})

    try:
        bot = subprocess.run(['powershell', '-ExecutionPolicy', 'Bypass', '-File',
                              os.path.join(HERE, 'matrix_bot.ps1'),
                              '-Server', args.server, '-Modes', args.modes, '-Counts', args.counts,
                              '-Reps', args.reps, '-WarmupSeconds', str(args.warmup),
                              '-StableSeconds', str(args.stable), '-InitSeconds', str(args.init),
                              '-Adaptive', str(int(args.adaptive)), '-StopGain', str(args.stop_gain)])
    finally:
        for proc in (net, cpu):
            proc.terminate()
            proc.wait()

    # 이번 실행에서 새로 쓴 파일만 묶어, 예전 결과가 섞이지 않게 한다.
    files = [p for p in glob.glob(os.path.join(RESULTS, '*.csv')) if os.path.getmtime(p) >= started]
    out_zip = os.path.join(HERE, f'results_{tag}.zip')
    with zipfile.ZipFile(out_zip, 'w', zipfile.ZIP_DEFLATED) as z:
        for path in files:
            z.write(path, os.path.basename(path))
    print(f'끝 (봇 종료 코드 {bot.returncode}). 이 파일을 옮겨 주세요: {out_zip}')
    for path in sorted(files):
        print(' ', os.path.basename(path))


if __name__ == '__main__':
    main()
