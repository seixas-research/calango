# Remote HPC execution

The {guilabel}`Remote Access` panel submits calculations to a cluster over
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

{guilabel}`Test Connection` verifies everything, reports your remote
`$HOME`, and **auto-detects the scheduler** by probing for `sbatch`, then
`qsub`, on the remote `PATH`.

:::{important}
Passwords and key passphrases are kept **in memory only** — never written
to disk, and never placed on a command line where `ps` could see them: they
travel to the helper process over stdin. Everything else (host, user, key
path, scheduler settings) is remembered between sessions. Unknown host keys
are accepted on first contact, like a fresh `ssh` answered with "yes", and
pinned thereafter.
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

% TODO screenshot: Remote Access panel on the Queue & Logs tab — job ID, RUNNING state, remote stdout streaming into the console
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

## The helper process

SSH and SFTP never run inside the GUI. Every operation spawns
`calango_remote.py` — a bundled paramiko script — through the embedded
interpreter's Python as its own `QProcess`:

- One JSON request arrives on the helper's stdin: the connection config,
  the operation (`probe`, `upload`, `submit`, `monitor`, `download`) and
  its arguments. **Credentials ride in that stdin payload, never in
  `argv`**, so they are invisible to `ps`.
- The helper answers with one JSON object per line on stdout — `uploaded`,
  `state`, `log` and `downloaded` events while the operation runs, then a
  final `result` object with `ok` and any error text.
- `probe`, `upload`, `submit` and `download` are short-lived, one process
  per operation; `monitor` is a single long-lived process that polls the
  queue and tails `calango_job.out` / `calango_job.err` by byte offset,
  emitting only the new text.

This is the same philosophy as the local job runner: the GUI reads
structured events from a subprocess it can always kill. A wedged
connection, a firewalled port, a cluster that stops answering — none of
them can hang the interface, and aborting is always possible.

:::{note}
If `paramiko` is missing, the very first helper event says so explicitly —
`pip install paramiko` into Calango's Python environment — rather than the
connection appearing to hang.
:::
