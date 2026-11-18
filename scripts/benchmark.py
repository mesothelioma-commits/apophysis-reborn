#!/usr/bin/env python3
"""Run bounded, reproducible renderer benchmarks without extra Python packages.

Each invocation creates a new private report directory. Existing destinations
are never reused. Rendering is measured in fresh CLI processes; validation and
hashing happen after the timer stops. See docs/BENCHMARKS.md for measurement scope.
"""
from __future__ import annotations

import argparse
import ctypes
from dataclasses import dataclass
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import re
import shlex
import statistics
import struct
import subprocess
import sys
import time
import uuid

ROOT = Path(__file__).resolve().parents[1]
SCHEMA = "apophysis-reborn.benchmark.v1"
MIB = 1024 * 1024
MAX_LOG_BYTES = 4 * MIB
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


class BenchmarkError(RuntimeError):
    pass


def require(condition, message):
    if not condition:
        raise BenchmarkError(message)


def is_link(path):
    try:
        info = path.lstat()
    except FileNotFoundError:
        return False
    return path.is_symlink() or bool(getattr(info, "st_file_attributes", 0) & 0x400)


def reject_links(path):
    path = Path(os.path.abspath(path))
    for item in (path, *path.parents):
        require(not is_link(item), "Symlink/junction/reparse-point path is not admitted: " + str(item))
    return path


def regular_file(path, label):
    path = reject_links(path)
    require(path.is_file(), f"{label} is not a regular file: {path}")
    return path


def digest(path):
    path = regular_file(path, "Hash input")
    before = path.stat()
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(MIB), b""):
            value.update(block)
    after = path.stat()
    require((before.st_dev, before.st_ino, before.st_size, before.st_mtime_ns) ==
            (after.st_dev, after.st_ino, after.st_size, after.st_mtime_ns),
            "File changed while being hashed: " + str(path))
    return value.hexdigest()


def create_report_directory(path, protected=()):
    path = reject_links(path)
    require(not path.exists(), "Output directory already exists; choose a new directory: " + str(path))
    require(path.parent.is_dir(), "Output parent directory must already exist: " + str(path.parent))
    for item in protected:
        item = Path(item).resolve()
        require(path != item and path not in item.parents,
                "Output directory overlaps an input: " + str(item))
    # An exclusive mkdir is the final admission check; no recursive deletion or
    # replacement of a preexisting directory is ever attempted.
    path.mkdir(mode=0o700, exist_ok=False)
    return path


def write_json(path, value):
    reject_links(path)
    temporary = path.with_name(path.name + ".new")
    reject_links(temporary)
    with temporary.open("x", encoding="utf-8", newline="\n") as stream:
        json.dump(value, stream, indent=2, ensure_ascii=False, allow_nan=False)
        stream.write("\n")
    os.replace(temporary, path)


def read_text(path):
    regular_file(path, "Process log")
    require(path.stat().st_size <= MAX_LOG_BYTES, "Process log exceeds the report size limit")
    return path.read_text(encoding="utf-8", errors="replace")


def parse_json(text):
    def invalid_constant(value):
        raise BenchmarkError("Nonfinite JSON number: " + value)
    return json.loads(text, parse_constant=invalid_constant)


def render_device_name(device):
    return device["name"] + (" (software Vulkan)" if device["software"] else "")


def windows_peak(handle):
    """Query the child's kernel peak working-set counter, when supported."""
    if os.name != "nt":
        return None
    from ctypes import wintypes
    class Counters(ctypes.Structure):
        _fields_ = [("cb", wintypes.DWORD), ("PageFaultCount", wintypes.DWORD)] + [
            (name, ctypes.c_size_t) for name in (
                "PeakWorkingSetSize", "WorkingSetSize", "QuotaPeakPagedPoolUsage",
                "QuotaPagedPoolUsage", "QuotaPeakNonPagedPoolUsage", "QuotaNonPagedPoolUsage",
                "PagefileUsage", "PeakPagefileUsage")]
    try:
        function = ctypes.WinDLL("psapi", use_last_error=True).GetProcessMemoryInfo
        function.argtypes = [wintypes.HANDLE, ctypes.POINTER(Counters), wintypes.DWORD]
        function.restype = wintypes.BOOL
        value = Counters()
        value.cb = ctypes.sizeof(value)
        if function(wintypes.HANDLE(int(handle)), ctypes.byref(value), value.cb):
            return int(value.PeakWorkingSetSize)
    except (AttributeError, OSError, ValueError):
        pass
    return None


