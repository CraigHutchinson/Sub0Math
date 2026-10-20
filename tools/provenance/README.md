# Upstream provenance

Generic tooling for repositories that port code from another repository: a manifest pins the exact
upstream commit and each ported file's blob; `check` proves the manifest is true, `drift` reports what
changed upstream since, `record` re-pins after porting. Product-agnostic; standard library plus `git`.

```bash
python <Sub0Math>/tools/provenance/upstream_provenance.py check --upstream-dir ../Upstream
```

Run from the repository being checked (or pass `--repo-root`); `--manifest` defaults to
`upstream/bikelab.json`. Locate Sub0Math through CPM like the perf tooling, or check it out in CI.

## One issue for files several repositories port

`driftOwner` (boolean, default true) on the manifest and on each `ported` entry. An entry's own value
wins, otherwise the manifest's applies. `drift` reports only owned entries, and "other new or changed
files" follows the manifest-level value. `check` always covers every entry. Exactly one repository
should own each upstream file; `ownedBy` on a non-owned entry names it (documentation only).

## CI

Provenance check step (pin `<sha>`):

```yaml
- uses: actions/checkout@v4
  with: { repository: CraigHutchinson/Sub0Math, ref: <sha>, path: .sub0math, sparse-checkout: tools/provenance }
- run: python3 .sub0math/tools/provenance/upstream_provenance.py check --upstream-dir .upstream/bikelab
```

Weekly drift issue: call `.github/workflows/upstream-drift-reusable.yml@<sha>` (see its header).
