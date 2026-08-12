"""Calango's persistent SSH/SFTP session, driven by RemoteClient over one
long-lived QProcess.

WHY A SESSION AND NOT ONE PROCESS PER OPERATION
-----------------------------------------------
This helper used to be spawned fresh for every upload / submit / status
poll, opening a new SSH connection each time. Against a key that is merely
wasteful. Against a cluster with keyboard-interactive two-factor auth it is
unusable: every operation would demand a new one-time code, and a monitor
that polls the queue every 30 s cannot ask the human for a TOTP token every
30 s. So the process now authenticates ONCE and multiplexes everything after
that over the same transport, which is what SSH channels are for.

WHY PARAMIKO AND NOT OpenSSH ControlMaster
------------------------------------------
ControlMaster/ControlPersist is the obvious no-dependency alternative — the
user's own `ssh` handles 2FA once and later invocations ride the control
socket. It was rejected for three reasons:

  * Windows. Calango ships a Windows installer, and Win32 OpenSSH does not
    implement connection multiplexing at all (no unix-domain control socket).
    ControlMaster would mean "HPC works everywhere except Windows".
  * The 2FA prompt would belong to `ssh`, i.e. to a terminal Calango does not
    have. Driving it would mean a pty and screen-scraping prompts. Paramiko
    hands us the challenge as structured data (title, instructions, prompts,
    echo flags), which is exactly what a GUI dialog needs.
  * paramiko is already a declared dependency of the HPC panel, so this adds
    nothing to the install.

The cost is that paramiko does not read ~/.ssh/config, so ProxyJump-style
bastion setups are not inherited. That is a known gap, not an oversight.

PROTOCOL (version 2)
--------------------
One JSON object per line each way. Every request carries an integer "id";
every event echoes the id it belongs to, so several operations can be in
flight over the one connection at once. Session-level events use id 0.

Requests (stdin):
    {"id": 1, "op": "connect", "config": {...}}
    {"id": 2, "op": "probe"|"upload"|"submit"|"monitor"|"download",
     "args": {...}}
    {"id": 3, "op": "cancel", "args": {"target": 2}}
    {"id": 4, "op": "disconnect"|"shutdown"}
    {"id": 0, "op": "auth_response", "prompt_id": 7, "responses": [...]}

Events (stdout):
    {"id": 0, "event": "hello", "protocol": 2, "paramiko": "3.4.0"}
    {"id": 0, "event": "session", "state": "connecting"|"authenticating"|
                                           "connected"|"disconnected"|
                                           "reconnecting"|"needs_reauth"}
    {"id": 0, "event": "auth_prompt", "prompt_id": 7, "name": ...,
     "instruction": ..., "prompts": [{"prompt": ..., "echo": false}]}
    {"id": N, "event": "uploaded"|"downloaded"|"state"|"log", ...}
    {"id": N, "event": "result", "ok": bool, "error": ..., "kind": ...}

Failures are classified by "kind" — "auth", "hostkey", "network", "remote",
"auth_required", "internal" — because the GUI has to react differently: a
wrong password is the user's to fix, a dropped TCP connection is not, and a
changed host key must never be shrugged off.

Credentials travel on stdin (never argv) so they are invisible to `ps`.
Nothing is written to disk. The password/passphrase is held in memory for
the life of the process so a dropped KEY/PASSWORD session can silently
reconnect; a keyboard-interactive response is single-use by construction and
is never retained — which is also why a session that needed 2FA refuses to
reconnect on its own and waits to be told to, rather than pestering the user
every poll.
"""

import fnmatch
import json
import os
import posixpath
import queue
import re
import shlex
import socket
import stat
import sys
import threading
import time

PROTOCOL = 2

_out_lock = threading.Lock()


def emit(obj):
    with _out_lock:
        sys.stdout.write(json.dumps(obj) + "\n")
        sys.stdout.flush()


def bail(message):
    """Fatal, before any session exists (id 0 so the GUI always sees it)."""
    emit({"id": 0, "event": "result", "ok": False, "error": message,
          "kind": "internal"})
    sys.exit(1)


try:
    import paramiko
