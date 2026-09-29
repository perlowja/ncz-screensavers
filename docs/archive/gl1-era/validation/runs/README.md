# Validation Run Records

Each file in this directory is the captured `--reviewer-summary` output of
one invocation of `bash validation/validate.sh`, kept for audit/replay.

## Filename format

```
<UTC-timestamp>_reviewer_summary.json
```

`UTC-timestamp` is `YYYY-MM-DDTHH-MM-SSZ` (ISO 8601, file-system-safe).

## Contents

One JSON object per line (10 lines for a passing run), plus a final
`REVIEWER_RESULT:` line carrying the overall verdict. Each line is
<200 bytes; total file is ~1.1 KB. The `validate.sh` self-validation
step (`python3 -c 'import sys,json;json.loads(...)'`) ensures every
line parses before `REVIEWER_RESULT` is emitted, so any file in this
directory is guaranteed to be all-parseable JSON.

## How to re-derive

```sh
WAIVE_RUNTIME=1 bash validation/validate.sh --reviewer-summary 2>/dev/null > runs/<timestamp>_reviewer_summary.json
```

`WAIVE_RUNTIME=1` is required when running on a build host without a
live Wayland session. See `docs/REVIEWER-VERIFICATION.md` for the
rationale and the reviewer-verifier handoff shape.

## Re-parse any captured run

```sh
python3 -c "
import json
with open('validation/runs/<file>') as f:
    for line in f:
        line = line.strip()
        if not line: continue
        if line.startswith('REVIEWER_RESULT: '):
            line = line[len('REVIEWER_RESULT: '):]
        json.loads(line)
print('all lines parse as JSON')
"
```
