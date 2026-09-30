# ncz_screensaver_patch.py — readable splice spec for upstream
# /usr/bin/ncz-screensaver.
#
# The upstream launcher is the 2400+-line /usr/bin/ncz-screensaver in the
# ncz-screensavers Debian package. The bug fix in this branch is a
# small change at three call sites (see the unified-diff patch in
# patches/launcher-gpu-class.patch). This module renders the patch as
# normal Python so a reviewer can read it as code (rather than as a
# diff) and so a unit test can confirm the patch applies cleanly to
# the upstream source if/when we get a chance to verify it offline.
#
# The actual patch text (in unified-diff form, suitable for `git am`)
# lives in patches/launcher-gpu-class.patch. This file is the readable
# commentary + the helper that the launcher's gpu_class() uses.
#
# The three splice sites are:
#
#   1. gpu_class()  (line ~820-870 in 0.7.1)
#      BEFORE:  code, ident = _calibrator_json(["--identify"], env, 30)
#               if code == 3:
#                   return {"class": "weak", "source": "refused",
#                           "software": True}
#               if ident is None:
#                   return entry or fallback_entry("", gpu)
#      AFTER:   augment_calibrator_env(env)
#               code, ident = _calibrator_json(["--identify"], env, 30)
#               if code == 3:
#                   return classify_from_calibrator_result(
#                       code=3, gpu=gpu, renderer_hint="",
#                       all_gpus=_all_known_gpus()).to_dict()
#               if ident is None:
#                   if entry is None:
#                       # ... new refusal-aware fallback for the
#                       # exit-2 path the launcher's pre-fix code does
#                       # not handle ...
#                       return classify_from_calibrator_result(...).to_dict()
#                   return entry
#
#   2. gpu_class() (just above the calibrator call)
#      BEFORE:  for name in ("NCZ_ALLOW_SOFTWARE", "NCZ_CALIBRATE_MS"):
#                   env.pop(name, None)
#      AFTER:   augment_calibrator_env(env)         # Sky1 loader path
#               for name in ("NCZ_ALLOW_SOFTWARE", "NCZ_CALIBRATE_MS"):
#                   env.pop(name, None)
#
#   3. Top of module (just after the docstring):
#      ADD:     try:
#                   from gpu_classifier import (
#                       classify_from_calibrator_result,
#                       augment_calibrator_env,
#                       discover_gpus,
#                   )
#               except ImportError:
#                   def classify_from_calibrator_result(*a, **kw): return None
#                   def augment_calibrator_env(env): return env
#                   def discover_gpus(): return []
#
#   4. New helper `_all_known_gpus()` (between the existing
#      class_from_topology() and the calibrator-bin helpers). It
#      merges list_gpus() (DRM cards) with discover_gpus() (misc
#      devices) so classify_from_calibrator_result() can see both.
#
# The patch in patches/launcher-gpu-class.patch applies all four
# changes; the helper `_all_known_gpus()` lives in the launcher
# itself (not in gpu_classifier) so the launcher keeps a single
# namespace it can call.

from __future__ import annotations

from typing import Mapping, Optional

from gpu_classifier import (
    ClassifierEntry,
    EXIT_SOFTWARE_REFUSED,
    classify_from_calibrator_result,
    augment_calibrator_env,
    discover_gpus,
)


def _refusal_fallback_dict(
    gpu: Optional[Mapping[str, object]],
    all_gpus: Optional[list] = None,
) -> dict:
    """Splice-site replacement for the launcher's `if code == 3:` line.

    Returns a dict that matches the launcher's existing return shape so
    downstream consumers (`offload_targets`, `plan_for`, the settings
    UI, the cache file) don't need to change. The dict has the same
    keys as before plus optional `note`, `driver`, `effective_driver`
    fields that the UI can ignore.

    The only behaviour change is: when a real hardware GPU is bound,
    this returns the topology class (mid for mali/mali_kbase) instead
    of unconditionally returning weak. The original weak-on-refused line
    is preserved as the no-hardware-bound branch of
    `classify_from_calibrator_result`.
    """
    entry = classify_from_calibrator_result(
        code=EXIT_SOFTWARE_REFUSED,
        gpu=gpu,
        all_gpus=all_gpus,
    )
    return entry.to_dict()


def _no_ident_fallback_dict(
    code: int,
    gpu: Optional[Mapping[str, object]],
    renderer_hint: str,
    all_gpus: Optional[list] = None,
) -> dict:
    """Splice-site replacement for the launcher's `if ident is None:`
    branch.

    The pre-fix launcher returned `entry or fallback_entry("", gpu)`.
    On .66 today this means fallback_entry("", display_gpu) which is
    weak (display_gpu.driver == "linlondp" — a display controller, not
    a 3D GPU). The new classifier consults the misc/mali0 entry too
    and returns "mid".
    """
    entry = classify_from_calibrator_result(
        code=code,
        gpu=gpu,
        renderer_hint=renderer_hint,
        all_gpus=all_gpus,
    )
    return entry.to_dict()


__all__ = [
    "_refusal_fallback_dict",
    "_no_ident_fallback_dict",
    "classify_from_calibrator_result",
    "augment_calibrator_env",
    "discover_gpus",
]
