# Remote HPC execution

The {guilabel}`HPC` panel submits calculations to a cluster over
SSH, monitors the queue, and brings the results back — without leaving
Calango. The staged job is the same self-contained directory a local run
uses ({doc}`/simulations/jobs`), which is exactly what makes it uploadable:
`run.py` plus the geometry *are* the whole job, and the cluster never needs
Calango installed.

:::{warning}
Remote execution needs `paramiko` in Calango's Python environment, and SSH
access to a cluster running SLURM, PBS or SGE.
:::

**All network traffic runs in a separate helper process driven over a JSON
protocol**, mirroring how local jobs are isolated: the interface never
blocks on the network, and a hung connection can always be resolved by
killing the helper rather than the application (details below).

**The connection is a session, not a login per operation.** Calango
authenticates once and multiplexes every upload, submission, status poll,
log tail and download over that one SSH transport. On a cluster with
keyboard-interactive **two-factor authentication** this is the difference
between usable and unusable: you type the one-time code once, not once per
30-second queue poll.

---

## Connection

The {guilabel}`Connection` tab holds the cluster credentials:

| Field | Meaning |
|---|---|
| {guilabel}`Host`, {guilabel}`Port` | Hostname and port, default **22** |
| {guilabel}`User` | Your account name on the cluster |
| {guilabel}`Auth` | {guilabel}`SSH key` or {guilabel}`Password` |
| {guilabel}`Key file` | Path to a private key, e.g. `~/.ssh/id_rsa`. Leave empty to let your SSH agent or default keys authenticate |
| {guilabel}`Password` | The password — or, in key mode, the passphrase for an encrypted key |
| {guilabel}`Remote dir` | Working directory on the cluster, relative to `$HOME`, default `calango_jobs` |

{guilabel}`Connect` opens the session, reports your remote `$HOME`, and
**auto-detects the scheduler** by probing for `sbatch`, then `qsub`, on the
remote `PATH`. The status line next to it tracks the session, and
{guilabel}`Disconnect` closes it — a job already submitted keeps running on
the cluster regardless.

You do not have to press it first: asking for any operation while
disconnected authenticates and then runs it.

### Two-factor authentication

If the cluster asks for a second factor, Calango puts the server's own
challenge on screen — its wording, its prompts, with the answer masked
whenever the server says it must not be echoed — and sends what you type
straight back. Clusters that ask for the password and the code in one
exchange get the password filled in from the {guilabel}`Password` field, so
only the code is asked for.

The code is never stored: it exists on the wire and nowhere else, which is
also why a session opened with 2FA is **never re-established silently**. If
such a session drops, the panel says so and waits — you press
{guilabel}`Connect`. A session opened with a key or a password has nothing
single-use about it and simply reconnects by itself on the next operation.

:::{important}
Passwords and key passphrases are kept **in memory only** — never written
to disk, and never placed on a command line where `ps` could see them: they
travel to the helper process over stdin. One-time codes are not kept even
that long. Everything else (host, user, key path, scheduler settings) is
remembered between sessions. Unknown host keys are accepted on first
contact, like a fresh `ssh` answered with "yes", and pinned thereafter — a
host key that *changes* is refused outright and reported as such.
:::

:::{note}
In key mode the {guilabel}`Password` field is the key's **passphrase** and
is never sent to the server, only used to unlock the key file locally.
:::

---

## Scheduler settings

The {guilabel}`Scheduler` tab describes the batch job:

- {guilabel}`Scheduler` — {guilabel}`SLURM`, {guilabel}`PBS` or
  {guilabel}`SGE`.
- {guilabel}`Queue` — partition or queue name; empty uses the cluster
  default.
- {guilabel}`Tasks / cores` — requested cores.
- {guilabel}`Walltime` — `HH:MM:SS`, default **01:00:00**.
- {guilabel}`Setup` — shell lines executed before the payload: module
  loads, `conda activate`, anything your site needs.

```bash
module load python
source ~/venvs/ase/bin/activate
```

The generated wrapper carries the right directives for your scheduler —
`#SBATCH`, `#PBS` or `#$` — and **always redirects output to fixed file
names** (`calango_job.out`, `calango_job.err`), so the monitor knows what to
tail regardless of how the site templates its jobs.

