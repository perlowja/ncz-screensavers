Round 3: fix a crash on real hardware. You are on macOS, cannot build; the coordinator builds on forky arm64 and runs on real Wayland hardware (labwc/wlroots 0.20.2, Mali GPU), reporting exact runtime output back.

CRASH (confirmed via WAYLAND_DEBUG=1 on real hardware):
```
listener function for opcode 1 of wl_seat is NULL
```
followed by SIGABRT (core dump). This is libwayland's own guard: a listener struct was registered for wl_seat but doesn't have a function pointer for EVERY event in the wl_seat interface at the bound version. wl_seat has two events: `capabilities` (opcode 0) and `name` (opcode 1, present since wl_seat version 2). The generated header wl-screenhack.p or the build/*-client-protocol.h (regenerate locally if you want to check field names/order) declares `struct wl_seat_listener { void (*capabilities)(...); void (*name)(...); };` in that order.

FIX: find the seat_listener struct definition (initialized with `.capabilities = seat_handle_capabilities` or similar designated initializer, or positional). If designated initializers are used, add `.name = seat_handle_name,` and implement a `static void seat_handle_name(void *data, struct wl_seat *seat, const char *name)` function (can be a no-op that does nothing, or logs at a low verbosity — no functional need for the seat name in a screensaver). If POSITIONAL initializers are used (no `.field =`), that is itself a bug (fragile, silently miscompiles which slot fills which event) — convert to designated initializers while you're in there, for every listener struct in the file (registry_listener, seat_listener, keyboard_listener, layer_surface_listener, wl_callback listener if any) so this class of bug cannot recur.

ALSO: audit every OTHER listener struct in the file the same way — compare against what the generated protocol headers declare (registry has global/global_remove; wl_keyboard has keymap/enter/leave/key/modifiers/repeat_info; zwlr_layer_surface_v1 has configure/closed) and confirm every event has a real handler, not a missing/NULL slot. Report which structs you checked and what (if anything) was already complete vs needed a handler added.

Build cleanliness: -Wall -Wextra, zero warnings, zero errors (same bar as round 2).
