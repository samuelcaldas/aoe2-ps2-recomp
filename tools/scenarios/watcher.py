"""Observer of guest diagnostics: waits for a marker in a growing log file."""

from __future__ import annotations

import time
from pathlib import Path
from typing import Callable

READ_CHUNK_BYTES = 64 * 1024
MAX_POLL_SECONDS = 0.5


class LogMarkerWatcher:
    """Follows a log that the guest process appends to, including markers split across reads."""

    def __init__(
        self,
        log_path: Path,
        is_alive: Callable[[], bool],
        *,
        clock: Callable[[], float] = time.monotonic,
        sleep: Callable[[float], None] = time.sleep,
    ) -> None:
        self._log_path = log_path
        self._is_alive = is_alive
        self._clock = clock
        self._sleep = sleep

    def await_marker(self, marker: bytes, timeout: float) -> bool:
        """True once the marker appears; False on timeout or when the guest exits first."""
        if not marker:
            raise ValueError("marker must not be empty")
        deadline = self._clock() + timeout
        while not self._log_path.exists():
            if not self._is_alive() or self._clock() >= deadline:
                return False
            self._sleep(MAX_POLL_SECONDS)
        return self._scan(marker, deadline)

    def _scan(self, marker: bytes, deadline: float) -> bool:
        remainder = b""
        with self._log_path.open("rb") as source:
            while True:
                chunk = source.read(READ_CHUNK_BYTES)
                if chunk:
                    data = remainder + chunk
                    if marker in data:
                        return True
                    keep = len(marker) - 1
                    remainder = data[-keep:] if keep else b""
                    if self._clock() >= deadline:
                        return False
                    continue
                if not self._is_alive() or self._clock() >= deadline:
                    return False
                self._sleep(min(MAX_POLL_SECONDS, max(0.0, deadline - self._clock())))
