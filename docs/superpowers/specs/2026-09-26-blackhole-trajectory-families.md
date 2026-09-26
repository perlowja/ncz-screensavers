# blackhole camera: use real trajectory families instead of hardcoded sinusoids

Research note, 2026-09-26. Replaces the "invent some varied curves" approach with two
REAL trajectory families from general relativity and orbital mechanics. Both are cheap to
evaluate in a shader and both produce far more variety from far fewer magic numbers than
the current four hardcoded choreographies.

## The key radii (Schwarzschild, in units of M)

These are hard constraints on where a camera may go, and also the interesting places to
put it:

| radius | what it is |
|---|---|
| **2M** | event horizon |
| **3M** | **photon sphere** — unstable circular orbit for light |
| **4M** | marginally bound orbit — boundary between bound and unbound |
| **6M** | **ISCO**, innermost stable circular orbit for massive particles |

The current camera sits at roughly 9.8-18 in shader units, comfortably outside all of
this. There is real drama available closer in, down toward 6M, that the piece is not
using.

## Family 1 — ZOOM-WHIRL orbits (bound camera)

**This is the single best idea available here.** Zoom-whirl is a genuinely relativistic
phenomenon that does not exist in Newtonian gravity, where orbits are just Kepler conics.

The behaviour: an eccentric orbit where the camera **zooms** out to apoapsis and back,
then **whirls** through several rapid near-circular revolutions close to periapsis, then
zooms out again. The traced patterns look like rosettes and four-leaf clovers.

The mechanism is extreme perihelion precession: near the hole the precession per orbit is
so large that the particle executes multiple loops before falling back out to apastron.

**The parameter that matters, and why it is perfect for a screensaver:**

Zoom-whirl behaviour is quantified by a number **q**, the ratio of average angular
frequency to radial frequency per radial cycle.

- **q rational** -> the orbit is exactly PERIODIC. It closes on itself and returns to its
  initial conditions after a finite time. Produces clean, symmetric rosettes.
- **q irrational** -> the orbit is **precessing and APERIODIC — it never repeats.**

That is the whole ask, solved by one number. Draw `q` per launch: pick a rational for a
closed, hypnotic figure, or an irrational for a path that genuinely never retraces itself
for as long as the screensaver runs. One equation replaces four hardcoded modes and gives
unlimited variety.

Randomise per launch: `q`, eccentricity, periapsis radius (bounded above ISCO-ish for a
camera that survives), orbital plane orientation, and prograde vs retrograde.

## Family 2 — HYPERBOLIC FLYBY (unbound camera)

The Voyager gravity-assist geometry, which is exactly a one-pass "flyby" shot.

In the frame of the massive body the trajectory is a **hyperbola**: the same speed going
in and coming out, with the path **deflected through some angle**. The deflection is
controlled by how close the approach is — **closer periapsis produces greater
deflection**. Hyperbolic excess velocity is the speed retained at infinity.

For the camera this gives a clean, physical one-parameter family: choose a periapsis, get
a deflection angle, and the shot is a single dramatic sweep past the hole rather than a
loop. A grazing pass whips the view around hard; a distant pass barely bends. Randomising
periapsis alone produces everything from a gentle drift-by to a violent slingshot.

Voyager 1 took gravity assists from Jupiter and Saturn to reach solar escape velocity;
Voyager 2 used Jupiter, Saturn and Uranus to accelerate and then Neptune to DECELERATE in
order to reach Triton — worth noting because it shows the same geometry can slow a
trajectory as well as speed it, depending on approach geometry. Both signs are available
to the camera.

## How this maps onto the existing four modes

Do not keep four hardcoded choreographies. Replace them with:

- **bound / zoom-whirl** — parameterised by q, eccentricity, periapsis, plane, direction.
  Subsumes the current "diving arc" and "wide sweep" characters and does them better.
- **unbound / hyperbolic flyby** — parameterised by periapsis and approach angle.
  Subsumes "banking slingshot" properly, with real deflection instead of a scripted bank.

Two families with real parameters beat four scripted curves. Pick the family per launch,
then draw its parameters.

## Constraints that still apply

