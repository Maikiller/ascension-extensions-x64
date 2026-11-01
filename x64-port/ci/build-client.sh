#!/bin/bash
# Compila o cliente na maquina Windows de build, traz exe+pdb, gera a tabela .addr. Uso: build-client.sh
set -e
. "$(dirname "$(readlink -f "$0")")/env.sh"; win_ok; WIN_SRC_DIR=${WIN_SRC_DIR:?WIN_SRC_DIR em local.env}
$W/push.sh RelWithDebInfo | tail -6
cd $W/out
sshpass -e scp -q "$WIN_USER@$WIN_HOST:$WIN_SRC_DIR/bin/Win/WowClient64.pdb" "$WIN_USER@$WIN_HOST:$WIN_SRC_DIR/bin/Win/WowClient64.exe" "$WIN_USER@$WIN_HOST:$WIN_SRC_DIR/bin/Win/XvidDecoder.dll" .
python3 $W/tools/tags.py $W/src > $W/tags.json
python3 $W/tools/mapaddr.py $W/tags.json WowClient64.pdb WowClient64.exe WowClient64.addr mapa-rel.json
