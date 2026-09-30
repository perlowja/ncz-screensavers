# tests/ — unit tests for tools/gpu-classifier

Two test files. Standard library only. No mocks of /opt/cixgpu-pro paths
in the gpu-classifier tests (they are pure-function tests). The
calibrator-env tests do mock the platform and the path-existence logic
so they can run on any CI host.

## Run from anywhere

```
python3 -m unittest discover -v -s tools/gpu-classifier/tests
```

## Run from the package directory

```
cd tools/gpu-classifier
python3 -m unittest discover -v tests
```

## Expected output

```
test_calibration_success_with_benchmark_is_mid_on_v66 (test_classifier.RefusalFallbackTests) ... ok
test_calibration_success_with_zero_ms_falls_through (test_classifier.RefusalFallbackTests) ... ok
test_class_from_topology_mali_kbase_on_v66_is_mid (test_classifier.ClassFromTopologyTests) ... ok
test_iris_xe (test_classifier.ClassFromRendererTests) ... ok
test_mali_g720_immortalis_is_mid (test_classifier.ClassFromRendererTests) ... ok
test_refused_with_mali_kbase_bound_returns_mid (test_classifier.RefusalFallbackTests) ... ok
test_refused_with_no_gpu_at_all_returns_weak (test_classifier.RefusalFallbackTests) ... ok
... (30+ tests)
----------------------------------------------------------------------
Ran 30 tests in 0.005s

OK
```

## Tests at a glance

`test_classifier.py` (27 tests):

* `ClassFromMsTests` (4): boundary table 8.0/20.0 plus the live .66 number 17.812.
* `ClassFromRendererTests` (10): real renderer strings from
  Intel/NVIDIA/AMD/Mali/Apple/Mesa llvmpipe.
* `ClassFromTopologyTests` (5): driver -> class mapping, with .66
  mali_kbase as the headline case.
* `RefusalFallbackTests` (12): the bug-fix branches.
* `DictShapeCompatTests` (3): the dict shape the launcher's cache and
  the settings UI rely on.
* `TableContractTests` (2): regex/class order lock-down.

`test_calibrator_env.py` (15 tests):

* `PlatformDetectionTests` (2): is_sky1_arm64 returns bool, False on x86_64.
* `NoOpTests` (3): no-op on non-Sky1, no-op without cixgpu-pro.
* `Sky1EnvAugmentationTests` (10): LD_LIBRARY_PATH prepend, NCZ_GPU_BACKEND,
  vendor pin, __EGL_PLATFORM=surfaceless only when no compositor alive.
* `Sky1AugmentedFullStackTests` (1): the four-key env matches the
  manual .66 probe exactly.
* `AugmentedFromOsEnvironTests` (3): augmented_calibrator_env
  returns a fresh dict.

Total: 42 tests. None depend on a live GPU or a network connection.