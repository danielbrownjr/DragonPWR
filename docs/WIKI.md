# GitHub Wiki publishing

The GitHub Wiki is a **generated mirror** of canonical Markdown in this
repository. That avoids maintaining two divergent copies of hardware facts,
bench evidence and protocol details.

## Sources mirrored into the wiki

The build script maps:

- `README.md` → Project Overview
- `docs/INDEX.md` → Documentation Index
- `docs/INSTALLATION_AND_RECOVERY.md` → Installation and Recovery
- `docs/ARCHITECTURE.md` → Architecture
- `docs/API.md` → API
- `docs/HARDWARE_ANALYSIS.md` → Hardware Analysis
- `docs/METERING_REVERSE_ENGINEERING.md` → Metering Reverse Engineering
- `docs/BENCH_NOTES.md` → Bench Notes
- `docs/ROADMAP.md` → Roadmap
- `analysis/README.md` → Stock Firmware Analysis Tools

`wiki/Home.md` and `wiki/_Sidebar.md` provide the human navigation layer.
`scripts/build_wiki.py` rewrites local links and copies the hardware images into
the generated wiki tree.

## One-time GitHub bootstrap

GitHub reports Wiki support enabled for this repository, but GitHub does not
create the backing `.wiki.git` repository until the first wiki page exists.

One manual step is therefore required:

1. Open the repository's **Wiki** tab.
2. Create any initial page (a placeholder `Home` is fine).
3. Run the **Publish wiki** workflow from Actions, or merge/push a later docs
   change to `main`.

The workflow first checks whether the backing wiki repository exists. If it has
not been initialized, it exits successfully with a warning instead of failing
the repository CI.

## Publishing behavior

`.github/workflows/publish-wiki.yml` runs when canonical docs, wiki navigation
or the builder change on `main`, and can also be run manually.

The workflow:

1. checks out the exact commit,
2. runs `python scripts/build_wiki.py`,
3. verifies the wiki backend exists,
4. mirrors `.wiki-out/` to the repository wiki.

The publishing action is pinned to a commit SHA and receives only
`contents: write`, the permission required to push the same repository's wiki.

## Editing rule

**Repository Markdown wins.** Do not treat edits made directly in the GitHub
Wiki UI as authoritative; the next generated publish can overwrite them.