- Keep the camera outside the horizon and clear of the photon sphere. The interesting band
  is roughly 6M-20M; going below ISCO is for effect, not for parking.
- The shot must stay LEGIBLE. A q that whirls too tightly becomes an unreadable blur.
  Bound the whirl count per zoom.
- Anything derived from the orbital angle must be PERIODIC in it — see commit `4d6cf88`,
  where a linear dependence on `atan` produced a hard colour seam.

## Sources

- [Zoom-Whirl Orbits in Black Hole Binaries (arXiv:0907.0671)](https://arxiv.org/pdf/0907.0671)
- [Black Hole Orbits: Zoom-Whirls and Four-Leaf Clovers — astrobites](https://astrobites.org/2017/01/25/black-hole-orbits/)
- [Periodic orbits around Kerr Sen black holes (arXiv:1804.05883)](https://arxiv.org/pdf/1804.05883)
- [Eccentric black hole mergers and zoom-whirl behavior (arXiv:1209.4085)](https://arxiv.org/pdf/1209.4085)
- [Hyperbolic trajectory — Wikipedia](https://en.wikipedia.org/wiki/Hyperbolic_trajectory)
- [Gravity assist — The Planetary Society](https://www.planetary.org/articles/20130926-gravity-assist)
- [Innermost stable circular orbit — Wikipedia](https://en.wikipedia.org/wiki/Innermost_stable_circular_orbit)
- [Photon Spheres, ISCOs, and OSCOs (INSPIRE)](https://inspirehep.net/files/ae9208e4c643219402bf15c821b00aee)
- [Geodesics in the Schwarzschild geometry — Hirata lecture notes](https://hirata10.github.io/ph6820/lec17_bh_trajectories.pdf)

---

## Tilt the hole's own axis, and precess it during the shot

Operator: *"we want to be able to tilt the black hole on its axis randomly during these
trajectories."*

Currently the disk is fixed in the XY plane — `disk_color` takes `r=length(p.xy)` and
`a=atan(p.y,p.x)`, so the disk normal is hardcoded to +Z. All the apparent tilt comes from
moving the CAMERA. That is why every launch has the same relationship between the disk and
the frame: the hole never changes its own orientation.

**Two separate things to add, and both are physical:**

### 1. Random axis orientation per launch

Give the hole a spin axis drawn per launch rather than assuming +Z. Rotate the ray into
disk-local space before the `length/atan` calls, or equivalently rotate the disk normal.
Draw the axis uniformly on the sphere (avoid the naive lat/long draw, which clusters at
the poles), then evaluate the disk in that frame.

This alone makes edge-on, face-on and everything between reachable, independently of where
the camera is. Edge-on is the most dramatic view — that is the configuration where the
lensed image of the far side of the disk arcs over the top of the hole — and the current
code can only reach it by camera placement.

### 2. Precess the axis DURING the shot

A disk misaligned with a spinning black hole's angular momentum does not stay put: frame
dragging torques it, and the disk precesses about the spin axis (Lense-Thirring; the inner
region tends to align via the Bardeen-Petterson effect while the outer disk stays tilted).

So a **slowly precessing disk axis is physically motivated**, not a gimmick. Implement as
a slow rotation of the disk normal about a fixed spin axis, with the cone half-angle and
the precession rate drawn per launch.

Keep it SLOW — this should be something a viewer notices over a minute, not a tumble. It
is the difference between "the camera is moving" and "the object itself is alive".

The combination is what produces real variety: a zoom-whirl camera on a randomised
trajectory, around a hole whose own disk is tilted differently every launch and slowly
precessing during it. Three independent sources of change instead of one scripted curve.

### Implementation notes

- Rotate INTO disk space once, at the top of the disk evaluation, and leave the rest of
  the disk math untouched. Cheaper and less error-prone than reworking the disk.
- The lensing and photon-ring geometry stay in world space around the hole's centre; only
  the disk plane rotates.
- Draw the precession rate low enough that a 30-second capture shows a visible but
  unhurried change.
- The seam rule still applies: `a` is `atan` in the ROTATED frame and remains
  discontinuous, so anything driven by it must be periodic (commit `4d6cf88`).