def run_process(argv, directory, timeout=120.0, env=None):
    """Capture one child. Shell syntax is always passed as literal arguments.

    wait4 reaps exactly this POSIX child and provides its own resource record,
    avoiding cumulative RUSAGE_CHILDREN maxima from previous benchmark runs.
    """
    require(isinstance(argv, list) and argv and all(isinstance(arg, str) and "\0" not in arg for arg in argv),
            "Commands must be nonempty argument lists")
    require(math.isfinite(timeout) and timeout > 0, "Process timeout must be positive")
    directory.mkdir(mode=0o700, exist_ok=False)
    write_json(directory / "command.json", {"argv": argv, "shell": False})
    stdout, stderr = directory / "stdout.txt", directory / "stderr.txt"
    with stdout.open("xb") as out, stderr.open("xb") as err:
        start = time.perf_counter()
        child = subprocess.Popen(argv, stdin=subprocess.DEVNULL, stdout=out, stderr=err,
                                 shell=False, env=env, cwd=directory)
        usage, peak, final_windows_peak = None, None, None
        problem = None
        try:
            while True:
                if os.name == "posix" and hasattr(os, "wait4"):
                    pid, status, resource_usage = os.wait4(child.pid, os.WNOHANG)
                    if pid:
                        child.returncode = os.waitstatus_to_exitcode(status)
                        usage = resource_usage
                        break
                else:
                    measured = windows_peak(getattr(child, "_handle", 0))
                    if measured is not None:
                        peak = max(peak or 0, measured)
                    if child.poll() is not None:
                        measured = windows_peak(getattr(child, "_handle", 0))
                        if measured is not None and measured > 0:
                            final_windows_peak = measured
                            peak = max(peak or 0, measured)
                        break
                if time.perf_counter() - start > timeout:
                    problem = "Process exceeded its timeout"
                    break
                if stdout.stat().st_size > MAX_LOG_BYTES or stderr.stat().st_size > MAX_LOG_BYTES:
                    problem = "Process output exceeded the bounded log limit"
                    break
                time.sleep(0.005)
        finally:
            if child.returncode is None:
                child.kill()
                if os.name == "posix" and hasattr(os, "wait4"):
                    _, status, usage = os.wait4(child.pid, 0)
                    child.returncode = os.waitstatus_to_exitcode(status)
                else:
                    child.wait()
            elapsed = time.perf_counter() - start
    for log in (stdout, stderr):
        if log.stat().st_size > MAX_LOG_BYTES:
            problem = "Process output exceeded the bounded log limit; retained logs are truncated"
            with log.open("r+b") as stream:
                stream.truncate(MAX_LOG_BYTES)
    memory_method = None
    if usage is not None and sys.platform.startswith("linux"):
        peak = int(usage.ru_maxrss) * 1024
        memory_method = "Linux wait4 per-child ru_maxrss, KiB converted to bytes"
    elif usage is not None and sys.platform == "darwin":
        peak = int(usage.ru_maxrss)
        memory_method = "Darwin wait4 per-child ru_maxrss, bytes"
    elif peak is not None:
        memory_method = "Windows GetProcessMemoryInfo PeakWorkingSetSize, bytes"
    if peak is not None and peak <= 0:
        peak, memory_method = None, None
    result = {"argv": argv, "return_code": child.returncode, "wall_seconds": elapsed,
              "peak_process_resident_bytes": peak, "memory_measurement": memory_method,
              "memory_peak_complete_lifetime": peak is not None and (usage is not None or final_windows_peak is not None),
              "memory_unavailable_reason": None if peak is not None else "No supported per-child resident-memory counter available",
              "user_cpu_seconds": float(usage.ru_utime) if usage is not None else None,
              "system_cpu_seconds": float(usage.ru_stime) if usage is not None else None,
              "stdout_file": "stdout.txt", "stderr_file": "stderr.txt", "error": problem}
    write_json(directory / "process.json", result)
    require(problem is None, problem or "Process failed")
    return result


def succeeded(result, label):
    require(result["return_code"] == 0, f"{label} exited {result['return_code']}; inspect its stdout.txt and stderr.txt")


