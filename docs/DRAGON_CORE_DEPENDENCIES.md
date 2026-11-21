# Controlled dragon-core fork dependency policy

DragonPWR owns its own integration and release cadence. The preferred source
for shared Dragon-family components is
[justinh-rahb/dragon-core](https://github.com/justinh-rahb/dragon-core).
When upstream review is pending, the approved fallback is
[danielbrownjr/dragon-core](https://github.com/danielbrownjr/dragon-core).
Neither repository's merge timetable alone is a DragonPWR release gate.

## Scope and source contract

The allowlist currently covers `dc_evlog`, `dc_wifi`, `dc_ui` and
`dc_portal`. Each component must resolve from the same approved URL and
expected `components/<name>` path in `main/idf_component.yml` and
`dependencies.lock`. All lockfile revisions are immutable 40-digit Git SHAs,
with valid 64-digit component hashes. Fork manifests **must** use an exact
40-digit Git SHA matching the lock; floating branches and tags are prohibited.
Upstream `dc_ui` is also exact-SHA pinned. The existing upstream
`v0.30.0` manifest pins for `dc_evlog`, `dc_wifi` and `dc_portal`
are grandfathered **only** when their locked revision is
`8d5a61de8544a5b82cc3c6d9c7dc593ce06bb8bd`; any lock-only SHA
change is rejected. Do not introduce new moving upstream tags without a
separately reviewed policy change.

The validator accepts only the repository's canonical block-style dependency
YAML subset, not arbitrary YAML constructs. It rejects inline/flow mappings,
anchors, unknown dependency names and keys, duplicate keys, and unexpected
nested source records. This is deliberate: a superficially approved nested
`git` field must never hide the actual Git source used by ESP-IDF. Adding
another component, source type, or YAML construct requires an explicit policy
and test update. The sole existing non-`dragon-core` download is the pinned
Espressif mDNS registry component; the lock's IDF built-in source is checked
separately.

For each fork component, commit a matching record in
`config/dragon_core_fork_pins.json` containing its exact revision,
the exact upstream base revision and a tracking issue or PR in an approved
dragon-core repository. Records for components pinned upstream are forbidden.
The check rejects mismatched sources, paths, metadata or revisions before build.\nThe component hash is format-validated here and independently checked against\nthe downloaded component contents by ESP-IDF during the actual build.\nNeither this static check nor a hash supplied in the same lockfile proves that\na specific Git commit was reviewed or approved.
This provenance is *traceability*, not evidence of review or acceptance.

## Development and promotion

1. Make shared changes on a scoped fork branch and open an upstream PR or
   issue for tracking. Record the upstream base and review the fork-only delta.
2. Pin the **exact fork commit** in the manifest, regenerate
   `dependencies.lock` with ESP-IDF 5.3.5, then record the exact pin and
   tracking link in the provenance registry. Commit manifest, lock and record
   together. Never hand-edit component hashes as a substitute for resolution.
3. CI must run the policy validator, positive/negative policy tests, locked C2
   build, and C3 V2 build with inactive-output assertions. Confirm that CI
   resolved the requested Git revision, and inspect warnings and annotations.
4. Before any merge or production release, independently review fork-only
   changes, verify actual UI/runtime behavior as appropriate, and preserve
   rollback and C2 heap/flash safeguards. Electrical qualification and V2
   mains/output enabling remain separate, stricter gates.
5. Identify the exact candidate PR head, reviewed dependency SHA, compiled
   artifacts and validation evidence. Production may use a fully verified fork
   pin without waiting for an upstream merge; release still requires explicit
   authorization and the normal release procedure. CI success is not hardware
   acceptance or release authorization.

## Returning to upstream

After upstream acceptance, compare actual component-tree content (not only
commit ancestry) between fork and upstream. Review any differences. Switch
both manifest and lock back to the accepted exact upstream SHA, delete that
component's fork provenance record, rebuild and rerun relevant acceptance
checks. If upstream rejects or modifies the change, decide explicitly whether
to maintain the fork delta, revise it, or revert it. Do not silently switch
dependencies or imply an upstream merge automatically approves firmware.

## Current baseline

The current upstream pins are intentionally unchanged by adoption of this
policy. `config/dragon_core_fork_pins.json` begins empty. A separate
integration PR may later pin the pending shared UI fix from upstream PR #78.

This policy does not authorize merging, tagging, releasing, flashing, output
actuation, or any destructive repository action.
