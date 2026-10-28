# Copyright (C) 2026 iceman50
"""Allocate build numbers locally, independently of the Git revision."""

import os
from pathlib import Path


def next_build_number(counter):
    counter = Path(counter)
    # Separate build configurations can run concurrently. Keep the lock outside
    # build/ too, and replace the counter atomically while holding it.
    with counter.with_suffix(".lock").open("a+b") as lock:
        if os.name == "nt":
            import msvcrt

            lock.seek(0)
            msvcrt.locking(lock.fileno(), msvcrt.LK_LOCK, 1)
        else:
            import fcntl

            fcntl.flock(lock.fileno(), fcntl.LOCK_EX)
        try:
            previous = int(counter.read_text(encoding="ascii").strip()) if counter.exists() else 0
            if previous < 0:
                raise ValueError("Local build number must not be negative")
            number = previous + 1
            temporary = counter.with_suffix(".tmp")
            temporary.write_text(str(number) + "\n", encoding="ascii")
            temporary.replace(counter)
            return number
        finally:
            if os.name == "nt":
                lock.seek(0)
                msvcrt.locking(lock.fileno(), msvcrt.LK_UNLCK, 1)
            else:
                fcntl.flock(lock.fileno(), fcntl.LOCK_UN)


def gen_build_number(target, source, env):
    number = next_build_number(env.File("#/.build-number").abspath)
    Path(target[0].abspath).write_text(
        "// Generated locally by SCons; do not commit.\n"
        "#define DCPP_BUILD_NUMBER %d\n" % number,
        encoding="ascii",
    )