def nonnegative(value, label, integer=False):
    if integer:
        require(isinstance(value, str) and re.fullmatch(r"[0-9]+", value) is not None, "Invalid integer " + label)
        result = int(value)
    else:
        try:
            result = float(value)
        except (TypeError, ValueError):
            raise BenchmarkError("Invalid number " + label) from None
        require(math.isfinite(result), "Nonfinite measurement " + label)
    require(result >= 0, "Negative measurement " + label)
    return result


def fields(line, prefix):
    require(line.startswith(prefix), "Unexpected CLI summary")
    result = {}
    try:
        tokens = shlex.split(line[len(prefix):])
    except ValueError as error:
        raise BenchmarkError("Malformed quoted CLI summary") from error
    for token in tokens:
        require("=" in token, "Malformed CLI summary token")
        name, value = token.split("=", 1)
        require(name and name not in result, "Duplicate CLI summary field: " + name)
        result[name] = value
    return result


def parse_render_summary(text, width, height, samples, backend, device=None):
    lines = [line for line in text.splitlines() if line.startswith("Rendered ")]
    require(len(lines) == 1, "Expected exactly one completed render summary")
    match = re.fullmatch(r"Rendered ([0-9]+)x([0-9]+) (.*)", lines[0])
    require(match is not None and (int(match[1]), int(match[2])) == (width, height),
            "Completed render dimensions differ from the benchmark")
    data = fields(match[3], "")
    required = {"samples", "plotted", "invalid_orbit_steps", "workers", "seed", "elapsed_ms",
                "backend", "device", "sampling_ms", "reconstruction_ms", "format", "alpha", "resumed"}
    require(set(data) == required, "Missing or unexpected render-summary fields")
    for key in ("samples", "plotted", "invalid_orbit_steps", "workers", "seed", "elapsed_ms", "alpha", "resumed"):
        data[key] = nonnegative(data[key], key, integer=True)
    for key in ("sampling_ms", "reconstruction_ms"):
        data[key] = nonnegative(data[key], key)
    require(data["samples"] == samples and data["seed"] == 123456 and (backend != "cpu" or data["workers"] > 0) and
            data["plotted"] <= samples and data["invalid_orbit_steps"] <= samples and data["resumed"] == 0,
            "Completed counters/settings differ from the fresh benchmark request")
    require(data["backend"] == backend and "CPU fallback:" not in text,
            "A backend fallback cannot count as the requested benchmark")
    if backend == "vulkan":
        require(device is not None and data["device"] == render_device_name(device),
                "Completed Vulkan device does not match the selected device")
    data["width"], data["height"] = width, height
    return data


def parse_plan(text, case):
    lines = [line for line in text.splitlines() if line.startswith("Render plan: ")]
    require(len(lines) == 1, "Expected exactly one render plan")
    data = fields(lines[0], "Render plan: ")
    for key in ("width", "height", "samples", "alpha", "host_bytes", "disk_bytes", "strip_rows", "strips"):
        require(key in data, "Render plan omitted " + key)
        data[key] = nonnegative(data[key], key, integer=True)
    require((data["width"], data["height"], data["samples"]) == (case.width, case.height, case.samples),
            "Plan dimensions/sample count differ")
    require(data["host_bytes"] <= case.memory_mib * MIB and data["disk_bytes"] <= case.disk_mib * MIB,
            "Admitted render plan exceeds its stated budgets")
    require(data.get("requested_backend") == case.backend and data.get("format") == case.format and
            data["alpha"] == int(case.alpha) and data["strips"] > 0 and data["strip_rows"] > 0,
            "Render plan omitted or changed the requested format/backend/strips")
    return data


def select_device(report, selection=None):
    require(isinstance(report, dict) and isinstance(report.get("devices"), list), "Invalid Vulkan discovery report")
    require(report.get("compiled") is True and report.get("loader_available") is True,
            "Vulkan is not compiled/available in this renderer; use --backend cpu")
    usable = []
    for item in report["devices"]:
        require(isinstance(item, dict) and isinstance(item.get("software"), bool) and
                isinstance(item.get("usable"), bool) and isinstance(item.get("id"), str) and
                isinstance(item.get("name"), str) and type(item.get("index")) is int,
                "Malformed Vulkan device identity")
        if item["usable"]:
            usable.append(item)
    if selection is not None:
        matches = [item for item in usable if item["id"] == selection or str(item["index"]) == selection]
        require(len(matches) == 1, "The explicit Vulkan device is missing, unusable or ambiguous")
        return matches[0]
    physical = [item for item in usable if not item["software"]]
    require(physical, "No usable physical Vulkan device; explicitly choose --device ID to benchmark software Vulkan")
    return physical[0]