---

## Submitting and monitoring

{menuselection}`Simulation --> New Remote Calculation`
({kbd}`Ctrl+Shift+R`) — or {guilabel}`Submit Calculation…` in the panel —
opens the usual calculator dialog, then runs the full sequence:

1. Stage `run.py` and `structure.extxyz` into a local job directory,
   exactly as a local run would ({doc}`/simulations/jobs`).
2. Generate `job.sh` from the scheduler settings.
3. Upload the directory over SFTP, creating the remote path as needed.
4. Submit with `sbatch` or `qsub` and capture the job ID from the
   scheduler's reply.
5. Start monitoring.

The {guilabel}`Queue & Logs` tab then shows the job ID and remote
directory, the live queue state polled with `squeue` or `qstat` (every
**10 s** by default), and the remote standard output and error streamed
incrementally into the console — errors in red. PBS and SGE short codes are
normalized to readable states (`R` → RUNNING, `qw` → PENDING, `H`/`hqw` →
HELD), so the display reads the same whatever the cluster runs.
{guilabel}`Cancel Job` issues `scancel` or `qdel`.

% TODO screenshot: HPC panel on the Queue & Logs tab — job ID, RUNNING state, remote stdout streaming into the console
```{figure} /_static/img/remote_queue_logs.png
:alt: The Queue and Logs tab streaming a running remote job
:width: 92%
:figclass: screenshot

Queue & Logs during a run — the queue state is polled, and the remote job's stdout and stderr stream into the console as they are written.
```

---

## Retrieving results

When the job leaves the queue, Calango waits one extra poll for the
filesystem to flush trailing output, then **downloads the results
automatically** — structures, trajectories, logs and metric files — into
the same local job directory the inputs came from. Trajectories and final
geometries open directly in a new tab, exactly as a finished local run's
would.

{guilabel}`Download Results` repeats the transfer manually — useful for
pulling intermediate output from a long-running job without waiting for it
to finish.

The task appears in the Process Manager like any other, so its directory
stays one click away for later analysis ({doc}`/simulations/jobs`).

---

## The session helper

SSH and SFTP never run inside the GUI. One long-lived `calango_remote.py` —
a bundled paramiko script — runs under the embedded interpreter's Python as
a `QProcess`, holds the authenticated transport, and serves every operation
over it:

- Requests arrive on its stdin, one JSON object per line, each carrying an
  `id`: `connect`, then `probe`, `upload`, `submit`, `monitor`, `download`,
  plus `cancel`, `disconnect` and the `auth_response` that answers a
  challenge. **Credentials ride in that stdin payload, never in `argv`**, so
  they are invisible to `ps`.
- Events come back the same way, tagged with the `id` they belong to, so
  several operations can be in flight at once over the one connection:
  `uploaded`, `state`, `log`, `downloaded`, then a `result` object with `ok`
  and — on failure — a `kind` (`auth`, `hostkey`, `network`, `remote`,
  `auth_required`) so the interface can tell a rejected password from a
  dropped link.
- Session-level events (`session`, `auth_prompt`) use id 0. The prompt event
  is what raises the two-factor dialog.
- `monitor` polls the queue and tails `calango_job.out` / `calango_job.err`
  by byte offset, emitting only the new text — now as one request among many
  rather than a process of its own.
- The transport is kept alive with SSH keepalives every 30 s, because campus
  firewalls drop idle NAT entries long before an overnight job finishes.

This is the same philosophy as the local job runner: the GUI reads
structured events from a subprocess it can always kill. A wedged
connection, a firewalled port, a cluster that stops answering — none of
them can hang the interface, and aborting is always possible.

:::{note}
Paramiko rather than OpenSSH `ControlMaster` for the multiplexing, for three
reasons: Windows OpenSSH implements no connection multiplexing at all, the
2FA prompt would belong to a terminal Calango does not have, and paramiko is
already a dependency. The trade-off is that `~/.ssh/config` is **not** read,
so `ProxyJump` bastion setups are not inherited — connect to the bastion's
own address, or open a tunnel outside Calango and point the panel at it.
:::

:::{note}
If `paramiko` is missing, the very first helper event says so explicitly —
`pip install paramiko` into Calango's Python environment — rather than the
connection appearing to hang.
:::
