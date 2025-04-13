import os
import time
import threading
import subprocess
from pathlib import Path

FUZZER_CMD = [
    "/usr/bin/python3",
    "infra/helper.py",
    "run_fuzzer",
    "mupdf",
    "xps_fuzzer",
    "--corpus-dir",
    "build/out/mupdf_xps2"
]

TIMEOUT_DIR = Path("build/out/mupdf")
POLL_INTERVAL = 5  # seconds

# Event to signal a restart
restart_event = threading.Event()
restart_event.set()

def monitor_timeouts():
    print("[monitor] Started monitoring for timeout files...")
    seen_count = 0
    while True:
        current_files = list(TIMEOUT_DIR.glob("timeout-*"))
        current_count = len(current_files)

        if current_count > seen_count:
            print(f"[monitor] Detected new timeout files: {current_count} > {seen_count}")
            seen_count = current_count
            restart_event.set()

        time.sleep(POLL_INTERVAL)

def run_fuzzer():
    while True:
        print("[fuzzer] Starting fuzzer...")
        proc = subprocess.Popen(FUZZER_CMD)
        restart_event.clear()

        while True:
            if restart_event.is_set():
                print("[fuzzer] Restart signal received. Killing fuzzer...")
                proc.kill()
                proc.wait()
                restart_event.clear()
                break

            # Check if the process exited
            retcode = proc.poll()
            if retcode is not None:
                print(f"[fuzzer] Fuzzer exited with code {retcode}. Restarting...")
                break

            time.sleep(1)

if __name__ == "__main__":
    os.makedirs(TIMEOUT_DIR, exist_ok=True)

    monitor_thread = threading.Thread(target=monitor_timeouts, daemon=True)
    fuzzer_thread = threading.Thread(target=run_fuzzer)

    monitor_thread.start()
    fuzzer_thread.start()

    fuzzer_thread.join()