def image_fingerprint(path, width, height, depth, alpha):
    path = regular_file(path, "Rendered image")
    with path.open("rb") as stream:
        header = stream.read(33)
    require(len(header) == 33 and header[:8] == PNG_SIGNATURE and header[12:16] == b"IHDR" and
            struct.unpack(">II", header[16:24]) == (width, height) and
            header[24:26] == bytes((depth, 6 if alpha else 2)) and path.stat().st_size > 57,
            "Rendered PNG header does not match its benchmark case")
    return {"name": path.name, "bytes": path.stat().st_size, "sha256": digest(path),
            "width": width, "height": height, "bit_depth": depth, "channels": 4 if alpha else 3}


def sequence_fingerprints(manifest, directory, case, device):
    raw = regular_file(manifest, "Sequence manifest").read_bytes()
    require(len(raw) <= MIB, "Benchmark sequence manifest is too large")
    body, marker, checksum = raw.rpartition(b"SHA256 ")
    require(marker and checksum.endswith(b"\n") and len(checksum) == 65 and
            hashlib.sha256(body).hexdigest().encode() == checksum[:-1], "Sequence manifest checksum mismatch")
    lines = body.decode("utf-8").splitlines()
    require(len(lines) == 6 and lines[0] == "APO-SEQUENCE-1" and lines[2] == "3",
            "Unexpected sequence manifest schema/count")
    images, records = [], []
    for expected_frame, line in enumerate(lines[3:]):
        values = shlex.split(line)
        require(len(values) == 12 and values[0] == str(expected_frame) and values[1] == "123456" and
                values[2] == f"frame_{expected_frame:06}.png" and values[4] == "2" and
                values[5:8] == ["0", "0", ""] and values[10] == case.backend,
                "Sequence frame identity, publication or actual backend mismatch")
        if case.backend == "vulkan":
            require(device is not None and values[11] == render_device_name(device), "Sequence used another Vulkan device")
        require(re.fullmatch(r"[0-9a-f]{64}", values[3]) is not None, "Invalid frame model digest")
        image = image_fingerprint(directory / values[2], case.width, case.height, 16, True)
        require(str(image["bytes"]) == values[8] and image["sha256"] == values[9],
                "Published frame differs from its durable manifest")
        images.append(image)
        records.append({"frame": expected_frame, "model_sha256": values[3],
                        "backend": values[10], "device": values[11]})
    return images, records


@dataclass(frozen=True)
class Case:
    name: str
    backend: str
    workers: int
    width: int
    height: int
    samples: int
    format: str = "png8"
    alpha: bool = False
    memory_mib: int = 128
    disk_mib: int = 1024
    strip_rows: int = 32
    sequence: bool = False
    large: bool = False


def case_arguments(exe, fixture, clip, output, case, device, gpu_memory):
    common = ["--samples", str(case.samples), "--threads", str(case.workers),
              "--backend", case.backend, "--memory-mib", str(case.memory_mib),
              "--disk-mib", str(case.disk_mib), "--strip-rows", str(case.strip_rows),
              "--format", case.format]
    if case.alpha:
        common.append("--alpha")
    if case.backend == "vulkan":
        require(device is not None, "A Vulkan case needs a selected device")
        common += ["--device", device["id"], "--gpu-memory-mib", str(gpu_memory),
                   "--no-cpu-fallback" if case.sequence else "--no-fallback"]
    if case.sequence:
        return [str(exe), "animation", "render", str(clip), "--output-dir", str(output),
                "--prefix", "frame", "--first", "0", "--last", "2", "--width", str(case.width),
                "--height", str(case.height), *common]
    return [str(exe), "--flame", str(fixture), "--output", str(output), "--seed", "123456",
            "--size", str(case.width), str(case.height), "--oversample", "1", "--filter-radius", "0.5", *common]


