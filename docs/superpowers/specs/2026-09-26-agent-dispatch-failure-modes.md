# Agent dispatch failure modes — measured, 2026-09-25/26

Every entry here cost real work in one session of supervising four
concurrent coding agents across the fleet. They are ordered by how much
they cost.

## 1. Batching commits to the end loses everything

**Observed four times.** An agent works for 10-25 minutes, accumulates
real findings, then the turn ends — cancelled, rate-limited, or the
process vanishes — and **every uncommitted finding is gone**. Nothing in
the transcript survives into the next run.

The worst case: an rss-sdl2 root-cause investigation died blocked on a
hung `sshpass` with real diagnostic findings in context and produced
nothing at all.

**Fix, and put it in the brief, not just in hope:** commit and push after
every meaningful discovery, not at the end of a phase and not when it is
tidy. A scratch document under `docs/superpowers/rounds/` committed
repeatedly is the right shape. **A messy committed document beats a clean
lost one.** Say that sentence in the brief; agents act on it.

## 2. `pkill -f '<pattern>'` kills the shell running it

**Self-inflicted, twice.** `pkill -f 'zoder exec'` issued over SSH matches
the remote shell whose own command line contains that text, so the command
kills itself. Symptom is `exit code 255` with no other explanation, or a
background task dying with exit 144.

**Fix:** bracket one character so the pattern cannot match itself —
`pkill -f 'z[o]der exec'`. Same trick as `grep -c '[z]oder exec'`.

Already documented for `pgrep` in the fleet rules; it applies identically
to `pkill`, and knowing the `pgrep` case did not prevent the `pkill` case.

## 3. Unbounded remote calls hang the whole run

`sshpass`/`ssh` with no timeout can block indefinitely — a password prompt
nobody answers, an unreachable host, a wedged remote process. The agent
sits there until its turn budget expires and dies with nothing.

**Fix:** every remote call gets `timeout 60`, plus
`-o NumberOfPasswordPrompts=1 -o ConnectTimeout=10`. If a call times out,
note it and move on rather than retrying the same thing.

## 4. Concurrency against one provider causes cascading rate limits

Four concurrent MiniMax agents hit HTTP 429 fleet-wide. Earlier failures
that looked like "turn did not complete" after 170-770s were **almost
certainly 429s landing mid-turn**, misdiagnosed as long-task exploration
problems — and resuming three at once made it worse, all three dying in
under a second with zero tools.

**Fix:** serialize. One job per provider at a time, with a quota probe
before dispatch and a backoff on 429. The probe must hit the real
endpoint (`POST /v1/chat/completions`) — the native API 401s for a valid
key and reads as an auth failure.

## 5. A supervisor loop cannot see its own premises change

Two variants, both self-inflicted:

- **A stale prompt overrides a new decision.** The operator amended the
  scoring rubric; the supervisor kept re-dispatching a fixed prompt that
  still carried the withdrawn instruction, and the agent dutifully undid
  the amendment on the next cycle. The agent did what it was told; the
  telling was stale.
- **A running loop does not re-read its own config.** A job added to the
  supervisor's array while it was running was never picked up, because
  bash read the array once at startup. The highest-value task in the queue
  sat undispatched for six hours.
- **No completion condition.** The loop kept re-dispatching finished work;
  38 rounds produced zero commits, each agent correctly reporting
  `"No state change. No action."`

**Fix:** treat a supervisor's prompt files as mutable state that must be
rewritten when a decision changes, restart the loop when its job list
changes, and give it a real completion check rather than a round counter.

## 6. Agents invent disqualifying criteria

A scoring agent cut the highest-scoring target in its family (9 of 9) on
subject matter the rubric never mentioned, and recorded it as a normal
table row. It would have shipped as a plain `CUT`.

**Fix:** state in the brief that the agent scores against the given axes
and nothing else, and that anything it wants to exclude on other grounds
gets **surfaced for a human decision, never resolved in the table.**

## 7. Briefs go stale between writing and dispatch

A brief said "fix the 3 known black rss hacks". The real number was
thirteen — the figure came from a curation document that had been
superseded by a newer matrix. The agent that swept the extra targets in
was right and the supervisor's correction was wrong.

**Fix:** re-derive counts from the live artifact at dispatch time, not
from a prose document. When an agent contradicts a brief's numbers, check
the primary source before correcting the agent.

## The common thread

Five of these seven are **the supervisor's fault, not the agent's**. The
agents mostly did what they were told; the instructions were stale, the
loop could not notice, or the dispatch mechanics were broken. When
supervising concurrent agents, the failure to look for first is in the
supervision, not the worker.

---

## Test-host policy — MEDUSA is the display, PEGASUS is the lab

Operator, 2026-09-26: *"we only want one test at once"* and *"if PEGASUS is available, use
it for separate tests."*

### MEDUSA (192.168.207.84) — the VISIBLE display. ONE test at a time.

This is the screen a human actually watches. Concurrent renderers split the GPU, make any
frame-rate judgement meaningless, and have repeatedly been left running for hours after
the job that launched them finished. Three separate leaks were cleared on 2026-09-26
alone, including a `blackhole` that ran 22 minutes alongside a shadertoy rotation the
operator was evaluating.

**Never launch a renderer on MEDUSA directly.** Use the lock helper:

    ncz-display-run [-t SECONDS] <binary> [args...]   # take the display
    ncz-display-run --stop                            # clear it
    ncz-display-run --who                             # who holds it

It takes an exclusive `flock`, kills any existing renderer first, records the holder in
`/tmp/ncz-display.owner`, and cleans up on exit even if the caller is killed. If the
display is busy it REFUSES and names the holder rather than stacking a second renderer.

It kills with `-9` deliberately: several of these binaries do not honour SIGTERM
(confirmed on `neongravity-0` and `amigajuggler`, which hang at exit and leave zombies).

The visible session belongs to **uid 1001 `jasonperlow`**, socket
`/run/user/1001/wayland-0` — NOT uid 1000 `medusa`. Launching on medusa's socket renders
into a compositor nobody is looking at. The helper handles this; do not hand-roll it.

### PEGASUS (192.168.207.85) — the automated lab. Parallel work goes here.

Headless capture, sweeps, matrices, per-target evidence. Multiple agents may use it
concurrently, and it is where anything that does not need a human watching belongs.

**Default to PEGASUS.** Only take MEDUSA's display when a human is going to LOOK at the
result in real time.

Two standing hazards there:
- `mapscroller_gles3` wedges and never exits (observed at 2h18m and at 4min). Exclude it
  from sweeps or enforce a hard per-target timeout.
- `/tmp` is tmpfs and has filled, after which `fwrite` reports success while files land
  0-byte — silent data loss that reads as "black frames". Use `~/build-tmp/` and check
  headroom before a large sweep.

### Every brief that renders anything must say

1. Which host, and why that host.
2. That MEDUSA is taken via `ncz-display-run` or not at all.
3. **Clean up before reporting** — verify nothing you started is still running. An agent
   that leaves a renderer alive has not finished, whatever its report says.
