"""Calango's SSH/SFTP helper, driven by RemoteClient over a QProcess.

One JSON request object arrives on stdin:

    {"config": {"host": ..., "port": 22, "username": ...,
                "auth": "key"|"password", "key_path": "...",
                "password": "...", "timeout": 15},
     "op": "probe"|"upload"|"submit"|"monitor"|"download",
     "args": {...}}

Credentials travel on stdin (never argv) so they are invisible to `ps`.
Every response is one JSON object per line on stdout ("event" objects
while the operation runs, one final {"event": "result", "ok": ...}).
Running paramiko out-of-process keeps the GUI responsive and means a
network hang can always be resolved by killing this process.
"""

import fnmatch
import json
import os
import posixpath
import re
import shlex
import stat
import sys
import time

def emit(obj):
    sys.stdout.write(json.dumps(obj) + "\n")
    sys.stdout.flush()


def fail(message):
    emit({"event": "result", "ok": False, "error": message})
    sys.exit(1)


try:
    import paramiko
except ImportError:
    fail("The 'paramiko' package is not installed in Calango's Python "
         "environment. Install it with: pip install paramiko")


def connect(config):
    client = paramiko.SSHClient()
    client.load_system_host_keys()
    # Lab/HPC pragmatism: accept unknown hosts on first contact, like a
    # fresh `ssh` answered with "yes" (the key is then pinned by paramiko).
    client.set_missing_host_key_policy(paramiko.AutoAddPolicy())

    kwargs = dict(
        hostname=config["host"],
        port=int(config.get("port", 22)),
        username=config["username"],
        timeout=float(config.get("timeout", 15)),
    )
    if config.get("auth") == "password":
        kwargs["password"] = config.get("password", "")
        kwargs["look_for_keys"] = False
        kwargs["allow_agent"] = False
    else:
        key_path = os.path.expanduser(config.get("key_path", "")) or None
        if key_path:
            kwargs["key_filename"] = key_path
        # A non-empty password doubles as the key's passphrase.
        if config.get("password"):
            kwargs["passphrase"] = config["password"]
    client.connect(**kwargs)
    return client


def run(client, command):
    """Run one remote command; returns (exit_status, stdout, stderr)."""
    _, stdout, stderr = client.exec_command(command)
    out = stdout.read().decode("utf-8", "replace")
    err = stderr.read().decode("utf-8", "replace")
    return stdout.channel.recv_exit_status(), out, err


def sftp_makedirs(sftp, remote_dir):
    parts = [p for p in remote_dir.split("/") if p]
    path = "/" if remote_dir.startswith("/") else ""
    for part in parts:
        path = posixpath.join(path, part) if path else part
        try:
            sftp.stat(path)
        except IOError:
            sftp.mkdir(path)


# --------------------------------------------------------------------------
# Operations
# --------------------------------------------------------------------------

def op_probe(client, args):
    _, home, _ = run(client, "echo $HOME")
    scheduler = ""
    for candidate, key in (("sbatch", "slurm"), ("qsub", "qsub")):
        _, out, _ = run(client, f"command -v {candidate}")
        if out.strip():
            scheduler = key
            break
    emit({"event": "result", "ok": True,
          "home": home.strip(), "scheduler": scheduler})


def op_upload(client, args):
    sftp = client.open_sftp()
    remote_dir = args["remote_dir"]
    sftp_makedirs(sftp, remote_dir)
    for local_path in args["files"]:
        name = os.path.basename(local_path)
        sftp.put(local_path, posixpath.join(remote_dir, name))
        emit({"event": "uploaded", "file": name})
    emit({"event": "result", "ok": True})


_JOB_ID_PATTERNS = (
    re.compile(r"Submitted batch job (\d+)"),   # SLURM
    re.compile(r"Your job (\d+)"),              # SGE
    re.compile(r"^(\d+)(?:\.\S+)?\s*$"),        # PBS: "12345.headnode"
)


def op_submit(client, args):
    command = f"cd {shlex.quote(args['remote_dir'])} && {args['command']}"
    status, out, err = run(client, command)
    if status != 0:
        fail(f"submission failed (exit {status}): {err.strip() or out.strip()}")
    job_id = ""
    for line in out.splitlines():
        for pattern in _JOB_ID_PATTERNS:
            match = pattern.search(line.strip())
            if match:
                job_id = match.group(1)
                break
        if job_id:
            break
    emit({"event": "result", "ok": True, "job_id": job_id, "raw": out.strip()})


