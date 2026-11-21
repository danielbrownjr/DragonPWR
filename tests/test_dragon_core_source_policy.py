#!/usr/bin/env python3
"""Positive/negative dependency-policy regression tests (no extra packages)."""
import json
import re
from pathlib import Path
from tempfile import TemporaryDirectory
import unittest
from check_dc_ui_pin import (validate, validate_other_manifests, parse_manifest,
                              parse_lock, COMPONENTS, UPSTREAM, FORK)

# Real checked-in configuration must always be validated separately from
# synthetic source-policy fixtures, including when dc_ui uses the approved fork.
ACTIVE_MANIFEST = Path("main/idf_component.yml").read_text(encoding="utf-8")
ACTIVE_LOCK = Path("dependencies.lock").read_text(encoding="utf-8")
ACTIVE_PINS = json.loads(Path("config/dragon_core_fork_pins.json").read_text())
FORK_REV = "a" * 40
UPSTREAM_BASE = "b" * 40
TRACK = "https://github.com/justinh-rahb/dragon-core/pull/78"


def replace_component(content, component, field, old, new):
    start = content.index("  " + component + ":\n")
    match = re.search(r"\n  (?=[A-Za-z_][\w./-]*:)", content[start + 3:])
    end = start + 3 + match.start() if match else len(content)
    block = content[start:end]
    needle = field + ": " + old
    if block.count(needle) != 1:
        raise AssertionError(f"fixture missing {component} {field}: {old}")
    return content[:start] + block.replace(needle, field + ": " + new) + content[end:]



def upstream_fixture():
    """Build a syntactically valid upstream-only policy fixture.

    This is deliberately *not* the checked-in manifest/lock and does not claim
    fork commits exist upstream. The actual checked-in pin is separately tested.
    """
    manifest, lock = ACTIVE_MANIFEST, ACTIVE_LOCK
    for name in COMPONENTS:
        if parse_manifest(manifest)[name]["git"] == FORK:
            manifest = replace_component(manifest, name, "git", FORK, UPSTREAM)
            lock = replace_component(lock, name, "git", FORK, UPSTREAM)
    return manifest, lock


MANIFEST, LOCK = upstream_fixture()
BASE = {"fork_pins": {}}
BASE_UI_REV = parse_manifest(MANIFEST)["dc_ui"]["version"]
BASE_UI_HASH = parse_lock(LOCK)["dc_ui"]["component_hash"]


def fork_fixture():
    manifest = replace_component(
        MANIFEST, "dc_ui", "git", UPSTREAM, FORK)
    manifest = replace_component(
        manifest, "dc_ui", "version",
        BASE_UI_REV, FORK_REV)
    lock = replace_component(LOCK, "dc_ui", "git", UPSTREAM, FORK)
    lock = replace_component(
        lock, "dc_ui", "version",
        BASE_UI_REV, FORK_REV)
    pins = {"fork_pins": {"dc_ui": {
        "revision": FORK_REV, "upstream_base": UPSTREAM_BASE,
        "tracking_url": TRACK,
    }}}
    return manifest, lock, pins


def verify(manifest=MANIFEST, lock=LOCK, pins=None):
    return validate(manifest, lock, json.dumps(BASE if pins is None else pins))


