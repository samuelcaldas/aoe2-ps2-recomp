#!/usr/bin/env python3
"""Tests for the containerized platform build driver (tools/docker_build.py)."""
import unittest
import shlex
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
        self.assertIn("--name=aoe2-build-desktop", command)
        self.assertIn("--hostname=aoe2-build-desktop", command)
        self.assertIn("--label=aoe2.platform=desktop", command)
        self.assertIn("--label=aoe2.role=build", command)

    def test_default_jobs_is_eight(self):
        self.assertEqual(docker_build.DEFAULT_JOBS, 8)
        args = docker_build.parse_args(["--platform", "desktop"])
        self.assertEqual(args.jobs, 8)

    def test_pc_platform_alias_resolves_to_desktop(self):
        args = docker_build.parse_args(["--platform", "pc"])
        self.assertEqual(docker_build.canonical_platform_name(args.platform), "desktop")
        spec = docker_build.select_spec(args.platform, diagnostics=False)
        self.assertEqual(spec, docker_build.PLATFORMS["desktop"])
        self.assertEqual(docker_build.container_name_for(args), "aoe2-build-desktop")

    def test_concurrent_platforms_have_distinct_container_identifications(self):
        platforms = ["desktop", "android", "switch"]
        container_names = set()
        for platform in platforms:
            args = docker_build.parse_args(["--platform", platform])
            name = docker_build.container_name_for(args)
            self.assertNotIn(name, container_names, f"collision on container name: {name}")
            container_names.add(name)
            cmd = docker_build.container_run_command(docker_build.PLATFORMS[platform], args)
            self.assertIn(f"--name={name}", cmd)
            self.assertIn(f"--hostname={name}", cmd)
            self.assertIn(f"--label=aoe2.platform={platform}", cmd)
        # Diagnostic variant also has unique identification
        diag_args = docker_build.parse_args(["--platform", "desktop", "--diagnostics"])
        diag_name = docker_build.container_name_for(diag_args)
        self.assertEqual(diag_name, "aoe2-build-desktop-diagnostics")
        self.assertNotIn(diag_name, container_names)

    def test_android_build_uses_build_cache(self):
        android_script = docker_build.build_script(docker_build.PLATFORMS["android"], jobs=8)
        self.assertIn("--build-cache", android_script)

    def test_test_flag_appends_test_commands(self):
        desktop_script = docker_build.build_script(self.spec, jobs=8, test=True)
        self.assertIn("ctest --test-dir", desktop_script)

    def test_ensure_container_clean_removes_stopped_container(self):
        inspect_res = mock.Mock(returncode=0, stdout="false\n")
        with mock.patch("subprocess.run", side_effect=[inspect_res, mock.Mock(returncode=0)]) as run_mock:
            docker_build.ensure_container_clean("aoe2-build-desktop")
            self.assertEqual(run_mock.call_count, 2)
            rm_call = run_mock.call_args_list[1][0][0]
            self.assertIn("rm", rm_call)
            self.assertIn("aoe2-build-desktop", rm_call)

    def test_ensure_container_clean_rejects_running_container(self):
        inspect_res = mock.Mock(returncode=0, stdout="true\n")
        with mock.patch("subprocess.run", return_value=inspect_res):
            with self.assertRaises(docker_build.PreflightError) as ctx:
                docker_build.ensure_container_clean("aoe2-build-desktop")
            self.assertIn("is already running", str(ctx.exception))

    def test_build_script_uses_requested_jobs_and_platform_build_dir(self):
        script = docker_build.build_script(self.spec, jobs=6)
        self.assertIn(self.spec.build_dir, script)
        self.assertIn("-j 6", script)
        self.assertIn("set -euo pipefail", script)

    def test_cmake_build_commands_disable_function_log_tracker(self):
        specs = {
            "desktop": docker_build.PLATFORMS["desktop"],
            "desktop-diagnostics": docker_build.DIAGNOSTIC_VARIANTS["desktop"],
            "switch": docker_build.PLATFORMS["switch"],
        }
        for name, spec in specs.items():
            with self.subTest(platform=name):
                commands = [shlex.split(line) for line in build_script_for(spec).splitlines()]
                configure = next(command for command in commands if command[:2] == ["cmake", "-S"])
                self.assertIn("-DPS2X_ENABLE_FUNCTION_LOG_TRACKER=OFF", configure)

    def test_cmake_builds_use_debug_and_verbose_output(self):
        for name, spec in (("desktop", docker_build.PLATFORMS["desktop"]),
                           ("desktop-diagnostics", docker_build.DIAGNOSTIC_VARIANTS["desktop"]),
                           ("switch", docker_build.PLATFORMS["switch"])):
            with self.subTest(platform=name):
                commands = [shlex.split(line) for line in build_script_for(spec).splitlines()]
                configure = next(command for command in commands if command[:2] == ["cmake", "-S"])
                build = next(command for command in commands if command[:2] == ["cmake", "--build"])
                self.assertIn("-DCMAKE_BUILD_TYPE=Debug", configure)
                self.assertNotIn("-DCMAKE_BUILD_TYPE=Release", configure)
                self.assertIn("-v", build)
        self.assertIn("-DAOE2_RUNTIME_DIAGNOSTICS=ON", build_script_for(docker_build.PLATFORMS["switch"]))
        self.assertIn("--output-on-failure -V", docker_build.build_script(self.spec, jobs=1, test=True))

    def test_build_script_stages_hd_assets_before_configuring(self):
        script = docker_build.build_script(self.spec, jobs=6)
        stage = "python3 tools/stage_hd_assets.py --platform desktop"
        verify = "python3 tools/verify_hd_stage.py --platform desktop"
        self.assertIn(stage, script)
        self.assertIn(verify, script)
        self.assertLess(script.index(stage), script.index(verify))
        self.assertLess(script.index(verify), script.index("cmake -S"))
        self.assertIn("game/hd-edition/catalog.json", script)

    def test_android_and_switch_build_specs_stage_their_platform_assets(self):
        for platform in ("android", "switch"):
            script = docker_build.build_script(docker_build.PLATFORMS[platform], jobs=2)
            self.assertIn(f"python3 tools/stage_hd_assets.py --platform {platform}", script)
            self.assertIn(f"python3 tools/verify_hd_stage.py --platform {platform}", script)

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
