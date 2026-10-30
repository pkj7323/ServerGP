# 한 PC에서 전 모드를 짧게 돌려 접속, CSV, 크래시 여부만 확인한다. 측정값으로 쓰지 않는다.
param([int]$Bots = 200, [int]$Seconds = 25, [string[]]$Only = @())

$root = Split-Path $PSScriptRoot -Parent
$out = "$PSScriptRoot\results_smoke"
New-Item -ItemType Directory -Force $out | Out-Null

$cases = @(
    @{ Tag = 'base';            Mode = 'base';            DbRate = 0 },
    @{ Tag = 'sector_brute';    Mode = 'sector_brute';    DbRate = 0 },
    @{ Tag = 'send_per_packet'; Mode = 'send_per_packet'; DbRate = 0 },
    @{ Tag = 'lua_npc_lock';    Mode = 'lua_npc_lock';    DbRate = 0 },
    @{ Tag = 'lua_global_lock'; Mode = 'lua_global_lock'; DbRate = 0 },
    @{ Tag = 'db_thread';       Mode = 'base';            DbRate = 20 },
    @{ Tag = 'db_sync';         Mode = 'db_sync';         DbRate = 20 }
)

foreach ($c in $cases) {
    if ($Only.Count -and $Only -notcontains $c.Tag) { continue }
    $env:EXP_SERVER_CSV = "$out\server_$($c.Tag).csv"
    $env:EXP_DB_CONN = 'DRIVER={ODBC Driver 18 for SQL Server};SERVER=DESKTOP-PARK\SQLEXPRESS01;DATABASE=2021180009_SGP_Bench;Trusted_Connection=Yes;TrustServerCertificate=Yes;'
    $server = Start-Process (Join-Path $root "Server\x64\exp\$($c.Mode)\Server.exe") -WorkingDirectory (Join-Path $root 'Server\Server') `
        -PassThru -WindowStyle Minimized -RedirectStandardOutput "$out\server_$($c.Tag).log" -RedirectStandardError "$out\server_$($c.Tag).err"

    $deadline = (Get-Date).AddSeconds(90)
    while ((Get-Date) -lt $deadline -and -not $server.HasExited) {
        if (Get-NetTCPConnection -OwningProcess $server.Id -State Listen -ErrorAction SilentlyContinue) { break }
        Start-Sleep -Milliseconds 500
    }
    Start-Sleep -Seconds 12 # NPC 초기화가 끝날 때까지 기다린다

    & "$PSScriptRoot\run_bot.ps1" -Bots $Bots -Tag $c.Tag -Seconds $Seconds -ConnectMs 10 -DbRate $c.DbRate -OutDir $out | Out-Null

    if ($server.HasExited) { "CRASH $($c.Tag) exit=$($server.ExitCode)" }
    else { Stop-Process -Id $server.Id -Force; "ok    $($c.Tag)" }
}
python "$PSScriptRoot\summarize.py" $out --warmup 8 --tail 1
