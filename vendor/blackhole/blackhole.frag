#version 300 es
precision highp float;
precision highp int;
out vec4 fragColor;
// Time-driven palette state: phase + rate are randomised per-launch.
uniform float u_time, u_seed, u_radius, u_temperature, u_density, u_rotation;
uniform float u_inclination, u_orbit_rate, u_jet, u_star_density;
uniform float u_camera_mode, u_palette, u_approach, u_periapsis;
uniform float u_palette_phase, u_palette_rate, u_palette_contrast;
// Disk colour model. 0 = stylised palette (default, our own art direction).
// 1 = kipthorne: blackbody only, Doppler and gravitational shift deliberately
//     OFF - the Interstellar/DNGR look Thorne and Double Negative rendered
//     for the film, a symmetric warm-white disk.
// 2 = faithful: blackbody with gravitational redshift AND relativistic
//     Doppler folded into the observed temperature, plus beaming on
//     brightness. Physically correct and visibly asymmetric.
// Colour model derived from hydrogendeuteride/BlackHoleRayTracer (MIT).
// The Planck curve is evaluated analytically rather than sampled from a
// blackbody LUT texture, so no binary asset is required.
// Palettes (ids): 0 stylized, 1 kipthorne, 2 faithful, 3 singularity,
// 4 slingshot, 5 whitehole. u_pal_a/u_pal_b/u_pal_mix cross-fade two palettes
// (mix = 0 renders only palette a).
uniform float u_pal_a, u_pal_b, u_pal_mix;
// Options (see docs/BLACKHOLE-OPTIONS.md). All default to the previous look.
uniform float u_spin, u_isco, u_exposure, u_beaming, u_bloom, u_fringe;
uniform float u_lensing, u_nebula_amt, u_hot_sector, u_fade;
// Camera path selector: 0 = legacy per-launch path, 1..7 = orbit, slow-orbit,
// equatorial, polar, plunge, slingshot, drift. u_fly_t is time in cycles,
// u_fly_var a 0..1 per-cycle variation, u_dmin/u_dmax the distance range,
// u_incl the orbit-plane inclination in radians (<0 = per-type default).
uniform float u_flyby, u_fly_t, u_fly_var, u_dmin, u_dmax, u_incl;
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
uniform float u_max_steps;
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
 // Star field hashed in DIRECTION space (equal-angle cube map), not on a
 // longitude/latitude grid. The old 720x360 lat/long grid packs cells ever
 // tighter toward the poles: a lensed image of a pole (bright grainy
 // "searchlight" patch) and cells stretched into horizontal dashes elsewhere
 // (reported on PEGASUS 2026-09-28 in all colour modes). Each cell now holds
 // at most one round, softly falling-off star at a jittered position, so
 // magnification by the lens enlarges a dot instead of a square cell.
 vec3 ad=abs(d);
 float m=max(max(ad.x,max(ad.y,ad.z)),1e-6);
 vec2 fuv;float fid;
 if(ad.x>=ad.y&&ad.x>=ad.z){fuv=d.yz/m;fid=d.x>0.?0.:1.;}
 else if(ad.y>=ad.z){fuv=d.xz/m;fid=d.y>0.?2.:3.;}
 else{fuv=d.xy/m;fid=d.z>0.?4.:5.;}
 fuv=atan(fuv)*(4./PI);                       // equal-angle, [-1,1]
 const float K=208.;                            // ~ same total cell count as 720x360
 vec2 g=(fuv*.5+.5)*K;
 vec2 cell=floor(g),f=fract(g);
 vec2 hc=cell+vec2(fid*37.1,fid*91.7);
 float n=hash21(hc);
 float on=step(1.-.0022*u_star_density,n);
 vec2 sp=vec2(hash21(hc+3.1),hash21(hc+8.7))*.6+.2;
 float dist=length(f-sp);
 float s=on*smoothstep(.34,.0,dist);
 vec3 c=mix(vec3(.55,.7,1),vec3(1,.72,.45),hash21(hc+7.));
 return c*s*s*(.65+.35*sin(u_seed+n*40.))*2.4;
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
// Analytic blackbody colour over ~1000K..40000K, approximating the Planck
// locus in sRGB - the same curve a blackbody LUT texture bakes.
vec3 blackbodyRGB(float kelvin){
 float t=clamp(kelvin,1000.,40000.)/100.;
 float r,g,b;
 if(t<=66.) r=1.; else r=clamp(1.292936186*pow(t-60.,-0.1332047592),0.,1.);
 if(t<=66.) g=clamp(0.3900815788*log(max(t,1.))-0.6318414438,0.,1.);
 else       g=clamp(1.129890861*pow(t-60.,-0.0755148492),0.,1.);
 if(t>=66.)      b=1.;
 else if(t<=19.) b=0.;
 else            b=clamp(0.5432067892*log(t-10.)-1.196254089,0.,1.);
 return vec3(r,g,b);
}

