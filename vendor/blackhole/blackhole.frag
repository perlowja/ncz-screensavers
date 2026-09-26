#version 300 es
precision highp float;
precision highp int;
out vec4 fragColor;
// Time-driven palette state: phase + rate are randomised per-launch.
uniform float u_time, u_seed, u_radius, u_temperature, u_density, u_rotation;
uniform float u_inclination, u_orbit_rate, u_jet, u_star_density;
uniform float u_camera_mode, u_palette, u_approach, u_periapsis;
uniform float u_palette_phase, u_palette_rate, u_palette_contrast;
// Per-launch flight-path parameters. Drawn per-launch in C; replace the
// previously-hardcoded curve coefficients so each run traces a distinct
// path shape (not a rescaled version of one).
//   d_base   : base distance (was the 10.5 / 9.8 / 10.8 / 18 literals).
//   d_swing  : amplitude of the cos(phase) radial swing.
//   d_harm_a : secondary dist harmonic amplitude (modes 0, 1, 2).
//   d_harm_f : secondary dist harmonic frequency multiplier.
//   o_rate   : orbital rate multiplier on the phase ramp.
//   o_harm_a : secondary orbital harmonic amplitude (the .28 / .34 / .55).
//   o_harm_f : secondary orbital harmonic frequency multiplier.
//   o_count  : how many orbits before phase wraps; combined with orbit_rate
//              this sets the visible duration of a coherent arc.
//   sign     : +1 prograde, -1 retrograde (flips orbit direction and roll).
//   e_swing  : elevation swing amplitude (the .48..78).
//   e_freq   : elevation swing frequency multiplier on phase.
//   ph_jit   : deterministic per-launch phase offset so launches aren't
//              phase-locked at t=0.
uniform float u_path_d_base, u_path_d_swing, u_path_d_harm_amp, u_path_d_harm_freq;
uniform float u_path_o_rate, u_path_o_harm_amp, u_path_o_harm_freq;
uniform float u_path_o_count, u_path_sign;
uniform float u_path_e_swing, u_path_e_freq, u_path_phase_jitter;
uniform vec2 u_resolution;
// hue, spatial scale, cloud coverage, yaw; tilt and bounded noise offset.
uniform vec4 u_nebula;
uniform vec2 u_nebula_axis;
// Which decorrelation scheme the nebula was given this run (0..2).
uniform float u_nebula_scheme;
// Per-launch disk-axis orientation. xyz is a unit vector along the BH's
// spin axis, drawn uniformly on the sphere (not via naive lat/long, which
// clusters at the poles). w is a half-angle wobble term in [-1,1]: the
// disk normal is the spin axis plus a small in-cone tilt. The disk stays
// near-perpendicular to the spin axis at w=0; |w|=1 lets the disk tilt up
// to ~90° away from the axis. This is what makes edge-on views reachable
// independently of where the camera is.
uniform vec4 u_disk_axis;
// Slow disk precession (Lense-Thirring frame dragging). The disk normal
// swings around u_disk_precess.xyz at rate u_disk_precess.w rad/s. Typical
// launches run 0.02..0.08 rad/s so a minute of capture shows visible but
// unhurried change. The precession axis and rate are both drawn per launch.
uniform vec4 u_disk_precess;
// Per-launch trajectory family: 0 = zoom-whirl (bound), 1 = hyperbolic
// flyby (unbound). Drawn 50/50 so both families actually appear.
uniform float u_camera_family;
// Zoom-whirl ratio of angular to radial frequency per radial cycle.
// Rational q -> exactly periodic closed rosette. Irrational q ->
// precesses and NEVER retraces. Drawn per-launch; defaults to a
// value just above 1 for the flyby family (where it is unused).
uniform float u_orbit_q;
// Orbital eccentricity (zoom-whirl: 0.15..0.7; flyby: > 1).
uniform float u_orbit_e;
// Argument of periapse (radians). The orbit's periapse direction;
// rotates the swept arc around the focus. Drawn [0, 2pi] per launch.
uniform float u_orbit_omega;
#define PI 3.14159265358979323846
float hash21(vec2 p){p=fract(p*vec2(123.34,345.45));p+=dot(p,p+34.345+u_seed*.00001);return fract(p.x*p.y);}
float noise(vec2 p){vec2 i=floor(p),f=fract(p);f=f*f*(3.-2.*f);return mix(mix(hash21(i),hash21(i+vec2(1,0)),f.x),mix(hash21(i+vec2(0,1)),hash21(i+1.),f.x),f.y);}
float fbm(vec2 p){float v=0.,a=.5;for(int i=0;i<4;i++){v+=a*noise(p);p=p*2.03+vec2(17.13,-11.7);a*=.5;}return v;}

