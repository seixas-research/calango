#!/usr/bin/env python3
"""End-to-end test of the persistent SSH session helper against a REAL SSH
server — a paramiko server running on 127.0.0.1 that demands keyboard-
interactive two-factor authentication.

This exists because the thing being tested cannot be tested any other way.
There is no 2FA cluster on a build machine, and mocking paramiko would test
the mock: the whole point of the design is how a real SSH server behaves
during multi-factor auth — a password that returns PARTIAL success plus a
list of remaining factors, an INFO_REQUEST carrying prompts with echo flags,
and a transport that stays usable for hours afterwards. paramiko can act as
the server side of exactly that exchange, so the protocol under test is the
genuine one; only the cluster behind it is fake (a temp directory plus a
five-command shell).

What it pins:
  * one authentication serves every later operation (the entire reason the
    session exists) — asserted by counting authentications server-side;
  * the 2FA challenge reaches the GUI as data and the answer goes back;
  * a wrong code fails as kind="auth", not as a network error;
  * a stale answer to an expired challenge is refused;
  * a dropped KEY/PASSWORD session reconnects silently, and a dropped 2FA
    session does NOT — it stops with kind="auth_required" and no new prompt,
    which is the invariant that makes 30-second polling tolerable;
  * upload / submit / monitor / download over the shared transport.

Run: python3 tests/remote_session_test.py [path/to/calango_remote.py]

A second entry point, `--serve <mode> <root>`, publishes the same fake
cluster on stdout as "PORT n" and then takes one-word commands on stdin
("drop", "quit"). That is what the C++ RemoteSessionTest drives, so the Qt
side is exercised against the same real SSH server rather than a replay of
canned protocol lines.
"""

import json
import os
import socket
import subprocess
import sys
import tempfile
import threading
import time

try:
    import paramiko
except ImportError:
    print("SKIP: paramiko not installed")
    sys.exit(0)

FAILURES = []
PASSED = 0


def check(ok, what):
    global PASSED
    print("  %s %s" % ("ok  " if ok else "FAIL", what))
    if ok:
        PASSED += 1
    else:
        FAILURES.append(what)


# ---------------------------------------------------------------------------
# A fake cluster: SFTP over a temp dir plus a shell that knows five commands
# ---------------------------------------------------------------------------

class StubSFTPHandle(paramiko.SFTPHandle):
    def stat(self):
        try:
            return paramiko.SFTPAttributes.from_stat(
                os.fstat(self.readfile.fileno()))
        except OSError as exc:
            return paramiko.SFTPServer.convert_errno(exc.errno)


def make_sftp_server(root):
    class StubSFTPServer(paramiko.SFTPServerInterface):
        ROOT = root

        def _real(self, path):
            return os.path.normpath(self.ROOT + self.canonicalize(path))

        def list_folder(self, path):
            path = self._real(path)
            try:
                out = []
                for name in os.listdir(path):
                    attr = paramiko.SFTPAttributes.from_stat(
                        os.stat(os.path.join(path, name)))
                    attr.filename = name
                    out.append(attr)
                return out
            except OSError as exc:
                return paramiko.SFTPServer.convert_errno(exc.errno)

        def stat(self, path):
            try:
                return paramiko.SFTPAttributes.from_stat(os.stat(self._real(path)))
            except OSError as exc:
                return paramiko.SFTPServer.convert_errno(exc.errno)

        lstat = stat

        def open(self, path, flags, attr):
            path = self._real(path)
            try:
                handle_fd = os.open(path, flags, 0o666)
            except OSError as exc:
                return paramiko.SFTPServer.convert_errno(exc.errno)
            if flags & os.O_WRONLY:
                mode = "ab" if flags & os.O_APPEND else "wb"
            elif flags & os.O_RDWR:
                mode = "a+b" if flags & os.O_APPEND else "r+b"
            else:
                mode = "rb"
            try:
                stream = os.fdopen(handle_fd, mode)
            except OSError as exc:
                return paramiko.SFTPServer.convert_errno(exc.errno)
            handle = StubSFTPHandle(flags)
            handle.filename = path
            handle.readfile = stream
            handle.writefile = stream
            return handle

        def mkdir(self, path, attr):
            try:
                os.mkdir(self._real(path))
            except OSError as exc:
                return paramiko.SFTPServer.convert_errno(exc.errno)
            return paramiko.SFTP_OK

        def remove(self, path):
            try:
                os.remove(self._real(path))
            except OSError as exc:
                return paramiko.SFTPServer.convert_errno(exc.errno)
            return paramiko.SFTP_OK

        def chattr(self, path, attr):
            return paramiko.SFTP_OK

    return StubSFTPServer