def aggregate_runs(runs):
    measured = [run for run in runs if not run["warmup"]]
    require(measured, "No measured repetitions")
    for run in runs:
        require(run["process"]["return_code"] == 0 and not run["process"].get("error"), "Cannot aggregate failed runs")
        value = run["process"]["wall_seconds"]
        require(type(value) in (int, float) and math.isfinite(value) and value > 0, "Invalid elapsed measurement")
    signatures = {tuple((image["name"], image["bytes"], image["sha256"]) for image in run["images"]) for run in runs}
    require(len(signatures) == 1, "Repeated identical inputs produced different encoded images")
    values = [run["process"]["wall_seconds"] for run in measured]
    peaks = [run["process"]["peak_process_resident_bytes"] for run in measured
             if run["process"]["peak_process_resident_bytes"] is not None]
    median = statistics.median(values)
    result = {"measured_repetitions": len(measured), "warmup_repetitions": len(runs) - len(measured),
              "wall_seconds_min": min(values), "wall_seconds_p50": median,
              "wall_seconds_max": max(values), "wall_seconds_mean": statistics.mean(values),
              "peak_process_resident_bytes_max": max(peaks) if peaks else None,
              "memory_measurements_available": len(peaks), "repeatability_checked": len(runs) > 1,
              "encoded_outputs_identical": True if len(runs) > 1 else None,
              "image_sha256": [image["sha256"] for image in measured[0]["images"]]}
    samples = measured[0]["requested_samples_total"]
    result["requested_samples_per_wall_second"] = samples / median
    result["frames_per_wall_second"] = len(measured[0]["images"]) / median
    counters = [run.get("render") for run in measured]
    if all(counters):
        result["sampling_ms_p50"] = statistics.median(item["sampling_ms"] for item in counters)
        result["reconstruction_ms_p50"] = statistics.median(item["reconstruction_ms"] for item in counters)
        result["plotted_samples"] = counters[0]["plotted"]
        result["invalid_orbit_steps"] = counters[0]["invalid_orbit_steps"]
        result["actual_workers"] = sorted({item["workers"] for item in counters})
    return result


def default_threads():
    count = os.cpu_count() or 1
    return sorted({1, min(2, count), min(4, count)})


def parse_threads(text):
    require(re.fullmatch(r"[0-9]+(?:,[0-9]+)*", text or "") is not None, "--threads requires a comma-separated list of positive integers")
    result = [int(item) for item in text.split(",")]
    require(len(result) <= 8 and len(set(result)) == len(result) and all(1 <= item <= 64 for item in result),
            "--threads admits at most eight distinct values in 1..64")
    return sorted(result)