except ImportError:
    bail("The 'paramiko' package is not installed in Calango's Python "
         "environment. Install it with: pip install paramiko")


class RemoteError(Exception):
    """An operation failure that already knows how the GUI should read it."""

    def __init__(self, message, kind="remote"):
        super().__init__(message)
        self.kind = kind


# ---------------------------------------------------------------------------
# Host keys
# ---------------------------------------------------------------------------

def known_hosts_path():
    return os.path.expanduser("~/.ssh/known_hosts")


def host_key_names(host, port):
    # OpenSSH writes the bare hostname for port 22 and "[host]:port"
    # otherwise; both forms have to be looked up or a non-standard port
    # re-adds the same key on every connection.
    return [host] if int(port) == 22 else ["[%s]:%d" % (host, int(port)), host]


def check_host_key(transport, host, port):
    key = transport.get_remote_server_key()
    path = known_hosts_path()
    hosts = paramiko.HostKeys()
    if os.path.exists(path):
        try:
            hosts.load(path)
        except Exception:  # noqa: BLE001 — an unreadable known_hosts is not fatal
            hosts = paramiko.HostKeys()

    for name in host_key_names(host, port):
        entry = hosts.lookup(name)
        if entry is None:
            continue
        known = entry.get(key.get_name())
        if known is None:
            continue  # a different key type for the same host: not a mismatch
        if known.asbytes() != key.asbytes():
            raise RemoteError(
                "host key for %s changed — this is either a reinstalled "
                "server or a man-in-the-middle. Verify the new fingerprint "
                "and remove the old line from ~/.ssh/known_hosts before "
                "connecting again." % host,
                kind="hostkey")
        return  # known and matching

    # Unknown host: trust on first use, like `ssh` answered with "yes".
    # APPENDED by hand rather than through HostKeys.save(), which rewrites
    # the whole file and silently drops every line paramiko could not parse
    # (certificates, unsupported key types) — a data-loss bug in the user's
    # own ssh config that they would never connect back to Calango.
    line = "%s %s %s\n" % (host_key_names(host, port)[0], key.get_name(),
                           key.get_base64())
    try:
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "a", encoding="utf-8") as handle:
            handle.write(line)
    except OSError:
        pass  # not being able to remember the key is not worth refusing over


# ---------------------------------------------------------------------------
# Session
# ---------------------------------------------------------------------------

_PASSWORD_PROMPT = re.compile(r"password", re.IGNORECASE)