class DependencyPolicyTests(unittest.TestCase):
    def test_checked_in_configuration(self):
        expected = "fork" if parse_manifest(ACTIVE_MANIFEST)["dc_ui"]["git"] == FORK else "upstream"
        self.assertEqual(
            validate(ACTIVE_MANIFEST, ACTIVE_LOCK, json.dumps(ACTIVE_PINS))["dc_ui"],
            expected,
        )

    def test_upstream_policy_fixture(self):
        self.assertEqual(verify()["dc_ui"], "upstream")

    def test_fork_positive(self):
        m, l, p = fork_fixture()
        self.assertEqual(verify(m, l, p)["dc_ui"], "fork")

    def test_unknown_source_rejected(self):
        m = replace_component(MANIFEST, "dc_ui", "git", UPSTREAM,
                              "https://github.com/attacker/dragon-core.git")
        with self.assertRaisesRegex(ValueError, "unapproved"):
            verify(m)

    def test_manifest_lock_source_mismatch_rejected(self):
        m, _, _ = fork_fixture()
        with self.assertRaisesRegex(ValueError, "inconsistent"):
            verify(m)

    def test_moving_branch_rejected(self):
        m = replace_component(
            MANIFEST, "dc_ui", "version",
            BASE_UI_REV, "main")
        with self.assertRaisesRegex(ValueError, "exact SHA"):
            verify(m)

    def test_lock_mismatch_rejected(self):
        m, l, p = fork_fixture()
        l = replace_component(l, "dc_ui", "version", FORK_REV, "c" * 40)
        with self.assertRaisesRegex(ValueError, "must match"):
            verify(m, l, p)

    def test_missing_fork_provenance_rejected(self):
        m, l, _ = fork_fixture()
        with self.assertRaisesRegex(ValueError, "provenance"):
            verify(m, l)

    def test_untracked_fork_base_rejected(self):
        m, l, p = fork_fixture()
        p["fork_pins"]["dc_ui"]["upstream_base"] = "main"
        with self.assertRaisesRegex(ValueError, "upstream base"):
            verify(m, l, p)

    def test_wrong_fork_provenance_sha_rejected(self):
        m, l, p = fork_fixture()
        p["fork_pins"]["dc_ui"]["revision"] = "f" * 40
        with self.assertRaisesRegex(ValueError, "provenance SHA"):
            verify(m, l, p)

    def test_wrong_tracking_url_rejected(self):
        m, l, p = fork_fixture()
        p["fork_pins"]["dc_ui"]["tracking_url"] = "https://example.com/foo"
        with self.assertRaisesRegex(ValueError, "tracking_url"):
            verify(m, l, p)

    def test_stale_provenance_rejected(self):
        _, _, p = fork_fixture()
        with self.assertRaisesRegex(ValueError, "stale fork"):
            verify(pins=p)

    def test_wrong_component_path_rejected(self):
        m = replace_component(MANIFEST, "dc_ui", "path",
                              "components/dc_ui", "components/dc_evlog")
        with self.assertRaisesRegex(ValueError, "component path"):
            verify(m)

    def test_bad_component_hash_rejected(self):
        original = BASE_UI_HASH
        l = replace_component(LOCK, "dc_ui", "component_hash", original, "bad")
        with self.assertRaisesRegex(ValueError, "component hash"):
            verify(lock=l)

    def test_other_core_component_fork_requires_exact_sha(self):
        m = replace_component(MANIFEST, "dc_wifi", "git", UPSTREAM, FORK)
        l = replace_component(LOCK, "dc_wifi", "git", UPSTREAM, FORK)
        with self.assertRaisesRegex(ValueError, "exact SHA"):
            verify(m, l)

    def test_unknown_provenance_component_rejected(self):
        p = {"fork_pins": {"dc_unknown": {}}}
        with self.assertRaisesRegex(ValueError, "unknown component"):
            verify(pins=p)


    def test_lock_source_flow_map_and_nested_decoy_rejected(self):
        # The attack demonstrated in independent review DP36-01: decoy Git
        # fields in the nested dependency list and a real flow-map source.
        l = LOCK.replace(
            "  dc_ui:\n    component_hash:",
            "  dc_ui:\n    component_hash:", 1)
        component_start = l.index("  dc_ui:\n")
        component_end = l.index("  dc_wifi:\n", component_start)
        block = l[component_start:component_end]
        block = block.replace(
            "    - name: idf\n      version: '>=5.3'\n",
            "    - name: idf\n      git: " + UPSTREAM +
            "\n      path: components/dc_ui\n      version: '>=5.3'\n", 1)
        block = block.replace(
            "    source:\n      git: " + UPSTREAM +
            "\n      path: components/dc_ui\n      type: git",
            "    source: {git: 'https://github.com/attacker/evil.git', "
            "path: components/dc_ui, type: git}", 1)
        l = l[:component_start] + block + l[component_end:]
        with self.assertRaisesRegex(ValueError, "unsupported YAML syntax"):
            verify(lock=l)

    def test_legacy_upstream_tag_lock_sha_drift_rejected(self):
        l = replace_component(LOCK, "dc_evlog", "version",
                              "8d5a61de8544a5b82cc3c6d9c7dc593ce06bb8bd",
                              "e" * 40)
        with self.assertRaisesRegex(ValueError, "legacy upstream tag lock SHA drift"):
            verify(lock=l)

    def test_unknown_manifest_git_component_rejected(self):
        m = MANIFEST + "  dc_moonraker:\n    git: https://github.com/attacker/evil.git\n    version: main\n"
        with self.assertRaisesRegex(ValueError, "unexpected/missing dependency"):
            verify(m)

    def test_unknown_manifest_component_key_rejected(self):
        m = MANIFEST.replace(
            "  dc_ui:\n", "  dc_ui:\n    override_path: ../vendor/dc_ui\n", 1)
        with self.assertRaisesRegex(ValueError, "unsupported/missing dependency key"):
            verify(m)

    def test_unknown_lock_dependency_rejected(self):
        # Syntactically ordinary block YAML: rejection must be based on the
        # unexpected dependency name, not merely unsupported flow syntax.
        l = LOCK.replace(
            "  dc_ui:\n",
            "  evil_dependency:\n    version: 1.0.0\n  dc_ui:\n", 1)
        with self.assertRaisesRegex(ValueError, "unexpected/missing dependency"):
            verify(lock=l)

    def test_lock_source_path_rejected(self):
        l = replace_component(LOCK, "dc_ui", "path",
                              "components/dc_ui", "components/dc_portal")
        with self.assertRaisesRegex(ValueError, "component path"):
            verify(lock=l)

    def test_upstream_exact_sha_mismatch_rejected(self):
        m = replace_component(MANIFEST, "dc_ui", "version",
                              BASE_UI_REV,
                              "f" * 40)
        with self.assertRaisesRegex(ValueError, "upstream manifest/lock"):
            verify(m)

    def test_upstream_dc_ui_tag_rejected(self):
        m = replace_component(MANIFEST, "dc_ui", "version",
                              BASE_UI_REV,
                              "v0.30.0")
        with self.assertRaisesRegex(ValueError, "upstream manifest/lock"):
            verify(m)

    def test_tracking_url_wrong_repo_rejected(self):
        m, l, p = fork_fixture()
        p["fork_pins"]["dc_ui"]["tracking_url"] = (
            "https://github.com/justinh-rahb/other-repo/pull/78")
        with self.assertRaisesRegex(ValueError, "tracking_url"):
            verify(m, l, p)

    def test_lock_flow_and_yaml_alias_rejected(self):
        l = LOCK.replace(
            "      type: git\n",
            "      type: git\n      extra: &sneaky evil\n", 1)
        with self.assertRaisesRegex(ValueError, "unsupported YAML syntax"):
            verify(lock=l)

    def test_unexpected_registry_source_rejected(self):
        l = LOCK.replace("registry_url: https://components.espressif.com/",
                         "git: https://github.com/attacker/evil.git", 1)
        with self.assertRaisesRegex(ValueError, "unexpected repository or source"):
            verify(lock=l)


    def test_other_component_git_manifest_rejected(self):
        with TemporaryDirectory() as tmp:
            p = Path(tmp) / "dp_other" / "idf_component.yml"
            p.parent.mkdir()
            p.write_text("dependencies:\n  evil:\n    git: https://github.com/attacker/evil.git\n"
                         "    version: main\n", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "unapproved Git"):
                validate_other_manifests(Path(tmp))

    def test_existing_registry_component_manifest_accepted(self):
        with TemporaryDirectory() as tmp:
            p = Path(tmp) / "dp_portal" / "idf_component.yml"
            p.parent.mkdir()
            p.write_text("dependencies:\n  espressif/cjson:\n"
                         "    version: \"1.7.19\"\n", encoding="utf-8")
            validate_other_manifests(Path(tmp))


if __name__ == "__main__":
    unittest.main()