// ---- HSL helpers (palette interpolation in hue-space avoids the grey
//      midpoint that linear-RGB complement mixing passes through) ----
vec3 hsl2rgb(vec3 c){vec3 rgb=clamp(abs(mod(c.x*6.+vec3(0.,4.,2.),6.)-3.)-1.,0.,1.);return c.z+c.y*(rgb-.5)*(1.-abs(2.*c.z-1.));}
// Each palette is a list of (hue_offset, sat, lum) stops. u_palette selects
// the family; the active hue rotates with u_time via u_palette_phase + rate.
struct Stop{float h,s,l;};
// Hue interpolation along the SHORT arc so wrapping never produces a jump.
float hueLerp(float a,float b,float k){float d=b-a;d-=floor(d+.5);return a+d*k;}
Stop stopAHue(Stop a,Stop b,float k){return Stop(hueLerp(a.h,b.h,k),a.s+(b.s-a.s)*k,a.l+(b.l-a.l)*k);}

// Five named palettes + a sixth "psychedelic" used when u_palette>=5.
// Each is a 4-stop ramp. Stops chosen to span temperature hot->cool with
// at least two clearly different hues so a single disk carries several
// colours at once. Saturation is high but not maxed out so the disk does
// not crush to neon; one named palette (3, ultraviolet) is intentionally
// restrained for the "classical" look.
void paletteStops(float idx,out Stop s0,out Stop s1,out Stop s2,out Stop s3){
 if(idx<.5){                              // solar gold (warm, restrained)
  s0=Stop(.085,.85,.10); s1=Stop(.085,.90,.42);
  s2=Stop(.07,.95,.72);  s3=Stop(.04,.75,.98);
 }else if(idx<1.5){                       // blue-hot
  s0=Stop(.62,.80,.08);  s1=Stop(.55,.95,.34);
  s2=Stop(.48,.85,.66);  s3=Stop(.52,.60,.98);
 }else if(idx<2.5){                       // ember (deep red->orange->yellow)
  s0=Stop(.97,.95,.06);  s1=Stop(.05,.98,.32);
  s2=Stop(.10,.95,.62);  s3=Stop(.13,.80,.97);
 }else if(idx<3.5){                       // ultraviolet (classical, restrained)
  s0=Stop(.72,.90,.06);  s1=Stop(.78,.80,.30);
  s2=Stop(.82,.70,.58);  s3=Stop(.86,.55,.96);
 }else if(idx<4.5){                       // exotic mint -> magenta split
  s0=Stop(.42,.65,.10);  s1=Stop(.50,.95,.36);
  s2=Stop(.86,.85,.62);  s3=Stop(.92,.90,.96);
 }else{                                   // psychedelic: 4 widely-separated hues
  s0=Stop(.00,.90,.10);  s1=Stop(.18,.95,.40);
  s2=Stop(.55,.95,.66);  s3=Stop(.82,.90,.98);
 }
}
vec3 palette(float t){
 // Active hue offset: drifts continuously over time, randomised per-launch.
 float active_h=u_palette_phase+u_time*u_palette_rate;
 Stop s0,s1,s2,s3;paletteStops(u_palette,s0,s1,s2,s3);
 // Map the 4 stops into a hue-rotated frame so each stop's hue advances by
 // active_h (mod 1). Keeps relative relationships, adds global drift.
 s0.h=fract(s0.h+active_h); s1.h=fract(s1.h+active_h);
 s2.h=fract(s2.h+active_h); s3.h=fract(s3.h+active_h);
 // 4-stop ramp interpolated with hue-space lerp (no grey midpoint).
 // Branches on t so each segment interpolates only between adjacent stops.
 Stop r;
 if(t<.33)r=stopAHue(s0,s1,t/.33);
 else if(t<.66)r=stopAHue(s1,s2,(t-.33)/.33);
 else if(t<.90)r=stopAHue(s2,s3,(t-.66)/.24);
 else r=s3;
 return hsl2rgb(vec3(r.h,r.s,r.l));
}

