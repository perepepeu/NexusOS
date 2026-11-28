"""Valida o artefato final com no maximo duas VMs em paralelo."""
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path
import hashlib
import json
import subprocess
import sys

suites = [
    ("storage", ["scripts/check-storage.py"]),
    ("diagnostics", ["scripts/check-kernel.py", "--diagnostics"]),
    ("scheduler", ["scripts/check-kernel.py", "--scheduler"]),
    ("user", ["scripts/check-kernel.py", "--user", "--quick"]),
    ("syscalls", ["scripts/check-kernel.py", "--syscalls", "--quick"]),
    ("elf", ["scripts/check-kernel.py", "--elf", "--quick"]),
    ("processes", ["scripts/check-kernel.py", "--processes", "--quick"]),
    ("memtest", ["scripts/check-kernel.py", "--memtest", "--quick"]),
    ("normal", ["scripts/check-kernel.py"]),
    ("terminal", ["scripts/check-kernel.py", "--terminal", "--quick"]),
    ("power", ["scripts/check-power.py"]),
]
report = Path("build/release-check.json")
Path("build").mkdir(exist_ok=True)
# Um resultado antigo nao pode ser confundido com a validacao deste build.
if report.exists(): report.unlink()
image = Path("build/nexusos.iso")
image_hash = hashlib.sha256(image.read_bytes()).hexdigest()
version = Path("VERSION").read_text().strip()

def run_suite(item):
    name, args = item
    log = Path(f"build/release-check-{name}.log")
    print(f"INICIANDO: {name}; log: {log}", flush=True)
    with log.open("wb") as output:
        result = subprocess.run([sys.executable, *args], stdout=output, stderr=subprocess.STDOUT)
    if result.returncode:
        raise RuntimeError(f"Suite {name} falhou; consulte {log}")
    print(f"OK: release/{name}", flush=True)
    return {"suite": name, "command": args, "log": str(log), "passed": True}

results = []
with ThreadPoolExecutor(max_workers=2) as pool:
    pending = [pool.submit(run_suite, item) for item in suites]
    try:
        for future in as_completed(pending): results.append(future.result())
    except Exception:
        for future in pending: future.cancel()
        raise
if hashlib.sha256(image.read_bytes()).hexdigest() != image_hash or Path("VERSION").read_text().strip() != version:
    raise RuntimeError("ISO/VERSION alterados durante a validacao")
report.write_text(json.dumps({"version": version, "iso_sha256": image_hash,
    "suites": sorted(results, key=lambda item: item["suite"])}, indent=2) + "\n")
print(f"OK: release {version}; SHA256 da ISO: {image_hash}; resultado: {report}", flush=True)