class FakeCluster:
    """One SSH server; `mode` decides which factors it demands.

    mode="2fa"      password (partial) then a one-time code
    mode="password" password only
    mode="combined" one keyboard-interactive round asking password AND code
    mode="key"      public key only
    """

    CODE = "424242"
    PASSWORD = "hunter2"
    JOB_ID = "4242"
    instances = 0   # each one is a distinct host key the helper must record

    def __init__(self, mode, root, authorized_key=None):
        FakeCluster.instances += 1
        self.mode = mode
        self.root = root
        self.authorized_key = authorized_key
        self.host_key = paramiko.RSAKey.generate(2048)
        self.authentications = 0     # completed logins
        self.prompt_rounds = 0       # keyboard-interactive challenges issued
        self.job_state = "PENDING"
        self.transports = []
        self.sock = socket.socket()
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.bind(("127.0.0.1", 0))
        self.sock.listen(8)
        self.port = self.sock.getsockname()[1]
        self.stop = threading.Event()
        self.thread = threading.Thread(target=self._accept_loop, daemon=True)
        self.thread.start()

    def close(self):
        self.stop.set()
        try:
            self.sock.close()
        except OSError:
            pass
        for transport in self.transports:
            try:
                transport.close()
            except Exception:  # noqa: BLE001
                pass

    def drop_connections(self):
        """Kill every live transport — the cluster-side of a dropped VPN."""
        for transport in list(self.transports):
            try:
                transport.close()
            except Exception:  # noqa: BLE001
                pass
        self.transports = []

    def _accept_loop(self):
        while not self.stop.is_set():
            try:
                conn, _ = self.sock.accept()
            except OSError:
                return
            threading.Thread(target=self._serve, args=(conn,),
                             daemon=True).start()

    def _serve(self, conn):
        transport = paramiko.Transport(conn)
        transport.add_server_key(self.host_key)
        transport.set_subsystem_handler("sftp", paramiko.SFTPServer,
                                        make_sftp_server(self.root))
        try:
            transport.start_server(server=_ServerInterface(self))
        except Exception:  # noqa: BLE001 — a client that hangs up mid-handshake
            return
        self.transports.append(transport)

    # -- the five-command shell -------------------------------------------
    def shell(self, command):
        if "echo $HOME" in command:
            return 0, self.root + "\n"
        if "command -v sbatch" in command:
            return 0, "/usr/bin/sbatch\n"
        if "command -v qsub" in command:
            return 0, ""
        if "sbatch" in command:
            return 0, "Submitted batch job %s\n" % self.JOB_ID
        if "squeue" in command:
            return 0, ("" if self.job_state is None
                       else self.job_state + "\n")
        if "scancel" in command:
            self.job_state = None
            return 0, "cancelled\n"
        return 127, ""


