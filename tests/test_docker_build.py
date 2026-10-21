#!/usr/bin/env python3
"""Tests for the containerized platform build driver (tools/docker_build.py)."""
import unittest
from unittest import mock

from tools import docker_build


def build_script_for(spec) -> str:
    return docker_build.build_script(spec, jobs=1)


class TestDockerBuildCommands(unittest.TestCase):
    def setUp(self):
        self.spec = docker_build.PLATFORMS["desktop"]
        self.args = docker_build.parse_args(["--platform", "desktop", "--jobs", "6"])

    def test_image_build_uses_platform_dockerfile_and_host_ids(self):
        command = docker_build.image_build_command(self.spec, uid=1234, gid=5678)
        self.assertIn(str(docker_build.REPO_ROOT / self.spec.dockerfile), command)
        self.assertIn("BUILD_UID=1234", command)
        self.assertIn("BUILD_GID=5678", command)
        self.assertEqual(command[:3], ["docker", "--context", docker_build.DOCKER_CONTEXT])

    def test_run_mounts_repo_and_caps_resources(self):
        command = docker_build.container_run_command(self.spec, self.args)
        self.assertIn(f"{docker_build.REPO_ROOT}:{docker_build.CONTAINER_ROOT}", command)
        self.assertIn("--memory=18g", command)
        self.assertIn("--memory-swap=18g", command)
        self.assertIn("--cpus=6", command)
        self.assertIn("LC_ALL=C", command)
        self.assertIn("--rm", command)

    def test_build_script_uses_requested_jobs_and_platform_build_dir(self):
        script = docker_build.build_script(self.spec, jobs=6)
        self.assertIn(self.spec.build_dir, script)
        self.assertIn("-j 6", script)
        self.assertIn("set -euo pipefail", script)

    def test_every_platform_declares_a_dockerfile_that_exists(self):
        for name, spec in docker_build.PLATFORMS.items():
            self.assertTrue((docker_build.REPO_ROOT / spec.dockerfile).is_file(), name)

    def test_preflight_fails_without_generated_sources(self):
        with mock.patch.object(docker_build, "generated_source_count", return_value=0):
            with self.assertRaises(docker_build.PreflightError):
                docker_build.check_preflight(self.args)

    def test_diagnostics_desktop_build_uses_its_own_directory_and_flag(self):
        spec = docker_build.select_spec("desktop", diagnostics=True)
        self.assertEqual(spec.build_dir, "build/docker-desktop-diagnostics")
        self.assertIn("-DAOE2_RUNTIME_DIAGNOSTICS=ON", build_script_for(spec))
        self.assertNotEqual(spec.build_dir, self.spec.build_dir)

    def test_normal_desktop_build_does_not_enable_diagnostics(self):
        spec = docker_build.select_spec("desktop", diagnostics=False)
        self.assertEqual(spec.build_dir, "build/docker-desktop")
        self.assertNotIn("-DAOE2_RUNTIME_DIAGNOSTICS=ON", build_script_for(spec))

    def test_diagnostics_is_rejected_for_platforms_without_a_variant(self):
        with self.assertRaises(docker_build.PreflightError):
            docker_build.select_spec("android", diagnostics=True)

    def test_parse_args_accepts_diagnostics_flag(self):
        args = docker_build.parse_args(["--platform", "desktop", "--diagnostics"])
        self.assertTrue(args.diagnostics)

    def test_unknown_platform_is_rejected(self):
        with self.assertRaises(SystemExit):
            docker_build.parse_args(["--platform", "dreamcast"])


if __name__ == "__main__":
    unittest.main()