class Session:
    """One authenticated transport, shared by every operation."""

    def __init__(self):
        self.transport = None
        self.config = None
        self.lock = threading.RLock()      # serializes (re)connection
        self.interactive_used = False      # 2FA answered by the human?
        self.prompt_timeout = 180.0
        self.prompt_id = 0
        self.pending_prompt = None         # queue.Queue for the answer
        self.prompt_lock = threading.Lock()
        self.reauth_needed = False
        self.last_key_error = None
        self.closing = False

    # -- state ------------------------------------------------------------
    def is_active(self):
        return (self.transport is not None and self.transport.is_active()
                and self.transport.is_authenticated())

    def announce(self, state, **extra):
        event = {"id": 0, "event": "session", "state": state}
        event.update(extra)
        emit(event)

    # -- authentication ---------------------------------------------------
    def _interactive_handler(self, auto_password):
        """Build the paramiko keyboard-interactive callback.

        `auto_password` is answered locally when a prompt is plainly asking
        for the login password (many 2FA clusters ask password + code over
        keyboard-interactive in one exchange, and re-typing a password the
        user already entered is pure friction). It is None in key mode: what
        the form calls a password there is a KEY PASSPHRASE, and sending a
        private key's passphrase to the server as an answer would leak it.
        """

        def handler(title, instructions, prompt_list):
            answers = [None] * len(prompt_list)
            ask = []
            for index, (text, echo) in enumerate(prompt_list):
                if auto_password and not echo and _PASSWORD_PROMPT.search(text):
                    answers[index] = auto_password
                else:
                    ask.append(index)
            if not ask:
                return answers

            with self.prompt_lock:
                self.prompt_id += 1
                prompt_id = self.prompt_id
                inbox = queue.Queue(maxsize=1)
                self.pending_prompt = (prompt_id, inbox)

            emit({"id": 0, "event": "auth_prompt", "prompt_id": prompt_id,
                  "name": title or "", "instruction": instructions or "",
                  "prompts": [{"prompt": prompt_list[i][0],
                               "echo": bool(prompt_list[i][1])} for i in ask]})
            try:
                given = inbox.get(timeout=self.prompt_timeout)
            except queue.Empty:
                raise RemoteError(
                    "no answer to the authentication challenge within %d s"
                    % self.prompt_timeout, kind="auth")
            finally:
                with self.prompt_lock:
                    self.pending_prompt = None
            if given is None:
                raise RemoteError("authentication cancelled", kind="auth")

            for slot, index in enumerate(ask):
                answers[index] = given[slot] if slot < len(given) else ""
            self.interactive_used = True
            # `given` (the one-time code) goes out of scope here and is never
            # stored: a TOTP token is worthless a minute later and keeping one
            # would be a liability with no upside.
            return answers

        return handler

    def _try_publickey(self, user):
        """Agent keys first, then the named file. Returns the still-allowed
        methods, or raises if no key was accepted at all."""
        config = self.config
        last = None
        try:
            agent_keys = paramiko.Agent().get_keys()
        except Exception:  # noqa: BLE001
            agent_keys = ()
        for key in agent_keys:
            try:
                return self.transport.auth_publickey(user, key)
            except paramiko.AuthenticationException as exc:
                last = exc

        path = os.path.expanduser(config.get("key_path", "") or "")
        candidates = [path] if path else [
            os.path.expanduser("~/.ssh/" + name)
            for name in ("id_ed25519", "id_ecdsa", "id_rsa")]
        passphrase = config.get("password") or None
        for candidate in candidates:
            if not candidate or not os.path.exists(candidate):
                continue
            key = None
            for loader in (paramiko.Ed25519Key, paramiko.ECDSAKey,
                           paramiko.RSAKey):
                try:
                    key = loader.from_private_key_file(candidate, passphrase)
                    break
                except paramiko.PasswordRequiredException:
                    raise RemoteError(
                        "the private key %s is encrypted — enter its "
                        "passphrase in the Password field" % candidate,
                        kind="auth")
                except Exception:  # noqa: BLE001 — wrong key type, try next
                    continue
            if key is None:
                continue
            try:
                return self.transport.auth_publickey(user, key)
            except paramiko.AuthenticationException as exc:
                last = exc
        # None, not an exception: "no key worked" is not the end of the
        # ladder. A 2FA cluster in Key mode typically refuses publickey and
        # then wants password + code, and raising here would report "no SSH
        # key was accepted" on a server that never intended to take one.
        self.last_key_error = last
        return None

    def authenticate(self):
        config = self.config
        user = config["username"]
        transport = self.transport
        password = config.get("password") or ""
        key_mode = config.get("auth") != "password"
        auto_password = None if key_mode else (password or None)

        # SSH authentication is a LADDER, not one call. A 2FA cluster answers
        # a correct password with PARTIAL success plus the list of factors it
        # still wants, and paramiko RETURNS that list rather than raising —
        # so code that reads "auth_password() did not throw" as "logged in"
        # happily proceeds on an unauthenticated transport and fails later
        # with a baffling channel error. Everything below loops on
        # is_authenticated() for that reason.
        try:
            remaining = transport.auth_none(user)
        except paramiko.BadAuthenticationType as exc:
            remaining = list(exc.allowed_types)
        except paramiko.AuthenticationException:
            remaining = ["password", "keyboard-interactive", "publickey"]
        except (paramiko.SSHException, EOFError, OSError) as exc:
            raise RemoteError("SSH handshake failed: %s" % exc, kind="network")

        tried = set()
        while not transport.is_authenticated():
            order = (["publickey", "keyboard-interactive", "password"]
                     if key_mode else
                     ["password", "keyboard-interactive", "publickey"])
            method = next((m for m in order
                           if m in remaining and m not in tried), None)
            if method is None:
                raise RemoteError(
                    "authentication failed — the server offered %s and none "
                    "of them succeeded%s"
                    % (", ".join(remaining) or "nothing",
                       "" if self.last_key_error is None
                       else " (last key error: %s)" % self.last_key_error),
                    kind="auth")
            tried.add(method)
            try:
                if method == "publickey":
                    accepted = self._try_publickey(user)
                    if accepted is None:
                        remaining = [m for m in remaining if m != "publickey"]
                        continue
                    remaining = accepted
                elif method == "password":
                    if not password:
                        continue
                    # fallback=False is load-bearing. paramiko's default
                    # fallback answers a refused password auth by replaying
                    # the SAME password into whatever keyboard-interactive
                    # asks next — which on a 2FA cluster means sending the
                    # account password as the one-time code, burning a login
                    # attempt and never showing the human the real prompt.
                    remaining = transport.auth_password(user, password,
                                                        fallback=False)
                else:
                    remaining = transport.auth_interactive(
                        user, self._interactive_handler(auto_password))
            except RemoteError:
                raise
            except paramiko.BadAuthenticationType as exc:
                remaining = list(exc.allowed_types)
            except paramiko.AuthenticationException as exc:
                # A wrong one-time code ends the attempt rather than looping:
                # the server has already consumed a login try, and silently
                # falling through to another method would mean a second guess
                # the user did not authorise. They retry by connecting again.
                if method == "keyboard-interactive":
                    raise RemoteError("two-factor authentication failed: %s"
                                      % exc, kind="auth")
                remaining = [m for m in remaining if m != method]
                if not remaining:
                    raise RemoteError("authentication failed: %s" % exc,
                                      kind="auth")
            except (paramiko.SSHException, EOFError, OSError) as exc:
                raise RemoteError("connection lost during authentication: %s"
                                  % exc, kind="network")
            remaining = list(remaining or [])

    def connect(self, config=None, announce_state="connecting"):
        """Open and authenticate. Caller holds `lock`."""
        if config is not None:
            self.config = config
        if self.config is None:
            raise RemoteError("no connection configured", kind="internal")
        self.close_transport()
        self.prompt_timeout = float(self.config.get("prompt_timeout", 180))
        self.interactive_used = False
        self.reauth_needed = False
        self.last_key_error = None

        host = self.config["host"]
        port = int(self.config.get("port", 22))
        timeout = float(self.config.get("timeout", 15))
        self.announce(announce_state, host=host)
        try:
            sock = socket.create_connection((host, port), timeout)
        except OSError as exc:
            raise RemoteError("cannot reach %s:%d — %s" % (host, port, exc),
                              kind="network")
        # The socket must NOT keep the connect timeout: paramiko reads from it
        # for the life of the session, and a 15 s read timeout would tear down
        # a perfectly healthy idle connection between two 30 s status polls.
        sock.settimeout(None)

        transport = paramiko.Transport(sock)
        try:
            transport.start_client(timeout=timeout)
        except (paramiko.SSHException, EOFError, OSError) as exc:
            transport.close()
            raise RemoteError("SSH handshake failed: %s" % exc, kind="network")
        self.transport = transport
        try:
            check_host_key(transport, host, port)
            self.announce("authenticating", host=host)
            self.authenticate()
        except Exception:
            self.close_transport()
            raise
        # Idle HPC sessions sit behind campus firewalls that drop silent NAT
        # entries in minutes; without this a job monitored overnight loses the
        # connection long before the job ends.
        transport.set_keepalive(30)
        self.announce("connected", host=host,
                      interactive=self.interactive_used)

    def ensure(self):
        """Guarantee a live authenticated transport, or raise.

        The reconnection POLICY lives here, and it is the whole reason this
        file exists: a session authenticated non-interactively can be rebuilt
        silently, but one that needed a human-entered one-time code cannot —
        the code is spent. Reconnecting such a session automatically would
        make a background poll pop a 2FA dialog at random, which is exactly
        the behaviour the persistent session was built to remove. So it stops
        and waits to be told to reconnect.
        """
        with self.lock:
            if self.is_active():
                return
            if self.config is None:
                raise RemoteError("not connected", kind="auth_required")
            if self.interactive_used or self.reauth_needed:
                self.reauth_needed = True
                self.announce("needs_reauth")
                raise RemoteError(
                    "the SSH session dropped and it was opened with "
                    "two-factor authentication, so it cannot be restored "
                    "without you — press Connect to authenticate again.",
                    kind="auth_required")
            last = None
            for delay in (0, 2, 5):
                if delay:
                    time.sleep(delay)
                try:
                    self.connect(announce_state="reconnecting")
                    return
                except RemoteError as exc:
                    last = exc
                    if exc.kind in ("auth", "hostkey"):
                        break  # retrying a rejected password just locks it out
            self.reauth_needed = True
            self.announce("needs_reauth")
            raise RemoteError("could not restore the SSH session: %s" % last,
                              kind=(last.kind if last else "network"))

    def answer_prompt(self, prompt_id, responses):
        with self.prompt_lock:
            pending = self.pending_prompt
        if not pending:
            return False
        expected, inbox = pending
        # A stale answer (the user finished typing after the challenge timed
        # out and a new one was issued) must not be fed to the new challenge.
        if int(prompt_id) != expected:
            return False
        try:
            inbox.put_nowait(responses)
        except queue.Full:
            return False
        return True

    def sftp(self):
        # A new SFTPClient per operation rather than one shared instance:
        # SFTPClient is NOT thread-safe (one request id counter, one reply
        # map), and a download running next to a monitor's log tail would
        # interleave replies and hang. A channel is cheap; a wedged SFTP
        # session is not.
        try:
            return paramiko.SFTPClient.from_transport(self.transport)
        except (paramiko.SSHException, EOFError, OSError) as exc:
            raise RemoteError("could not open SFTP: %s" % exc, kind="network")

    def run(self, command):
        """Run one remote command; returns (exit_status, stdout, stderr)."""
        try:
            channel = self.transport.open_session(timeout=30)
            channel.exec_command(command)
            out = channel.makefile("rb").read().decode("utf-8", "replace")
            err = channel.makefile_stderr("rb").read().decode("utf-8", "replace")
            status = channel.recv_exit_status()
            channel.close()
            return status, out, err
        except (paramiko.SSHException, EOFError, OSError) as exc:
            raise RemoteError("remote command failed: %s" % exc, kind="network")

    def close_transport(self):
        if self.transport is not None:
            try:
                self.transport.close()
            except Exception:  # noqa: BLE001
                pass
            self.transport = None


