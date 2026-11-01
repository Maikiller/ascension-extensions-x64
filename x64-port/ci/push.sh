#!/bin/bash
# Envia o fonte (branch coa) para a maquina Windows de build e compila. Uso: push.sh [Release|RelWithDebInfo]
set -e
CFG=${1:-RelWithDebInfo}
. "$(dirname "$(readlink -f "$0")")/env.sh"; win_ok; WIN_SRC_DIR=${WIN_SRC_DIR:?WIN_SRC_DIR em local.env}
cd $W/src
NEW=$(git rev-parse ${SRC_REF:-HEAD})
git archive --format=tar.gz -o /tmp/claude-0/wd-src.tgz $NEW
# O git archive grava a data do COMMIT nos arquivos: commit mais velho que os .obj do build anterior passava como
# "nada mudou" no MSBuild (01/11: um fix de cabecalho saiu com o exe identico ao do build anterior). Os arquivos que mudaram
# desde o fonte que esta na maquina de build (.src-sha) sao extraidos de novo com a data de agora (tar -m). Sem .src-sha
# conhecido: tudo com a data de agora (build completo).
LAST=$(sshpass -e ssh $WIN_USER@$WIN_HOST "cmd /c type ${WIN_SRC_DIR//\//\\}\\.src-sha 2>nul" 2>/dev/null | tr -dc '0-9a-f')
rm -f /tmp/claude-0/wd-touch.tgz; FULL=0; TOUCH=0
if [ -n "$LAST" ] && git cat-file -e "$LAST^{commit}" 2>/dev/null; then
  mapfile -t CH < <(git diff --name-only --diff-filter=d "$LAST" "$NEW")
  if [ ${#CH[@]} -gt 0 ]; then git archive --format=tar.gz -o /tmp/claude-0/wd-touch.tgz $NEW -- "${CH[@]}"; TOUCH=1; fi
  echo "push: ${#CH[@]} arquivo(s) mudaram desde ${LAST:0:7}"
else
  FULL=1; echo "push: .src-sha desconhecido (${LAST:-vazio}): extrai tudo com data de agora"
fi
cat > /tmp/claude-0/wd-build.ps1 <<PS
\$ErrorActionPreference='Continue'
\$d='${WIN_SRC_DIR//\//\\}'
if ($FULL) { tar -xmzf C:\Windows\Temp\wd-src.tgz -C \$d } else { tar -xzf C:\Windows\Temp\wd-src.tgz -C \$d }
if ($TOUCH) { tar -xmzf C:\Windows\Temp\wd-touch.tgz -C \$d; Remove-Item C:\Windows\Temp\wd-touch.tgz }
Set-Content -NoNewline -Path (Join-Path \$d '.src-sha') -Value '$NEW'
\$cm='H:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
Set-Location \$d
& \$cm -S . -B build/x64 -G "Visual Studio 17 2022" -A x64 -DWOWDECOMP_DISABLE_SIGNATURE_CHECKS=ON 2>&1 | Select -Last 1
& \$cm --build build/x64 --config $CFG --target WowClient --parallel 2>&1 | Out-File -Encoding utf8 D:\build\wd-build.log
Select-String -Path D:\build\wd-build.log -Pattern ' error |fatal error|WowClient.vcxproj ->' | Select -Last 15 | % { \$_.Line }
PS
F=(/tmp/claude-0/wd-src.tgz /tmp/claude-0/wd-build.ps1); [ -f /tmp/claude-0/wd-touch.tgz ] && F+=(/tmp/claude-0/wd-touch.tgz)
sshpass -e scp -q "${F[@]}" $WIN_USER@$WIN_HOST:C:/Windows/Temp/
sshpass -e ssh $WIN_USER@$WIN_HOST 'powershell -NoProfile -ExecutionPolicy Bypass -File C:\Windows\Temp\wd-build.ps1'