class _ServerInterface(paramiko.ServerInterface):
    def __init__(self, cluster):
        self.cluster = cluster
        self.password_ok = False

    def get_allowed_auths(self, username):
        if self.cluster.mode == "password":
            return "password"
        if self.cluster.mode == "combined":
            return "keyboard-interactive"
        if self.cluster.mode == "key":
            return "publickey"
        if self.cluster.mode == "key2fa":
            return "publickey,keyboard-interactive"
        return "password,keyboard-interactive"

    def check_auth_publickey(self, username, key):
        authorized = self.cluster.authorized_key
        if authorized is not None and key.asbytes() == authorized.asbytes():
            self.cluster.authentications += 1
            return paramiko.AUTH_SUCCESSFUL
        return paramiko.AUTH_FAILED

    def check_auth_password(self, username, password):
        if password != FakeCluster.PASSWORD:
            return paramiko.AUTH_FAILED
        if self.cluster.mode == "password":
            self.cluster.authentications += 1
            return paramiko.AUTH_SUCCESSFUL
        self.password_ok = True
        # Partial success: the trap the ladder in the helper exists for.
        return paramiko.AUTH_PARTIALLY_SUCCESSFUL

    def check_auth_interactive(self, username, submethods):
        self.cluster.prompt_rounds += 1
        if self.cluster.mode == "key2fa":
            return paramiko.InteractiveQuery(
                "Two-factor", "Key refused; use your token",
                ("Verification code: ", False))
        if self.cluster.mode == "combined":
            return paramiko.InteractiveQuery(
                "Login", "Password and token",
                ("Password: ", False), ("Verification code: ", False))
        if not self.password_ok:
            return paramiko.AUTH_FAILED
        return paramiko.InteractiveQuery(
            "Two-factor", "Enter the code from your authenticator",
            ("Verification code: ", False))

    def check_auth_interactive_response(self, responses):
        if self.cluster.mode == "key2fa":
            if list(responses) != [FakeCluster.CODE]:
                return paramiko.AUTH_FAILED
            self.cluster.authentications += 1
            return paramiko.AUTH_SUCCESSFUL
        if self.cluster.mode == "combined":
            if list(responses) != [FakeCluster.PASSWORD, FakeCluster.CODE]:
                return paramiko.AUTH_FAILED
        elif list(responses) != [FakeCluster.CODE]:
            return paramiko.AUTH_FAILED
        self.cluster.authentications += 1
        return paramiko.AUTH_SUCCESSFUL

    def check_channel_request(self, kind, chanid):
        return (paramiko.OPEN_SUCCEEDED if kind == "session"
                else paramiko.OPEN_FAILED_ADMINISTRATIVELY_PROHIBITED)

    def check_channel_exec_request(self, channel, command):
        text = command.decode() if isinstance(command, bytes) else command

        def work():
            # The 50 ms is not decoration: closing the channel from this
            # thread can outrun the channel-success reply this call is about
            # to return, and the client then sees "Channel closed" instead of
            # its output.
            time.sleep(0.05)
            status, out = self.cluster.shell(text)
            channel.sendall(out.encode())
            channel.send_exit_status(status)
            channel.close()

        threading.Thread(target=work, daemon=True).start()
        return True


# ---------------------------------------------------------------------------
# Driving the helper exactly as RemoteClient does
# ---------------------------------------------------------------------------

class Helper:
    def __init__(self, path):
        self.process = subprocess.Popen(
            [sys.executable, "-u", path],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, text=True, bufsize=1)
        self.next_id = 1
        self.seen = []      # every event read, for after-the-fact assertions

    def send(self, request):
        self.process.stdin.write(json.dumps(request) + "\n")
        self.process.stdin.flush()

    def request(self, op, **fields):
        request_id = self.next_id
        self.next_id += 1
        payload = {"id": request_id, "op": op}
        payload.update(fields)
        self.send(payload)
        return request_id

    def read(self, timeout=20):
        """Next event, or None on timeout/EOF."""
        result = {}
        done = threading.Event()

        def reader():
            line = self.process.stdout.readline()
            if line.strip():
                result["event"] = json.loads(line)
            done.set()

        thread = threading.Thread(target=reader, daemon=True)
        thread.start()
        done.wait(timeout)
        return result.get("event")

    def wait_for(self, predicate, timeout=25):
        """Drain events until one matches; returns it (None on timeout)."""
        deadline = time.time() + timeout
        while time.time() < deadline:
            event = self.read(timeout=max(0.5, deadline - time.time()))
            if event is None:
                continue
            self.seen.append(event)
            if predicate(event):
                return event
        return None

    def start_log(self):
        """Forget everything seen so far, so a later assertion about what did
        NOT happen ('no prompt after the drop') means what it says."""
        self.seen = []

    def result_for(self, request_id, timeout=25):
        return self.wait_for(lambda e: e.get("id") == request_id
                             and e.get("event") == "result", timeout)

    def close(self):
        try:
            self.send({"id": 0, "op": "shutdown"})
            self.process.stdin.close()
            self.process.wait(timeout=5)
        except Exception:  # noqa: BLE001
            self.process.kill()


