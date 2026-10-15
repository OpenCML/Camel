#!/usr/bin/env python3
"""Wall time and peak resident size of a command.

    python3 docs/design/cpp-memory/tools/measure.py [--rounds N] [--timeout S] [--label L] -- cmd args...

Prints one line: label, exit code, median wall time [min, max] in seconds, peak RSS in MB
(ru_maxrss of the child), and the first line of stdout. The container has no /usr/bin/time.
"""
import os
import resource
import statistics
import subprocess
import sys
import time


def main():
    argv = sys.argv[1:]
    rounds, timeout, label = 1, 300.0, None
    while argv and argv[0] != "--":
        if argv[0] == "--rounds":
            rounds = int(argv[1]); argv = argv[2:]
        elif argv[0] == "--timeout":
            timeout = float(argv[1]); argv = argv[2:]
        elif argv[0] == "--label":
            label = argv[1]; argv = argv[2:]
        else:
            break
    if argv and argv[0] == "--":
        argv = argv[1:]
    label = label or " ".join(argv)
    times, rss, code, first = [], 0, None, ""
    for _ in range(rounds):
        r, w = os.pipe()
        t0 = time.monotonic()
        pid = os.fork()
        if pid == 0:
            os.close(r)
            p = subprocess.Popen(argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            try:
                out, err = p.communicate(timeout=timeout)
                rc = p.returncode
            except subprocess.TimeoutExpired:
                p.kill(); out, err = p.communicate(); rc = 124
            ru = resource.getrusage(resource.RUSAGE_CHILDREN)
            text = (out or err or b"").decode("utf-8", "replace").strip().splitlines()
            line = text[0][:160] if text else ""
            os.write(w, ("%d\t%d\t%s" % (rc, ru.ru_maxrss, line)).encode())
            os._exit(0)
        os.close(w)
        data = b""
        while True:
            chunk = os.read(r, 65536)
            if not chunk:
                break
            data += chunk
        os.close(r)
        os.waitpid(pid, 0)
        times.append(time.monotonic() - t0)
        rc, kb, line = data.decode().split("\t", 2)
        code, first = int(rc), line
        rss = max(rss, int(kb))
    print("%-44s exit=%-4d %7.3f s [%.3f, %.3f]  rss=%6.1f MB  | %s"
          % (label, code, statistics.median(times), min(times), max(times), rss / 1024.0, first))


if __name__ == "__main__":
    main()
