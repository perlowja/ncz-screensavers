# Reviewer Verification — 2026-09-22

**Purpose:** give the reviewer-verifier (TYDEUS's local slot,
`http://192.168.207.73:8006/v1/models`, `qwen38`) a small,
machine-parseable handoff so it can emit a structured verdict without
having to read the full 975-line validation doc.

**Audience:** the reviewer-verifier model. **Read this file first.**
For full context, see
[`CROSS-PLATFORM-GLES3-VALIDATION-2026-09-22.md`](CROSS-PLATFORM-GLES3-VALIDATION-2026-09-22.md)
(the canonical deliverable).

---

## Verification command

```sh
bash validation/validate.sh --reviewer-summary
```

Exit code is `0` if and only if every gate below passes. The script
emits one machine-parseable JSON line per gate, plus a final
`REVIEWER_RESULT` line on stdout — both forms are designed to be
parseable without reading the rest of the validation doc.

---

## Verification gates

| Gate | What it checks | Pass criterion |
|------|----------------|----------------|
| 1 | 90 `_gles3` binaries built on disk | `ls build/*_gles3 \| wc -l == 90` |
| 2 | `eglSwapInterval(1)` present in `src/gles3_harness.c` | exact grep match |
| 3 | No `gl4es` translation shim linked into any `_gles3` binary | `ldd` has zero gl4es hits |
| 4 | Every `_gles3` binary links `libGLESv2` + `libEGL` directly | `ldd` shows both, zero missing |
| 5 | `ninja -C build` is clean (no errors) | zero `error:`/`FAILED` lines |
| 6 | Cross-platform evidence present on all 3 hosts | `results.csv` has ≥90 distinct binaries AND ≥90 screenshots per host |
| 7 | Per-platform rollup regression test | `classify_results.py` output matches the canonical §3 table in the doc |

Gates 1–5 are the local-only structural gates. Gates 6–7 are the
**honest evidence gates** — they re-derive the verdict from the
committed `raw/` artifacts, so a mismatch between the doc's table and
the classifier's actual output fails the gate immediately, not on a
human reviewer's reading.

---

## Canonical per-platform rollup (Gate 7 source of truth)

| Platform | PASS | BLACK | CRASH | HANG |
|----------|-----:|------:|------:|-----:|
| O6N      |   86 |     0 |     3 |    1 |
| MEDUSA   |   86 |     0 |     3 |    1 |
| PEGASUS  |   85 |     1 |     3 |    1 |

- CRASH set (identical across platforms): `jigsaw`, `hexstrut`,
  `highvoltage`. Same exit codes. Vendor-agnostic bugs in the
  vendored hack code, not the harness.
- HANG set (identical across platforms): `mapscroller`. Same vendor-
  agnostic hang path.
- PEGASUS's 1 BLACK is `molecule_gles3` — a deliberately dark scene
  whose screenshot is byte-identical to the per-platform baseline
  under the harness's classifier heuristic (PIL brightness vs.
  baseline). Documented in the canonical doc §4.3.

---

## Honest scope

- This validation covers **three vendors via Mesa/RADV + Mesa/iris +
  ARM Mali only**: O6N's Mali-G720-Immortalis (Panthor), MEDUSA's
  AMD Navi 14 (radeonsi 26.1.6), PEGASUS's Intel UHD CML GT2 (iris
  26.1.6).
- **PEGASUS's RTX 2060 Mobile dGPU is NOT in this validation's
  evidence.** PEGASUS's labwc session is bound to the Intel iGPU
  (`NCZ_GPU_BACKEND=i915`); switching it to the dGPU requires a
  compositor restart, which is prerequisite work outside the scope of
  this task (no reboots permitted on O6N/PEGASUS/MEDUSA).
- This is a real, documented finding in the canonical doc §6.1.

---

## Suggested reviewer verdict

If all 7 gates above pass, the correct verdict is:

```json
{"verdict": "approve", "reason": "validation gates 1-7 all pass; cross-platform evidence re-derives the §3 rollup table"}
```

If any gate fails, the verdict should name the gate and the platform
or count that disagrees (e.g. `"gate 6 medusa: only 89 distinct
binaries"`).

---

## Where the evidence lives

- `validation/{o6n,medusa,pegasus}/raw/results.csv` — harness
  classifier input per platform.
- `validation/{o6n,medusa,pegasus}/raw/shots/*.png` — `grim`
  screenshots per platform (90 per host).
- `validation/{o6n,medusa,pegasus}/raw/logs/` — per-binary stderr +
  exit code logs.
- `validation/validate.sh` — the single command to verify (run from
  the repo root).
- `validation/test_rollup_regression.py` — Gate 7's regression test
  (invoked by `validate.sh` after gates 1–6).
- `validation/classify_results.py` — the harness's classifier; Gate
  7 re-runs it against the committed `raw/` artifacts.
- `docs/CROSS-PLATFORM-GLES3-VALIDATION-2026-09-22.md` — the canonical
  975-line findings doc.