_STATUS_COMMANDS = {
    # -h: no header; %T: long state name (PENDING/RUNNING/...)
    "slurm": "squeue -h -j {job_id} -o %T",
    "pbs": "qstat -f {job_id} 2>/dev/null | awk '/job_state/ {{print $3}}'",
    "sge": "qstat 2>/dev/null | awk '$1 == \"{job_id}\" {{print $5}}'",
}

_STATE_NAMES = {
    "R": "RUNNING", "Q": "PENDING", "H": "HELD", "E": "EXITING",
    "r": "RUNNING", "qw": "PENDING", "hqw": "HELD", "Eqw": "ERROR",
}


def query_state(client, scheduler, job_id):
    command = _STATUS_COMMANDS.get(scheduler, _STATUS_COMMANDS["slurm"])
    _, out, _ = run(client, command.format(job_id=shlex.quote(job_id)))
    state = out.strip().splitlines()[0].strip() if out.strip() else ""
    return _STATE_NAMES.get(state, state)


def tail_file(sftp, path, offset):
    """New bytes of a remote file since `offset` ('' if unreadable)."""
    try:
        size = sftp.stat(path).st_size
        if size <= offset:
            return "", offset
        with sftp.open(path, "r") as handle:
            handle.seek(offset)
            data = handle.read(size - offset)
        return data.decode("utf-8", "replace"), size
    except IOError:
        return "", offset


def op_monitor(client, args):
    sftp = client.open_sftp()
    remote_dir = args["remote_dir"]
    scheduler = args["scheduler"]
    job_id = args["job_id"]
    poll_s = float(args.get("poll_s", 10))
    streams = {
        "out": [posixpath.join(remote_dir, "calango_job.out"), 0],
        "err": [posixpath.join(remote_dir, "calango_job.err"), 0],
    }

    last_state = None
    gone_since = None
    while True:
        state = query_state(client, scheduler, job_id)
        if state and state != last_state:
            emit({"event": "state", "state": state})
            last_state = state

        for name, entry in streams.items():
            text, entry[1] = tail_file(sftp, entry[0], entry[1])
            if text:
                emit({"event": "log", "stream": name, "text": text})

        if not state:
            # Not in the queue anymore. Give the filesystem one extra poll
            # to flush trailing output, then finish.
            if gone_since is None:
                gone_since = time.time()
            elif time.time() - gone_since >= poll_s:
                emit({"event": "result", "ok": True,
                      "state": last_state or "FINISHED"})
                return
        else:
            gone_since = None
        time.sleep(poll_s)


def op_download(client, args):
    sftp = client.open_sftp()
    remote_dir = args["remote_dir"]
    local_dir = args["local_dir"]
    patterns = args.get("patterns") or ["*"]
    os.makedirs(local_dir, exist_ok=True)

    downloaded = []
    for entry in sftp.listdir_attr(remote_dir):
        if stat.S_ISDIR(entry.st_mode):
            continue
        if not any(fnmatch.fnmatch(entry.filename, p) for p in patterns):
            continue
        sftp.get(posixpath.join(remote_dir, entry.filename),
                 os.path.join(local_dir, entry.filename))
        downloaded.append(entry.filename)
        emit({"event": "downloaded", "file": entry.filename})
    emit({"event": "result", "ok": True, "files": downloaded})


OPS = {
    "probe": op_probe,
    "upload": op_upload,
    "submit": op_submit,
    "monitor": op_monitor,
    "download": op_download,
}


def main():
    try:
        request = json.loads(sys.stdin.readline())
    except (ValueError, EOFError) as exc:
        fail(f"invalid request: {exc}")

    op = OPS.get(request.get("op"))
    if op is None:
        fail(f"unknown operation {request.get('op')!r}")

    try:
        client = connect(request["config"])
    except Exception as exc:  # noqa: BLE001 — every auth error must reach the GUI
        fail(f"connection failed: {exc}")

    try:
        op(client, request.get("args", {}))
    except Exception as exc:  # noqa: BLE001
        fail(str(exc))
    finally:
        client.close()


if __name__ == "__main__":
    main()
