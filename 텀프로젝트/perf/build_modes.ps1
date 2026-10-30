# 비교 모드별 서버와 측정용 봇을 Release x64로 빌드한다.
# 결과: Server\x64\exp\<모드>\Server.exe, STRESS_TEST\x64\exp\bot\STRESS_TEST.exe
param([string[]]$Only = @())

$ErrorActionPreference = 'Stop'
$msbuild = 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe'
$root = Split-Path $PSScriptRoot -Parent

$modes = [ordered]@{
    base            = ''
    sector_brute    = '/DEXP_SECTOR_BRUTE=1'
    send_per_packet = '/DEXP_SEND_PER_PACKET=1'
    lua_npc_lock    = '/DEXP_LUA_MODE=1'
    lua_global_lock = '/DEXP_LUA_MODE=2'
    db_sync         = '/DEXP_DB_SYNC=1'
}

function Invoke-Build($project, $outDir, $intDir, $defines) {
    $env:CL = $defines
    & $msbuild $project /nologo /v:m /m /t:Build /p:Configuration=Release /p:Platform=x64 "/p:OutDir=$outDir\" "/p:IntDir=$intDir\"
    if ($LASTEXITCODE -ne 0) { throw "build failed: $project $defines" }
}

try {
    foreach ($name in $modes.Keys) {
        if ($Only.Count -and $Only -notcontains $name) { continue }
        "=== $name"
        $dir = Join-Path $root "Server\x64\exp\$name"
        Invoke-Build (Join-Path $root 'Server\Server\Server.vcxproj') $dir "$dir\obj" "/DEXP_METRICS=1 $($modes[$name])"
    }
    if (-not $Only.Count -or $Only -contains 'bot') {
        "=== bot"
        $dir = Join-Path $root 'STRESS_TEST\x64\exp\bot'
        Invoke-Build (Join-Path $root 'STRESS_TEST\STRESS_TEST\STRESS_TEST.vcxproj') $dir "$dir\obj" ''
    }
}
finally {
    Remove-Item Env:CL -ErrorAction SilentlyContinue
}
