#!/bin/bash
# Compila o Extensions64.dll (repo ext, branch x64) na maquina Windows de build. Saida: out/Extensions64.dll + .pdb + log de erros
. "$(dirname "$(readlink -f "$0")")/env.sh"; win_ok
cd $W/ext && git archive --format=tar.gz -o /tmp/claude-0/ext-src.tgz ${EXT_REF:-HEAD}
# git archive nao leva arquivos nao commitados: commitar antes
cat > /tmp/claude-0/ext-build.ps1 <<'PS'
$ErrorActionPreference='Continue'
$d='D:\build\ext64'
if(Test-Path $d\src){ Remove-Item -Recurse -Force $d\src,$d\WotLKExtensions,$d\third_party -EA 0 }
New-Item -ItemType Directory -Force $d | Out-Null
tar -xzf C:\Windows\Temp\ext-src.tgz -C $d
$cm='H:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
Set-Location $d
& $cm -S . -B build -G "Visual Studio 17 2022" -A x64 "-DCOA_SHOP_URL=__COA_SHOP_URL__" 2>&1 | Out-File -Encoding utf8 D:\build\ext64-cfg.log
Get-Content D:\build\ext64-cfg.log | Select-String "Error|error" | Select -First 5 | % { $_.Line }
& $cm --build build --config RelWithDebInfo -- /m /v:m 2>&1 | Out-File -Encoding utf8 D:\build\ext64-build.log
$e = Select-String -Path D:\build\ext64-build.log -Pattern ' error ' 
"ERROS: " + $e.Count
Select-String -Path D:\build\ext64-build.log -Pattern 'Extensions.vcxproj ->' | % { $_.Line }
PS
sed -i "s#__COA_SHOP_URL__#${COA_SHOP_URL:-}#" /tmp/claude-0/ext-build.ps1
sshpass -e scp -q /tmp/claude-0/ext-src.tgz /tmp/claude-0/ext-build.ps1 $WIN_USER@$WIN_HOST:C:/Windows/Temp/
sshpass -e ssh $WIN_USER@$WIN_HOST 'powershell -NoProfile -ExecutionPolicy Bypass -File C:\Windows\Temp\ext-build.ps1'
sshpass -e scp -q $WIN_USER@$WIN_HOST:D:/build/ext64-build.log $W/out/ext64-build.log
iconv -f utf-8 -t utf-8 -c $W/out/ext64-build.log | grep ' error ' | sed -E 's/.*\\(src|WotLKExtensions)\\/\1\\/; s/ \[D:.*//' | sort -u > $W/out/ext64-errors.txt
wc -l < $W/out/ext64-errors.txt