def connect_2fa(helper, cluster, code=FakeCluster.CODE,
                password=FakeCluster.PASSWORD):
    """Drive one full connect, answering the challenge. Returns (result,
    prompt event)."""
    request_id = helper.request("connect", config={
        "host": "127.0.0.1", "port": cluster.port, "username": "user",
        "auth": "password", "password": password, "timeout": 10,
        "prompt_timeout": 20})
    prompt = helper.wait_for(lambda e: e.get("event") in ("auth_prompt",
                                                          "result"))
    if prompt is not None and prompt.get("event") == "auth_prompt":
        helper.send({"id": 0, "op": "auth_response",
                     "prompt_id": prompt["prompt_id"], "responses": [code]})
        return helper.result_for(request_id), prompt
    return prompt, None


# ---------------------------------------------------------------------------

def main():
    here = os.path.dirname(os.path.abspath(__file__))
    helper_path = (sys.argv[1] if len(sys.argv) > 1
                   else os.path.join(os.path.dirname(here), "assets", "remote",
                                     "calango_remote.py"))
    if not os.path.exists(helper_path):
        print("FAIL: helper not found at %s" % helper_path)
        return 1

    # known_hosts is written on first contact; point HOME at a scratch dir so
    # the test never touches the developer's real one.
    workspace = tempfile.mkdtemp(prefix="calango_remote_test_")
    fake_home = os.path.join(workspace, "home")
    os.makedirs(os.path.join(fake_home, ".ssh"))
    os.environ["HOME"] = fake_home
    os.environ.pop("SSH_AUTH_SOCK", None)  # no agent keys in the ladder

    remote_root = os.path.join(workspace, "cluster")
    os.makedirs(remote_root)

    # --- 1. keyboard-interactive 2FA, then everything on one login --------
    print("Two-factor login and session reuse:")
    cluster = FakeCluster("2fa", remote_root)
    helper = Helper(helper_path)
    helper.start_log()
    hello = helper.wait_for(lambda e: e.get("event") == "hello", timeout=15)
    check(hello is not None and hello.get("protocol") == 2,
          "helper announces protocol 2")

    result, prompt = connect_2fa(helper, cluster)
    check(prompt is not None, "server's 2FA challenge reaches the client")
    if prompt:
        check(prompt["prompts"][0]["prompt"].startswith("Verification code"),
              "the prompt text is passed through verbatim")
        check(prompt["prompts"][0]["echo"] is False,
              "and its echo flag (a code must not be echoed)")
        check("authenticator" in prompt.get("instruction", ""),
              "the server's instruction text survives")
    check(result is not None and result.get("ok") is True,
          "connect succeeds after the code is answered")
    check(result is not None and result.get("interactive") is True,
          "the result records that a human factor was used")
    check(any(e.get("event") == "session" and e.get("state") == "connected"
              for e in helper.seen), "a session/connected event is emitted")
    check(cluster.authentications == 1, "exactly one login so far")

    # Four operations, one connection. This is the whole point.
    # Relative, exactly as the panel builds it ("calango_jobs/<stamp>"):
    # the SFTP server roots every path at the fake cluster directory the
    # way a real account is rooted at $HOME.
    job_dir = "job1"
    job_path = os.path.join(remote_root, "job1")
    local_dir = os.path.join(workspace, "staging")
    os.makedirs(local_dir)
    with open(os.path.join(local_dir, "run.py"), "w") as handle:
        handle.write("print('hi')\n")
    with open(os.path.join(local_dir, "job.sh"), "w") as handle:
        handle.write("#!/bin/sh\n")

    probe_id = helper.request("probe", args={})
    probe = helper.result_for(probe_id)
    check(probe is not None and probe.get("ok")
          and probe.get("scheduler") == "slurm",
          "probe finds the scheduler over the existing session")

    upload_id = helper.request("upload", args={
        "remote_dir": job_dir,
        "files": [os.path.join(local_dir, "run.py"),
                  os.path.join(local_dir, "job.sh")]})
    uploaded = []
    while True:
        event = helper.wait_for(lambda e: e.get("id") == upload_id)
        if event is None or event.get("event") == "result":
            break
        uploaded.append(event.get("file"))
    check(event is not None and event.get("ok"), "upload reports success")
    check(sorted(uploaded) == ["job.sh", "run.py"],
          "both files are announced as uploaded")
    check(os.path.exists(os.path.join(job_path, "run.py")),
          "and land on the server through SFTP")

    submit_id = helper.request("submit", args={
        "remote_dir": job_dir, "command": "sbatch job.sh"})
    submitted = helper.result_for(submit_id)
    check(submitted is not None and submitted.get("job_id") == "4242",
          "the SLURM job id is parsed out of the submission")

    check(cluster.authentications == 1,
          "STILL one login after probe + upload + submit")
    check(cluster.prompt_rounds == 1,
          "and exactly one 2FA challenge was ever issued")

    # --- 2. monitor: state changes, log tail, cancel ----------------------
    print("Monitoring:")
    with open(os.path.join(job_path, "calango_job.out"), "w") as handle:
        handle.write("step 1\n")
    monitor_id = helper.request("monitor", args={
        "remote_dir": job_dir, "scheduler": "slurm", "job_id": "4242",
        "poll_s": 0.4})
    state = helper.wait_for(lambda e: e.get("id") == monitor_id
                            and e.get("event") == "state")
    check(state is not None and state.get("state") == "PENDING",
          "the queue state is reported")
    log = helper.wait_for(lambda e: e.get("id") == monitor_id
                          and e.get("event") == "log")
    check(log is not None and "step 1" in log.get("text", ""),
          "the remote job log is tailed over the same session")
    cluster.job_state = None  # job leaves the queue
    finished = helper.result_for(monitor_id, timeout=20)
    check(finished is not None and finished.get("ok"),
          "the monitor finishes when the job leaves the queue")
    check(cluster.authentications == 1, "monitoring needed no new login")

    # --- 3. download ------------------------------------------------------
    with open(os.path.join(job_path, "result.extxyz"), "w") as handle:
        handle.write("1\nH 0 0 0\n")
    results_dir = os.path.join(workspace, "results")
    download_id = helper.request("download", args={
        "remote_dir": job_dir, "local_dir": results_dir,
        "patterns": ["*.extxyz"]})
    downloaded = helper.result_for(download_id)
    check(downloaded is not None and downloaded.get("ok")
          and downloaded.get("files") == ["result.extxyz"],
          "download pulls back only the matching files")
    check(os.path.exists(os.path.join(results_dir, "result.extxyz")),
          "and writes them locally")

    # --- 4. a 2FA session that drops must NOT re-prompt on its own -------
    print("Reconnection policy after a drop:")
    helper.start_log()
    cluster.drop_connections()
    time.sleep(0.3)
    probe_id = helper.request("probe", args={})
    dropped = helper.result_for(probe_id, timeout=20)
    check(dropped is not None and dropped.get("ok") is False,
          "an operation after the drop fails rather than hanging")
    check(dropped is not None and dropped.get("kind") == "auth_required",
          "and says re-authentication is required, not 'network'")
    check(any(e.get("event") == "session" and e.get("state") == "needs_reauth"
              for e in helper.seen),
          "the session announces needs_reauth")
    check(not any(e.get("event") == "auth_prompt" for e in helper.seen),
          "NO 2FA prompt was raised behind the user's back")
    check(cluster.prompt_rounds == 1, "the server saw no second challenge")

    # An explicit connect is what re-authenticates — and only then.
    result, prompt = connect_2fa(helper, cluster)
    check(prompt is not None, "an explicit connect does challenge again")
    check(result is not None and result.get("ok"), "and restores the session")
    check(cluster.authentications == 2, "which is the second login")

    # --- 5. a wrong code is an auth failure, and is recoverable -----------
    print("Wrong code:")
    helper2 = Helper(helper_path)
    helper2.start_log()
    helper2.wait_for(lambda e: e.get("event") == "hello", timeout=15)
    bad, _ = connect_2fa(helper2, cluster, code="000000")
    check(bad is not None and bad.get("ok") is False, "a wrong code fails")
    check(bad is not None and bad.get("kind") == "auth",
          "classified as auth, so the GUI can say whose fault it is")
    good, _ = connect_2fa(helper2, cluster)
    check(good is not None and good.get("ok"),
          "and the user gets another go without restarting anything")

    # --- 6. a stale answer must not satisfy a new challenge --------------
    print("Stale challenge answers:")
    helper2.start_log()
    connect_id = helper2.request("connect", config={
        "host": "127.0.0.1", "port": cluster.port, "username": "user",
        "auth": "password", "password": FakeCluster.PASSWORD,
        "prompt_timeout": 8})
    prompt = helper2.wait_for(lambda e: e.get("event") == "auth_prompt")
    check(prompt is not None, "a challenge is pending")
    stale_id = helper2.request("auth_response",
                               prompt_id=prompt["prompt_id"] - 1,
                               responses=[FakeCluster.CODE])
    stale = helper2.result_for(stale_id, timeout=10)
    check(stale is not None and stale.get("ok") is False,
          "an answer carrying the wrong prompt_id is refused")
    helper2.send({"id": 0, "op": "auth_response",
                  "prompt_id": prompt["prompt_id"],
                  "responses": [FakeCluster.CODE]})
    accepted = helper2.result_for(connect_id, timeout=15)
    check(accepted is not None and accepted.get("ok"),
          "while the matching answer still goes through")

    # Cancelling the dialog has to end the attempt, not leave the connect
    # request hanging until the prompt times out three minutes later.
    print("Cancelling the dialog:")
    helper2.start_log()
    connect_id = helper2.request("connect", config={
        "host": "127.0.0.1", "port": cluster.port, "username": "user",
        "auth": "password", "password": FakeCluster.PASSWORD,
        "prompt_timeout": 60})
    prompt = helper2.wait_for(lambda e: e.get("event") == "auth_prompt")
    check(prompt is not None, "a challenge is pending again")
    helper2.send({"id": 0, "op": "auth_cancel",
                  "prompt_id": prompt["prompt_id"]})
    cancelled = helper2.result_for(connect_id, timeout=20)
    check(cancelled is not None and cancelled.get("ok") is False,
          "the connect attempt fails promptly instead of waiting out the "
          "prompt timeout")
    helper2.close()
    cluster.close()

    # --- 7. password-only: a drop reconnects silently ---------------------
    print("Non-interactive session reconnects by itself:")
    plain = FakeCluster("password", remote_root)
    helper3 = Helper(helper_path)
    helper3.start_log()
    helper3.wait_for(lambda e: e.get("event") == "hello", timeout=15)
    connect_id = helper3.request("connect", config={
        "host": "127.0.0.1", "port": plain.port, "username": "user",
        "auth": "password", "password": FakeCluster.PASSWORD})
    result = helper3.result_for(connect_id)
    check(result is not None and result.get("ok"), "password login works")
    check(result is not None and result.get("interactive") is False,
          "and is marked non-interactive")
    plain.drop_connections()
    time.sleep(0.3)
    helper3.start_log()
    probe_id = helper3.request("probe", args={})
    recovered = helper3.result_for(probe_id, timeout=25)
    check(recovered is not None and recovered.get("ok"),
          "the next operation transparently reconnects")
    check(plain.authentications == 2, "which cost a second (silent) login")
    check(not any(e.get("event") == "auth_prompt" for e in helper3.seen),
          "with no prompt, because nothing human was needed")
    helper3.close()
    plain.close()

    # --- 8. combined password+code round, password answered locally ------
    print("Password and code in one challenge:")
    combined = FakeCluster("combined", remote_root)
    helper4 = Helper(helper_path)
    helper4.start_log()
    helper4.wait_for(lambda e: e.get("event") == "hello", timeout=15)
    connect_id = helper4.request("connect", config={
        "host": "127.0.0.1", "port": combined.port, "username": "user",
        "auth": "password", "password": FakeCluster.PASSWORD,
        "prompt_timeout": 20})
    prompt = helper4.wait_for(lambda e: e.get("event") in ("auth_prompt",
                                                           "result"))
    check(prompt is not None and prompt.get("event") == "auth_prompt",
          "the combined challenge still reaches the user")
    if prompt and prompt.get("event") == "auth_prompt":
        check(len(prompt["prompts"]) == 1,
              "but only the code is asked — the password the user already "
              "typed is filled in locally")
        helper4.send({"id": 0, "op": "auth_response",
                      "prompt_id": prompt["prompt_id"],
                      "responses": [FakeCluster.CODE]})
        result = helper4.result_for(connect_id)
        check(result is not None and result.get("ok"),
              "and the two-answer response the server wanted is assembled")
    helper4.close()
    combined.close()
    helper.close()

    # --- 9. key authentication, and the passphrase that must not leak ----
    print("Key authentication:")
    key_dir = os.path.join(workspace, "keys")
    os.makedirs(key_dir)
    key_file = os.path.join(key_dir, "id_rsa")
    user_key = paramiko.RSAKey.generate(2048)
    user_key.write_private_key_file(key_file)

    keyed = FakeCluster("key", remote_root, authorized_key=user_key)
    helper5 = Helper(helper_path)
    helper5.start_log()
    helper5.wait_for(lambda e: e.get("event") == "hello", timeout=15)
    connect_id = helper5.request("connect", config={
        "host": "127.0.0.1", "port": keyed.port, "username": "user",
        "auth": "key", "key_path": key_file})
    result = helper5.result_for(connect_id)
    check(result is not None and result.get("ok"), "public-key login works")
    check(result is not None and result.get("interactive") is False,
          "and needs nothing from the user")
    helper5.close()

    # A host key that CHANGED is the one failure that must not be shrugged
    # off and retried — trust on first use is only defensible if the second
    # use is checked. Simulated by rewriting the recorded key, which is
    # indistinguishable from the server's key changing under us.
    print("A changed host key:")
    known_hosts = os.path.join(fake_home, ".ssh", "known_hosts")
    entry = "[127.0.0.1]:%d" % keyed.port
    impostor = paramiko.RSAKey.generate(2048)
    with open(known_hosts) as handle:
        lines = handle.read().splitlines()
    with open(known_hosts, "w") as handle:
        for line in lines:
            handle.write(("%s ssh-rsa %s" % (entry, impostor.get_base64()))
                         if line.startswith(entry) else line)
            handle.write("\n")
    helper7 = Helper(helper_path)
    helper7.start_log()
    helper7.wait_for(lambda e: e.get("event") == "hello", timeout=15)
    connect_id = helper7.request("connect", config={
        "host": "127.0.0.1", "port": keyed.port, "username": "user",
        "auth": "key", "key_path": key_file})
    refused = helper7.result_for(connect_id)
    check(refused is not None and refused.get("ok") is False,
          "the connection is refused")
    check(refused is not None and refused.get("kind") == "hostkey",
          "with its own error kind, not lumped in with a bad password")
    helper7.close()
    keyed.close()

    # In KEY mode the Password field is a private key's PASSPHRASE. Sending
    # it to the server as the answer to a "Password:" challenge would hand
    # the key's protection to the far end, so the local auto-fill that key
    # mode's password field would otherwise trigger must not happen.
    print("A key passphrase is not a login password:")
    combined2 = FakeCluster("combined", remote_root)
    helper6 = Helper(helper_path)
    helper6.start_log()
    helper6.wait_for(lambda e: e.get("event") == "hello", timeout=15)
    helper6.request("connect", config={
        "host": "127.0.0.1", "port": combined2.port, "username": "user",
        "auth": "key", "key_path": key_file,
        "password": "the-key-passphrase", "prompt_timeout": 8})
    prompt = helper6.wait_for(lambda e: e.get("event") in ("auth_prompt",
                                                           "result"))
    check(prompt is not None and prompt.get("event") == "auth_prompt",
          "the server's challenge still arrives in key mode")
    if prompt and prompt.get("event") == "auth_prompt":
        texts = [entry["prompt"] for entry in prompt["prompts"]]
        check(len(texts) == 2 and any("Password" in t for t in texts),
              "and BOTH prompts are put to the user — the passphrase is "
              "never offered to the server as a password")
    helper6.close()
    combined2.close()

    # Key mode on a cluster that OFFERS publickey but refuses this key, then
    # wants a token. "No key worked" must not end the login attempt — the
    # ladder has to walk on to the next method the server named.
    print("Key refused, token accepted:")
    key2fa = FakeCluster("key2fa", remote_root)  # no authorized key at all
    helper8 = Helper(helper_path)
    helper8.start_log()
    helper8.wait_for(lambda e: e.get("event") == "hello", timeout=15)
    connect_id = helper8.request("connect", config={
        "host": "127.0.0.1", "port": key2fa.port, "username": "user",
        "auth": "key", "key_path": key_file, "prompt_timeout": 20})
    prompt = helper8.wait_for(lambda e: e.get("event") in ("auth_prompt",
                                                           "result"))
    check(prompt is not None and prompt.get("event") == "auth_prompt",
          "a rejected key falls through to the keyboard-interactive factor")
    if prompt and prompt.get("event") == "auth_prompt":
        helper8.send({"id": 0, "op": "auth_response",
                      "prompt_id": prompt["prompt_id"],
                      "responses": [FakeCluster.CODE]})
        result = helper8.result_for(connect_id)
        check(result is not None and result.get("ok"),
              "and the session opens on the token alone")
    helper8.close()
    key2fa.close()

    # --- 10. the security invariant --------------------------------------
    # Asserted from the OUTSIDE, over everything the helper was allowed to
    # touch, rather than by reading its source for suspicious calls: HOME was
    # redirected at the top of this test precisely so this sweep is total.
    print("Secrets:")
    written = []
    for root, _, names in os.walk(fake_home):
        for name in names:
            path = os.path.join(root, name)
            written.append(os.path.relpath(path, fake_home))
            with open(path, "rb") as handle:
                blob = handle.read()
            check(FakeCluster.CODE.encode() not in blob,
                  "%s does not contain the one-time code" % name)
            check(FakeCluster.PASSWORD.encode() not in blob,
                  "%s does not contain the password" % name)
    check(written == [os.path.join(".ssh", "known_hosts")],
          "the only file the helper ever wrote is known_hosts (got %s)"
          % (written or "nothing"))
    with open(os.path.join(fake_home, ".ssh", "known_hosts")) as handle:
        lines = [line for line in handle if line.strip()]
    # One line per distinct server, no matter how many times it was logged
    # into: a duplicate would mean the trust-on-first-use lookup is broken,
    # and a missing one would mean known_hosts was rewritten rather than
    # appended to (which is how the whole file's unparseable lines get lost).
    check(len(lines) == FakeCluster.instances
          and all("ssh-rsa" in line for line in lines),
          "one host-key line per cluster it met, appended not rewritten "
          "(%d lines / %d clusters)" % (len(lines), FakeCluster.instances))

    print("\n%d checks passed, %d failed" % (PASSED, len(FAILURES)))
    for failure in FAILURES:
        print("  FAILED: %s" % failure)
    return 1 if FAILURES else 0


def serve():
    """`--serve <mode> <root>`: run the fake cluster for another process."""
    mode = sys.argv[2] if len(sys.argv) > 2 else "2fa"
    root = sys.argv[3] if len(sys.argv) > 3 else tempfile.mkdtemp()
    os.makedirs(root, exist_ok=True)
    cluster = FakeCluster(mode, root)
    sys.stdout.write("PORT %d\n" % cluster.port)
    sys.stdout.flush()
    # readline() rather than `for line in sys.stdin`: iterating a pipe reads
    # ahead by a block, so a driver that sends one short command and waits
    # for the effect would deadlock against its own unread newline.
    while True:
        line = sys.stdin.readline()
        if not line:
            break
        command = line.strip()
        if command == "drop":
            cluster.drop_connections()
        elif command == "finish":
            cluster.job_state = None
        elif command == "queue":
            cluster.job_state = "RUNNING"
        elif command == "quit":
            break
        sys.stdout.write("OK %s\n" % command)
        sys.stdout.flush()
    cluster.close()
    return 0


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--serve":
        sys.exit(serve())
    sys.exit(main())
