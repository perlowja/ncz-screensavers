#__init__.py — exposes the package's public surface so the upstream launcher
# can `from gpu_classifier import classify_from_calibrator_result, augment_calibrator_env`
# after dropping this directory into /usr/share/ncz-screensavers/ or
# wherever the launcher sources it.
#
# Don't add heavy imports here — augment_calibrator_env only needs os/pathlib,
# gpu_classifier only needs dataclasses/re/typing, and pulling in
# ncz_screensaver_patch transitively is fine (it imports gpu_classifier).

from gpu_classifier import (
    ClassifierEntry,
    CLASSES,
    CLASS_RANK,
    CLASS_WEAK_MS,
    CLASS_MID_MS,
    RENDERER_TABLE,
    DRIVER_NAMES_MID,
    DRIVER_NAMES_DISPLAY_CONTROLLER,
    DRIVER_NAMES_HARDWARE_GPU_DRM,
    DRIVER_NAMES_HARDWARE_GPU_MISC,
    GpuTopology,
    classify_from_calibrator_result,
    class_from_ms,
    class_from_renderer,
    class_from_topology,
    class_from_topology_compat,
    discover_gpus,
    driver_is_hardware_gpu,
    driver_is_display_controller,
    augment_calibrator_env,
    EXIT_OK,
    EXIT_ERROR,
    EXIT_NO_CONFIG,
    EXIT_SOFTWARE_REFUSED,
    EXIT_NO_COMPOSITOR,
)
from calibrator_env import (
    augmented_calibrator_env,
    is_sky1_arm64,
    sky1_loader_paths_available,
)
from ncz_screensaver_patch import (
    _refusal_fallback_dict,
    _no_ident_fallback_dict,
)

__all__ = [
    "ClassifierEntry",
    "CLASSES",
    "CLASS_RANK",
    "CLASS_WEAK_MS",
    "CLASS_MID_MS",
    "RENDERER_TABLE",
    "DRIVER_NAMES_MID",
    "DRIVER_NAMES_DISPLAY_CONTROLLER",
    "DRIVER_NAMES_HARDWARE_GPU_DRM",
    "DRIVER_NAMES_HARDWARE_GPU_MISC",
    "GpuTopology",
    "classify_from_calibrator_result",
    "class_from_ms",
    "class_from_renderer",
    "class_from_topology",
    "class_from_topology_compat",
    "discover_gpus",
    "driver_is_hardware_gpu",
    "driver_is_display_controller",
    "augment_calibrator_env",
    "EXIT_OK",
    "EXIT_ERROR",
    "EXIT_NO_CONFIG",
    "EXIT_SOFTWARE_REFUSED",
    "EXIT_NO_COMPOSITOR",
    "augmented_calibrator_env",
    "is_sky1_arm64",
    "sky1_loader_paths_available",
    "_refusal_fallback_dict",
    "_no_ident_fallback_dict",
]
