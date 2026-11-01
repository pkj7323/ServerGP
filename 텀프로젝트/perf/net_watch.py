"""NIC 대역폭과 TCP 재전송을 초 단위로 기록한다. 서버 PC와 봇 PC에서 각각 실행한다.

표준 라이브러리만 쓰므로 Python만 있으면 된다.
사용: python net_watch.py --out results\\net_server.csv --seconds 600 [--nic 이더넷]

NIC 카운터는 드라이버가 갱신할 때만 바뀐다. counter_updates가 작으면
peak_*_mbps는 10 ms 값이 아니라 그 갱신 간격의 평균이다.
"""
import argparse
import ctypes
import ctypes.wintypes as wt
import csv
import time

iphlpapi = ctypes.WinDLL('iphlpapi')
AF_INET = 2
IF_TYPE_ETHERNET = 6
IF_OPER_UP = 1
WIRE_BITS_PER_BYTE = 8 / 1_000_000


class MIB_IF_ROW2(ctypes.Structure):
    _fields_ = [
        ('InterfaceLuid', ctypes.c_uint64),
        ('InterfaceIndex', wt.DWORD),
        ('InterfaceGuid', ctypes.c_byte * 16),
        ('Alias', ctypes.c_wchar * 257),
        ('Description', ctypes.c_wchar * 257),
        ('PhysicalAddressLength', wt.DWORD),
        ('PhysicalAddress', ctypes.c_byte * 32),
        ('PermanentPhysicalAddress', ctypes.c_byte * 32),
        ('Mtu', wt.DWORD),
        ('Type', wt.DWORD),
        ('TunnelType', wt.DWORD),
        ('MediaType', wt.DWORD),
        ('PhysicalMediumType', wt.DWORD),
        ('AccessType', wt.DWORD),
        ('DirectionType', wt.DWORD),
        ('InterfaceAndOperStatusFlags', ctypes.c_byte),
        ('OperStatus', wt.DWORD),
        ('AdminStatus', wt.DWORD),
        ('MediaConnectState', wt.DWORD),
        ('NetworkGuid', ctypes.c_byte * 16),
        ('ConnectionType', wt.DWORD),
        ('TransmitLinkSpeed', ctypes.c_uint64),
        ('ReceiveLinkSpeed', ctypes.c_uint64),
        ('InOctets', ctypes.c_uint64),
        ('InUcastPkts', ctypes.c_uint64),
        ('InNUcastPkts', ctypes.c_uint64),
        ('InDiscards', ctypes.c_uint64),
        ('InErrors', ctypes.c_uint64),
        ('InUnknownProtos', ctypes.c_uint64),
        ('InUcastOctets', ctypes.c_uint64),
        ('InMulticastOctets', ctypes.c_uint64),
        ('InBroadcastOctets', ctypes.c_uint64),
        ('OutOctets', ctypes.c_uint64),
        ('OutUcastPkts', ctypes.c_uint64),
        ('OutNUcastPkts', ctypes.c_uint64),
        ('OutDiscards', ctypes.c_uint64),
        ('OutErrors', ctypes.c_uint64),
        ('OutUcastOctets', ctypes.c_uint64),
        ('OutMulticastOctets', ctypes.c_uint64),
        ('OutBroadcastOctets', ctypes.c_uint64),
        ('OutQLen', ctypes.c_uint64),
    ]


class MIB_TCPSTATS(ctypes.Structure):
    _fields_ = [(name, wt.DWORD) for name in (
        'RtoAlgorithm', 'RtoMin', 'RtoMax', 'MaxConn', 'ActiveOpens', 'PassiveOpens', 'AttemptFails',
        'EstabResets', 'CurrEstab', 'InSegs', 'OutSegs', 'RetransSegs', 'InErrs', 'OutRsts', 'NumConns')]


def read_row(row):
    if iphlpapi.GetIfEntry2(ctypes.byref(row)) != 0:
        raise OSError('GetIfEntry2 실패')


def read_tcp():
    stats = MIB_TCPSTATS()
    if iphlpapi.GetTcpStatisticsEx(ctypes.byref(stats), AF_INET) != 0:
        raise OSError('GetTcpStatisticsEx 실패')
    return stats


