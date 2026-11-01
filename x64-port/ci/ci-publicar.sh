#!/bin/bash
# Publica na maquina de teste o ultimo build OK do CI (ou ext/src dados). So manual (job de publicacao).
# Uso: ci-publicar.sh [EXT7 SRC7]
# Recusa se o WowClient64 estiver aberto. Renomeia Logs\ -> Logs-ate-<HHmm>, copia exe/pdb/addr + DLL/pdb, confere md5.
set -uo pipefail
. "$(dirname "$(readlink -f "$0")")/env.sh"; O=$W/out
read -r E S < <( [ $# -ge 2 ] && echo "$1 $2" || cat $O/ci/ultimo-ok )
C=$O/cliente-$S
for f in $C/WowClient64.exe $C/WowClient64.pdb $C/WowClient64.addr $O/Extensions64-$E.dll $O/Extensions64-$E.pdb; do
  [ -f "$f" ] || { echo "FALHA: falta $f"; exit 1; }
done
K=$SSH_KEY; P=$PC_HOST; D=$PC_DIR; [ -n "${PC_HOST:-}" ] && [ -n "${PC_DIR:-}" ] || { echo "FALHA: PC_HOST/PC_DIR em local.env"; exit 1; }
pc() { ssh -p ${PC_PORT:-22} -i $K -o BatchMode=yes -o ConnectTimeout=10 $P "powershell -NoProfile -Command \"$1\""; }
# Get-Process pelo ssh nao enxerga a sessao do usuario: testa se o exe/DLL estao travados (jogo aberto).
R=$(pc "\$r='LIVRE'; foreach(\$n in 'WowClient64.exe','Extensions64.dll'){ try{ \$f=[IO.File]::Open('$D/'+\$n,'Open','ReadWrite','None'); \$f.Close() } catch { if(Test-Path ('$D/'+\$n)){ \$r='EM USO' } } }; \$r" | tr -d '\r') || { echo "FALHA: PC inacessivel"; exit 1; }
[ "$R" = LIVRE ] || { echo "FALHA: WowClient64 aberto no PC (arquivos em uso); nao publico durante teste"; exit 1; }
# Logs\ so e renomeado a pedido (RENOMEAR_LOGS=1): no meio da bateria, renomear esconde rodadas do bateria.py.
[ "${RENOMEAR_LOGS:-0}" = 1 ] && pc "\$d='$D'; if(Test-Path \$d\\Logs){Rename-Item \$d\\Logs ('Logs-ate-' + (Get-Date -f yyyyMMdd-HHmm))}"
T=$(mktemp -d); trap 'rm -rf $T' EXIT
cp $C/WowClient64.exe $C/WowClient64.pdb $C/WowClient64.addr $T/
cp $O/Extensions64-$E.dll $T/Extensions64.dll; cp $O/Extensions64-$E.pdb $T/Extensions64.pdb
cp $W/tools/bateria.ps1 $T/bateria.ps1   # ferramenta da bateria acompanha o build
scp -q -P ${PC_PORT:-22} -i $K $T/* "$P:$D/" || { echo "FALHA: scp"; exit 1; }
OK=1
while read -r n h; do
  l=$(md5sum < $T/$n | cut -c1-8)
  [ "${h,,}" = "$l" ] && echo "ok  $n $l" || { echo "DIFERENTE $n pc=$h local=$l"; OK=0; }
done < <(pc "Get-ChildItem '$D' -File | ? Name -match '^((WowClient64|Extensions64)\.|bateria\.ps1$)' | % { \$_.Name + ' ' + (Get-FileHash \$_.FullName -Algorithm MD5).Hash.Substring(0,8) }" | tr -d '\r')
[ $OK = 1 ] || exit 1
echo "$E $S $(date '+%F %H:%M')" >> $O/ci/publicados.txt
echo "PUBLICADO: ext $E src $S"
