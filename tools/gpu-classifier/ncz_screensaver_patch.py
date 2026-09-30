# ncz_screensaver_patch.py — readable splice spec for upstream /usr/bin/ncz-screensaver.
#
# The upstream launcher is the 2400-line /usr/bin/ncz-screensaver in the
# ncz-screensavers Debian package. The bug fix in this branch is a
# 25-line change at three call sites. This module renders the patch as
# normal Python so a reviewer can read it as code (rather than as a
# diff) and so a unit test can confirm the patches apply cleanly to
# the upstream source if/when we get a chance to verify it offline.
#
# The actual patch text (in unified-diff form, suitable for `git am`)
# lives in patches/launcher-gpu-class.patch. This file is the readable
# commentary.
#
# The three splice sites are:
#
#   1. gpu_class()  (line ~804-870 in 0.7.1)
#      BEFORE:  code, ident = _calibrator_json(["--identify"], env, 30)
#               if code == 3:
#                   return {"class": "weak", "source": "refused",
#                           "software": True}
#      AFTER:   code, ident = _calibrator_json(["--identify"], env, 30)
#               if code == 3:
#                   return _refusal_fallback(gpu)
#
#   2. gpu_class() (same function, just above the calibrator call)
#      BEFORE:  env = build_child_env("calibrate", settings, offload=False)
#               ...
#               for name in ("NCZ_ALLOW_SOFTWARE", "NCZ_CALIBRATE_MS"):
#                   env.pop(name, None)
#               code, ident = _calibrator_json(...)
#      AFTER:   env = build_child_env("calibrate", settings, offload=False)
#               ...
#               augment_calibrator_env(env)
#               for name in ("NCZ_ALLOW_SOFTWARE", "NCZ_CALIBRATE_MS"):
#                   env.pop(name, None)
#               code, ident = _calibrator_json(...)
#
#   3. class_from_topology(gpu) (line ~733-744 in 0.7.1)
#      No change required — the existing implementation already returns
#      "mid" for mali_kbase, which is what we need on .66.
#
# The helper function `_refusal_fallback` is added near the bottom of
# gpu_class.py (or as a new top-level function in the launcher). It is
# a one-line wrapper around `classify_from_calibrator_result` so the
# launcher can keep its existing dict-returning shape.

from __future__ import annotations

from typing import Mapping, Optional

from .gpu_classifier import (
    ClassifierEntry,
    EXIT_SOFTWARE_REFUSED,
    classify_from_calibrator_result,
)


def _refusal_fallback(gpu: Optional[Mapping[str, object]]) -> dict:
    """Splice-site replacement for the `if code == 3:` line in
    gpu_class().

    Returns a dict that matches the launcher's existing return shape so
    downstream consumers (`offload_targets`, `plan_for`, the settings
    UI, the cache file) don't need to change. The dict has the same
    keys as before plus an optional `note` and `driver` that the UI can
    ignore.

    The only behaviour change is: when a real hardware GPU is bound,
    this returns the topology class (mid for mali_kbase) instead of
    unconditionally returning weak. The original weak-on-refused line
    is preserved as the no-hardware-bound branch of
    `classify_from_calibrator_result`.

    The 3 lines of code this replaces:

        if code == 3:
            return {"class": "weak", "source": "refused",
                    "software": True}

    become this single call:

        if code == 3:
            return _refusal_fallback(gpu)
    """
    entry = classify_from_calibrator_result(
        code=EXIT_SOFTWARE_REFUSED,
        gpu=gpu,
    )
    return entry.to_dict()


__all__ = ["_refusal_fallback"]