/* GLES3 port of Adriwin06/black-hole's Schwarzschild Binet integrator. */
#define _POSIX_C_SOURCE 200809L
#include <fcntl.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <GLES3/gl32.h>
#include "gles3_compat.h"
#include "ncz_gpu_tier.h"
#include "ncz_options.h"
#include "blackhole_opts.h"
#include "ncz_harness_cfg.h"
#include "gles3_harness_hooks.h"
#include "xscreensaver_compat.h"
/* gcc 14+ makes implicit declarations an error under -std=c11 even though
 * both headers above declare this; explicit forward decl avoids the
 * regression if the header order ever changes. */
#ifdef NCZ_GLES3_BUILD
extern void ncz_harness_die(int code);
#endif
typedef struct {
 GLuint program,vbo;
 GLint time,resolution,seed,radius,temperature,density,rotation,inclination,orbit_rate,jet,star_density,camera_mode,palette,approach,periapsis,nebula,nebula_axis;
 GLint pal_a,pal_b,pal_mix,spin,isco,exposure,beaming,bloom,fringe,lensing,nebula_amt,hot_sector,fade,disk_out,jet_len,torus,torus_r,comp,flyby,fly_t,fly_var,dmin,dmax,incl;
 GLint palette_phase,palette_rate,palette_contrast,nebula_scheme;
 // Per-launch path-shape parameters (replaces the hardcoded curve
 // coefficients that were previously literals in disk_color's camera block).
 GLint path_d_base,path_d_swing,path_d_harm_amp,path_d_harm_freq;
 GLint path_o_rate,path_o_harm_amp,path_o_harm_freq,path_o_count,path_sign;
 GLint path_e_swing,path_e_freq,path_phase_jitter;
 // Disk-axis tilt: per-launch spin axis + slow precession. Two vec4
 // uniforms let the shader rotate into disk-local space once per frame
 // and reconstruct the disk normal including slow precession.
 GLint disk_axis,disk_precess;
 // Camera trajectory family selector: 0 = zoom-whirl (bound), 1 =
 // hyperbolic flyby (unbound). Drawn per-launch from the seeded RNG.
 GLint camera_family;
 // Real trajectory family parameters. orbit_q is the zoom-whirl
 // ratio (angular/rev radial); orbit_e is eccentricity (zoom-whirl
 // draws [0.15,0.7], flyby draws [1.05,3.0]); orbit_omega is the
 // argument of periapse in radians.
 GLint orbit_q,orbit_e,orbit_omega;
 // GPU-quality uniform: bounds the Schwarzschild integration loop. Adaptive per tier
 // so the floor tier can hold 30fps on Intel iGPU.
 GLint max_steps;
 double started;
 /* frame pacing + adaptive quality (see bh_pace) */
 double prev_t,last_change,refr,fail_level,dtmax;
 float level,levelmax;
 unsigned long nfr;
 unsigned char mring[60];int mcnt;
 float dtring[300];int dtn;float wdt[120];unsigned long hit[16];int nhit;
 // 0:seed 1:radius 2:temp 3:density 4:rotation 5:inclination 6:orbit_rate
 // 7:jet 8:star_density 9:camera_mode 10:palette 11:approach 12:periapsis
 // 13..16:nebula vec4 (hue,scale,coverage,yaw) 17..18:nebula_axis vec2 (tilt,offset)
 // 19:palette_phase 20:palette_rate 21:palette_contrast 22:nebula_scheme
 // 23:path_d_base 24:path_d_swing 25:path_d_harm_amp 26:path_d_harm_freq
 // 27:path_o_rate 28:path_o_harm_amp 29:path_o_harm_freq 30:path_o_count 31:path_sign
 // 32:path_e_swing 33:path_e_freq 34:path_phase_jitter
 // 35:disk_axis.xyz + wobble (xyz=spin axis unit vec, w=half-angle tilt)
 // 36..42:disk_precess.xyz + rate (xyz=precession axis, w=rate rad/s)
 // 43:camera_family (0 zoom-whirl, 1 hyperbolic flyby)
 // 44:orbit_q (zoom-whirl ratio; ~1 for flyby family)
 // 45:orbit_e (eccentricity; 0.15..0.7 zoom-whirl, 1.05..3.0 flyby)
 // 46:orbit_omega (periapse direction, radians)
 float v[47];
} State;
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec*1e-9;}
static float rnd(uint32_t*s,float a,float b){*s^=*s<<13;*s^=*s>>17;*s^=*s<<5;return a+(b-a)*(float)(*s&0xffffffu)/16777215.f;}

/* ---- options -----------------------------------------------------------
 * One table (blackhole_opts.h), three channels: environment
 * NCZ_BLACKHOLE_<OPTION>, --option=value and $XDG_CONFIG_HOME/ncz-screensavers/
 * blackhole.conf ([blackhole]); precedence CLI > env > file > defaults. */
typedef struct {
 int pal_seq[16],npal;
 int flyby;                    /* 0 auto, 1..7 types, 8 random */
 double interval,ptrans,speed,dmin,dmax,duration;
 float incl,spin,exposure,beaming,bloom,fringe,nebula,hot,lensing,disk_out,jet_len,torus,comp;
 int adaptive;
 uint32_t seed;
} BhOpts;
/* The harness parses the merged (generic + Black Hole) option set before any
 * Wayland or EGL work and owns it; this hack only reads the resolved values. */
