# Ambiente local do CI/build do porte x64. Os dados da rede (hosts, usuarios, senhas, caminhos) ficam em
# local.env (fora do git; modelo em local.env.example). Uso: . "$(dirname "$(readlink -f "$0")")/env.sh"
_coa_ci=$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")
[ -f "$_coa_ci/local.env" ] && . "$_coa_ci/local.env"
W=${COA_WORK:-$(readlink -f "$_coa_ci/../../..")}   # raiz de trabalho: ext/, src/, out/, tags.json
SSH_KEY=${SSH_KEY:-$HOME/.ssh/id_ed25519}
export SSHPASS=${WIN_PASS:-}
win_ok() { [ -n "${WIN_HOST:-}" ] && [ -n "${WIN_USER:-}" ] && [ -n "${WIN_PASS:-}" ] || { echo "ERRO: WIN_HOST/WIN_USER/WIN_PASS em $_coa_ci/local.env"; exit 1; }; }
