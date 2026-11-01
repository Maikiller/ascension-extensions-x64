#!/bin/bash
# CI do porte CoA x64 (chamado pelo Jenkins via ssh no host de build).
# Uso: ci-build.sh <EXT_SHA|branch> <SRC_SHA|branch> [FORCAR_CLIENTE=0|1]
# - Compila o cliente (so se o src mudou desde o ultimo build), o Extensions64 e o Extensions32.
# - Gera .addr/procs64, roda checksig/audit/chkexpect/chkretaddr e arquiva:
#     out/cliente-<src>/  (exe, pdb, addr, procs64)     out/Extensions64-<ext>.{dll,pdb}
# - Falha (exit 1) se compilar com erro ou se alguma conferencia acusar problema.
#   Problemas de chkexpect/chkretaddr = precisa de rebase: listas em out/ci/rebase-<src>-chk*.txt
set -uo pipefail
. "$(dirname "$(readlink -f "$0")")/env.sh"; win_ok
EXT_IN=${1:-x64}; SRC_IN=${2:-coa-cliente-20261031}; FORCAR=${3:-0}
R=$W/out/ci; mkdir -p $R
exec 9>$W/.ci.lock
flock -n 9 || { echo "ERRO: outro build do x64 em andamento (lock $W/.ci.lock)"; exit 1; }
falha() { echo "FALHA: $*"; echo "FALHA: $*" >> $R/ultimo.txt; exit 1; }
: > $R/ultimo.txt

git -C $W/ext fetch -q forgejo || falha "fetch ext"
git -C $W/src fetch -q origin || falha "fetch src"
resolve() { git -C "$1" rev-parse --verify -q "$3/$2^{commit}" || git -C "$1" rev-parse --verify -q "$2^{commit}"; }
EXT=$(resolve $W/ext "$EXT_IN" forgejo) || falha "ext $EXT_IN nao existe"
SRC=$(resolve $W/src "$SRC_IN" origin) || falha "src $SRC_IN nao existe"
E=${EXT:0:7}; S=${SRC:0:7}
echo "ext $E  src $S" | tee -a $R/ultimo.txt

# As conferencias leem a arvore de trabalho (tools/, ext/src, src/ p/ tags): ela tem que ser o commit pedido.
for p in "ext $EXT x64-port/tools src WotLKExtensions CMakeLists.txt" "src $SRC ."; do
  set -- $p; d=$1; c=$2; shift 2
  [ "$(git -C $W/$d rev-parse HEAD)" = "$c" ] || falha "$d: HEAD $(git -C $W/$d rev-parse --short HEAD) != ${c:0:7} pedido (push pendente ou outro branch)"
  git -C $W/$d diff --quiet HEAD -- "$@" || falha "$d: arvore de trabalho com alteracoes nao commitadas em $* (commitar antes)"
done

# A maquina Windows de build (VM) nao sobe sozinha depois de reboot do hipervisor.
if ! timeout 5 bash -c "</dev/tcp/$WIN_HOST/22" 2>/dev/null; then
  echo "maquina de build fora: qm start ${HV_VMID:-}"
  [ -n "${HV_HOST:-}" ] && ssh -i $SSH_KEY -o BatchMode=yes root@$HV_HOST qm start ${HV_VMID:-} || true
  for i in $(seq 60); do timeout 5 bash -c "</dev/tcp/$WIN_HOST/22" 2>/dev/null && break; sleep 5; done
  timeout 5 bash -c "</dev/tcp/$WIN_HOST/22" 2>/dev/null || falha "maquina de build nao respondeu"
  sleep 30
fi

cd $W
# 1. Cliente (lento): so quando o src mudou.
if [ "$FORCAR" = 1 ] || [ "$(cat $R/ultimo-src 2>/dev/null)" != "$SRC" ] || [ ! -f out/cliente-$S/WowClient64.pdb ]; then
  echo "== cliente $S"
  SRC_REF=$SRC bash build-client.sh > $R/cliente-$S.log 2>&1
  grep -q 'WowClient.vcxproj ->' $R/cliente-$S.log || { tail -20 $R/cliente-$S.log; falha "cliente nao compilou (log out/ci/cliente-$S.log)"; }
  grep -E ' error |fatal error' $R/cliente-$S.log | head -5
  rm -f out/procs64.json; python3 tools/ent.py > /dev/null 2>&1; [ -s out/procs64.json ] || falha "procs64.json nao gerado"
  # Trava: fonte C/C++ mudou e o exe saiu identico ao do ultimo cliente = o build incremental nao viu a mudanca
  # (01/11: 05276be com o exe do fe04e56, data do commit mais velha que os .obj). Ver push.sh (.src-sha / tar -m).
  P=$(cat $R/ultimo-src 2>/dev/null || true); P7=${P:0:7}
  if [ -n "$P" ] && [ "$P7" != "$S" ] && [ -f out/cliente-$P7/WowClient64.exe ] && cmp -s out/WowClient64.exe out/cliente-$P7/WowClient64.exe \
     && git -C $W/src diff --name-only "$P" "$SRC" | grep -qE '\.(c|cc|cpp|h|hpp|inl)$'; then
    falha "cliente $S: exe identico ao de $P7 com fonte C/C++ diferente (build incremental nao pegou a mudanca)"
  fi
  mkdir -p out/cliente-$S && cp out/WowClient64.exe out/WowClient64.pdb out/WowClient64.addr out/procs64.json out/cliente-$S/
  echo $SRC > $R/ultimo-src
