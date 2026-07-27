"""Structured job logging for Calango-generated ASE scripts.

This module is staged next to ``run.py`` in every job directory, so a
generated script can simply do::

    from calango_log import CalangoLog

    log = CalangoLog()
    log.metric(step, energy=..., temperature=...)
    log.progress(step, total)
    log.event("warning", "SCF did not converge")

It writes two JSON files into the current working directory (the job
directory):

``metrics.json``
    ``{"metrics": [{"step": 0, "energy": -1.23, ...}, ...],
       "progress": {"step": 12, "total": 200, "percent": 6.0}}``
    Polled by Calango's Results panel for live plots. Written atomically
    (temp file + ``os.replace``) so a poll never observes a half-written
    file.

``log.json``
    ``{"log": [{"level": "info", "message": "..."}, ...]}``

It also routes Python warnings (ASE, PyTorch, SciPy, GPAW, ...) to
``warnings.log`` so they stay out of stdout and the Results "Log" tab
remains readable.

The module has no dependencies beyond the standard library and is safe to
run standalone — a script exported from Calango keeps working as long as
this file sits beside it.
"""

from __future__ import annotations

import json
import logging
import os
import threading
import warnings

__all__ = ["CalangoLog", "capture_warnings"]

METRICS_FILE = "metrics.json"
EVENTS_FILE = "log.json"
WARNINGS_FILE = "warnings.log"


def capture_warnings(path: str = WARNINGS_FILE) -> None:
    """Send Python warnings to `path` instead of stderr.

    Called automatically by :class:`CalangoLog`; exposed separately for
    scripts that want the warning routing without the metric logger.
    """
    logger = logging.getLogger("py.warnings")
    logger.handlers.clear()
    logger.addHandler(logging.FileHandler(path, mode="w"))
    logger.propagate = False
    logging.captureWarnings(True)
    # "default" (not "ignore"): every distinct warning is recorded once per
    # location, which is what makes warnings.log useful for diagnosis.
    warnings.simplefilter("default")


class CalangoLog:
    """Thread-safe JSON metric/event logger for one job.

    Every ``metric``/``progress``/``event`` call rewrites the whole file.
    That is O(n) per sample but keeps the reader trivial (plain
    ``json.load``, no incremental parsing) and the sample counts involved
    are small — generated scripts sample on an interval that caps a run at
    a few hundred points.
    """

    def __init__(self, capture_warnings_to: str | None = WARNINGS_FILE) -> None:
        self._lock = threading.Lock()
        self._metrics: list[dict] = []
        self._events: list[dict] = []
        self._progress: dict | None = None
        if capture_warnings_to:
            capture_warnings(capture_warnings_to)

    @staticmethod
    def _flush(path: str, data: dict) -> None:
        # Atomic replace: the Results panel polls these files while the job
        # is writing them, and must never read a truncated document.
        tmp = path + ".tmp"
        with open(tmp, "w") as handle:
            json.dump(data, handle)
        os.replace(tmp, path)

    def _write_metrics(self) -> None:
        data = {"metrics": self._metrics}
        if self._progress is not None:
            data["progress"] = self._progress
        self._flush(METRICS_FILE, data)

    def metric(self, step, **fields) -> None:
        """Record one sample. ``None`` fields are skipped, not written as null."""
        entry = {"step": int(step)}
        for key, value in fields.items():
            if value is not None:
                entry[key] = float(value)
        with self._lock:
            self._metrics.append(entry)
            self._write_metrics()

    def progress(self, step, total) -> None:
        """Update the run's completion fraction (drives the progress bar)."""
        step, total = int(step), int(total)
        percent = (100.0 * step / total) if total > 0 else 0.0
        with self._lock:
            self._progress = {"step": step, "total": total, "percent": percent}
            self._write_metrics()

    def event(self, level, message) -> None:
        """Append a log event (level is free-form: info/warning/error)."""
        with self._lock:
            self._events.append({"level": str(level), "message": str(message)})
            self._flush(EVENTS_FILE, {"log": self._events})
