# Stream OPS — addresses, monitoring, incidents, launch waves, distribution

**Goal.** No installed PC depends on a GitHub address; the owner learns
about a broken download, update or cloud within 15 minutes and has a
rehearsed fix; the launch opens in waves that stop by themselves.
Brief: `docs/PLAN-END-TO-END.md` §4 "OPS"; packages WP-15, 35, 60, 74, 82,
83, 94. Owns `infra/**`, `.github/workflows/{monitor,drill,fleet,edge}.yml`,
`docs/ops/**`, `tools/ops/**`.

## State (2026-10-23, session B, first spawn)

In progress: WP-15 address plumbing (tools/repo.sh `apt|download|get` and
`AUROS_DOMAIN`), then infra/edge, addrtest, monitor.