float g_hit=0.;   // index of the disk crossing being shaded (0 = primary image)
vec3 srgbLin(vec3 c){return pow(c,vec3(2.2));}
// Piecewise-linear color ramp with 5 stops (already linear light), x in 0..1.
vec3 ramp5(float x,vec3 c0,vec3 c1,vec3 c2,vec3 c3,vec3 c4){
 float t=clamp(x,0.,1.)*4.;
 if(t<1.)return mix(c0,c1,t);
 if(t<2.)return mix(c1,c2,t-1.);
 if(t<3.)return mix(c2,c3,t-2.);
 return mix(c3,c4,t-3.);
}
// Saturation about luma. The analytic blackbody fit is pale in sRGB and the
// tonemap pulls it further toward white, so the physical modes boost it.
vec3 satBoost(vec3 c,float k){float l=dot(c,vec3(.299,.587,.114));return max(mix(vec3(l),c,k),vec3(0.));}
// Disk color and brightness factor for one palette. Everything the palette
// contributes lives here so two palettes can be cross-faded.
vec3 palTint(float mode,float hm,float dop,float grav,float r,float a,float paletteT,float sector,float drama){
 float dopE=mix(1.,dop,u_beaming);
 if(mode<.5){
  // stylized: unchanged look, hot sector gated by the option (default 0.22)
  float boost=1.+u_hot_sector*drama*sector*(1.-smoothstep(4.,8.,r));
  return palette(paletteT)*dopE*dopE*boost;
 }
 if(mode<1.5){
  // kipthorne: symmetric warm blackbody, no Doppler, redshift or beaming
  return satBoost(blackbodyRGB(mix(1900.,4300.,hm)),1.4)*(.85+.35*hm);
 }
 // Circular-orbit speed for a Schwarzschild hole (rs=1): v^2 = .5/(r-1);
 // line-of-sight Doppler D = sqrt(1-v^2)/(1-v sin a); sin a>0 = approaching.
 float v2=.5/max(r-1.,.6);
 float v=min(sqrt(min(v2,.72))*u_beaming,.9);
 float D=sqrt(1.-v*v)/max(1.-v*sin(a),.12);
 if(mode<2.5){
  float g=D*grav;
  return satBoost(blackbodyRGB(mix(3400.,10500.,hm)*clamp(g*g,.2,3.6)),2.4)*(.42*clamp(D*D*D,.05,3.2));
 }
 if(mode<3.5){
  // singularity: the Singularity desktop look, white on black. Sampled from
  // its default wallpaper: background #000000, mark #ffffff, neutral greys.
  float d3=mix(1.,dop,.6*u_beaming);
  vec3 c=mix(vec3(.985,.99,1.),vec3(1.),hm);
  return c*mix(.5,1.,hm)*1.25*d3*d3;
 }
 if(mode<4.5){
  // slingshot: brick/rust disk, orange -> gold -> near white where beaming is
  // strongest, blue-white limb on the secondary (lensed) image.
  float g=D*grav;
  float x=clamp(pow(g,1.15)*(.30+.70*hm)*.95,0.,1.);
  vec3 c=ramp5(x,srgbLin(vec3(.365,.125,.078)),srgbLin(vec3(.69,.29,.165)),srgbLin(vec3(1.,.70,.28)),srgbLin(vec3(1.,.88,.54)),srgbLin(vec3(1.,.953,.784)));
  if(g_hit>.5)c=mix(c,srgbLin(vec3(.812,.890,1.)),.5);
  return c*(.85*clamp(D*D*D,.05,3.4)*(.6+.8*hm));
 }
 // whitehole: icy, symmetric and calm. Ramp from deep teal-navy at the outer
 // glow through steel blue, teal/seafoam and ice blue to a pure white core.
 float x=clamp(.12+1.05*pow(hm,.9),0.,1.);
 vec3 c=ramp5(x,srgbLin(vec3(.063,.20,.29)),srgbLin(vec3(.227,.427,.561)),srgbLin(vec3(.31,.816,.784)),srgbLin(vec3(.812,.91,1.)),vec3(1.));
 // faint chromatic fringe: cyan on the inner edge, magenta on the outer edge
 float ein=1.-smoothstep(0.,.7,r-u_isco),eout=smoothstep(8.,10.5,r)*(1.-smoothstep(10.5,12.5,r));
 c+=u_fringe*(vec3(-.05,.30,.36)*ein+vec3(.36,-.04,.38)*eout);
 float d5=mix(1.,dop,.25*u_beaming);
 return max(c,vec3(0.))*1.15*d5;
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
 float edge=smoothstep(u_isco,u_isco+.7,r)*(1.-smoothstep(10.5,12.5,r));
 float heat=clamp(pow(3./max(r,u_isco),.75)*u_temperature,0.,1.);
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
 // Hot sector. Previously cos^4 at 0.65 amplitude, which rotated as a hard
 // narrow lobe and read as a searchlight sweeping the disk (reported on
 // PEGASUS 2026-09-28, visible in ALL colour modes). Widened to cos^2 and
 // cut to 0.22 so it is a broad brightening rather than a spotlight.
 float sector=max(cos(a-(.22*u_time+.00001*u_seed)),0.);
 sector*=sector;
 // The physical colour modes derive their asymmetry from Doppler beaming
 // (dop) which is already applied; an artistic hot sector on top of that is
 // double-counting, so it is stylised-only.
 float hm=clamp(heat,0.,1.);
 vec3 cA=palTint(u_pal_a,hm,dop,grav,r,a,paletteT,sector,drama);
 if(u_pal_mix>.001){
  vec3 cB=palTint(u_pal_b,hm,dop,grav,r,a,paletteT,sector,drama);
  cA=mix(cA,cB,u_pal_mix);
 }
 return cA*edge*(.32+1.2*n)*u_density;
}
// Jet picks up the palette so it tracks the rest of the scene instead of a
// fixed blue. Sampled at a hot temperature so it sits at the bright stop.
vec3 jet_tint(){
 Stop s0,s1,s2,s3;paletteStops(u_palette,s0,s1,s2,s3);
 Stop hot=stopAHue(s2,s3,.85);
 float active_h=u_palette_phase+u_time*u_palette_rate*1.5;
 hot.h=fract(hot.h+active_h);
 vec3 c=hsl2rgb(vec3(hot.h,.9,.58));
 // Physical colour modes: relativistic jets are non-thermal synchrotron
 // emission, cool blue-white, not part of the blackbody disk palette.
 return (u_pal_a<.5)?c:(u_pal_a<3.5&&u_pal_a>2.5)?vec3(1.):vec3(.55,.72,1.);
}
// Two thin collimated beams along +-axis (the disk normal), rendered from the
// ray's closest approach to the axis line (unlensed straight-ray approximation).
// This replaces a screen-fixed cone test (smoothstep on |dot(ray,axis)|) that
// painted a large ellipse wherever the view direction was within ~13 degrees of
// the axis, textured by screen-space noise: the "searchlight with large dots"
// artifact reported on PEGASUS 2026-09-28 in ALL colour modes.
vec3 jet_glow(vec3 cam,vec3 ray,vec3 axis){
 float b=dot(ray,axis);
 float den=1.-b*b;
 if(den<1e-4)return vec3(0.);
 float pole=smoothstep(.02,.35,den);   // soften near pole-on views
 float d=dot(ray,cam),e=dot(axis,cam);
 float t=(b*e-d)/den;               // distance along the ray to closest approach
 if(t<0.)return vec3(0.);
 float sa=(e-b*d)/den;              // height along the jet axis at closest approach
 float h=abs(sa);
 if(h<3.2||h>60.)return vec3(0.);
 vec3 q=cam+t*ray-sa*axis;
 float w=.22+.07*h;                 // gently opening beam
 float prof=exp(-dot(q,q)/(w*w));
 // Knots streaming outward, smooth in beam-space (no screen-space blocks).
 float knots=.55+.45*noise(vec2(h*.55-u_time*1.6*sign(sa),atan(q.y,q.x)*.0+u_seed*.01));
 float fade=smoothstep(3.2,5.5,h)*exp(-h*.045);
 float near=smoothstep(1.,7.,t);       // no hard cut where the closest approach passes the camera
 return jet_tint()*prof*knots*fade*pole*near;
}
// Zero velocity and acceleration at each envelope endpoint.
float ease5(float a,float b,float x){float t=clamp((x-a)/(b-a),0.,1.);return t*t*t*(t*(t*6.-15.)+10.);}