def cases_for(options):
    cases = []
    backends = ["cpu", "vulkan"] if options.backend == "both" else [options.backend]
    for backend in backends:
        workers = options.threads if backend == "cpu" else [1]
        cases += [Case(f"{backend}_sampling_t{count}", backend, count, 512, 384, options.samples) for count in workers]
        count = max(workers)
        cases.append(Case(f"{backend}_stream_2048", backend, count, 2048, 2048, options.samples,
                          format="png16", alpha=True))
        cases.append(Case(f"{backend}_animation_3", backend, count, 320, 240,
                          max(1, min(262144, options.samples // 8)), format="png16", alpha=True, sequence=True))
        if options.large_16k:
            cases.append(Case(f"{backend}_stream_16k", backend, count, 16384, 16384, options.samples,
                              format="png16", alpha=True, disk_mib=8192, large=True))
    return cases


def host_information():
    cpu = platform.processor()
    if sys.platform.startswith("linux"):
        try:
            for line in Path("/proc/cpuinfo").read_text().splitlines():
                if line.startswith("model name") and ":" in line:
                    cpu = line.split(":", 1)[1].strip()
                    break
        except OSError:
            pass
    limits = {}
    if sys.platform.startswith("linux"):
        for name in ("cpu.max", "memory.max"):
            try:
                value = (Path("/sys/fs/cgroup") / name).read_text().strip()
                if re.fullmatch(r"(?:max|[0-9]+)(?: [0-9]+)?", value):
                    limits[name] = value
            except OSError:
                pass
    return {"system": platform.system(), "release": platform.release(), "machine": platform.machine(),
            "cpu_model": cpu or None, "logical_processors": os.cpu_count(), "python_version": platform.python_version(),
            "python_pointer_bits": struct.calcsize("P") * 8,
            "affinity_processors": len(os.sched_getaffinity(0)) if hasattr(os, "sched_getaffinity") else None,
            "cgroup_v2_limits": limits,
            "load_average_at_start": list(os.getloadavg()) if hasattr(os, "getloadavg") else None,
            "vulkan_validation_requested": "VK_LAYER_KHRONOS_validation" in os.environ.get("VK_INSTANCE_LAYERS", ""),
            "timing_clock": "time.perf_counter; fresh process startup through process completion"}


def default_executable():
    for relative in ("bin/windows-x64/flame-cpu.exe", "build/flame-cpu", "build/Release/flame-cpu.exe", "build/flame-cpu.exe"):
        path = ROOT / relative
        if path.is_file():
            return path
    raise BenchmarkError("No built CLI found; pass --exe with the path to flame-cpu or flame-cpu.exe")


def report_text(report):
    lines = ["Apophysis Reborn renderer benchmark", f"Result: {report['result']}",
             f"Recorded UTC: {report['recorded_at_utc']}", f"Renderer SHA256: {report['renderer']['sha256']}",
             f"Host: {report['host']['system']} {report['host']['machine']}",
             "Timing includes process startup, sampling, reconstruction, encoding and filesystem work.",
             "Warmups are excluded; timings are observations, not speed pass/fail thresholds.",
             "Resident memory covers the CLI process, not GPU VRAM or only renderer buffers.", ""]
    for case in report["cases"]:
        summary = case.get("summary")
        if summary:
            rss = summary["peak_process_resident_bytes_max"]
            memory = f"{rss / MIB:.2f} MiB" if rss is not None else "unavailable"
            stability = "yes" if summary["repeatability_checked"] else "not assessed (one run)"
            lines += [case["name"],
                      f"  backend: {case['backend']}; requested workers: {case['workers']}; device class: {case['device_class']}",
                      f"  median wall: {summary['wall_seconds_p50']:.6f} s; range: {summary['wall_seconds_min']:.6f}..{summary['wall_seconds_max']:.6f} s",
                      f"  repeats: {summary['measured_repetitions']}; warmups: {summary['warmup_repetitions']}; peak process resident memory: {memory}",
                      f"  requested samples / wall second: {summary['requested_samples_per_wall_second']:.2f}; output hashes stable: {stability}", ""]
        else:
            lines += [case["name"] + ": incomplete", ""]
    if report.get("cpu_thread_scaling"):
        lines += ["CPU scaling (same encoded images; median total-process time):"]
        for item in report["cpu_thread_scaling"]:
            lines += [f"  requested {item['workers']} workers: {item['relative_to_one_worker']:.3f}x relative to one worker"]
    if report.get("error"):
        lines += ["", "ERROR: " + report["error"]]
    lines += ["", "No physical GPU acceptance is inferred from software Vulkan.",
              "These benchmark images have bounded sample counts and are not finished-quality artwork.",
              "See report.json and per-run stdout.txt/stderr.txt/process.json for evidence."]
    return "\n".join(lines) + "\n"


def save_report(directory, report):
    write_json(directory / "report.json", report)
    text = directory / "report.txt"
    reject_links(text)
    text.write_text(report_text(report), encoding="utf-8", newline="\n")


def open_report(path):
    # Keep this optional and do not let a shell or path expansion interpret the
    # report filename. Windows Explorer selects the actual report file.
    if os.name == "nt":
        try:
            subprocess.Popen(["explorer.exe", "/select,", str(path)], shell=False,
                             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        except OSError:
            pass


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, help="Built native flame-cpu executable")
    parser.add_argument("--output-dir", type=Path, help="New output directory; its parent must exist")
    parser.add_argument("--backend", choices=("cpu", "vulkan", "both"), default="cpu")
    parser.add_argument("--device", help="Explicit usable Vulkan ID/index; required for software Vulkan")
    parser.add_argument("--samples", type=int, default=2097152)
    parser.add_argument("--threads", help="CPU worker counts, e.g. 1,2,4")
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--warmups", type=int, default=1)
    parser.add_argument("--gpu-memory-mib", type=int, default=256)
    parser.add_argument("--timeout", type=int, default=120, help="Timeout per ordinary child in seconds; 16K gets at least 600")
    parser.add_argument("--large-16k", action="store_true", help="Add one measured 16K run per backend, without warmups")
    parser.add_argument("--keep-images", action="store_true", help="Retain rendered images; default retains hashes and removes only owned images")
    parser.add_argument("--no-open", action="store_true", help="Do not select the report in Windows Explorer")
    options = parser.parse_args(argv)
    report, directory = None, None
    try:
        require(1 <= options.samples <= 100000000, "--samples must be in 1..100000000")
        require(1 <= options.repeats <= 10 and 0 <= options.warmups <= 3, "Use 1..10 repeats and 0..3 warmups")
        require(1 <= options.timeout <= 3600 and 16 <= options.gpu_memory_mib <= 65536, "Invalid timeout or GPU memory budget")
        require(options.backend != "cpu" or options.device is None, "--device requires --backend vulkan or both")
        options.threads = parse_threads(options.threads) if options.threads else default_threads()
        exe = regular_file(options.exe or default_executable(), "Renderer")
        fixture = regular_file(ROOT / "benchmarks/ember-v1.flame", "Benchmark fixture")
        if options.output_dir is None:
            parent = reject_links(ROOT / "out")
            parent.mkdir(mode=0o700, exist_ok=True)
            target = parent / ("benchmark-" + datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%S") + "-" + uuid.uuid4().hex[:8])
        else:
            target = options.output_dir
        directory = create_report_directory(target, (exe, fixture, Path(__file__)))
        report = {"schema": SCHEMA, "profile_version": 1, "result": "INCOMPLETE",
                  "recorded_at_utc": datetime.now(timezone.utc).isoformat(), "host": host_information(),
                  "renderer": {"path": str(exe), "sha256": digest(exe)},
                  "fixture": {"name": fixture.name, "sha256": digest(fixture)},
                  "configuration": {key: value for key, value in vars(options).items() if key not in {"exe", "output_dir"}},
                  "cases": [], "cpu_thread_scaling": [], "error": None,
                  "measurement_scope": "Fresh CLI process per run; no concurrent benchmark children; warmups excluded; hashing outside timed interval"}
        save_report(directory, report)
        probe = run_process([str(exe), "--build-info", "--json"], directory / "build-info", options.timeout)
        if probe["return_code"] == 0:
            info = parse_json(read_text(directory / "build-info/stdout.txt"))
            require(isinstance(info, dict) and isinstance(info.get("version"), str), "Invalid --build-info JSON")
            report["renderer"]["build_info"] = info
        else:
            report["renderer"]["build_info"] = None
            report["renderer"]["build_info_unavailable_reason"] = "Renderer did not provide --build-info --json; see probe logs"
        discovery = run_process([str(exe), "--list-devices", "--json"], directory / "device-discovery", options.timeout)
        succeeded(discovery, "Device discovery")
        devices = parse_json(read_text(directory / "device-discovery/stdout.txt"))
        require(isinstance(devices, dict) and isinstance(devices.get("devices"), list), "Invalid device inventory")
        report["device_inventory"] = devices
        device = select_device(devices, options.device) if options.backend != "cpu" else None
        report["selected_vulkan_device"] = device
        clip = directory / "orbit.apoproject"
        create = run_process([str(exe), "animation", "create", str(fixture), "--preset", "camera-orbit",
                              "--frames", "3", "--fps", "24", "--seed", "123456", "--output", str(clip)],
                             directory / "create-animation", options.timeout)
        succeeded(create, "Benchmark clip creation")
        report["animation_source_sha256"] = digest(clip)
        case_list = cases_for(options)
        # Plan every case before the first measured render. Plans and clip
        # creation are retained as evidence but excluded from benchmark time.
        for case in case_list:
            case_dir = directory / case.name
            case_dir.mkdir(mode=0o700)
            output = case_dir / "estimate-images" if case.sequence else case_dir / "estimate.png"
            if case.sequence:
                output.mkdir(mode=0o700)
            command = case_arguments(exe, fixture, clip, output, case, device, options.gpu_memory_mib)
            plan_result = run_process(command + ["--estimate"], case_dir / "plan", options.timeout)
            succeeded(plan_result, "Benchmark preflight")
            entry = {**case.__dict__, "device_class": ("software Vulkan" if device["software"] else "physical Vulkan") if case.backend == "vulkan" else "CPU reference",
                     "plan": None if case.sequence else parse_plan(read_text(case_dir / "plan/stdout.txt"), case), "runs": []}
            if case.sequence:
                require(not any(output.iterdir()), "Sequence estimate created rendering state")
            else:
                require(not output.exists(), "Render estimate published an image")
            report["cases"].append(entry)
        save_report(directory, report)
        for case, entry in zip(case_list, report["cases"]):
            count = 1 if case.large else options.repeats + options.warmups
            warmups = 0 if case.large else options.warmups
            for index in range(count):
                warmup = index < warmups
                label = ("warmup" if warmup else "run") + f"-{index + 1:02}"
                run_dir = directory / case.name / label
                output = run_dir / "images" if case.sequence else run_dir / "image.png"
                # run_process owns the run directory exclusively; sequence
                # outputs need an existing directory before the child starts.
                command = case_arguments(exe, fixture, clip, output, case, device, options.gpu_memory_mib)
                if case.sequence:
                    output = directory / case.name / (label + "-images")
                    output.mkdir(mode=0o700)
                    command = case_arguments(exe, fixture, clip, output, case, device, options.gpu_memory_mib)
                print(f"{case.name}: {label}", flush=True)
                process = run_process(command, run_dir, max(options.timeout, 600) if case.large else options.timeout)
                succeeded(process, case.name)
                run = {"warmup": warmup, "directory": run_dir.relative_to(directory).as_posix(),
                       "process": process, "requested_samples_total": case.samples * (3 if case.sequence else 1)}
                if case.sequence:
                    images, frame_records = sequence_fingerprints(output / ".apo-sequence-frame/manifest.aposequence", output, case, device)
                    run.update({"images": images, "frames": frame_records, "render": None,
                                "counter_scope": "Sequence manifest verifies published frames/backend; per-frame plotted counters are not exported"})
                else:
                    summary = parse_render_summary(read_text(run_dir / "stdout.txt"), case.width, case.height,
                                                   case.samples, case.backend, device)
                    require(summary["format"] == case.format and summary["alpha"] == int(case.alpha), "Completed image format differs")
                    run.update({"render": summary, "images": [image_fingerprint(output, case.width, case.height,
                                                                                 16 if case.format == "png16" else 8, case.alpha)]})
                entry["runs"].append(run)
                # Retain durable sequence manifests and command logs. Only known
                # regular image files produced in this private run are removed.
                if not options.keep_images:
                    for image in run["images"]:
                        owned = (output / image["name"]) if case.sequence else output
                        regular_file(owned, "Benchmark-owned image").unlink()
                save_report(directory, report)
            entry["summary"] = aggregate_runs(entry["runs"])
            save_report(directory, report)
        cpu = [entry for entry in report["cases"] if entry["name"].startswith("cpu_sampling_t")]
        baseline = next((entry for entry in cpu if entry["workers"] == 1), None)
        if baseline:
            for entry in cpu:
                require(entry["summary"]["image_sha256"] == baseline["summary"]["image_sha256"],
                        "CPU worker-count variants produced different encoded images")
                report["cpu_thread_scaling"].append({"workers": entry["workers"],
                    "relative_to_one_worker": baseline["summary"]["wall_seconds_p50"] / entry["summary"]["wall_seconds_p50"]})
        require(digest(exe) == report["renderer"]["sha256"] and digest(fixture) == report["fixture"]["sha256"] and
                digest(clip) == report["animation_source_sha256"], "Benchmark executable or source changed during execution")
        if device:
            final = run_process([str(exe), "--list-devices", "--json"], directory / "device-discovery-after", options.timeout)
            succeeded(final, "Final device discovery")
            final_device = select_device(parse_json(read_text(directory / "device-discovery-after/stdout.txt")), device["id"])
            require(final_device == device, "Selected Vulkan device/driver identity changed during the benchmark")
        report["result"] = "PASS"
        report["completed_at_utc"] = datetime.now(timezone.utc).isoformat()
        save_report(directory, report)
        print(report_text(report), end="")
        print("Report:", directory / "report.txt")
        if not options.no_open:
            open_report(directory / "report.txt")
        return 0
    except (BenchmarkError, OSError, ValueError, KeyboardInterrupt) as error:
        message = str(error) or "Interrupted by the user"
        if report is not None and directory is not None:
            report["result"], report["error"] = "FAIL", message
            try:
                save_report(directory, report)
                print("Failure report:", directory / "report.txt", file=sys.stderr)
            except (BenchmarkError, OSError, ValueError) as save_error:
                print("Could not finish the failure report:", save_error, file=sys.stderr)
        print("Benchmark failed:", message, file=sys.stderr)
        return 130 if isinstance(error, KeyboardInterrupt) else 1


if __name__ == "__main__":
    raise SystemExit(main())