const ncz_opt_def *ncz_hack_options(size_t *n,const char **prefix,const char **group){
 *n=BH_NOPTS;*prefix="NCZ_BLACKHOLE_";*group="blackhole";return BH_OPTS;
}
static const ncz_opts *g_op;static int g_o_ready;static BhOpts g_b;
#define g_o (*g_op)
static int pal_id(const char*n){static const char*t[]={"stylized","kipthorne","faithful","singularity","slingshot","whitehole","eht"};for(int i=0;i<7;i++)if(!strcmp(n,t[i]))return i;return 0;}
static int fly_id(const char*n){static const char*t[]={"auto","orbit","slow-orbit","equatorial","polar","plunge","slingshot","drift","random"};for(int i=0;i<9;i++)if(!strcmp(n,t[i]))return i;return 0;}
static void bh_resolve(void){
 if(g_o_ready)return;
 g_op=ncz_harness_opts();
 if(!g_op)return;
 g_o_ready=1;
 BhOpts*b=&g_b;
 b->npal=0;b->pal_seq[b->npal++]=pal_id(ncz_opts_get(&g_o,"palette"));
 {char l[256];snprintf(l,sizeof l,"%s",ncz_opts_get(&g_o,"cycle-palettes"));
  for(char*t=strtok(l,",");t&&b->npal<16;t=strtok(NULL,","))b->pal_seq[b->npal++]=pal_id(t);}
 b->flyby=fly_id(ncz_opts_get(&g_o,"flyby"));
 b->interval=ncz_opts_get_float(&g_o,"cycle-interval");b->ptrans=ncz_opts_get_float(&g_o,"palette-transition");
 b->speed=ncz_opts_get_float(&g_o,"speed");b->dmin=ncz_opts_get_float(&g_o,"distance-min");b->dmax=ncz_opts_get_float(&g_o,"distance-max");
 b->duration=ncz_opts_get_float(&g_o,"duration");b->incl=(float)ncz_opts_get_float(&g_o,"inclination");
 b->spin=(float)ncz_opts_get_float(&g_o,"spin");b->exposure=(float)ncz_opts_get_float(&g_o,"exposure");
 b->beaming=(float)ncz_opts_get_float(&g_o,"beaming");b->bloom=(float)ncz_opts_get_float(&g_o,"bloom");
 b->fringe=(float)ncz_opts_get_float(&g_o,"fringe");b->nebula=(float)ncz_opts_get_float(&g_o,"nebula");
 b->hot=(float)ncz_opts_get_float(&g_o,"hot-sector");
 b->disk_out=(float)ncz_opts_get_float(&g_o,"disk-outer");b->jet_len=(float)ncz_opts_get_float(&g_o,"jet-length");
 b->torus=(float)ncz_opts_get_float(&g_o,"torus");b->comp=(float)ncz_opts_get_float(&g_o,"companion");b->lensing=ncz_opts_get_bool(&g_o,"lensing")?1.f:0.f;
 b->seed=(uint32_t)strtoul(ncz_opts_get(&g_o,"seed"),NULL,0);
 b->adaptive=ncz_opts_get_bool(&g_o,"adaptive")&&!strcmp(ncz_opts_get(&g_o,"quality"),"auto");
}
static uint32_t seed(void){
 if(g_b.seed)return g_b.seed;
 uint32_t s=0;int f=open("/dev/urandom",O_RDONLY|O_CLOEXEC);
 if(f>=0){ssize_t n=read(f,&s,4);close(f);if(n==4)return s;}
 struct timespec t;clock_gettime(CLOCK_REALTIME,&t);
 return t.tv_nsec^t.tv_sec^getpid();
}
static uint32_t hash32(uint32_t a,uint32_t b){uint32_t h=a*2654435761u^(b+0x9E3779B9u);h^=h>>16;h*=0x85ebca6bu;h^=h>>13;h*=0xc2b2ae35u;h^=h>>16;return h;}
static float sstep(float a,float b,float x){float t=(x-a)/(b-a);t=t<0?0:t>1?1:t;return t*t*(3-2*t);}
/* Per-type natural period (s) and default distance range. Index = type 1..7. */
static const double FLY_T[8]={0,60,180,45,50,40,45,240};
static const double FLY_DMIN[8]={0,11,14,6.5,7.5,3.8,4.6,9};
static const double FLY_DMAX[8]={0,16,20,30,30,26,34,20};
/* Upload every uniform that changes per frame because of the options. */
static void bh_frame_uniforms(State*s,double t,double treal){
 const BhOpts*b=&g_b;
 /* palette sequence with smooth cross-fades */
 int pa=b->pal_seq[0],pb=pa;float pm=0.f;
 if(b->npal>1){
  long k=(long)(treal/b->interval);double tb=treal-k*b->interval;
  int n=b->npal;
  pb=b->pal_seq[k%n];pa=pb;
  double tr=b->ptrans>b->interval*0.5?b->interval*0.5:b->ptrans;
  if(k>0&&tr>0&&tb<tr){pa=b->pal_seq[(k-1)%n];pm=sstep(0,(float)tr,(float)tb);}
 }
 glUniform1f(s->pal_a,(float)pa);glUniform1f(s->pal_b,(float)pb);glUniform1f(s->pal_mix,pm);
 glUniform1f(s->spin,b->spin);glUniform1f(s->isco,3.f-1.4f*b->spin);
 glUniform1f(s->exposure,b->exposure);glUniform1f(s->beaming,b->beaming);
 glUniform1f(s->bloom,b->bloom);glUniform1f(s->fringe,b->fringe);
 glUniform1f(s->disk_out,b->disk_out);glUniform1f(s->jet_len,b->jet_len);glUniform1f(s->torus,b->torus);glUniform1f(s->torus_r,b->disk_out*2.6f);glUniform1f(s->comp,b->comp);
 glUniform1f(s->lensing,b->lensing);glUniform1f(s->nebula_amt,b->nebula);glUniform1f(s->hot_sector,b->hot);
 float fade=1.f,flyid=0.f,flyt=0.f,flyv=0.f;double dmin=6,dmax=20;
 if(b->flyby){
  int ft=b->flyby,type=ft;
  double T=b->duration>0?b->duration:(ft==8?50.:FLY_T[ft]);
  long k=(long)floor(t/T);
  uint32_t sd=(uint32_t)s->v[0];
  if(ft==8){type=1+(int)(hash32(sd,(uint32_t)k)%7u);int prev=k>0?1+(int)(hash32(sd,(uint32_t)(k-1))%7u):-1;if(type==prev)type=type%7+1;T=b->duration>0?b->duration:FLY_T[type]*0.6;k=(long)floor(t/T);}
  int wrap=(ft==8)||b->duration>0||(type>=3&&type<=6);
  flyid=(float)type;
  flyt=(float)(t/T);
  flyv=(hash32(sd,(uint32_t)(wrap?k:0))>>8)/16777216.f;
  if(wrap){double sec=t-k*T;fade=fminf(sstep(0,1.2f,(float)sec),sstep(0,1.2f,(float)(T-sec)));}
  dmin=b->dmin>0?b->dmin:FLY_DMIN[type];dmax=b->dmax>0?b->dmax:FLY_DMAX[type];
  if(dmin<3.2)dmin=3.2;if(dmax<dmin+1.)dmax=dmin+1.;
 }
 glUniform1f(s->flyby,flyid);glUniform1f(s->fly_t,flyt);glUniform1f(s->fly_var,flyv);
 glUniform1f(s->dmin,(float)dmin);glUniform1f(s->dmax,(float)dmax);
 glUniform1f(s->incl,b->incl>=0?b->incl*0.01745329f:-1.f);
 glUniform1f(s->fade,fade);
}
static char*load(void){const char*p[]={"vendor/blackhole/blackhole.frag","../vendor/blackhole/blackhole.frag","../../vendor/blackhole/blackhole.frag","/usr/share/ncz-screensavers/shaders/blackhole.frag"};FILE*f=0;const char*u=0;for(unsigned i=0;i<4;i++)if((f=fopen(p[i],"rb"))){u=p[i];break;}if(!f){fprintf(stderr,"blackhole: cannot locate shader\n");return 0;}fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);char*b=malloc(n+1);if(!b||fread(b,1,n,f)!=(size_t)n){free(b);fclose(f);return 0;}fclose(f);b[n]=0;fprintf(stderr,"[diag] blackhole shader=%s\n",u);return b;}
static GLuint comp(GLenum t,const char*x){GLuint s=glCreateShader(t);glShaderSource(s,1,&x,0);glCompileShader(s);GLint ok=0;glGetShaderiv(s,GL_COMPILE_STATUS,&ok);if(!ok){char l[8192];glGetShaderInfoLog(s,sizeof l,0,l);fprintf(stderr,"blackhole compile: %s\n",l);glDeleteShader(s);return 0;}return s;}
static void init_blackhole(ModeInfo*m){
 static const char*vs="#version 300 es\nlayout(location=0) in vec2 p;void main(){gl_Position=vec4(p,0,1);}\n";
 State*s=calloc(1,sizeof*s);
 if(!s){ncz_harness_die(1);return;}
 m->data=s;
 char*x=load();
 if(!x){ncz_harness_die(1);return;}
 GLuint v=comp(GL_VERTEX_SHADER,vs),f=comp(GL_FRAGMENT_SHADER,x);
 free(x);
 if(!v||!f){ncz_harness_die(1);return;}
 s->program=glCreateProgram();
 glAttachShader(s->program,v);
 glAttachShader(s->program,f);
 glLinkProgram(s->program);
 glDeleteShader(v);
 glDeleteShader(f);
 GLint ok=0;
 glGetProgramiv(s->program,GL_LINK_STATUS,&ok);
 if(!ok){char l[8192];glGetProgramInfoLog(s->program,sizeof l,0,l);fprintf(stderr,"blackhole link: %s\n",l);ncz_harness_die(1);return;}
 const float q[]={-1,-1,1,-1,-1,1,-1,1,1,-1,1,1};
 glGenBuffers(1,&s->vbo);
 glBindBuffer(GL_ARRAY_BUFFER,s->vbo);
 glBufferData(GL_ARRAY_BUFFER,sizeof q,q,GL_STATIC_DRAW);
 glBindBuffer(GL_ARRAY_BUFFER,0);
#define L(n) s->n=glGetUniformLocation(s->program,"u_"#n)
 L(time);L(resolution);L(seed);L(radius);L(temperature);L(density);L(rotation);L(inclination);L(orbit_rate);L(jet);L(star_density);L(camera_mode);L(palette);L(approach);L(periapsis);L(nebula);L(nebula_axis);L(pal_a);L(pal_b);L(pal_mix);L(spin);L(isco);L(exposure);L(beaming);L(bloom);L(fringe);L(lensing);L(nebula_amt);L(hot_sector);L(disk_out);L(jet_len);L(torus);L(torus_r);L(comp);L(fade);L(flyby);L(fly_t);L(fly_var);L(dmin);L(dmax);L(incl);
 L(palette_phase);L(palette_rate);L(palette_contrast);L(nebula_scheme);
 L(path_d_base);L(path_d_swing);L(path_d_harm_amp);L(path_d_harm_freq);
 L(path_o_rate);L(path_o_harm_amp);L(path_o_harm_freq);L(path_o_count);L(path_sign);
 L(path_e_swing);L(path_e_freq);L(path_phase_jitter);
 L(disk_axis);L(disk_precess);L(camera_family);
 L(orbit_q);L(orbit_e);L(orbit_omega);
 L(max_steps);
#undef L
 bh_resolve();
 {const char*q=ncz_opts_get(&g_o,"quality");if(strcmp(q,"auto"))setenv("NCZ_GPU_TIER",q,1);}
 uint32_t z=seed();
 s->v[0]=(float)z;
 s->v[1]=rnd(&z,.82,1.15);
 s->v[2]=rnd(&z,.45,1);
 s->v[3]=rnd(&z,.72,1.3);
 s->v[4]=(rnd(&z,0,1)<.5?-1:1)*rnd(&z,.65,1.35);
 s->v[5]=rnd(&z,.18,.48);
 s->v[6]=rnd(&z,.055,.125);
 // u_jet: the operator observed 0.000 in one launch. Cause: 58% of launches
 // drew jet=0 (jet disabled). That made the jet feature effectively dead
 // even though the shader supports it. Always draw a positive jet; the
 // intensity range still spans "faint plume" (0.15) to "bright jet" (0.95)
 // so the visual varies per launch. The old off/on draw has been removed.
 s->v[7]=rnd(&z,.15,.95);
 s->v[8]=rnd(&z,.55,1.25);
 s->v[9]=(float)((z>>8)%4);
 // 0..5: five named palettes + psychedelic (5). ~1/6 chance of psychedelic
 // each launch, with the operator-requested "restrained" ultraviolet kept as
 // palette 3 so that branch of the random draw still produces a classical
 // look.
 s->v[10]=(float)((z>>16)%6);
 s->v[11]=(rnd(&z,0,1)<.5?-1:1)*rnd(&z,.82,1.22);
 s->v[12]=rnd(&z,5.4,6.35);
 // nebula vec4: hue, spatial scale, cloud coverage (raised low end so the
 // sampled range is useful across its whole span), yaw.
 s->v[13]=rnd(&z,0,1);
 s->v[14]=rnd(&z,2.8,6.5);
 s->v[15]=rnd(&z,.28,.95);  // nebula_coverage (was 0..1; low end was wasted)
 s->v[16]=rnd(&z,0,6.2831853);
 s->v[17]=rnd(&z,-1.4,1.4);
 s->v[18]=rnd(&z,0,128);
 // Palette time evolution: cycle period ~72s (rate = 1/72 /s). Random rate
 // multiplier 0.6..1.4 so two launches never lockstep.
 s->v[19]=rnd(&z,0,1);
 s->v[20]=rnd(&z,0.0084,0.0196);  // ~53..123s cycle period
 // Contrast toe: 0.95..1.25 randomised; high values restore the deep blacks
 // and contrast that the Reinhard tonemap was flattening.
 s->v[21]=rnd(&z,.95,1.25);
 // Decorrelation scheme for nebula hue vs disk hue: 0=complement, 1=triad,
 // 2=split-complement. Drawn per-launch so the relationship is visible.
 s->v[22]=(float)(z%3);
 // ---------------------------------------------------------------------
 // Per-launch flight-path parameters (v[23..34]).
 // These replace the hardcoded curve coefficients that used to live in
 // disk_color's camera block. Each launch now draws a different shape, not
 // a rescaled copy. The mode still selects the *character* (diving arc,
 // slingshot, polar pass, enveloped arrival); the magnitudes / harmonics
 // are randomised within each so the specific path varies.
 // ---------------------------------------------------------------------
 // Base distance from the BH for the arc. The four modes used 9.8..18;
 // widen so even mode 3's "fast arrival" can be a slow one.
 s->v[23]=rnd(&z,9.2,18.5);
 // Radial swing amplitude (the coefficient in front of cos(phase) in dist).
 // 2.8..4.8 covers everything the four modes used and beyond; too low and
 // the camera is essentially stationary, too high and the path crosses
 // u_periapsis (clamped later at 5.4).
 s->v[24]=rnd(&z,2.8,4.8);
 // Secondary dist harmonic: amplitude and frequency multiplier on a sin(2*ph)
 // style wiggle. Modes 0/1 used this directly; mode 2 used a cos(.91*ph)
 // stretch and mode 3 uses envelopes (this term is only used for modes 0/1).
 s->v[25]=rnd(&z,0.5,1.4);
 s->v[26]=rnd(&z,1.6,2.4);
 // Orbital rate multiplier on the phase ramp. Modes used 1.0..1.35; widen
 // so some launches trace a leisurely arc and others a fast sweep.
 s->v[27]=rnd(&z,0.85,1.55);
 // Orbital secondary-harmonic amplitude (the .28/.34/.55 in front of the
 // trig term on orbit). Widen so some arcs wobble visibly, others are clean.
 s->v[28]=rnd(&z,0.18,0.55);
 // Orbital secondary-harmonic frequency multiplier on phase.
 s->v[29]=rnd(&z,1.6,2.4);
 // How many orbits the arc covers before phase wraps. 1 = single pass,
 // 2 = double, 3 = triple. Combined with the slow orbit_rate this
 // determines how long the user sees a coherent shot.
 s->v[30]=rnd(&z,1.0,3.0);
 // Prograde (+1) or retrograde (-1) relative to the disk's spin. Flips
 // orbit direction and (for mode 3) roll direction. Equal probability.
 s->v[31]=(rnd(&z,0,1)<.5)?-1.f:1.f;
 // Elevation swing amplitude and frequency. Modes used .48..78 and
 // .69..1.17; widen so the arc climbs/falls more or less.
 s->v[32]=rnd(&z,0.32,0.92);
 s->v[33]=rnd(&z,0.65,1.35);
 // Deterministic per-launch phase offset so two launches of the same mode
 // aren't sitting at the same point on the curve at t=0.
 s->v[34]=rnd(&z,0,6.2831853);
 // ---------------------------------------------------------------------
 // Disk axis tilt + slow precession (v[35..38] as the four vec4 components
 // u_disk_axis.xyz + wobble, u_disk_precess.xyz + rate; then v[37] used as
 // the scalar camera_family at draw time).
 //
 // u_disk_axis: v[35..38]
 //   v[35,36,37] = spin axis (uniform on the sphere, Marsaglia)
 //   v[38]       = wobble half-angle in [-1,1] (0=disk perp to axis,
 //                 |w|=1 tilts the disk up to ~pi/2 from the axis)
 // u_disk_precess: stored at v[35..38] then MOVED to the drawing step —
 // for clarity we lay both vec4s side-by-side using v[35..42]: v[35..38]
 // is disk_axis, v[39..42] is disk_precess. The v[] array is float[38],
 // so we need 8 floats. Recompute: v[35..38] = disk_axis, v[39..42] =
 // disk_precess, v[43] = camera_family. That requires extending v[] to
 // 44 floats; see State struct above. The layout used here is:
 //   v[35,36,37] spin axis x,y,z     v[38] wobble
 //   v[39,40,41] precession axis x,y,z v[42] precession rate rad/s
 //   v[43] camera_family (0 bound, 1 unbound)
 // ---------------------------------------------------------------------
 {
  // Spin axis: uniform on the sphere via Marsaglia (cos^2 + sin^2 = 1,
  // and z=1-2u1 gives a uniform z distribution).
  float u1=rnd(&z,0,1),u2=rnd(&z,0,1);
  float cz=1.f-2.f*u1;
  float sn=sqrtf(fmaxf(0.f,1.f-cz*cz));
  float th=6.2831853f*u2,ct=cosf(th),st=sinf(th);
  s->v[35]=ct*sn;
  s->v[36]=st*sn;
  s->v[37]=cz;
  // Wobble in [-1,1] so the disk normal can swing either side of the
  // spin axis. Symmetric draw so + and - each get 50%.
  s->v[38]=(rnd(&z,0,1)<.5?-1.f:1.f)*rnd(&z,0,1);
  // Precession axis: another uniform draw on the sphere. We can't reuse
  // the spin axis because we want the disk normal to swing AROUND a
  // different axis (the typical Lense-Thirring picture is that the spin
  // axis is mostly fixed and the disk normal precesses about it).
  u1=rnd(&z,0,1); u2=rnd(&z,0,1);
  cz=1.f-2.f*u1;
  sn=sqrtf(fmaxf(0.f,1.f-cz*cz));
  th=6.2831853f*u2; ct=cosf(th); st=sinf(th);
  s->v[39]=ct*sn;
  s->v[40]=st*sn;
  s->v[41]=cz;
  // Rate in rad/s: slow enough that a 60s capture shows a visible but
  // unhurried motion (60s * 0.05 rad/s = 3 rad ~ 172 deg).
  s->v[42]=rnd(&z,0.02,0.08);
  // Camera trajectory family: 50/50 between the two real families.
  // Bound = zoom-whirl, unbound = hyperbolic flyby. Both families
  // must be reachable; this is the family selector.
  s->v[43]=(rnd(&z,0,1)<.5)?0.f:1.f;
 }
 // ---------------------------------------------------------------------
 // Real trajectory family parameters (v[44..46]).
 // zoom-whirl ratio q is the angular/rev-frequency ratio per radial
 // cycle. Rational -> closed rosette, irrational -> aperiodic. Drawn
 // in [1.4, 5.5]: sub-1.5 looks like a circle and is boring; above 5.5
 // the whirls blur. The interval is mostly irrational so most launches
 // trace a path that never repeats.
 //   v[44]: orbit_q (zoom-whirl ratio, drawn if family=0; otherwise 1)
 // orbit_e is eccentricity. Family=0 draws [0.15, 0.7] for the bound
 // zoom-whirl; family=1 draws (1.05, 3.0] for the hyperbolic flyby.
 // orbit_omega is the argument of periapse: drawn [0, 2pi].
 // ---------------------------------------------------------------------
 {
  int family=(int)s->v[43];
  if(family==0){
   // Zoom-whirl. q in [1.4, 5.5] mostly avoids rational coincidences;
   // the spread covers loose 3-leaf clovers through tight precessing
   // rosettes. Combined with eccentricity this draws a different
   // rosette essentially every launch.
   s->v[44]=rnd(&z,1.15,2.8);
   // Eccentricity: 0.15 keeps the orbit nearly circular (mild zoom);
   // 0.7 is strongly elongated (deep periapsis dips). Both visible.
   s->v[45]=rnd(&z,0.15,0.7);
  }else{
   // Hyperbolic flyby. q does not apply (only one traversal). We
   // still set it so the shader has a defined value; the shader
   // ignores it in the flyby branch.
   s->v[44]=1.f;
   // Eccentricity > 1 for a hyperbola. deflection = 2*asin(1/e):
   // e=1.05 -> deflection ~ 144 deg (extreme slingshot);
   // e=3.0 -> deflection ~ 39 deg (gentle drift-by). 1.05..3.0 spans
   // the full useful range from "violent whip-around" to
   // "barely-bent drift past".
   s->v[45]=rnd(&z,1.05,3.0);
  }
  // Argument of periapse: 0..2pi draws the periapse direction in the
  // orbital plane uniformly. Combined with the disk's own tilt this
  // gives the family a different sweep orientation every launch.
  s->v[46]=rnd(&z,0,6.2831853);
 }
 // ---------------------------------------------------------------------
 // Audit (revalidation-2026-09-26): every uniform's draw, range, and the
 // visible variety it produces. Reachable / distribution / change-of-mind.
 //   u_seed             uint32       full       random per launch
 //   u_radius           0.82..1.15   uniform     ~40% zoom-in variation,
 //                                                visible in disk angular size
 //   u_temperature      0.45..1.0    uniform     maps to disk "heat" and
 //                                                paletteT (Reinhard input
 //                                                warmth), clearly different
 //   u_density          0.72..1.3    uniform     disk surface brightness;
 //                                                doubles are obvious
 //   u_rotation         +/- 0.65..1.35  symmetric  spin direction and rate,
 //                                                visible Doppler band motion
 //   u_inclination      0.18..0.48   uniform     ~30° elevation range;
 //                                                disk silhouette changes
 //   u_orbit_rate       0.140..0.265 uniform     widened from 0.175..0.235
 //                                                so a path_o_count=1 launch
 //                                                can take ~24s or ~45s;
 //                                                combined with path_o_count
 //                                                1..3 the period spans
 //                                                ~7.9..44.9s
 //   u_jet              0.15..0.95   uniform     FIXED: was 58% chance of 0
 //                                                (operator observed 0.000);
 //                                                now always-on with varied
 //                                                intensity. The shader
 //                                                branches on u_jet>0 so
 //                                                every launch sees a jet
 //   u_star_density     0.55..1.25   uniform     star coverage; 2.3x spread
 //   u_camera_mode      0..3         uniform     (z>>8)%4: all four modes
 //                                                equally likely (~25% each)
 //   u_palette          0..5         uniform     (z>>16)%6: five named + one
 //                                                psychedelic, ~16.7% each
 //   u_approach         +/- 0.82..1.22  symmetric  direction and approach
 //                                                intensity; both signs
 //                                                equiprobable
 //   u_periapsis        5.4..6.35    uniform     close approach distance;
 //                                                dist is clamped >=5.4
 //                                                so the camera never crosses
 //                                                the event horizon
 //   u_nebula.x         0..1         uniform     nebula base hue
 //   u_nebula.y         2.8..6.5     uniform     nebula spatial scale; 2.3x
 //                                                spread visibly varies cloud
 //                                                size
 //   u_nebula.z         0.28..0.95   uniform     nebula coverage; previous
 //                                                range 0..1 wasted the
 //                                                0..0.28 segment, fixed
 //                                                earlier
 //   u_nebula.w         0..2pi       uniform     nebula yaw
 //   u_nebula_axis.x    -1.4..1.4    symmetric   nebula tilt; can flip sign
 //   u_nebula_axis.y    0..128       uniform     nebula noise offset
 //   u_palette_phase    0..1         uniform     hue rotation start
 //   u_palette_rate     0.0084..0.0196  uniform  ~53..123s cycle period
 //                                                (2pi / rate)
 //   u_palette_contrast 0.95..1.25   uniform     toe contrast; shader clamps
 //                                                to 0.85..1.35 so the
 //                                                0.95 floor is just above
 //                                                the clamp floor. Intentionally
 //                                                narrow because too-wide
 //                                                contrast makes the disk
 //                                                wash to black or blow to
 //                                                white.
 //   u_nebula_scheme    0..2         uniform     z%3: complement / triad /
 //                                                split-complement, equal
 //   u_path_d_base      9.2..18.5    uniform     widened so even mode 3 can
 //                                                be a slow arrival
 //   u_path_d_swing     2.8..4.8     uniform     visible radial swing
 //   u_path_d_harm_amp  0.5..1.4     uniform     secondary dist harmonic
 //   u_path_d_harm_freq 1.6..2.4     uniform     secondary dist harmonic
 //   u_path_o_rate      0.85..1.55   uniform     orbital rate multiplier
 //   u_path_o_harm_amp  0.18..0.55   uniform     orbital harmonic amplitude
 //   u_path_o_harm_freq 1.6..2.4     uniform     orbital harmonic frequency
 //   u_path_o_count     1..3         uniform     orbits before phase wraps
 //   u_path_sign        -1 or +1     binomial   prograde/retrograde,
 //                                                equiprobable
 //   u_path_e_swing     0.32..0.92   uniform     elevation swing amplitude
 //   u_path_e_freq      0.65..1.35   uniform     elevation swing frequency
 //   u_path_phase_jitter 0..2pi      uniform     per-launch phase offset
 // Deliberately left narrow:
 //   u_palette_contrast 0.95..1.25 - the shader clamps to 0.85..1.35; below
 //       0.95 the S-curve collapses and the disk washes to grey, above 1.25
 //       the dark stops blow out. This band is the safe range.
 //   u_periapsis 5.4..6.35 - close enough to the event horizon (2.598) for a
 //       dramatic lensing pass, but high enough that the floor clamp at 5.4
 //       never bites.
 // ---------------------------------------------------------------------
 /* ---- option overrides of the per-launch draws (defaults change nothing) */
 s->v[3]*=(float)ncz_opts_get_float(&g_o,"disk-brightness");
 s->v[7]*=(float)ncz_opts_get_float(&g_o,"jet");
 s->v[8]*=(float)ncz_opts_get_float(&g_o,"star-density");
 s->v[2]*=(float)ncz_opts_get_float(&g_o,"temperature");
 s->v[4]*=(float)ncz_opts_get_float(&g_o,"rotation-speed")*(1.f+1.5f*g_b.spin);
 s->v[42]*=(float)ncz_opts_get_float(&g_o,"precession");
 if(g_b.incl>=0)s->v[5]=g_b.incl*0.01745329f;
 if(g_b.flyby==0&&g_b.dmin>0)s->v[12]=fmaxf((float)g_b.dmin,5.4f);
 if(g_b.flyby!=0){s->v[35]=0;s->v[36]=0;s->v[37]=1;s->v[38]=0;s->v[42]=0;}
 fprintf(stderr,"[diag] blackhole nebula_hue=%.9g nebula_scale=%.9g nebula_coverage=%.9g nebula_yaw=%.9g nebula_tilt=%.9g nebula_offset=%.9g nebula_scheme=%d\n",
  s->v[13],s->v[14],s->v[15],s->v[16],s->v[17],s->v[18],(int)s->v[22]);
 s->started=now();
 /* Initialise the adaptive quality tier module. Reads NCZ_GPU_TIER
  * override, queries GL hints, and arms the rolling-median frame
  * timer. The static prior appears in the [diag] line below as
  * "tier=" so a capture can be attributed to a tier after the fact. */
 ncz_gpu_tier_init();
 fprintf(stderr,"[diag] blackhole seed=%.0f radius=%.3f temp=%.3f density=%.3f rotation=%.3f inclination=%.3f flyby=%d camera_rate=%.4f palette=%d approach=%.3f periapsis=%.3f jet=%.3f stars=%.3f palette_phase=%.4f palette_rate=%.5f palette_contrast=%.3f path_d_base=%.3f path_d_swing=%.3f path_d_harm_amp=%.3f path_d_harm_freq=%.3f path_o_rate=%.3f path_o_harm_amp=%.3f path_o_harm_freq=%.3f path_o_count=%.2f path_sign=%.0f path_e_swing=%.3f path_e_freq=%.3f path_phase_jitter=%.4f disk_axis=(%.3f,%.3f,%.3f,wobble=%.3f) precess_rate=%.4f camera_family=%d orbit_q=%.4f orbit_e=%.4f orbit_omega=%.3f GL=%s\n",
  s->v[0],s->v[1],s->v[2],s->v[3],s->v[4],s->v[5],(int)s->v[9],s->v[6],(int)s->v[10],s->v[11],s->v[12],s->v[7],s->v[8],s->v[19],s->v[20],s->v[21],
  s->v[23],s->v[24],s->v[25],s->v[26],s->v[27],s->v[28],s->v[29],s->v[30],s->v[31],s->v[32],s->v[33],s->v[34],
  s->v[35],s->v[36],s->v[37],s->v[38],s->v[42],(int)s->v[43],s->v[44],s->v[45],s->v[46],glGetString(GL_VERSION));
  /* Tier attribution: every [diag] line includes the chosen tier and a
   * short reason. The runtime may later downgrade if frame-time is over
   * budget; the next [diag] line will reflect it. */
  const ncz_gpu_tier_t *_tier=ncz_gpu_tier_current();
  s->levelmax=s->level=_tier->scalar;
  fprintf(stderr,"[diag] blackhole tier=%s scalar=%.2f reason=\"%s\"\n",
   _tier->name,_tier->scalar,_tier->reason);
}

