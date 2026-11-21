#!/usr/bin/env python3
"""Positive/negative dependency-policy regression tests (no extra packages)."""
import json
import re
from pathlib import Path
import unittest
from check_dc_ui_pin import validate, UPSTREAM, FORK

MANIFEST = Path("main/idf_component.yml").read_text(encoding="utf-8")
LOCK = Path("dependencies.lock").read_text(encoding="utf-8")
BASE = json.loads(Path("config/dragon_core_fork_pins.json").read_text())
FORK_REV = "a" * 40
UPSTREAM_BASE = "b" * 40
TRACK = "https://github.com/justinh-rahb/dragon-core/pull/78"


def replace_component(content, component, field, old, new):
    start = content.index("  " + component + ":\n")
    match = re.search(r"\n  (?=[A-Za-z_][\\w./-]*:)", content[start + 3:])
    end = start + 3 + match.start() if match else len(content)
    block = content[start:end]
    needle = field + ": " + old
    if block.count(needle) != 1:
        raise AssertionError(f"fixture missing {component} {field}: {old}")
    return content[:start] + block.replace(needle, field + ": " + new) + content[end:]


def fork_fixture():
    manifest = replace_component(
        MANIFEST, "dc_ui", "git", UPSTREAM, FORK)
    manifest = replace_component(
        manifest, "dc_ui", "version",
        "33e6e31f1e83a8184f1e8f028cb03ad19a00380f", FORK_REV)
    lock = replace_component(LOCK, "dc_ui", "git", UPSTREAM, FORK)
    lock = replace_component(
        lock, "dc_ui", "version",
        "33e6e31f1e83a8184f1e8f028cb03ad19a00380f", FORK_REV)
    pins = {"fork_pins": {"dc_ui": {
        "revision": FORK_REV, "upstream_base": UPSTREAM_BASE,
        "tracking_url": TRACK,
    }}}
    return manifest, lock, pins


def verify(manifest=MANIFEST, lock=LOCK, pins=None):
    return validate(manifest, lock, json.dumps(BASE if pins is None else pins))


class DependencyPolicyTests(unittest.TestCase):
    def test_upstream_current_baseline(self):
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
            "33e6e31f1e83a8184f1e8f028cb03ad19a00380f", "main")
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
        original = "b83f5fa81779f7898292c4ea863a93c95413886e6bda20f08923249edc17e4f2"
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


if __name__ == "__main__":
    unittest.main()
