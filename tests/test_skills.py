#!/usr/bin/env python3
"""Unit tests verifying repository agent skills conform to standards and link integrity."""
from __future__ import annotations

import re
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent


class TestSkillsIntegrity(unittest.TestCase):
    """Test suite ensuring agent skills are correctly structured and link to valid targets."""

    def setUp(self) -> None:
        self.agents_skills_dir = REPO_ROOT / ".agents" / "skills"
        self.claude_skills_dir = REPO_ROOT / ".claude" / "skills"

    def test_skills_directories_exist(self) -> None:
        self.assertTrue(self.agents_skills_dir.is_dir(), ".agents/skills must exist")
        self.assertTrue(self.claude_skills_dir.is_dir(), ".claude/skills must exist")

    def test_expected_skills_present(self) -> None:
        expected = {"docker-build", "docker-validate"}
        found_agents = {p.name for p in self.agents_skills_dir.iterdir() if p.is_dir()}
        found_claude = {p.name for p in self.claude_skills_dir.iterdir() if p.is_dir()}
        self.assertTrue(expected.issubset(found_agents), f"Missing skills in .agents: {expected - found_agents}")
        self.assertTrue(expected.issubset(found_claude), f"Missing skills in .claude: {expected - found_claude}")

    def test_skills_frontmatter_and_links(self) -> None:
        skill_files = list(self.agents_skills_dir.glob("*/SKILL.md"))
        self.assertGreaterEqual(len(skill_files), 2)

        link_pattern = re.compile(r"\[.*?\]\(((\.\./|[a-zA-Z0-9_\-./]+)+)\)")

        for skill_path in skill_files:
            skill_dir = skill_path.parent
            skill_name = skill_dir.name
            content = skill_path.read_text(encoding="utf-8")

            # Validate YAML frontmatter
            self.assertTrue(content.startswith("---\n"), f"{skill_path} must start with YAML frontmatter")
            parts = content.split("---\n", 2)
            self.assertGreaterEqual(len(parts), 3, f"{skill_path} frontmatter must be enclosed by ---")

            frontmatter = parts[1]
            self.assertIn(f"name: {skill_name}", frontmatter, f"{skill_path} frontmatter name must match folder")
            self.assertIn("description:", frontmatter, f"{skill_path} frontmatter must include description")

            # Validate relative markdown links
            for match in link_pattern.finditer(content):
                target_str = match.group(1)
                # Skip anchors and external URLs
                if target_str.startswith("#") or target_str.startswith("http"):
                    continue
                resolved_target = (skill_dir / target_str).resolve()
                self.assertTrue(
                    resolved_target.exists(),
                    f"Broken link in {skill_path}: {target_str} (resolved to {resolved_target})",
                )

    def test_docker_build_skill_contents(self) -> None:
        build_skill = (self.agents_skills_dir / "docker-build" / "SKILL.md").read_text(encoding="utf-8")
        self.assertIn("tools/docker_build.py", build_skill)
        self.assertIn("aoe2-build-desktop", build_skill)
        self.assertIn("aoe2-build-switch", build_skill)
        self.assertIn("aoe2-build-android", build_skill)
        self.assertIn("-j8", build_skill)
        self.assertIn("--build-cache", build_skill)
        self.assertIn(".staging.lock", build_skill)
        self.assertIn("--diagnostics", build_skill)
        self.assertIn("-DAOE2_RUNTIME_DIAGNOSTICS=ON", build_skill)
        self.assertIn("-DCMAKE_BUILD_TYPE=Debug", build_skill)
        self.assertIn("AOE2_CALL_TRACE_FIRST", build_skill)

    def test_docker_validate_skill_contents(self) -> None:
        val_skill = (self.agents_skills_dir / "docker-validate" / "SKILL.md").read_text(encoding="utf-8")
        self.assertIn("tools/validate_desktop.py", val_skill)
        self.assertIn("tools/validate_switch.py", val_skill)
        self.assertIn("tools/view_switch_novnc.py", val_skill)
        self.assertIn("tools/validate_android.py", val_skill)
        self.assertIn("aoe2-validate-desktop", val_skill)
        self.assertIn("aoe2-validate-switch", val_skill)
        self.assertIn("aoe2-view-switch", val_skill)
        self.assertIn("aoe2-validate-android", val_skill)
        self.assertIn("-j8", val_skill)
        self.assertIn("AOE2_CALL_TRACE_MEM", val_skill)
        self.assertIn("AOE2_SHAPE_TRACE", val_skill)


if __name__ == "__main__":
    unittest.main()