# ---------------------------------------------------------------------------
# Operations — each runs on its own worker thread over the shared transport
# ---------------------------------------------------------------------------

class Context:
    """Per-request output channel plus its cancellation flag."""

    def __init__(self, request_id):
        self.id = request_id
        self.cancelled = threading.Event()

    def emit(self, obj):
        payload = {"id": self.id}
        payload.update(obj)
        emit(payload)


def sftp_makedirs(sftp, remote_dir):
    parts = [p for p in remote_dir.split("/") if p]
    path = "/" if remote_dir.startswith("/") else ""
    for part in parts:
        path = posixpath.join(path, part) if path else part
        try:
            sftp.stat(path)
        except IOError:
            sftp.mkdir(path)


def op_probe(session, args, ctx):
    _, home, _ = session.run("echo $HOME")
    scheduler = ""
    for candidate, key in (("sbatch", "slurm"), ("qsub", "qsub")):
        _, out, _ = session.run("command -v %s" % candidate)
        if out.strip():
            scheduler = key
            break
    ctx.emit({"event": "result", "ok": True,
              "home": home.strip(), "scheduler": scheduler})


def op_upload(session, args, ctx):
    sftp = session.sftp()
    try:
        remote_dir = args["remote_dir"]
        sftp_makedirs(sftp, remote_dir)
        for local_path in args["files"]:
            if ctx.cancelled.is_set():
                raise RemoteError("upload cancelled", kind="cancelled")
            name = os.path.basename(local_path)
            sftp.put(local_path, posixpath.join(remote_dir, name))
            ctx.emit({"event": "uploaded", "file": name})
        ctx.emit({"event": "result", "ok": True})
    finally:
        sftp.close()


