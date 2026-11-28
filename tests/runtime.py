#!/usr/bin/env python3
"""Container-runtime selection tests: no daemon and no images required."""
import os
import platform
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "cli"))

from stitch_fuzz import docker


class RuntimeTests(unittest.TestCase):
    def setUp(self):
        self.saved_env = os.environ.get("STITCH_RUNTIME")
        self.saved_run = docker._run
        self.calls = []

        def fake_run(cmd, **kw):
            self.calls.append(list(cmd))
            return subprocess.CompletedProcess(cmd, 0, stdout="", stderr="")

        docker._run = fake_run

    def tearDown(self):
        docker._run = self.saved_run
        if self.saved_env is None:
            os.environ.pop("STITCH_RUNTIME", None)
        else:
            os.environ["STITCH_RUNTIME"] = self.saved_env

    def test_default_runtime_is_docker(self):
        os.environ.pop("STITCH_RUNTIME", None)
        self.assertEqual(docker.runtime(), "docker")
        self.assertEqual(docker._cmd("run", "--rm"), ["docker", "run", "--rm"])

    def test_unknown_runtime_is_rejected(self):
        os.environ["STITCH_RUNTIME"] = "nerdctl"
        with self.assertRaises(docker.DockerError):
            docker.runtime()

    def test_runtime_and_buildx_checks_use_selected_binary(self):
        os.environ["STITCH_RUNTIME"] = "podman"
        docker.check_runtime()
        docker.check_buildx()
        self.assertEqual(self.calls, [["podman", "info"], ["podman", "buildx", "version"]])

        self.calls.clear()
        os.environ["STITCH_RUNTIME"] = "docker"
        docker.check_runtime()
        self.assertEqual(self.calls, [["docker", "info", "--format", "{{.ServerVersion}}"]])

    def test_build_flags_follow_runtime(self):
        with tempfile.TemporaryDirectory() as d:
            log = Path(d) / "build.log"
            build_args = ["-f", "Dockerfile", "-t", "image", "."]

            os.environ["STITCH_RUNTIME"] = "docker"
            docker._buildx(build_args, log, "test", quiet=True)
            self.assertEqual(self.calls[-1][:5], ["docker", "buildx", "build", "--progress=plain", "--load"])

            os.environ["STITCH_RUNTIME"] = "podman"
            docker._buildx(build_args, log, "test", quiet=True)
            cmd = self.calls[-1]
            self.assertEqual(cmd[:3], ["podman", "buildx", "build"])
            self.assertNotIn("--progress=plain", cmd)
            self.assertNotIn("--load", cmd)
            self.assertEqual(cmd[3:], build_args)

    def test_pull_tag_inspect_and_stop_use_selected_binary(self):
        os.environ["STITCH_RUNTIME"] = "podman"
        docker._try_pull("ghcr.io/example/image:tag", "image:tag")
        docker.image_exists("image:tag")
        docker.stop("container")
        docker.remove("container")
        self.assertEqual(self.calls, [
            ["podman", "pull", "-q", "ghcr.io/example/image:tag"],
            ["podman", "tag", "ghcr.io/example/image:tag", "image:tag"],
            ["podman", "image", "inspect", "image:tag"],
            ["podman", "stop", "-t", "5", "container"],
            ["podman", "rm", "-f", "container"],
        ])

    def test_created_uses_json_timestamp_on_both_runtimes(self):
        from stitch_fuzz import report

        def fake_run(cmd, **kw):
            self.calls.append(list(cmd))
            return subprocess.CompletedProcess(cmd, 0, stdout='"2026-11-26T19:21:00.879512594Z"', stderr="")

        docker._run = fake_run
        created = report._created("image:tag")
        self.assertIsNotNone(created)
        self.assertEqual(created.isoformat(), "2026-11-26T19:21:00+00:00")
        self.assertEqual(self.calls[-1][-2:], ["--format", "{{json .Created}}"])

    def test_generated_scripts_are_shell_valid(self):
        from stitch_fuzz import report, repro

        scripts = [
            report.RUN_SH.format(title="t", tag="x", platform=""),
            repro.RUN_SH.format(title="t", version="0", rid="id", platform="", toolchain='""',
                                 pins="", fetch="true\n", context="source", buildargs="",
                                 standalone="true\n", limit=1),
        ]
        for script in scripts:
            self.assertIn("STITCH_RUNTIME:-docker", script)
            r = subprocess.run(["sh", "-n"], input=script, text=True, capture_output=True)
            self.assertEqual(r.returncode, 0, r.stderr)

    def test_ci_podman_cache_key_uses_base_hash(self):
        ci = (ROOT / ".github" / "workflows" / "ci.yml").read_text()
        self.assertIn("key: stitch-python-${{ steps.img.outputs.base }}", ci)
        self.assertNotIn("key: stitch-python-${{ needs.engine.outputs.hash }}", ci)

    def test_no_stale_check_docker_calls(self):
        for path in (ROOT / "cli" / "stitch_fuzz").glob("*.py"):
            self.assertNotIn("check_docker(", path.read_text(), str(path))

    def test_reproducer_scripts_select_runtime(self):
        from stitch_fuzz import report, repro

        for template in (repro.RUN_SH, report.RUN_SH):
            self.assertIn("STITCH_RUNTIME:-docker", template)
            self.assertNotIn("docker run", template)
        self.assertIn("${{STITCH_RUNTIME:-docker}}", report.RUN_SH)

    @unittest.skipUnless(platform.system() == "Linux", "Linux user mapping")
    def test_rootless_podman_does_not_force_numeric_user(self):
        project = SimpleNamespace(root=Path("/project"), cfg={"docker": {}}, dockerfile=Path("/project/Dockerfile"))
        os.environ["STITCH_RUNTIME"] = "docker"
        docker_args = docker.run_args(project, "image")
        self.assertIn("--user", docker_args)
        self.assertIn("HOME=/tmp", docker_args)

        os.environ["STITCH_RUNTIME"] = "podman"
        podman_args = docker.run_args(project, "image")
        self.assertEqual(podman_args[0], "podman")
        self.assertNotIn("--user", podman_args)
        self.assertNotIn("HOME=/tmp", podman_args)


if __name__ == "__main__":
    unittest.main()