def find_nic(alias):
    """이름을 주면 그 NIC를, 아니면 켜져 있는 유선 NIC 중 수신량이 가장 많은 것을 고른다."""
    table = ctypes.c_void_p()
    if iphlpapi.GetIfTable2(ctypes.byref(table)) != 0:
        raise OSError('GetIfTable2 실패')
    try:
        count = ctypes.cast(table, ctypes.POINTER(wt.ULONG))[0]
        # MIB_IF_TABLE2는 개수 뒤에 8바이트 정렬된 행 배열이 온다.
        first = table.value + ctypes.sizeof(ctypes.c_uint64)
        rows = (MIB_IF_ROW2 * count).from_address(first)
        # 플래그 1번 비트는 실제 장치, 2번 비트는 드라이버 위의 필터 계층이다.
        matches = [r for r in rows if r.Alias == alias] if alias else \
            [r for r in rows if r.Type == IF_TYPE_ETHERNET and r.OperStatus == IF_OPER_UP
             and r.InterfaceAndOperStatusFlags & 1 and not r.InterfaceAndOperStatusFlags & 2]
        if not matches:
            raise SystemExit(f'NIC를 찾지 못함: {alias or "켜진 유선 NIC 없음"}')
        best = max(matches, key=lambda r: r.InOctets)
        picked = MIB_IF_ROW2()
        picked.InterfaceIndex = best.InterfaceIndex
        return picked
    finally:
        iphlpapi.FreeMibTable(table)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', required=True)
    ap.add_argument('--seconds', type=int, default=600)
    ap.add_argument('--nic', default='')
    ap.add_argument('--interval-ms', type=float, default=10)
    args = ap.parse_args()

    row = find_nic(args.nic)
    read_row(row)
    tcp = read_tcp()
    print(f'NIC={row.Alias} ({row.Description}) link={row.ReceiveLinkSpeed / 1e6:.0f} Mbps '
          f'RtoMin={tcp.RtoMin} ms RtoMax={tcp.RtoMax} ms')

    fields = ['sec', 'time', 'rx_mbps', 'tx_mbps', 'peak_rx_mbps', 'peak_tx_mbps', 'rx_pkts', 'tx_pkts',
              'in_discards', 'in_errors', 'out_discards', 'out_errors',
              'tcp_in_segs', 'tcp_out_segs', 'tcp_retrans', 'counter_updates']
    with open(args.out, 'w', newline='') as f:
        out = csv.writer(f)
        out.writerow(fields)
        start = time.perf_counter()

        def totals():
            read_row(row)
            return {k: getattr(row, k) for k in (
                'InOctets', 'OutOctets', 'InUcastPkts', 'OutUcastPkts',
                'InDiscards', 'InErrors', 'OutDiscards', 'OutErrors')}

        sec_row, sec_tcp, sec_time = totals(), read_tcp(), start
        prev_in, prev_out, prev_time = sec_row['InOctets'], sec_row['OutOctets'], start
        peak_rx = peak_tx = 0.0
        updates = 0

        for sec in range(1, args.seconds + 1):
            deadline = start + sec
            while (now := time.perf_counter()) < deadline:
                read_row(row)
                in_octets, out_octets = row.InOctets, row.OutOctets
                # 값이 바뀐 시점 사이의 평균을 재서, 갱신 간격을 그대로 드러낸다.
                if in_octets != prev_in or out_octets != prev_out:
                    elapsed = now - prev_time
                    if elapsed >= args.interval_ms / 1000:
                        peak_rx = max(peak_rx, (in_octets - prev_in) * WIRE_BITS_PER_BYTE / elapsed)
                        peak_tx = max(peak_tx, (out_octets - prev_out) * WIRE_BITS_PER_BYTE / elapsed)
                        prev_in, prev_out, prev_time = in_octets, out_octets, now
                        updates += 1
                time.sleep(0.001)

            cur, cur_tcp = totals(), read_tcp()
            elapsed = now - sec_time
            out.writerow([
                sec, time.strftime('%H:%M:%S'),
                round((cur['InOctets'] - sec_row['InOctets']) * WIRE_BITS_PER_BYTE / elapsed, 2),
                round((cur['OutOctets'] - sec_row['OutOctets']) * WIRE_BITS_PER_BYTE / elapsed, 2),
                round(peak_rx, 2), round(peak_tx, 2),
                cur['InUcastPkts'] - sec_row['InUcastPkts'], cur['OutUcastPkts'] - sec_row['OutUcastPkts'],
                cur['InDiscards'] - sec_row['InDiscards'], cur['InErrors'] - sec_row['InErrors'],
                cur['OutDiscards'] - sec_row['OutDiscards'], cur['OutErrors'] - sec_row['OutErrors'],
                cur_tcp.InSegs - sec_tcp.InSegs, cur_tcp.OutSegs - sec_tcp.OutSegs,
                cur_tcp.RetransSegs - sec_tcp.RetransSegs, updates])
            f.flush()
            sec_row, sec_tcp, sec_time = cur, cur_tcp, now
            peak_rx = peak_tx = 0.0
            updates = 0


if __name__ == '__main__':
    main()
