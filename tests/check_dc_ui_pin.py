#!/usr/bin/env python3
"""Fail-closed policy for DragonPWR's supported ESP-IDF dependency YAML subset.

The committed manifest/lock must use canonical block-style mapping syntax.
Unsupported YAML constructs are rejected, rather than guessed at with regexes.
ESP-IDF still verifies downloaded component bytes against its component_hash.
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
# Preserve previously reviewed upstream v0.30.0 only at its exact locked commit.
EXISTING_TAGS = {name: ("v0.30.0", "8d5a61de8544a5b82cc3c6d9c7dc593ce06bb8bd")
                 for name in ("dc_evlog", "dc_wifi", "dc_portal")}
MANIFEST_DEPS = set(COMPONENTS) | {"espressif/mdns"}
LOCK_DEPS = MANIFEST_DEPS | {"idf"}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def lines(text, filename):
    """Accept the canonical mapping subset, not general YAML.

    This avoids a second parser interpreting flow maps, anchors, inline comments,
    tabs, folded blocks or duplicate keys differently from this validator.
    """
    out = []
    for number, raw in enumerate(text.splitlines(), 1):
        if not raw.strip() or raw.lstrip().startswith("#"):
            continue
        require("\t" not in raw and "\r" not in raw, f"{filename}:{number}: tabs/CR prohibited")
        require(not any(c in raw for c in "{}[]&*") and "<<" not in raw,
                f"{filename}:{number}: unsupported YAML syntax")
        require(not re.search(r":\s*[|>](?:\s|$)", raw),
                f"{filename}:{number}: block scalars prohibited")
        require(not re.search(r"\s#", raw), f"{filename}:{number}: inline comments prohibited")
        indent = len(raw) - len(raw.lstrip(" "))
        require(indent % 2 == 0, f"{filename}:{number}: bad indentation")
        out.append((indent, raw.strip(), number))
    return out


def key_value(line, filename, number):
    require(": " in line or line.endswith(":"),
            f"{filename}:{number}: expected mapping entry")
    name, _, value = line.partition(":")
    require(bool(re.fullmatch(r"[A-Za-z_][\w./-]*", name)),
            f"{filename}:{number}: invalid key")
    return name, value.strip()


def top_section(data, expected, filename):
    require(data and data[0][:2] == (0, "dependencies:"),
            f"{filename}: dependencies must be first root mapping")
    root_keys = [entry[1] for entry in data if entry[0] == 0]
    require(root_keys == expected, f"{filename}: unexpected root keys")
    start = 1
    end = next((i for i, row in enumerate(data[1:], 1) if row[0] == 0),
               len(data))
    sections = {}
    for i in range(start, end):
        indent, entry, number = data[i]
        if indent == 2:
            name, value = key_value(entry, filename, number)
            require(name not in sections, f"{filename}: duplicate dependency {name}")
            sections[name] = [value, []]
        else:
            require(bool(sections), f"{filename}:{number}: orphan nested entry")
            list(sections.values())[-1][1].append((indent, entry, number))
    return sections


def fields(rows, indent, filename, context):
    result = {}
    for row_indent, entry, number in rows:
        require(row_indent == indent, f"{filename}:{number}: unexpected nesting in {context}")
        key, value = key_value(entry, filename, number)
        require(key not in result, f"{filename}: duplicate {context}.{key}")
        result[key] = value
    return result


def parse_manifest(text):
    name = FILES[0]
    sections = top_section(lines(text, name), ["dependencies:"], name)
    require(set(sections) == MANIFEST_DEPS,
            f"{name}: unexpected/missing dependency (explicit review required)")
    require(sections["espressif/mdns"] == ['"1.12.0"', []],
            f"{name}: mDNS dependency changed without policy update")
    result = {}
    for component in COMPONENTS:
        inline, rows = sections[component]
        require(inline == "", f"{name}: {component}: inline/flow dependency forbidden")
        parsed = fields(rows, 4, name, component)
        require(set(parsed) == {"git", "path", "version"},
                f"{name}: {component}: unsupported/missing dependency key")
        result[component] = parsed
    return result


def parse_lock(text):
    name = FILES[1]
    data = lines(text, name)
    sections = top_section(data, ["dependencies:", "direct_dependencies:",
                                   "manifest_hash:", "target:", "version:"], name)
    require(set(sections) == LOCK_DEPS,
            f"{name}: unexpected/missing dependency (explicit review required)")
    require(all(value == "" for value, _ in sections.values()),
            f"{name}: inline/flow dependency entry forbidden")
    # Require all named direct dependencies to match the approved manifest.
    direct_i = next(i for i, (indent, entry, _) in enumerate(data)
                    if indent == 0 and entry == "direct_dependencies:")
    direct = []
    for indent, entry, number in data[direct_i + 1:]:
        if indent == 0:
            break
        require(indent == 0 or (indent == 0 and entry.startswith("- ")),
                f"{name}:{number}: invalid direct dependency list")
    # Lock direct_dependencies is an unindented YAML sequence (not nested map).
    # The lines() helper allows these to be inspected separately.
    for indent, entry, number in data[direct_i + 1:]:
        if indent == 0 and entry.startswith("- "):
            direct.append(entry[2:])
        elif indent == 0:
            break
        else:
            raise ValueError(f"{name}:{number}: invalid direct dependency")
    require(set(direct) == MANIFEST_DEPS and len(direct) == len(MANIFEST_DEPS),
            f"{name}: unexpected direct dependencies")
    result = {}
    for component in COMPONENTS:
        rows = sections[component][1]
        direct_fields = {}
        source_rows = []
        mode = None
        for indent, entry, number in rows:
            if indent == 4:
                key, value = key_value(entry, name, number)
                require(key not in direct_fields, f"{name}: {component}: duplicate {key}")
                direct_fields[key] = value
                mode = key
            elif indent == 6 and mode == "source":
                source_rows.append((indent, entry, number))
            elif mode == "dependencies" and (indent == 4 or indent == 6):
                # IDF's nested list has '- name:' at indent 4 and attributes at 6.
                # Never interpret these fields as component source metadata.
                require(entry.startswith("- name: ") if indent == 4 else
                        bool(re.fullmatch(r"(?:require|version): .+", entry)),
                        f"{name}:{number}: unexpected dependency child")
            else:
                raise ValueError(f"{name}:{number}: unexpected lock nesting")
        require(set(direct_fields) == {"component_hash", "dependencies",
                                      "source", "version"},
                f"{name}: {component}: unexpected/missing lock key")
        require(direct_fields["dependencies"] == "" and
                direct_fields["source"] == "",
                f"{name}: {component}: lock dependencies/source must be block style")
        source = fields(source_rows, 6, name, f"{component}.source")
        require(set(source) == {"git", "path", "type"} and source["type"] == "git",
                f"{name}: {component}: invalid Git source block")
        result[component] = {
            **source, "version": direct_fields["version"],
            "component_hash": direct_fields["component_hash"],
        }
    # Verify the lock's target and root fields aren't shadowed.
    for root_key, pattern in (("manifest_hash", r"[0-9a-f]{64}"),
                              ("target", r"esp32c2"),
                              ("version", r"2[.]0[.]0")):
        match = [entry for indent, entry, _ in data if indent == 0
                 and entry.startswith(root_key + ": ")]
        require(len(match) == 1 and
                re.fullmatch(root_key + r": " + pattern, match[0]) is not None,
                f"{name}: invalid {root_key}")
    return result


def validate(manifest_text, lock_text, provenance_text):
    try:
        provenance = json.loads(provenance_text)
    except json.JSONDecodeError as exc:
        raise ValueError(f"invalid fork provenance JSON: {exc}") from exc
    require(isinstance(provenance, dict) and set(provenance) == {"fork_pins"},
            "fork provenance must contain only fork_pins")
    fork_pins = provenance["fork_pins"]
    require(isinstance(fork_pins, dict) and not (set(fork_pins) - set(COMPONENTS)),
            "unknown component in fork_pins")
    manifest = parse_manifest(manifest_text)
    locked = parse_lock(lock_text)
    seen_forks = set()
    for name in COMPONENTS:
        source = manifest[name]["git"]
        lock = locked[name]
        require(source in (UPSTREAM, FORK) and lock["git"] == source,
                f"{name}: unapproved or inconsistent Git source")
        require(manifest[name]["path"] == f"components/{name}" and
                lock["path"] == f"components/{name}",
                f"{name}: invalid component path")
        version = manifest[name]["version"]
        locked_version = lock["version"]
        require(bool(HASH.fullmatch(lock["component_hash"])),
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
            if name in EXISTING_TAGS and version == EXISTING_TAGS[name][0]:
                require(locked_version == EXISTING_TAGS[name][1],
                        f"{name}: legacy upstream tag lock SHA drift")
            else:
                require(bool(SHA.fullmatch(version)) and version == locked_version,
                        f"{name}: upstream manifest/lock must match exact SHA")
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
