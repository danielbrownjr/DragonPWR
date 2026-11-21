#!/usr/bin/env python3
"""Fail-closed dragon-core source/pin policy and reproducible lock validation.

Uses only the Python standard library so it runs before ESP-IDF resolves deps.
Existing upstream v0.30.0 manifests are grandfathered; fork pins are exact SHAs.
"""
from pathlib import Path
import json
import re

COMPONENTS = ("dc_evlog", "dc_wifi", "dc_ui", "dc_portal")
UPSTREAM = "https://github.com/justinh-rahb/dragon-core.git"
FORK = "https://github.com/danielbrownjr/dragon-core.git"
SHA = re.compile(r"[0-9a-f]{40}\Z")
HASH = re.compile(r"[0-9a-f]{64}\Z")
TRACKING = re.compile(r"https://github[.]com/(?:justinh-rahb|danielbrownjr)/"
                      r"dragon-core/(?:issues|pull)/[1-9][0-9]*\Z")
FILES = ("main/idf_component.yml", "dependencies.lock")
EXISTING_TAGS = {"dc_evlog": "v0.30.0", "dc_wifi": "v0.30.0",
                 "dc_portal": "v0.30.0"}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def component_section(source, name, filename):
    matches = re.findall(
        rf"(?ms)^  {re.escape(name)}:\n(.*?)(?=^  [A-Za-z_][\w./-]*:|\Z)",
        source,
    )
    require(len(matches) == 1, f"{filename}: expected one {name} dependency")
    return matches[0]


def field(block, name, component, filename):
    indent = 6 if name in ("git", "path") else 4
    matches = re.findall(
        rf"(?m)^ {{{indent}}}{re.escape(name)}: ([^\r\n]+)$", block
    )
    require(len(matches) == 1,
            f"{filename}: {component}: expected one {name}")
    return matches[0]


def validate(manifest_text, lock_text, provenance_text):
    try:
        provenance = json.loads(provenance_text)
    except json.JSONDecodeError as exc:
        raise ValueError(f"invalid fork provenance JSON: {exc}") from exc
    require(isinstance(provenance, dict) and set(provenance) == {"fork_pins"},
            "fork provenance must contain only fork_pins")
    fork_pins = provenance["fork_pins"]
    require(isinstance(fork_pins, dict), "fork_pins must be an object")
    require(not (set(fork_pins) - set(COMPONENTS)),
            "unknown component in fork_pins")
    seen_forks = set()

    for name in COMPONENTS:
        manifest = component_section(manifest_text, name, FILES[0])
        locked = component_section(lock_text, name, FILES[1])
        source = field(manifest, "git", name, FILES[0])
        locked_source = field(locked, "git", name, FILES[1])
        require(source in (UPSTREAM, FORK) and locked_source == source,
                f"{name}: unapproved or inconsistent Git source")
        expected_path = f"components/{name}"
        for block, filename in ((manifest, FILES[0]), (locked, FILES[1])):
            require(field(block, "path", name, filename) == expected_path,
                    f"{name}: invalid component path")
        version = field(manifest, "version", name, FILES[0])
        locked_version = field(locked, "version", name, FILES[1])
        component_hash = field(locked, "component_hash", name, FILES[1])
        require(bool(HASH.fullmatch(component_hash)),
                f"{name}: invalid locked component hash")
        require(bool(SHA.fullmatch(locked_version)),
                f"{name}: lock must resolve to an immutable Git SHA")

        if source == FORK:
            seen_forks.add(name)
            require(bool(SHA.fullmatch(version)) and version == locked_version,
                    f"{name}: fork manifest/lock must match exact SHA")
            pin = fork_pins.get(name)
            require(isinstance(pin, dict) and set(pin) ==
                    {"revision", "upstream_base", "tracking_url"},
                    f"{name}: fork provenance fields missing or unexpected")
            require(pin["revision"] == version,
                    f"{name}: fork provenance SHA differs from pin")
            require(isinstance(pin["upstream_base"], str) and
                    bool(SHA.fullmatch(pin["upstream_base"])),
                    f"{name}: upstream base must be exact SHA")
            require(isinstance(pin["tracking_url"], str) and
                    bool(TRACKING.fullmatch(pin["tracking_url"])),
                    f"{name}: tracking_url must be approved dragon-core PR/issue")
        else:
            require(name not in fork_pins,
                    f"{name}: stale fork provenance on upstream pin")
            require(
                (bool(SHA.fullmatch(version)) and version == locked_version)
                or (name in EXISTING_TAGS and version == EXISTING_TAGS[name]),
                f"{name}: upstream pin must be exact SHA or existing approved tag",
            )
    require(seen_forks == set(fork_pins), "fork metadata does not match pins")
    return {name: ("fork" if name in seen_forks else "upstream")
            for name in COMPONENTS}


def main():
    try:
        sources = validate(
            Path(FILES[0]).read_text(encoding="utf-8"),
            Path(FILES[1]).read_text(encoding="utf-8"),
            Path("config/dragon_core_fork_pins.json").read_text(encoding="utf-8"),
        )
    except (OSError, ValueError) as exc:
        raise SystemExit(f"FAIL: dragon-core dependency policy: {exc}") from exc
    print("PASS: dragon-core dependency sources, pins, hashes and provenance "
          + ", ".join(f"{name}={source}" for name, source in sources.items()))


if __name__ == "__main__":
    main()