// Sky/bloom constants per palette: x = nebula scale, base sky color, star tint,
// nebula tint, halo color, palette bloom strength, interior haze amount.
void palSky(float mode,out float neb,out vec3 base,out vec3 stint,out vec3 ntint,out vec3 halo,out float pbloom,out float haze){
 neb=1.;base=vec3(.002,.003,.007);stint=vec3(1.);ntint=vec3(1.);halo=vec3(1.);pbloom=0.;haze=0.;
 if(mode<2.5)return;
 if(mode<3.5){neb=0.;base=vec3(0.);stint=vec3(1.);halo=vec3(1.);pbloom=.15;return;}
 if(mode<4.5){neb=.30;base=vec3(.0035,.0055,.0135);stint=vec3(.85,.92,1.15);ntint=vec3(.55,.75,1.3);halo=vec3(1.,.62,.30);pbloom=.9;return;}
 neb=.55;base=vec3(.0016,.0042,.0100);stint=vec3(.9,1.,1.1);ntint=vec3(.35,.65,1.);halo=vec3(.55,.80,1.);pbloom=.7;haze=1.;
}
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
 bool useFly=u_flyby>.5;
 vec3 camV=vec3(0.),upH=vec3(0.,0.,1.);
 if(useFly){
  // Deterministic flyby camera: a curve in an orbital plane through the hole.
  // Plane inclination ii, node angle Om (from the per-cycle variation), radial
  // profile and angle by type. All functions of u_fly_t, so paths are smooth.
  float ty=u_flyby,f=u_fly_t,ff=fract(f),Om=u_fly_var*6.2831853;
  float sg=(fract(u_fly_var*7.31)<.5)?1.:-1.;
  float rr=.5*(u_dmin+u_dmax),nu=0.;
  float ii=(u_incl>=0.)?u_incl:.32+.3*(fract(u_fly_var*3.7)-.5);
  if(ty<2.5){
   nu=sg*6.2831853*f;                       // circular orbit, one revolution per cycle
  }else if(ty<4.5||(ty>5.5&&ty<6.5)){
   // Hyperbolic pass: equatorial (3), polar (4), slingshot (6).
   float e=(ty>5.5)?1.18:1.6;
   float nmax=acos(-1./e)*((ty>5.5)?.96:.93);
   nu=sg*nmax*sin((2.*ff-1.)*1.5707963);    // slow at the ends, fast at periapsis
   float r0=u_dmin*(1.+e)/(1.+e*cos(nu));
   rr=1./(1./r0+1./u_dmax)*(1.+u_dmin/u_dmax);   // soft cap: exactly dmin at periapsis
   if(u_incl<0.)ii=(ty<3.5)?.04:((ty<4.5)?1.5:.35);
  }else if(ty<5.5){
   // Plunge: dive to dmin and climb back out while sweeping through 1.4 pi.
   float c=cos(3.14159265*ff);
   rr=u_dmin+(u_dmax-u_dmin)*c*c;
   nu=Om+sg*4.4*ff;
   if(u_incl<0.)ii=.4;
  }else{
   // Drift: slow closed Lissajous-like wandering (periodic in ff).
   rr=mix(u_dmin,u_dmax,.5+.5*sin(6.2831853*ff+Om));
   nu=sg*6.2831853*ff;
   ii+=.3*sin(12.5663706*ff+Om*1.7);
  }
  rr=max(rr,u_dmin);
  vec3 e1=vec3(cos(Om),sin(Om),0.),e2=vec3(-sin(Om)*cos(ii),cos(Om)*cos(ii),sin(ii));
  camV=rr*(cos(nu)*e1+sin(nu)*e2);
  vec3 nn=normalize(cross(e1,e2));if(nn.z<0.)nn=-nn;
  upH=nn;                                    // plane normal is "up": stable horizon
  dist=rr;
  float cl=1.-clamp((rr-u_dmin)/(u_dmin*3.),0.,1.);
  closeFX=cl*cl;arrivalFX=4.*cl*(1.-cl);
 }
 dist=max(dist,useFly?3.2:5.4);
 vec3 cam=useFly?camV*(dist/max(length(camV),1e-4)):dist*vec3(cos(orbit)*cos(elev),sin(orbit)*cos(elev),sin(elev)),forward=normalize(-cam),baseRight=normalize(cross(forward,upH)),baseUp=cross(baseRight,forward),right=cos(roll)*baseRight+sin(roll)*baseUp,up=-sin(roll)*baseRight+cos(roll)*baseUp;
 // Disk normal at this frame. Used for the plane-crossing detection in
 // the integrator (line 318) and the jet axis check (line 320). Computing
 // it once per frame avoids recomputing the Rodrigues rotation in inner
 // loops. Falls back to +Z if the draw produced a near-degenerate axis
 // (probability zero from a uniform-on-sphere draw, but it's cheap).
 vec3 diskN=diskNormalAtTime(u_time);
 if(length(diskN)<.5||useFly)diskN=vec3(0.,0.,1.);
 // One ray, with subtle arrival-only peripheral distortion and framing drift.
 float r2=dot(p,p),shot=fract(u_time*u_orbit_rate/(2.*PI));
 vec2 lensP=p*(1.+.035*arrivalFX*r2/(1.+r2));
 lensP+=closeFX*vec2(u_path_sign*.12*sin(2.*PI*shot),.06);
 vec3 ray=normalize(forward+lensP.x*right+lensP.y*up);
 float impact=length(cross(cam,ray));bool captured=impact<2.598076;
 float u=1./length(cam),phi=0.;vec3 normal=normalize(cam),perp=cross(cross(normal,ray),normal);float plen=length(perp);vec3 tangent=plen>1e-6?perp/plen:right;float tang=dot(ray,tangent),du=abs(tang)>1e-6?-dot(ray,normal)/tang*u:200.*u;vec3 old=cam,pos=cam,color=vec3(0);float trans=1.;int nhit=0;
 // Disk-plane crossing test against diskN (the plane through the origin
 // with normal diskN). When old and pos are on opposite sides, their dot
 // products with diskN differ in sign and the segment crosses the disk.
 // The intersection point x lands in the disk plane up to fp precision;
 // we project it onto diskN=0 just to be sure, then take its radial dist
 // to gate the 2.8..13 band. The radial dist is taken in world XY because
 // the disk normal can be non-+Z; in disk_color the point is rotated into
 // the disk-local frame and the actual polar coords (r,a) live there.
 for(int i=0;i<int(u_max_steps);i++){
   float step=.04*(1.-.62*exp(-12.*(u-.667)*(u-.667)));
   du+=.5*(-u+u_lensing*1.5*u*u)*step;
   u+=du*step;
   du+=.5*(-u+u_lensing*1.5*u*u)*step;
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
    if(rd>u_isco-.2&&rd<13.){
     g_hit=float(nhit);nhit++;
     color+=trans*disk_color(x,diskN,closeFX);
     trans*=.72;
    }
   }
  }
 {
  float nA,pbA,hzA;vec3 bsA,stA,ntA,hlA;
  palSky(u_pal_a,nA,bsA,stA,ntA,hlA,pbA,hzA);
  float nM=nA,pbM=pbA,hzM=hzA;vec3 bsM=bsA,stM=stA,ntM=ntA,hlM=hlA;
  if(u_pal_mix>.001){
   float nB,pbB,hzB;vec3 bsB,stB,ntB,hlB;
   palSky(u_pal_b,nB,bsB,stB,ntB,hlB,pbB,hzB);
   nM=mix(nA,nB,u_pal_mix);pbM=mix(pbA,pbB,u_pal_mix);hzM=mix(hzA,hzB,u_pal_mix);
   bsM=mix(bsA,bsB,u_pal_mix);stM=mix(stA,stB,u_pal_mix);ntM=mix(ntA,ntB,u_pal_mix);hlM=mix(hlA,hlB,u_pal_mix);
  }
  if(!captured){
   vec3 d=length(pos-old)>1e-5?normalize(pos-old):ray;
   color+=trans*(stM*stars(d)+nM*ntM*nebula(d)*u_nebula_amt+bsM);
  }else if(hzM>.001){
   // whitehole: hazy blue-gray vortex interior instead of a black disc
   float ang=atan(p.y,p.x);   // periodic sampling: no seam at +-pi
   float sw=noise(vec2(cos(ang)*1.8+impact*2.-u_time*.05,sin(ang)*1.8+impact*3.+u_time*.07));
   vec3 hz=mix(srgbLin(vec3(.12,.29,.36)),srgbLin(vec3(.24,.45,.52)),sw);
   color+=trans*hzM*hz*(.35+.65*sw)*.55*(1.-.6*smoothstep(1.2,2.6,impact));
  }
  // Cheap halo: a wide falloff in impact parameter around the shadow edge,
  // tinted per palette; scaled by the palette strength and the bloom option.
  if(u_bloom>0.&&pbM>0.&&!captured){
   float ex=max(impact-2.598,0.);
   color+=u_bloom*pbM*(.55*exp(-ex*.6)+.25*exp(-ex*.14))*hlM*.30;
  }
 }
 if(u_jet>0.&&!captured){color+=trans*u_jet*1.2*jet_glow(cam,ray,diskN);}
 // Reinhard tonemap -> 1/2.2 gamma -> contrast toe + black point.
 // u_palette_contrast ~1.0 (gentle S-curve restoring Reinhard-flattened
 // contrast). 0.95-1.25 randomised per-launch.
 color*=u_exposure;
 color=color/(1.+color);
 color=pow(max(color,0.),vec3(1./2.2));
 // Soft S-curve: (c-.5)*k+.5, then small black point + small white point lift.
 float k=clamp(u_palette_contrast,.85,1.35);
 color=(color-.5)*k+.5;
 color=max(color-vec3(.018),vec3(0));
 fragColor=vec4(clamp(color*u_fade,0.,1.),1);
}
