# __init__.py — exposes the package's public surface so the upstream launcher
# can `from gpu_classifier import classify_from_calibrator_result, augment_calibrator_env`
# after dropping this directory into /usr/share/ncz-screensavers/ or
# wherever the launcher sources it.
#
# The directory name uses a dash ('gpu-classifier') to match the upstream
# tooling convention (apt path, systemd unit names, etc.); ruff's N999
# warns about that. Disable the warning locally — the import path uses
# underscores (`gpu_classifier`) and that's what downstream code uses.
#
# Don't add heavy imports here — augment_calibrator_env only needs os/pathlib,
# gpu_classifier only needs dataclasses/re/typing, and pulling in
# ncz_screensaver_patch transitively is fine (it imports gpu_classifier).

from calibrator_env import (
    augmented_calibrator_env,
    is_sky1_arm64,
    sky1_loader_paths_available,
)
from gpu_classifier import (
    CLASS_MID_MS,
    CLASS_RANK,
    CLASS_WEAK_MS,
    CLASSES,
    DRIVER_NAMES_DISPLAY_CONTROLLER,
    DRIVER_NAMES_HARDWARE_GPU_DRM,
    DRIVER_NAMES_HARDWARE_GPU_MISC,
    DRIVER_NAMES_MID,
    EXIT_ERROR,
    EXIT_NO_COMPOSITOR,
    EXIT_NO_CONFIG,
    EXIT_OK,
    EXIT_SOFTWARE_REFUSED,
    RENDERER_TABLE,
    ClassifierEntry,
    GpuTopology,
    augment_calibrator_env,
    class_from_ms,
    class_from_renderer,
    class_from_topology,
    class_from_topology_compat,
    classify_from_calibrator_result,
    discover_gpus,
    driver_is_display_controller,
    driver_is_hardware_gpu,
)
from ncz_screensaver_patch import (
    _no_ident_fallback_dict,
    _refusal_fallback_dict,
)

__all__ = [
    "CLASSES",
    "CLASS_MID_MS",
    "CLASS_RANK",
    "CLASS_WEAK_MS",
    "DRIVER_NAMES_DISPLAY_CONTROLLER",
    "DRIVER_NAMES_HARDWARE_GPU_DRM",
    "DRIVER_NAMES_HARDWARE_GPU_MISC",
    "DRIVER_NAMES_MID",
    "EXIT_ERROR",
    "EXIT_NO_COMPOSITOR",
    "EXIT_NO_CONFIG",
    "EXIT_OK",
    "EXIT_SOFTWARE_REFUSED",
    "RENDERER_TABLE",
    "ClassifierEntry",
    "GpuTopology",
    "_no_ident_fallback_dict",
    "_refusal_fallback_dict",
    "augment_calibrator_env",
    "augmented_calibrator_env",
    "class_from_ms",
    "class_from_renderer",
    "class_from_topology",
    "class_from_topology_compat",
    "classify_from_calibrator_result",
    "discover_gpus",
    "driver_is_display_controller",
    "driver_is_hardware_gpu",
    "is_sky1_arm64",
    "sky1_loader_paths_available",
]
