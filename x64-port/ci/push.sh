#!/bin/bash
# Envia o fonte (branch coa) para a maquina Windows de build e compila. Uso: push.sh [Release|RelWithDebInfo]
set -e
CFG=${1:-RelWithDebInfo}
. "$(dirname "$(readlink -f "$0")")/env.sh"; win_ok; WIN_SRC_DIR=${WIN_SRC_DIR:?WIN_SRC_DIR em local.env}
cd $W/src
git archive --format=tar.gz -o /tmp/claude-0/wd-src.tgz ${SRC_REF:-HEAD}
cat > /tmp/claude-0/wd-build.ps1 <<PS
\$ErrorActionPreference='Continue'
\$d='${WIN_SRC_DIR//\//\\}'
tar -xzf C:\Windows\Temp\wd-src.tgz -C \$d
\$cm='H:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
Set-Location \$d
& \$cm -S . -B build/x64 -G "Visual Studio 17 2022" -A x64 -DWOWDECOMP_DISABLE_SIGNATURE_CHECKS=ON 2>&1 | Select -Last 1
& \$cm --build build/x64 --config $CFG --target WowClient --parallel 2>&1 | Out-File -Encoding utf8 D:\build\wd-build.log
Select-String -Path D:\build\wd-build.log -Pattern ' error |fatal error|WowClient.vcxproj ->' | Select -Last 15 | % { \$_.Line }
PS
sshpass -e scp -q /tmp/claude-0/wd-src.tgz /tmp/claude-0/wd-build.ps1 $WIN_USER@$WIN_HOST:C:/Windows/Temp/
sshpass -e ssh $WIN_USER@$WIN_HOST 'powershell -NoProfile -ExecutionPolicy Bypass -File C:\Windows\Temp\wd-build.ps1'