_JOB_ID_PATTERNS = (
    re.compile(r"Submitted batch job (\d+)"),   # SLURM
    re.compile(r"Your job (\d+)"),              # SGE
    re.compile(r"^(\d+)(?:\.\S+)?\s*$"),        # PBS: "12345.headnode"
)


def op_submit(session, args, ctx):
    command = "cd %s && %s" % (shlex.quote(args["remote_dir"]), args["command"])
    status, out, err = session.run(command)
    if status != 0:
        raise RemoteError("submission failed (exit %d): %s"
                          % (status, err.strip() or out.strip()))
    job_id = ""
    for line in out.splitlines():
        for pattern in _JOB_ID_PATTERNS:
            match = pattern.search(line.strip())
            if match:
                job_id = match.group(1)
                break
        if job_id:
            break
    ctx.emit({"event": "result", "ok": True, "job_id": job_id,
              "raw": out.strip()})


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


def query_state(session, scheduler, job_id):
    command = _STATUS_COMMANDS.get(scheduler, _STATUS_COMMANDS["slurm"])
    _, out, _ = session.run(command.format(job_id=shlex.quote(job_id)))
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


def op_monitor(session, args, ctx):
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
    while not ctx.cancelled.is_set():
        # Every poll re-checks the session rather than assuming it: this loop
        # is the one that runs for hours, so it is the one that meets the
        # dropped connection. ensure() either restores it silently (key auth)
        # or refuses (2FA) — it never prompts from in here.
        session.ensure()
        state = query_state(session, scheduler, job_id)
        if state and state != last_state:
            ctx.emit({"event": "state", "state": state})
            last_state = state

        sftp = session.sftp()
        try:
            for name, entry in streams.items():
                text, entry[1] = tail_file(sftp, entry[0], entry[1])
                if text:
                    ctx.emit({"event": "log", "stream": name, "text": text})
        finally:
            sftp.close()

        if not state:
            # Not in the queue anymore. Give the filesystem one extra poll
            # to flush trailing output, then finish.
            if gone_since is None:
                gone_since = time.time()
            elif time.time() - gone_since >= poll_s:
                ctx.emit({"event": "result", "ok": True,
                          "state": last_state or "FINISHED"})
                return
        else:
            gone_since = None
        ctx.cancelled.wait(poll_s)
    ctx.emit({"event": "result", "ok": True, "state": last_state or "",
              "cancelled": True})


