"""Benchmark admission and honest measurement regression checks; no renderer needed."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import struct
import sys
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("apo_benchmark", Path(__file__).resolve().parents[1] / "scripts/benchmark.py")
bench = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = bench
spec.loader.exec_module(bench)


def summary(**changes):
    values = dict(samples="1000", plotted="900", invalid_orbit_steps="0", workers="2", seed="123456",
                  elapsed_ms="14", backend="cpu", device='"CPU reference (double precision)"',
                  sampling_ms="4.5", reconstruction_ms="8.25", format="png8", alpha="0", resumed="0")
    values.update(changes)
    return "Rendered 512x384 " + " ".join(f"{key}={value}" for key, value in values.items()) + "\n"


def run_record(seconds, warmup=False):
    return {"warmup": warmup, "requested_samples_total": 1000,
            "process": {"return_code": 0, "wall_seconds": seconds, "peak_process_resident_bytes": 123456, "error": None},
            "images": [{"name": "image.png", "bytes": 100, "sha256": "a" * 64}],
            "render": {"sampling_ms": 1, "reconstruction_ms": 2, "plotted": 900, "invalid_orbit_steps": 0, "workers": 1}}


def device(index=0, software=True, usable=True):
    return {"index": index, "id": f"device-{index}", "name": f"Device {index}", "software": software,
            "usable": usable, "api_version": "1.2", "driver_version": "1"}


class BenchmarkTests(unittest.TestCase):
    def test_render_counters_and_timings(self):
        value = bench.parse_render_summary(summary(), 512, 384, 1000, "cpu")
        self.assertEqual(value["plotted"], 900)
        self.assertEqual(value["sampling_ms"], 4.5)
        for change in ({"samples": "999"}, {"plotted": "1001"}, {"sampling_ms": "nan"},
                       {"sampling_ms": "inf"}, {"sampling_ms": "-1"}, {"elapsed_ms": "1.5"},
                       {"workers": "0"}, {"seed": "0"}, {"resumed": "1"}, {"alpha": "-1"}):
            with self.subTest(change=change), self.assertRaises(bench.BenchmarkError):
                bench.parse_render_summary(summary(**change), 512, 384, 1000, "cpu")
        for malformed in (summary() + summary(), summary().replace("samples=1000", ""),
                          summary().replace("samples=1000", "samples=1000 samples=1000")):
            with self.assertRaises(bench.BenchmarkError):
                bench.parse_render_summary(malformed, 512, 384, 1000, "cpu")

    def test_fallback_never_counts_as_gpu(self):
        selected = device()
        with self.assertRaises(bench.BenchmarkError):
            bench.parse_render_summary(summary(), 512, 384, 1000, "vulkan", selected)
        with self.assertRaises(bench.BenchmarkError):
            bench.parse_render_summary(summary(backend="vulkan", device='"Device 0 (software Vulkan)"') + "CPU fallback: unavailable\n",
                                       512, 384, 1000, "vulkan", selected)
        with self.assertRaises(bench.BenchmarkError):
            bench.parse_render_summary(summary(backend="vulkan", device='"Another device"'),
                                       512, 384, 1000, "vulkan", selected)
        self.assertEqual(bench.parse_render_summary(summary(backend="vulkan", workers="0", device='"Device 0 (software Vulkan)"'),
                                                   512, 384, 1000, "vulkan", selected)["backend"], "vulkan")

    def test_device_selection_requires_explicit_software(self):
        data = {"compiled": True, "loader_available": True, "devices": [device()]}
        with self.assertRaises(bench.BenchmarkError):
            bench.select_device(data)
        self.assertTrue(bench.select_device(data, "0")["software"])
        data["devices"].append(device(1, False))
        self.assertEqual(bench.select_device(data)["id"], "device-1")
        data["devices"][1]["usable"] = False
        with self.assertRaises(bench.BenchmarkError):
            bench.select_device(data, "1")
        data["loader_available"] = False
        with self.assertRaises(bench.BenchmarkError):
            bench.select_device(data, "0")

    def test_plans_enforce_budgets_and_request(self):
        case = bench.Case("cpu", "cpu", 1, 512, 384, 1000)
        text = "Render plan: width=512 height=384 samples=1000 format=png8 alpha=0 requested_backend=cpu host_bytes=100000 disk_bytes=200000 strip_rows=32 strips=12\n"
        self.assertEqual(bench.parse_plan(text, case)["strips"], 12)
        for invalid in (text.replace("host_bytes=100000", "host_bytes=999999999999"),
                        text.replace("disk_bytes=200000", "disk_bytes=999999999999"),
                        text.replace("width=512", "width=511"), text.replace("requested_backend=cpu", "requested_backend=vulkan")):
            with self.assertRaises(bench.BenchmarkError):
                bench.parse_plan(invalid, case)

    def test_warmup_exclusion_and_median(self):
        values = [run_record(100, True), run_record(3), run_record(1), run_record(2)]
        result = bench.aggregate_runs(values)
        self.assertEqual(result["wall_seconds_p50"], 2)
        self.assertEqual(result["wall_seconds_min"], 1)
        self.assertEqual(result["wall_seconds_max"], 3)
        self.assertEqual(result["requested_samples_per_wall_second"], 500)
        self.assertEqual(result["measured_repetitions"], 3)
        self.assertEqual(result["warmup_repetitions"], 1)

    def test_aggregation_refuses_bad_evidence(self):
        for value in (0, -1, float("nan"), float("inf"), True):
            with self.subTest(value=value), self.assertRaises(bench.BenchmarkError):
                bench.aggregate_runs([run_record(value)])
        values = [run_record(1), run_record(2)]
        values[1]["images"][0]["sha256"] = "b" * 64
        with self.assertRaises(bench.BenchmarkError):
            bench.aggregate_runs(values)
        values = [run_record(1)]
        values[0]["process"]["return_code"] = 1
        with self.assertRaises(bench.BenchmarkError):
            bench.aggregate_runs(values)
        with self.assertRaises(bench.BenchmarkError):
            bench.aggregate_runs([run_record(1, True)])

    def test_existing_output_is_preserved(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            existing = root / "existing"
            existing.mkdir()
            sentinel = existing / "keep.txt"
            sentinel.write_text("preserve")
            with self.assertRaises(bench.BenchmarkError):
                bench.create_report_directory(existing)
            self.assertEqual(sentinel.read_text(), "preserve")
            with self.assertRaises(bench.BenchmarkError):
                bench.create_report_directory(root / "missing" / "child")
            protected = root / "future" / "input.flame"
            with self.assertRaises(bench.BenchmarkError):
                bench.create_report_directory(root / "future", [protected])
            self.assertFalse((root / "future").exists())

    def test_symlink_output_ancestor_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "real").mkdir()
            try:
                (root / "link").symlink_to(root / "real", target_is_directory=True)
            except (OSError, NotImplementedError):
                self.skipTest("Symlink creation is unavailable to this test account")
            with self.assertRaises(bench.BenchmarkError):
                bench.create_report_directory(root / "link" / "run")
            self.assertFalse((root / "real" / "run").exists())

    def test_subprocess_arguments_remain_literal(self):
        with tempfile.TemporaryDirectory(prefix="apo bench ") as tmp:
            root = Path(tmp)
            literal = '; touch should-never-exist; $(echo injected) "quoted"'
            argv = [sys.executable, "-c", "import sys; print(sys.argv[1])", literal]
            result = bench.run_process(argv, root / "run", timeout=5)
            self.assertEqual(result["return_code"], 0)
            self.assertEqual((root / "run/stdout.txt").read_text().strip(), literal)
            self.assertFalse((root / "run/should-never-exist").exists())
            self.assertGreater(result["wall_seconds"], 0)
            self.assertFalse(json.loads((root / "run/command.json").read_text())["shell"])

    def test_timeout_produces_failure_record(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            with self.assertRaisesRegex(bench.BenchmarkError, "timeout"):
                bench.run_process([sys.executable, "-c", "import time; time.sleep(5)"], root / "run", timeout=0.05)
            process = json.loads((root / "run/process.json").read_text())
            self.assertNotEqual(process["return_code"], 0)
            self.assertIn("timeout", process["error"])

    @unittest.skipUnless(sys.platform.startswith("linux") and hasattr(os, "wait4"), "Linux wait4 per-child measurement")
    def test_memory_is_per_child_not_previous_peak(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            large = bench.run_process([sys.executable, "-c", "x=bytearray(48*1024*1024); print(len(x))"], root / "large", timeout=5)
            small = bench.run_process([sys.executable, "-c", "print('small')"], root / "small", timeout=5)
            self.assertGreater(large["peak_process_resident_bytes"], small["peak_process_resident_bytes"] + 16 * bench.MIB)
            self.assertIn("per-child", small["memory_measurement"])

    def test_thread_arguments_are_bounded(self):
        self.assertEqual(bench.parse_threads("4,1,2"), [1, 2, 4])
        for value in ("0", "65", "1,1", "1;exit", "", "1,2,3,4,5,6,7,8,9"):
            with self.assertRaises(bench.BenchmarkError):
                bench.parse_threads(value)

    def test_nonfinite_json_is_rejected(self):
        for text in ('{"value":NaN}', '{"value":Infinity}', '{"value":-Infinity}'):
            with self.assertRaises(bench.BenchmarkError):
                bench.parse_json(text)

    def test_sequence_checksums_and_actual_backend(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            case = bench.Case("sequence", "cpu", 1, 2, 2, 1000, format="png16", alpha=True, sequence=True)
            header = bench.PNG_SIGNATURE + struct.pack(">I", 13) + b"IHDR" + struct.pack(">II", 2, 2) + bytes((16, 6, 0, 0, 0)) + b"\0" * 40
            records = []
            for frame in range(3):
                name = f"frame_{frame:06}.png"
                (root / name).write_bytes(header)
                records.append(f'{frame} 123456 "{name}" "' + "a" * 64 + f'" 2 0 0 "" {len(header)} "{hashlib.sha256(header).hexdigest()}" "cpu" "CPU"')
            def write_manifest(lines):
                body = ("APO-SEQUENCE-1\n\"" + "b" * 64 + "\"\n3\n" + "\n".join(lines) + "\n").encode()
                path = root / "manifest.aposequence"
                path.write_bytes(body + b"SHA256 " + hashlib.sha256(body).hexdigest().encode() + b"\n")
                return path
            path = write_manifest(records)
            images, frames = bench.sequence_fingerprints(path, root, case, None)
            self.assertEqual(len(images), 3)
            self.assertEqual(frames[2]["frame"], 2)
            for changed in (records[0].replace('"cpu"', '"vulkan"'),
                            records[0].replace('frame_000000.png', '../outside.png'),
                            records[0].replace('" 2 0 0 ', '" 1 0 0 ')):
                path = write_manifest([changed, *records[1:]])
                with self.assertRaises(bench.BenchmarkError):
                    bench.sequence_fingerprints(path, root, case, None)
            path = write_manifest(records)
            path.write_bytes(path.read_bytes() + b"extra")
            with self.assertRaises(bench.BenchmarkError):
                bench.sequence_fingerprints(path, root, case, None)


if __name__ == "__main__":
    unittest.main()