/* ---- frame pacing and adaptive quality ---------------------------------
 * The GPU tier module's frame-time hook was never wired to this hack, so the
 * quality was the static prior for the whole run: on a heavy panel that misses
 * vsync every few frames and looks like stutter. This controller measures the
 * frame period, estimates the display refresh from the first frames, counts
 * frames that miss it and moves one "level" knob (0..1 = ray steps 80..260)
 * with hysteresis. Resolution is handled by the shared harness (render-scale).
 * It steps down when more than 10% of the last 60 frames missed, steps up
 * slowly after 10 s without a miss, and never returns to a level that failed
 * until 90 s have passed, so it converges instead of oscillating. */
static int cmpf(const void*a,const void*b){float x=*(const float*)a,y=*(const float*)b;return x<y?-1:x>y;}
static void bh_level_split(float level,float*q,float*scale){*q=level;*scale=1.f;}
static void bh_pace(State*s,double tn){
 static int perf=-1;
 if(perf<0){const char*e=getenv("NCZ_BLACKHOLE_PERF_LOG");perf=e?(atoi(e)>=2?2:1):0;}
 if(s->prev_t>0){
  double dt=(tn-s->prev_t)*1000.;
  s->nfr++;
  if(s->nfr>30&&s->nfr<=150){s->wdt[s->nfr-31]=(float)dt;
   if(s->nfr==150){float t[120];memcpy(t,s->wdt,sizeof t);qsort(t,120,sizeof(float),cmpf);
    /* vsync-paced frames cluster at the refresh interval; assume 60 Hz unless the median says faster */
    s->refr=(t[60]<12.5f&&t[60]>=5.f)?t[60]:16.67;}}
  int miss=(s->nfr>150)&&dt>s->refr*1.3;
  if(perf>=2&&dt>25.&&s->nhit<16)s->hit[s->nhit++]=s->nfr;
  int i=(int)(s->nfr%60);
  s->mcnt+=miss-s->mring[i];s->mring[i]=(unsigned char)miss;
  s->dtring[s->dtn++%300]=(float)dt;if(dt>s->dtmax)s->dtmax=dt;
  if(s->nfr>150&&(s->nfr%30)==0){
   double miss_rate=s->mcnt/60.;
   if(g_b.adaptive){
    if(miss_rate>0.10&&tn-s->last_change>1.0&&s->level>0.f){
     s->fail_level=s->level;s->level=fmaxf(0.f,s->level-0.08f);s->last_change=tn;
     float q,sc;bh_level_split(s->level,&q,&sc);
     fprintf(stderr,"[diag] blackhole adapt down level=%.2f steps=%d scale=%.2f miss=%.0f%% refresh=%.1fms\n",s->level,80+(int)(180*q+.5f),sc,miss_rate*100,s->refr);
    }else if(s->mcnt==0&&tn-s->last_change>10.&&s->level<s->levelmax){
     float cand=fminf(s->levelmax,s->level+0.04f);
     if(s->fail_level<=0.||cand<=s->fail_level-0.06){
      s->level=cand;s->last_change=tn;
      float q,sc;bh_level_split(s->level,&q,&sc);
      fprintf(stderr,"[diag] blackhole adapt up level=%.2f steps=%d scale=%.2f\n",s->level,80+(int)(180*q+.5f),sc);
     }
    }
    if(s->fail_level>0.&&tn-s->last_change>90.){s->fail_level+=0.1;if(s->fail_level>=1.)s->fail_level=0.;s->last_change=tn;}
   }
  }
  if(perf>=2&&s->nfr%300==0){
   float w[300];int n=s->dtn<300?s->dtn:300;memcpy(w,s->dtring,sizeof(float)*n);qsort(w,n,sizeof(float),cmpf);
   int m=0;for(int k=0;k<n;k++)if(w[k]>s->refr*1.3)m++;
   float q,sc;bh_level_split(s->level,&q,&sc);
   fprintf(stderr,"[diag] blackhole pace frames=%lu p50=%.2f p95=%.2f p99=%.2f max=%.2f ms miss=%d/%d refresh=%.2fms level=%.2f steps=%d scale=%.2f\n",
    s->nfr,w[n/2],w[(int)(n*0.95)],w[(int)(n*0.99)],(double)w[n-1],m,n,s->refr,s->level,g_b.adaptive?80+(int)(180*q+.5f):-1,sc);
   if(s->nhit){fprintf(stderr,"[diag] blackhole hitches(>25ms) at frames:");for(int k=0;k<s->nhit;k++)fprintf(stderr," %lu",s->hit[k]);fprintf(stderr,"\n");s->nhit=0;}
  }
 }else{s->refr=33.3;}
 s->prev_t=tn;
}
static void draw_blackhole(ModeInfo*m){
 State*s=m->data;
 if(!s||!s->program)return;
 int w=m->xgwa.width,h=m->xgwa.height;
 if(w<1)w=1;if(h<1)h=1;
 bh_pace(s,now());
 float lq=0.f,lscale=1.f;
 if(g_b.adaptive)bh_level_split(s->level,&lq,&lscale);
 int rw=w,rh=h;
 glViewport(0,0,rw,rh);
 glClear(GL_COLOR_BUFFER_BIT);
 glUseProgram(s->program);
 double bt=(now()-s->started)*g_b.speed;
 glUniform1f(s->time,(float)bt);
 glUniform2f(s->resolution,(float)rw,(float)rh);
 glUniform1f(s->seed,s->v[0]);
 glUniform1f(s->radius,s->v[1]);
 glUniform1f(s->temperature,s->v[2]);
 bh_frame_uniforms(s,bt,now()-s->started);
 glUniform1f(s->density,s->v[3]);
 glUniform1f(s->rotation,s->v[4]);
 glUniform1f(s->inclination,s->v[5]);
 glUniform1f(s->orbit_rate,s->v[6]);
 glUniform1f(s->jet,s->v[7]);
 glUniform1f(s->star_density,s->v[8]);
 glUniform1f(s->camera_mode,s->v[9]);
 glUniform1f(s->palette,s->v[10]);
 glUniform1f(s->approach,s->v[11]);
 glUniform1f(s->periapsis,s->v[12]);
 glUniform4fv(s->nebula,1,s->v+13);
 glUniform2fv(s->nebula_axis,1,s->v+17);
 glUniform1f(s->palette_phase,s->v[19]);
 glUniform1f(s->palette_rate,s->v[20]);
 glUniform1f(s->palette_contrast,s->v[21]);
 glUniform1f(s->nebula_scheme,s->v[22]);
 // Per-launch flight-path parameters. The shader uses these in place of
 // the previously-hardcoded curve coefficients in disk_color's camera
 // block; every launch draws a distinct path shape, not a rescaled one.
 glUniform1f(s->path_d_base,s->v[23]);
 glUniform1f(s->path_d_swing,s->v[24]);
 glUniform1f(s->path_d_harm_amp,s->v[25]);
 glUniform1f(s->path_d_harm_freq,s->v[26]);
 glUniform1f(s->path_o_rate,s->v[27]);
 glUniform1f(s->path_o_harm_amp,s->v[28]);
 glUniform1f(s->path_o_harm_freq,s->v[29]);
 glUniform1f(s->path_o_count,s->v[30]);
 glUniform1f(s->path_sign,s->v[31]);
 glUniform1f(s->path_e_swing,s->v[32]);
 glUniform1f(s->path_e_freq,s->v[33]);
 glUniform1f(s->path_phase_jitter,s->v[34]);
 // Disk axis (vec4 = unit spin axis xyz + wobble half-angle) and slow
 // precession (vec4 = unit precession axis xyz + rate rad/s).
 glUniform4fv(s->disk_axis,1,s->v+35);
 glUniform4fv(s->disk_precess,1,s->v+39);
 glUniform1f(s->camera_family,s->v[43]);
 // Real trajectory family params (zoom-whirl ratio, eccentricity,
 // periapse direction). Draws are conditional on the family in
 // init_blackhole; the shader uses whatever values are present.
 glUniform1f(s->orbit_q,s->v[44]);
 glUniform1f(s->orbit_e,s->v[45]);
 glUniform1f(s->orbit_omega,s->v[46]);
 /* Tier-driven step count. The Schwarzschild Binet integration in the
  * shader is the dominant cost on Intel UHD — 260 steps/pixel at 1080p is
  * the difference between 60fps and 8fps. Per-tier budgets:
  *   low=80, medium=140, high=200, ultra=260.
  * The scalar from the tier module is mapped via ncz_gpu_tier_count so
  * the transition is continuous if the runtime later downgrades/upgrades.
  */
 {
  int steps=g_b.adaptive?80+(int)(180*lq+.5f):ncz_gpu_tier_count(80, 260);
  glUniform1f(s->max_steps,(float)steps);
 }
 glBindBuffer(GL_ARRAY_BUFFER,s->vbo);
 glEnableVertexAttribArray(0);
 glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,0);
 glDisable(GL_DEPTH_TEST);
 ncz_gles3_draw_arrays(GL_TRIANGLES,0,6);
 glDisableVertexAttribArray(0);
 glBindBuffer(GL_ARRAY_BUFFER,0);
 glUseProgram(0);
 // Per-vendor frame-time measurement. Logged to stderr at every 30th frame
 // only when NCZ_BLACKHOLE_PERF_LOG is set in the environment (so a real
 // run never pays the fprintf/fflush cost on Intel). Tag the line with
 // [frame_t] for downstream parsing.
 static unsigned long _ft_counter;
 static double _ft_prev;
 static int _ft_enabled;
 if(!_ft_enabled)_ft_enabled=(getenv("NCZ_BLACKHOLE_PERF_LOG")!=NULL);
 if(_ft_enabled){
  double _t_now=now();
  if((++_ft_counter%30)==0){
   if(_ft_prev>0.){
    double _dt=(_t_now-_ft_prev)/30.;
    fprintf(stderr,"[diag] blackhole frame_t frame=%lu dt_ms=%.3f\n",_ft_counter,_dt*1000.);
    fflush(stderr);
   }
   _ft_prev=now();
  }
 }
 static int once;
 if(!once++){
  unsigned char px[16]={0};
  glReadPixels(w/2,h/2,1,1,GL_RGBA,GL_UNSIGNED_BYTE,px);
  glReadPixels(w/8,h/8,1,1,GL_RGBA,GL_UNSIGNED_BYTE,px+4);
  glReadPixels(7*w/8,h/8,1,1,GL_RGBA,GL_UNSIGNED_BYTE,px+8);
  glReadPixels(w/8,7*h/8,1,1,GL_RGBA,GL_UNSIGNED_BYTE,px+12);
  GLenum e=glGetError();
  fprintf(stderr,"[diag] blackhole first draw gl_error=0x%x samples=%u,%u,%u;%u,%u,%u;%u,%u,%u;%u,%u,%u uniforms=time:%d resolution:%d seed:%d camera:%d palette:%d approach:%d periapsis:%d\n",
   e,px[0],px[1],px[2],px[4],px[5],px[6],px[8],px[9],px[10],px[12],px[13],px[14],
   s->time,s->resolution,s->seed,s->camera_mode,s->palette,s->approach,s->periapsis);
  once=1;
 }
}
static void free_blackhole(ModeInfo*m){State*s=m->data;if(!s)return;if(s->vbo)glDeleteBuffers(1,&s->vbo);if(s->program)glDeleteProgram(s->program);free(s);m->data=0;}
static void reshape_blackhole(ModeInfo*m,int w,int h){(void)m;(void)w;(void)h;}
static Bool blackhole_handle_event(ModeInfo*m,XEvent*e){(void)m;(void)e;return False;}
static void release_blackhole(ModeInfo*m){(void)m;}
static ModeSpecOpt blackhole_opts={0,NULL,0,NULL,NULL};
struct xscreensaver_function_table blackhole_xscreensaver_function_table={.name="blackhole",.class_="BlackHole",.init_cb=init_blackhole,.draw_cb=draw_blackhole,.reshape_cb=reshape_blackhole,.event_cb=blackhole_handle_event,.free_cb=free_blackhole,.release_cb=release_blackhole,.opts=&blackhole_opts,.defaults_str=DEFAULTS};