else
  echo "== cliente $S ja compilado; restaurando out/ de out/cliente-$S"
  cp out/cliente-$S/WowClient64.exe out/cliente-$S/WowClient64.pdb out/cliente-$S/procs64.json out/
fi
# .addr sempre regerado: depende do extra-addr.txt (ext).
(cd out && python3 ../tools/mapaddr.py ../tags.json WowClient64.pdb WowClient64.exe WowClient64.addr mapa-rel.json) > $R/mapaddr.log 2>&1 || falha "mapaddr"
grep -E "extra-addr: .* (vftable .* nao achada|string .* ocorrencias)" $R/mapaddr.log && falha "mapaddr: linha vft:/str: do extra-addr nao resolvida"
cp out/WowClient64.addr out/cliente-$S/WowClient64.addr

# 2. Extensions64 / Extensions32
echo "== ext $E"
EXT_REF=$EXT bash build-ext.sh > $R/ext64-$E.log 2>&1
grep -q 'ERROS: 0' $R/ext64-$E.log && grep -q 'Extensions64.dll' $R/ext64-$E.log || { cat out/ext64-errors.txt | head -20; falha "Extensions64 nao compilou"; }
EXT_REF=$EXT bash build-ext32.sh > $R/ext32-$E.log 2>&1
grep -q 'ERROS: 0' $R/ext32-$E.log && grep -q 'Extensions.dll' $R/ext32-$E.log || { cat out/ext32-errors.txt | head -20; falha "Extensions32 nao compilou"; }
sshpass -e scp -q $WIN_USER@$WIN_HOST:D:/build/ext64/build/RelWithDebInfo/Extensions64.dll $WIN_USER@$WIN_HOST:D:/build/ext64/build/RelWithDebInfo/Extensions64.pdb out/ || falha "scp DLL"
cp out/Extensions64.dll out/Extensions64-$E.dll; cp out/Extensions64.pdb out/Extensions64-$E.pdb

# 3. Conferencias
echo "== conferencias"
python3 tools/protos.py > /dev/null 2>&1
CS=$(python3 tools/checksig.py 2>&1 | grep -E '^ok ')
AU=$(python3 tools/audit_hooks.py 2>&1 | grep -ciE '^PROB')
python3 tools/chkexpect.py > $R/rebase-$S-chkexpect.txt 2>&1; CE=$(tail -1 $R/rebase-$S-chkexpect.txt)
python3 tools/chkretaddr.py > $R/rebase-$S-chkretaddr.txt 2>&1; CR=$(tail -1 $R/rebase-$S-chkretaddr.txt)
{ echo "checksig: $CS"; echo "audit PROB: $AU"; echo "chkexpect: $CE"; echo "chkretaddr: $CR"
  echo "md5: exe $(md5sum < out/WowClient64.exe | cut -c1-8) addr $(md5sum < out/WowClient64.addr | cut -c1-8) dll $(md5sum < out/Extensions64.dll | cut -c1-8)"; } | tee -a $R/ultimo.txt
OK=1
echo "$CS" | grep -q 'diverge 0' || OK=0
echo "$CS" | grep -qE 'retorno_bool [1-9]' && OK=0   # bool lido como int (HasAura 01/11)
[ "$AU" = 0 ] || OK=0
echo "$CE" | grep -qE 'problemas 0( |$)' || OK=0
echo "$CR" | grep -q 'problemas 0$' || OK=0
[ $OK = 1 ] || falha "conferencias com problema: rebase/fix necessario (listas em out/ci/rebase-$S-*.txt)"
echo "$E $S" > $R/ultimo-ok
echo "OK: ext $E src $S pronto para publicar"