// Direction-space noise has no longitude seam or pole singularity.
float skyHash(vec3 p){p=fract(p*.1031);p+=dot(p,p.yzx+33.33);return fract((p.x+p.y)*p.z);}
float skyNoise(vec3 p){
 vec3 i=floor(p),f=fract(p);f=f*f*(3.-2.*f);
 return mix(mix(mix(skyHash(i),skyHash(i+vec3(1,0,0)),f.x),
                mix(skyHash(i+vec3(0,1,0)),skyHash(i+vec3(1,1,0)),f.x),f.y),
            mix(mix(skyHash(i+vec3(0,0,1)),skyHash(i+vec3(1,0,1)),f.x),
                mix(skyHash(i+vec3(0,1,1)),skyHash(i+vec3(1,1,1)),f.x),f.y),f.z);
}
vec3 nebula(vec3 d){
 // Decorrelate from the disk: nebula base hue is shifted by a scheme offset
 // (complement 0.5, triad 0.33, split-complement 0.42 by default).
 float scheme=u_nebula_scheme;
 float offset=(scheme<.5)?.5:(scheme<1.5)?.33:.42;
 float drift=u_palette_phase*0.5+u_time*u_palette_rate*0.35;
 float baseH=fract(u_nebula.x+offset+drift);
 float cy=cos(u_nebula.w),sy=sin(u_nebula.w);
 float ct=cos(u_nebula_axis.x),st=sin(u_nebula_axis.x);
 vec3 q=vec3(cy*d.x-sy*d.z,d.y,sy*d.x+cy*d.z);
 q=vec3(q.x,ct*q.y-st*q.z,st*q.y+ct*q.z);
 vec3 v=q*u_nebula.y+u_nebula_axis.y;
 float warp=skyNoise(v*.65+vec3(7,19,3));
 v+=1.8*vec3(warp,-warp,.5*warp);
 float n=.57*skyNoise(v)+.28*skyNoise(v*2.03+17.1)+.15*skyNoise(v*4.11-9.2);
 float band=exp(-pow((q.y+.24*(warp-.5))/.36,2.));
 float cloud=smoothstep(.30,.78,n+u_nebula.z)*(.20+.80*band);
 float filaments=smoothstep(.42,.72,n)*cloud;
 // Nebula sampled in HSL with its own decorrelated hue; never falls into the
 // disk palette's hue family because of the scheme offset.
 vec3 cool=hsl2rgb(vec3(fract(baseH+.00),.65,.55));
 vec3 warm=hsl2rgb(vec3(fract(baseH+.16),.70,.62));
 // Bounded radiance: keep nebula strictly below disk luminance so the disk
 // stays the subject. The 0.13 / 0.055 multipliers are the previous
 // ceiling; we cut them by ~30% so the nebula reads as backdrop.
 return .09*cloud*mix(cool,warm,warp)+.04*filaments*vec3(.65,.75,1.);
}
vec3 stars(vec3 d){
 vec2 uv=vec2(atan(d.z,d.x)/(2.*PI)+.5,asin(clamp(d.y,-1.,1.))/PI+.5);
 vec2 cell=floor(uv*vec2(720,360));
 float n=hash21(cell),s=smoothstep(1.-.0022*u_star_density,1.,n);
 vec3 c=mix(vec3(.55,.7,1),vec3(1,.72,.45),hash21(cell+7.));
 return c*s*s*(.65+.35*sin(u_seed+n*40.))*1.8+nebula(d);
}
// ---- Disk axis tilt ----
// Build the world-space disk normal at the current time. Two contributions:
//   1. Per-launch spin axis drawn uniformly on the sphere (u_disk_axis.xyz).
//      w is a half-angle wobble term in [-1,1] which tilts the disk up to
//      ~90° away from the axis at |w|=1.
//   2. Slow precession about u_disk_precess.xyz at rate u_disk_precess.w.
// Both motions are physically motivated: a BH's spin axis is fixed on a
// human time scale; the DISK misaligns with the spin axis and precesses
// via Lense-Thirring frame dragging. Keeping the precession slow means a
// viewer notices it over a minute, not as a tumble.
vec3 diskNormalAtTime(float t){
 // Spin-axis-relative wobble: rotate u_disk_axis.xyz by an angle w*pi/2
 // about an axis perpendicular to it. Pick a "perp" axis robustly via
 // cross with world +Z first; fall back to +X if the spin axis is ~+Z.
 // Returning (0,0,0) here from a degenerate input is fine: normalize(0)
 // returns (0,0,0) which we then project from in the integrator check,
 // and disk_color handles the n~+Z case identically (identity rotation).
 vec3 spin=u_disk_axis.xyz;
 vec3 ref=abs(spin.z)>.97?vec3(1.,0.,0.):vec3(0.,0.,1.);
 vec3 perp=cross(spin,ref);
 float pl=length(perp);
 vec3 perpN=pl>1e-4?perp/pl:vec3(1.,0.,0.);
 float wobble=u_disk_axis.w*1.5707963;   // up to pi/2 radian tilt
 float cw=cos(wobble),sw=sin(wobble);
 vec3 tilted=cw*spin+sw*perpN;
 // Precession: swing tilted about u_disk_precess.xyz. Rodrigues rotation.
 vec3 pAxis=u_disk_precess.xyz;
 float pa=length(pAxis);
 // If pAxis is degenerate (essentially zero), skip the precession and
 // return the tilted vector. The probability of this from a uniform draw
 // is exactly zero; the guard is here for safety only.
 if(pa<1e-4)return normalize(tilted+vec3(0.,0.,1e-4));
 vec3 pN=pAxis/pa;
 float ang=t*u_disk_precess.w;
 float c=cos(ang),sang=sin(ang);
 float dotN=dot(pN,tilted);
 vec3 swung=tilted*c+cross(pN,tilted)*sang+pN*(dotN*(1.-c));
 return normalize(swung);
}
// Rotate a world-space point into the frame where the disk sits in the XY
// plane (normal +Z). Equivalent to applying the inverse of the rotation
// that maps +Z to diskNormal. Implemented as Rodrigues' rotation about the
// axis (diskNormal x +Z).
vec3 toDiskLocal(vec3 p,vec3 n){
 vec3 axis=normalize(cross(n,vec3(0.,0.,1.)));
 float ang=acos(clamp(n.z,-1.,1.));    // angle from world +Z
 float c=cos(ang),s=sin(ang);
 if(length(axis)<1e-5)return p;        // n ~ +Z: identity
 return p*c + cross(axis,p)*s + axis*dot(axis,p)*(1.-c);
}
vec3 disk_color(vec3 p,vec3 diskN,float drama){
 // Rotate into the disk's local frame (z = disk normal at this instant).
 // Everything below runs in plane-polar (r, a) where a is the orbital
 // angle around the disk axis. The seam rule still applies: a is atan(...)
 // and discontinuous, so anything driven by it (bandHue here) MUST be
 // periodic in it.
 vec3 pL=toDiskLocal(p,diskN);
 float r=length(pL.xy),a=atan(pL.y,pL.x);
 float ph=a-u_rotation*u_time*.3/pow(max(r,1.1),1.5);
 float n=fbm(vec2(r*2.7+cos(ph)*5.,sin(ph)*5.+u_time*.08+u_seed*.01));
 n=mix(n,fbm(vec2(r*9.+cos(ph)*13.,sin(ph)*13.-u_time*.17)),.42);
 float edge=smoothstep(3.,3.7,r)*(1.-smoothstep(10.5,12.5,r));
 float heat=clamp(pow(3./max(r,3.),.75)*u_temperature,0.,1.);
 float dop=clamp(1.+.55/sqrt(max(r,1.5))*sin(a)*1.5,.35,1.8);
 float grav=sqrt(max(1.-1./max(r,1.001),.02));
 // Oil-slick iridescence: hue shifts with orbital angle and Doppler term so
 // a single frame carries several bands. Slow radius offset avoids the hue
 // fighting the temperature ramp.
 // NOTE: `a` is atan(p.y,p.x) and therefore JUMPS from +PI to -PI along one
 // radial line. Driving hue LINEARLY from it (previously (a/(2.*PI))*.18)
 // stepped bandHue by a full 0.18 across that seam, which rendered as a hard
 // straight colour boundary splitting the disk - observed live on MEDUSA as a
 // teal/gold edge down the middle. Use a periodic function of the angle
 // instead: sin/cos are continuous across the wrap and keep comparable
 // amplitude (+/-.09 here vs the old 0.18 peak-to-peak), so the disk gets the
 // same oil-slick banding with no discontinuity.
 float bandHue=.09*sin(a)+.045*sin(2.*a)+0.06*sin(ph*3.)+0.04*(dop-.35)/1.45;
 float radiusHue=(r-3.)*.012;
 float paletteT=clamp(heat*dop/grav,0.,1.)+bandHue+radiusHue;
 // A real disk-space hot sector also appears in the lensed disk images.
 float sector=max(cos(a-(.22*u_time+.00001*u_seed)),0.);
 sector*=sector;sector*=sector;
 float boost=1.+.65*drama*sector*(1.-smoothstep(4.,8.,r));
 return palette(paletteT)*edge*(.32+1.2*n)*u_density*dop*dop*boost;
}
// Jet picks up the palette so it tracks the rest of the scene instead of a
// fixed blue. Sampled at a hot temperature so it sits at the bright stop.
vec3 jet_color(float axis){
 Stop s0,s1,s2,s3;paletteStops(u_palette,s0,s1,s2,s3);
 Stop hot=stopAHue(s2,s3,.85);
 float active_h=u_palette_phase+u_time*u_palette_rate*1.5;
 hot.h=fract(hot.h+active_h);
 vec3 c=hsl2rgb(vec3(hot.h,hot.s,hot.l));
 return c*smoothstep(.975,.997,axis);
}
// Zero velocity and acceleration at each envelope endpoint.
float ease5(float a,float b,float x){float t=clamp((x-a)/(b-a),0.,1.);return t*t*t*(t*(t*6.-15.)+10.);}


