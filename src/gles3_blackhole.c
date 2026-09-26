/* GLES3 port of Adriwin06/black-hole's Schwarzschild Binet integrator. */
#define _POSIX_C_SOURCE 200809L
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <GLES3/gl32.h>
#include "gles3_compat.h"
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
 GLint palette_phase,palette_rate,palette_contrast,nebula_scheme;
 // Per-launch path-shape parameters (replaces the hardcoded curve
 // coefficients that were previously literals in disk_color's camera block).
 GLint path_d_base,path_d_swing,path_d_harm_amp,path_d_harm_freq;
 GLint path_o_rate,path_o_harm_amp,path_o_harm_freq,path_o_count,path_sign;
 GLint path_e_swing,path_e_freq,path_phase_jitter;
 double started;
 // 0:seed 1:radius 2:temp 3:density 4:rotation 5:inclination 6:orbit_rate
 // 7:jet 8:star_density 9:camera_mode 10:palette 11:approach 12:periapsis
 // 13..16:nebula vec4 (hue,scale,coverage,yaw) 17..18:nebula_axis vec2 (tilt,offset)
 // 19:palette_phase 20:palette_rate 21:palette_contrast 22:nebula_scheme
 // 23:path_d_base 24:path_d_swing 25:path_d_harm_amp 26:path_d_harm_freq
 // 27:path_o_rate 28:path_o_harm_amp 29:path_o_harm_freq 30:path_o_count 31:path_sign
 // 32:path_e_swing 33:path_e_freq 34:path_phase_jitter
 float v[35];
} State;
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec*1e-9;}
static uint32_t seed(void){uint32_t s=0;int f=open("/dev/urandom",O_RDONLY|O_CLOEXEC);if(f>=0){ssize_t n=read(f,&s,4);close(f);if(n==4)return s;}struct timespec t;clock_gettime(CLOCK_REALTIME,&t);return t.tv_nsec^t.tv_sec^getpid();}
static float rnd(uint32_t*s,float a,float b){*s^=*s<<13;*s^=*s>>17;*s^=*s<<5;return a+(b-a)*(float)(*s&0xffffffu)/16777215.f;}
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
 L(time);L(resolution);L(seed);L(radius);L(temperature);L(density);L(rotation);L(inclination);L(orbit_rate);L(jet);L(star_density);L(camera_mode);L(palette);L(approach);L(periapsis);L(nebula);L(nebula_axis);
 L(palette_phase);L(palette_rate);L(palette_contrast);L(nebula_scheme);
 L(path_d_base);L(path_d_swing);L(path_d_harm_amp);L(path_d_harm_freq);
 L(path_o_rate);L(path_o_harm_amp);L(path_o_harm_freq);L(path_o_count);L(path_sign);
 L(path_e_swing);L(path_e_freq);L(path_phase_jitter);
#undef L
 uint32_t z=seed();
 // Optional deterministic seed for capture runs (eval harness). When
 // NCZ_BLACKHOLE_SEED is set in the environment, use it instead of
 // /dev/urandom so a launch can be reproduced by seed value.
 {
  const char*env=getenv("NCZ_BLACKHOLE_SEED");
  if(env&&*env){
   unsigned long v=strtoul(env,NULL,0);
   z=(uint32_t)v;
  }
 }
 s->v[0]=(float)z;
 s->v[1]=rnd(&z,.82,1.15);
 s->v[2]=rnd(&z,.45,1);
 s->v[3]=rnd(&z,.72,1.3);
 s->v[4]=(rnd(&z,0,1)<.5?-1:1)*rnd(&z,.65,1.35);
 s->v[5]=rnd(&z,.18,.48);
 s->v[6]=rnd(&z,.140,.265);
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
 fprintf(stderr,"[diag] blackhole nebula_hue=%.9g nebula_scale=%.9g nebula_coverage=%.9g nebula_yaw=%.9g nebula_tilt=%.9g nebula_offset=%.9g nebula_scheme=%d\n",
  s->v[13],s->v[14],s->v[15],s->v[16],s->v[17],s->v[18],(int)s->v[22]);
 s->started=now();
 fprintf(stderr,"[diag] blackhole seed=%.0f radius=%.3f temp=%.3f density=%.3f rotation=%.3f inclination=%.3f flyby=%d camera_rate=%.4f palette=%d approach=%.3f periapsis=%.3f jet=%.3f stars=%.3f palette_phase=%.4f palette_rate=%.5f palette_contrast=%.3f path_d_base=%.3f path_d_swing=%.3f path_d_harm_amp=%.3f path_d_harm_freq=%.3f path_o_rate=%.3f path_o_harm_amp=%.3f path_o_harm_freq=%.3f path_o_count=%.2f path_sign=%.0f path_e_swing=%.3f path_e_freq=%.3f path_phase_jitter=%.4f GL=%s\n",
  s->v[0],s->v[1],s->v[2],s->v[3],s->v[4],s->v[5],(int)s->v[9],s->v[6],(int)s->v[10],s->v[11],s->v[12],s->v[7],s->v[8],s->v[19],s->v[20],s->v[21],
  s->v[23],s->v[24],s->v[25],s->v[26],s->v[27],s->v[28],s->v[29],s->v[30],s->v[31],s->v[32],s->v[33],s->v[34],glGetString(GL_VERSION));
}
static void draw_blackhole(ModeInfo*m){
 State*s=m->data;
 if(!s||!s->program)return;
 int w=m->xgwa.width,h=m->xgwa.height;
 if(w<1)w=1;if(h<1)h=1;
 glViewport(0,0,w,h);
 glClear(GL_COLOR_BUFFER_BIT);
 glUseProgram(s->program);
 glUniform1f(s->time,now()-s->started);
 glUniform2f(s->resolution,w,h);
 glUniform1f(s->seed,s->v[0]);
 glUniform1f(s->radius,s->v[1]);
 glUniform1f(s->temperature,s->v[2]);
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