def op_download(session, args, ctx):
    sftp = session.sftp()
    try:
        remote_dir = args["remote_dir"]
        local_dir = args["local_dir"]
        patterns = args.get("patterns") or ["*"]
        os.makedirs(local_dir, exist_ok=True)

        downloaded = []
        for entry in sftp.listdir_attr(remote_dir):
            if ctx.cancelled.is_set():
                break
            if stat.S_ISDIR(entry.st_mode):
                continue
            if not any(fnmatch.fnmatch(entry.filename, p) for p in patterns):
                continue
            sftp.get(posixpath.join(remote_dir, entry.filename),
                     os.path.join(local_dir, entry.filename))
            downloaded.append(entry.filename)
            ctx.emit({"event": "downloaded", "file": entry.filename})
        ctx.emit({"event": "result", "ok": True, "files": downloaded})
    finally:
        sftp.close()


OPS = {
    "probe": op_probe,
    "upload": op_upload,
    "submit": op_submit,
    "monitor": op_monitor,
    "download": op_download,
}


# ---------------------------------------------------------------------------
# Dispatch loop
# ---------------------------------------------------------------------------

class Daemon:
    def __init__(self, session=None):
        self.session = session or Session()
        self.contexts = {}
        self.contexts_lock = threading.Lock()
        self.stopped = threading.Event()

    def _spawn(self, name, target):
        thread = threading.Thread(target=target, name=name, daemon=True)
        thread.start()
        return thread

    def _run_op(self, ctx, function, args):
        try:
            self.session.ensure()
            function(self.session, args, ctx)
        except RemoteError as exc:
            ctx.emit({"event": "result", "ok": False, "error": str(exc),
                      "kind": exc.kind})
        except Exception as exc:  # noqa: BLE001 — nothing may kill the daemon
            ctx.emit({"event": "result", "ok": False, "error": str(exc),
                      "kind": "internal"})
        finally:
            with self.contexts_lock:
                self.contexts.pop(ctx.id, None)

    def _do_connect(self, ctx, config):
        try:
            with self.session.lock:
                self.session.connect(config)
            ctx.emit({"event": "result", "ok": True,
                      "interactive": self.session.interactive_used})
        except RemoteError as exc:
            self.session.announce("disconnected", reason=str(exc),
                                  kind=exc.kind)
            ctx.emit({"event": "result", "ok": False, "error": str(exc),
                      "kind": exc.kind})
        except Exception as exc:  # noqa: BLE001
            self.session.announce("disconnected", reason=str(exc),
                                  kind="internal")
            ctx.emit({"event": "result", "ok": False, "error": str(exc),
                      "kind": "internal"})
        finally:
            with self.contexts_lock:
                self.contexts.pop(ctx.id, None)

    def handle(self, request):
        op = request.get("op")
        request_id = int(request.get("id", 0))

        # Answers and cancellations are handled ON THE READER THREAD and must
        # never block: the thread they are unblocking is the one waiting for
        # them. (An earlier shape ran connect() inline here and deadlocked —
        # the auth handler waited for a stdin line that this thread was too
        # busy authenticating to read.)
        if op == "auth_response":
            ok = self.session.answer_prompt(request.get("prompt_id", 0),
                                            request.get("responses") or [])
            emit({"id": request_id, "event": "result", "ok": ok,
                  "error": "" if ok else "no challenge is waiting for an answer",
                  "kind": "" if ok else "auth"})
            return
        if op == "auth_cancel":
            self.session.answer_prompt(request.get("prompt_id", 0), None)
            return
        if op == "cancel":
            target = int((request.get("args") or {}).get("target", 0))
            with self.contexts_lock:
                ctx = self.contexts.get(target)
            if ctx:
                ctx.cancelled.set()
            emit({"id": request_id, "event": "result", "ok": ctx is not None})
            return
        if op == "disconnect":
            with self.contexts_lock:
                for ctx in self.contexts.values():
                    ctx.cancelled.set()
            self.session.close_transport()
            self.session.reauth_needed = False
            self.session.announce("disconnected", reason="closed by request")
            emit({"id": request_id, "event": "result", "ok": True})
            return
        if op == "shutdown":
            self.stopped.set()
            return

        ctx = Context(request_id)
        with self.contexts_lock:
            self.contexts[request_id] = ctx

        if op == "connect":
            self._spawn("connect", lambda: self._do_connect(
                ctx, request.get("config") or {}))
            return

        function = OPS.get(op)
        if function is None:
            ctx.emit({"event": "result", "ok": False,
                      "error": "unknown operation %r" % op, "kind": "internal"})
            with self.contexts_lock:
                self.contexts.pop(request_id, None)
            return
        args = request.get("args") or {}
        self._spawn(op, lambda: self._run_op(ctx, function, args))

    def serve(self, stream=None):
        stream = stream or sys.stdin
        emit({"id": 0, "event": "hello", "protocol": PROTOCOL,
              "paramiko": getattr(paramiko, "__version__", "?")})
        while not self.stopped.is_set():
            line = stream.readline()
            if not line:
                break
            line = line.strip()
            if not line:
                continue
            try:
                request = json.loads(line)
            except ValueError as exc:
                emit({"id": 0, "event": "result", "ok": False,
                      "error": "invalid request: %s" % exc, "kind": "internal"})
                continue
            try:
                self.handle(request)
            except Exception as exc:  # noqa: BLE001
                emit({"id": int(request.get("id", 0)), "event": "result",
                      "ok": False, "error": str(exc), "kind": "internal"})
        with self.contexts_lock:
            for ctx in self.contexts.values():
                ctx.cancelled.set()
        self.session.close_transport()


def main():
    Daemon().serve()


if __name__ == "__main__":
    main()