void main(){
 vec2 p=(2.*gl_FragCoord.xy-u_resolution)/u_resolution.y/u_radius;
 // Per-launch flight-path parameters replace what used to be hardcoded
 // curve coefficients. The four modes are gone; replaced by two REAL
 // trajectory families from orbital mechanics (zoom-whirl for bound,
 // hyperbolic flyby for unbound). One equation per family gives
 // unlimited distinct paths from a single per-launch `q` (zoom-whirl)
 // or eccentricity (both) draw.
 //
 // u_camera_family (scalar): 0 = zoom-whirl, 1 = hyperbolic flyby.
 // u_orbit_q (scalar): zoom-whirl ratio of angular to radial frequency.
 //   q rational -> closed periodic rosette. q irrational -> aperiodic,
 //   never repeats. Default 1 (unused for the flyby family).
 // u_orbit_e (scalar): eccentricity. Zoom-whirl draws [0.15, 0.7];
 //   flyby draws > 1 in [1.05, 3.0].
 // Use a SINGLE pass for the unbound flyby (don't multiply phase by
 // u_path_o_count; that would loop the flyby into back-to-back
 // identical passes). For bound zoom-whirl, u_path_o_count controls
 // how many radial cycles the camera traces before wraparound.
 float phase;
 float orbit,elev,dist,roll=0.,closeFX=0.,arrivalFX=0.;
 if(u_camera_family<.5){
  // ZOOM-WHIRL: phase is radians of true anomaly, advancing at
  // orbit_rate*o_count so o_count controls how many radial cycles
  // fit in the screensaver. Three orbits at q=3 = nine-leaf rosette.
  phase=u_time*u_orbit_rate*u_path_o_count+u_seed*.000001+u_path_phase_jitter;
  // ---- ZOOM-WHIRL (bound) ----
  //
  // Real relativistic phenomenon with no Newtonian analogue. The
  // camera zooms out to apoapsis and back, then whirls through
  // several revolutions at periapsis. q = ratio of angular to radial
  // frequency per radial cycle. ONE equation gives unlimited distinct
  // paths; q rational -> closed rosette, q irrational -> aperiodic,
  // never repeats.
  //
  // Parameterise by phase angle u (radians, monotonic with time).
  //   Radial:        r(u)   = p / (1 + e cos(u - omega))
  //   Orbital angle: psi(u) = q * (u - omega) + omega
  // so the camera makes q revolutions per radial cycle, with the
  // fastest sweep at periapsis.
  //
  // p derived from the periapse radius r_p: r_p = p/(1 + e), so
  // p = r_p (1 + e). u_periapsis is the closest approach in M.
  float u_=phase;
  float p_=u_periapsis*(1.+u_orbit_e);
  float cu=cos(u_-u_orbit_omega);
  float r_=p_/(1.+u_orbit_e*cu);
  r_=max(r_,5.5);   // stay outside event horizon
  float psi=u_orbit_q*(u_-u_orbit_omega)+u_orbit_omega;
  orbit=u_path_sign*psi;
  dist=r_;
  // Elevation oscillation: same parameterisation as before so
  // per-launch e_swing / e_freq still produce visible variety.
  elev=u_inclination+u_path_e_swing*sin(u_*u_path_e_freq);
  // closeFX / arrivalFX used by the integrator for peripheral
  // distortion; for bound orbits they ride the periapse passages.
  // closeFX peaks when r is near periapsis.
  float closeness=1.-clamp((r_-u_periapsis)/(u_periapsis*4.),0.,1.);
  closeFX=closeness*closeness;
  arrivalFX=4.*closeFX*(1.-closeFX);
 }else{
  // HYPERBOLIC FLYBY: ONE pass from one asymptote to the other.
  // u_path_o_count scales how often the sweep repeats (each cycle
  // is one asymptote-to-asymptote pass). phase = sine of time so
  // the camera oscillates between asymptotes smoothly without
  // edge discontinuities; combined with the radial discontinuity
  // avoidance by clamping `fu` to the asymptote range, this gives
  // a "bouncing through" flyby that doesn't tear at the wrap.
  float t=u_time*u_orbit_rate*u_path_o_count;
  phase=sin(t)+u_seed*.000001+u_path_phase_jitter;
  // ---- HYPERBOLIC FLYBY (unbound) ----
  //
  // Voyager-gravity-assist geometry. Incoming asymptote, periapse,
  // outgoing asymptote - same speed in and out, deflected through an
  // angle that depends on eccentricity: deflection = 2*asin(1/e).
  // Closer periapsis -> greater deflection. One pass, no wrap; the
  // camera arrives, slings past, and departs. The relative of the
  // zoom-whirl's infinite whirl count.
  //
  // Parameterise by true anomaly f in [-pi, +pi]. r(f) = p/(1+e cos f),
  // but here e > 1 so r diverges at f = arccos(-1/e) and the
  // asymptotes are at f = +/- (pi - arcsin(1/e)).
  //
  // Map u_ linearly onto [-(pi-d/2), +(pi-d/2)] so we see the full
  // sweep including a few samples on each asymptote but stay finite.
  float u_=phase;
  float deflect=2.*asin(min(1.,1./u_orbit_e));
  float half_sweep=(PI-deflect*.5)*.95;  // small margin from asymptotes
  // Wrap u_ to [-half_sweep, +half_sweep] for the camera angle around
  // the focus (orbital angle psi). The pattern repeats every time
  // u_ crosses +/- half_sweep, but the path *position* at the same
  // u_ is always the same, so it looks like the camera flies past
  // repeatedly with different "phases" of the asymptotic approach.
  float fu=clamp(u_,-half_sweep,half_sweep);
  float p_=u_periapsis*(1.+u_orbit_e);
  float cu=cos(fu-u_orbit_omega);
  float r_=p_/(1.+u_orbit_e*cu);
  r_=max(r_,5.5);
  // For flyby the orbital angle is just fu itself (the focus sees
  // the camera sweep from one asymptote to the other). Add a small
  // u_approach offset so two launches of the same e and periapsis
  // don't sit at the same psi.
  orbit=u_path_sign*(fu+u_orbit_omega+u_approach*.1);
  dist=r_;
  elev=u_inclination+u_path_e_swing*sin(u_*u_path_e_freq);
  // closeFX = (1+r_p/r) so peak at periapse; arrivalFX rides it.
  closeFX=1.-clamp(r_/(u_periapsis*8.),0.,1.);
  arrivalFX=4.*closeFX*(1.-closeFX);
 }
 dist=max(dist,5.4);
 vec3 cam=dist*vec3(cos(orbit)*cos(elev),sin(orbit)*cos(elev),sin(elev)),forward=normalize(-cam),baseRight=normalize(cross(forward,vec3(0,0,1))),baseUp=cross(baseRight,forward),right=cos(roll)*baseRight+sin(roll)*baseUp,up=-sin(roll)*baseRight+cos(roll)*baseUp;
 // Disk normal at this frame. Used for the plane-crossing detection in
 // the integrator (line 318) and the jet axis check (line 320). Computing
 // it once per frame avoids recomputing the Rodrigues rotation in inner
 // loops. Falls back to +Z if the draw produced a near-degenerate axis
 // (probability zero from a uniform-on-sphere draw, but it's cheap).
 vec3 diskN=diskNormalAtTime(u_time);
 if(length(diskN)<.5)diskN=vec3(0.,0.,1.);
 // One ray, with subtle arrival-only peripheral distortion and framing drift.
 float r2=dot(p,p),shot=fract(u_time*u_orbit_rate/(2.*PI));
 vec2 lensP=p*(1.+.035*arrivalFX*r2/(1.+r2));
 lensP+=closeFX*vec2(u_path_sign*.12*sin(2.*PI*shot),.06);
 vec3 ray=normalize(forward+lensP.x*right+lensP.y*up);
 float impact=length(cross(cam,ray));bool captured=impact<2.598076;
 float u=1./length(cam),phi=0.;vec3 normal=normalize(cam),perp=cross(cross(normal,ray),normal);float plen=length(perp);vec3 tangent=plen>1e-6?perp/plen:right;float tang=dot(ray,tangent),du=abs(tang)>1e-6?-dot(ray,normal)/tang*u:200.*u;vec3 old=cam,pos=cam,color=vec3(0);float trans=1.;
 // Disk-plane crossing test against diskN (the plane through the origin
 // with normal diskN). When old and pos are on opposite sides, their dot
 // products with diskN differ in sign and the segment crosses the disk.
 // The intersection point x lands in the disk plane up to fp precision;
 // we project it onto diskN=0 just to be sure, then take its radial dist
 // to gate the 2.8..13 band. The radial dist is taken in world XY because
 // the disk normal can be non-+Z; in disk_color the point is rotated into
 // the disk-local frame and the actual polar coords (r,a) live there.
 for(int i=0;i<260;i++){
   float step=.04*(1.-.62*exp(-12.*(u-.667)*(u-.667)));
   du+=.5*(-u+1.5*u*u)*step;
   u+=du*step;
   du+=.5*(-u+1.5*u*u)*step;
   phi+=step;
   if(u>=1.||u<=.0005)break;
   old=pos;
   pos=(cos(phi)*normal+sin(phi)*tangent)/u;
   float od=dot(old,diskN);
   float pd=dot(pos,diskN);
   if(od*pd<0.&&od!=pd){
    float t_=-od/(pd-od);
    vec3 x=mix(old,pos,t_);
    x=x-diskN*dot(x,diskN);
    float rd=length(x);
    if(rd>2.8&&rd<13.){
     color+=trans*disk_color(x,diskN,closeFX);
     trans*=.72;
    }
   }
  }
 if(!captured){vec3 d=length(pos-old)>1e-5?normalize(pos-old):ray;color+=trans*(stars(d)+vec3(.002,.003,.007));}
 if(u_jet>0.){float axis=abs(dot(ray,diskN));color+=u_jet*jet_color(axis)*(.45+.55*noise(p*45.+u_time*.2));}
 // Reinhard tonemap -> 1/2.2 gamma -> contrast toe + black point.
 // u_palette_contrast ~1.0 (gentle S-curve restoring Reinhard-flattened
 // contrast). 0.95-1.25 randomised per-launch.
 color=color/(1.+color);
 color=pow(max(color,0.),vec3(1./2.2));
 // Soft S-curve: (c-.5)*k+.5, then small black point + small white point lift.
 float k=clamp(u_palette_contrast,.85,1.35);
 color=(color-.5)*k+.5;
 color=max(color-vec3(.018),vec3(0));
 fragColor=vec4(clamp(color,0.,1.),1);
}
