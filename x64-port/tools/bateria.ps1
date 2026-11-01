# Bateria de testes do CoA x64 -- roda na maquina de teste, na pasta do jogo x64 (padrao: a pasta deste script).
#   powershell -ExecutionPolicy Bypass -File .\bateria.ps1            (rodadas ate digitar q)
#   powershell -ExecutionPolicy Bypass -File .\bateria.ps1 -SemProcdump
# Cada rodada abre o jogo (sob o procdump -e -ma: crash e __fastfail; travadas vem do AscBateria) e, quando ele
# fecha, junta tudo em Bateria\<AAAAMMDD-HHMMSS>\: Logs\, Errors\ e Crashes\ novos (Crash.txt/.dmp do cliente,
# bateria_*.log e Saida/Travado_*.dmp do AscBateria, dumps do procdump/WER), run.json e a sua nota.
# Depois, na maquina de analise: python3 tools/bateria.py tudo  (puxa, simboliza, agrupa e separa por sessao).
param(
    [switch]$SemProcdump,
    [string]$TipoDump = '-ma',   # -ma (completo, GB). O -mp saiu VAZIO na maquina de teste (sem threads/modulos; bateria 2b)
    [string]$Jogo = $PSScriptRoot,
    [string]$ProcDump = 'C:\Tools\procdump\procdump64.exe'
)
$ErrorActionPreference = 'Continue'
$exe = Join-Path $Jogo 'WowClient64.exe'
$base = Join-Path $Jogo 'Bateria'
New-Item -ItemType Directory -Force $base | Out-Null

function Md5($p) { if (Test-Path $p) { (Get-FileHash $p -Algorithm MD5).Hash.Substring(0, 8) } else { '-' } }

function Junta($origem, $destino, $desde, [switch]$Mover) {
    if (-not (Test-Path $origem)) { return 0 }
    $n = 0
    Get-ChildItem $origem -File -ErrorAction SilentlyContinue | Where-Object { $_.LastWriteTime -ge $desde } | ForEach-Object {
        New-Item -ItemType Directory -Force $destino | Out-Null
        if ($Mover) { Move-Item $_.FullName $destino -Force } else { Copy-Item $_.FullName $destino -Force }
        $n++
    }
    return $n
}

$rodada = 0
while ($true) {
    $rodada++
    $id = Get-Date -Format 'yyyyMMdd-HHmmss'
    $dir = Join-Path $base $id
    New-Item -ItemType Directory -Force $dir | Out-Null
    $inicio = (Get-Date).AddSeconds(-2)
    Write-Host "`n=== Rodada $rodada ($id) -- jogue o roteiro; ao fechar/cair o jogo, os arquivos sao recolhidos ===" -ForegroundColor Cyan

    Push-Location $Jogo
    # Aberto pelo procdump -x o jogo nasce "sob depurador" e o Windows liga o heap de depuracao (sem LFH, checagem
    # em cada free): logout/saida com milhoes de free levavam 30-60 s (dump 31/10 22:47: atexit s_fileHash -> SMemFree
    # -> ntdll). _NO_DEBUG_HEAP=1 e herdado pelo jogo e desliga isso; crash/__fastfail continuam sendo pegos.
    $env:_NO_DEBUG_HEAP = '1'
    if (-not $SemProcdump -and (Test-Path $ProcDump)) {
        # -e 1 -f: 1a chance so de estouro de pilha / __fastfail(GS) / AV -- o estouro mata o processo calado antes
        # da 2a chance (rodada 31/10 22:06). Sem -t: dump -ma em toda saida normal seriam GBs por rodada.
        $p = Start-Process -FilePath $ProcDump -ArgumentList @('-accepteula', '-e', '1', '-f', 'C00000FD', '-f', 'C0000409', '-f', 'C0000005', '-n', '3', $TipoDump, '-x', "`"$dir`"", "`"$exe`"") `
            -WorkingDirectory $Jogo -RedirectStandardOutput (Join-Path $dir 'procdump.txt') -NoNewWindow -PassThru -Wait
    } else {
        $p = Start-Process -FilePath $exe -WorkingDirectory $Jogo -PassThru -Wait
    }
    Pop-Location
    $fim = Get-Date
    Start-Sleep -Seconds 3   # WER termina de gravar

    $n = 0
    $n += Junta (Join-Path $Jogo 'Logs') (Join-Path $dir 'Logs') $inicio -Mover
    $n += Junta (Join-Path $Jogo 'Errors') (Join-Path $dir 'Errors') $inicio -Mover
    $n += Junta (Join-Path $Jogo 'Crashes') (Join-Path $dir 'Crashes') $inicio -Mover
    $n += Junta (Join-Path $Jogo 'DLL_Logs') (Join-Path $dir 'DLL_Logs') $inicio
    $n += Junta "$env:LOCALAPPDATA\CrashDumps" (Join-Path $dir 'WER') $inicio -Mover
    foreach ($f in 'WowClient_crash.log', 'MemoryBridge.log') {
        $fp = Join-Path $Jogo $f
        if ((Test-Path $fp) -and (Get-Item $fp).LastWriteTime -ge $inicio) { Copy-Item $fp $dir; $n++ }
    }
    $dumps = @(Get-ChildItem $dir -Recurse -Filter *.dmp -ErrorAction SilentlyContinue)

    # Sob o procdump, $p.ExitCode e o do procdump (= no de dumps); o do jogo vem na linha "Exit Code 0x..." da saida dele.
    $codigo = if ($p) { $p.ExitCode } else { $null }
    $pdTxt = Join-Path $dir 'procdump.txt'
    if (-not $SemProcdump -and (Test-Path $pdTxt)) {
        $m = Select-String -Path $pdTxt -Pattern 'Exit Code (0x[0-9A-Fa-f]+|-?\d+)' | Select-Object -Last 1
        $codigo = if ($m) { $m.Matches[0].Groups[1].Value } else { $null }
    }
    [ordered]@{
        id = $id; inicio = $inicio.ToString('s'); fim = $fim.ToString('s'); segundos = [int]($fim - $inicio).TotalSeconds
        codigo_saida = $codigo; procdump = (-not $SemProcdump); dll_md5 = (Md5 (Join-Path $Jogo 'Extensions64.dll'))
        exe_md5 = (Md5 $exe); arquivos = $n; dumps = $dumps.Count
    } | ConvertTo-Json | Set-Content (Join-Path $dir 'run.json') -Encoding UTF8

    $cor = if ($dumps.Count -gt 0) { 'Red' } else { 'Green' }
    Write-Host ("Rodada {0}: {1} s, saida {2}, {3} arquivo(s), {4} dump(s) -> {5}" -f $rodada, [int]($fim - $inicio).TotalSeconds, $codigo, $n, $dumps.Count, $dir) -ForegroundColor $cor
    $nota = Read-Host 'O que voce fez / onde caiu? (Enter = sem nota; q = encerrar a bateria)'
    if ($nota -eq 'q') { break }
    if ($nota) { Set-Content (Join-Path $dir 'nota.txt') $nota -Encoding UTF8 }
    $seguir = Read-Host 'Enter = proxima rodada, q = encerrar'
    if ($seguir -eq 'q') { break }
}
Write-Host "`nBateria encerrada. Avise o Claude: 'bateria pronta' (ele roda tools/bateria.py tudo)." -ForegroundColor Yellow
