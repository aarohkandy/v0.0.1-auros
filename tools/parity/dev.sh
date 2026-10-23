#!/bin/sh
# Development loop on homebase (the Mac has no KVM). From the repository root:
#   sh tools/parity/dev.sh [run.py arguments...]
# The image is ~/work/auros/parity/auros-desktop.img (see docs/handoff/streams/PARITY.md).
# Results are copied back to ./parity-out/.
#   PARITY_HB=1   run as a broker job (hb run) instead of directly; the broker
#                 refuses new jobs while the disk is under pressure
#   PARITY_GB=6   memory reserved for it (the VM has 4)
# Quick runs (a few minutes, one VM of 4 GB) go directly, niced, as the field manual allows.
set -e
K="-i $HOME/.ssh/homebase_ed25519"
H="ssh $K azureuser@100.108.213.121"
R=work/auros/parity
MEM="${PARITY_GB:-6}"; HOURS="${PARITY_HOURS:-0.7}"
$H "mkdir -p ~/$R/src ~/logs"
rsync -az --delete -e "ssh $K" tools/parity/ azureuser@100.108.213.121:$R/src/
[ -n "$PARITY_SCRIPT" ] && scp -q $K "$PARITY_SCRIPT" azureuser@100.108.213.121:$R/adhoc.sh && set -- "$@" --script /w/adhoc.sh
$H "docker image inspect auros-parity:dev >/dev/null 2>&1 || docker build -q -t auros-parity:dev ~/$R/src >/dev/null"
cat > /tmp/parity-job.sh <<EOS
cd \$HOME/$R
D="docker run --rm --privileged --device /dev/kvm --memory ${MEM}g -v \$PWD:/w -w /w auros-parity:dev"
\$D rm -rf /w/out /w/scratch /w/run.log
nice -n 10 \$D python3 src/run.py --image auros-desktop.img --out out --work /w/scratch $* > run.log 2>&1
\$D sh -c 'rm -rf /w/scratch; chown -R 1000:1000 /w/out /w/run.log'
EOS
scp -q $K /tmp/parity-job.sh azureuser@100.108.213.121:$R/job.sh
if [ -n "$PARITY_HB" ]; then
    ID=$($H "export PATH=\$HOME/.local/bin:\$PATH; hb run $MEM $HOURS --name parity-$(date +%s) sh \$HOME/$R/job.sh" | tee /dev/stderr | grep -o 'j-[a-z0-9]*' | head -1)
    [ -n "$ID" ] || { echo "the broker did not take it"; exit 1; }
    while :; do
        st=$($H "export PATH=\$HOME/.local/bin:\$PATH; hb show $ID" | grep -m1 '^ *state' || true)
        case "$st" in *running*|*queued*|*scheduled*|*starting*) sleep 20 ;; *) break ;; esac
    done
else
    $H "free -g | sed -n 2p; sh \$HOME/$R/job.sh"
fi
$H "tail -30 ~/$R/run.log"
rm -rf parity-out; mkdir -p parity-out
rsync -az -e "ssh $K" azureuser@100.108.213.121:$R/out/ parity-out/ || true
