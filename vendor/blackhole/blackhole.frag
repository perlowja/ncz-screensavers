#version 300 es
precision highp float;
precision highp int;
out vec4 fragColor;
uniform float u_time, u_seed, u_radius, u_temperature, u_density, u_rotation;
uniform float u_inclination, u_orbit_rate, u_jet, u_star_density;
uniform float u_camera_mode, u_palette, u_approach, u_periapsis;
uniform vec2 u_resolution;
#define PI 3.14159265358979323846
float hash21(vec2 p){p=fract(p*vec2(123.34,345.45));p+=dot(p,p+34.345+u_seed*.00001);return fract(p.x*p.y);}
float noise(vec2 p){vec2 i=floor(p),f=fract(p);f=f*f*(3.-2.*f);return mix(mix(hash21(i),hash21(i+vec2(1,0)),f.x),mix(hash21(i+vec2(0,1)),hash21(i+1.),f.x),f.y);}
float fbm(vec2 p){float v=0.,a=.5;for(int i=0;i<4;i++){v+=a*noise(p);p=p*2.03+vec2(17.13,-11.7);a*=.5;}return v;}
vec3 blackbody(float t){vec3 c=vec3(1,.18,.025),w=vec3(1,.62,.16),h=vec3(.72,.86,1);return t<.55?mix(c,w,t/.55):mix(w,h,(t-.55)/.45);}
vec3 palette(float t){
 if(u_palette<.5)return blackbody(t);                                      // solar gold
 if(u_palette<1.5)return mix(vec3(.025,.12,.8),vec3(.72,1.,1.),pow(t,.7)); // blue-hot
 if(u_palette<2.5)return mix(vec3(.32,.006,.002),vec3(1.,.56,.08),pow(t,1.25)); // ember
 if(u_palette<3.5)return mix(vec3(.12,.008,.38),vec3(1.,.35,.92),pow(t,.8)); // ultraviolet
 return mix(vec3(.005,.18,.11),vec3(.45,1.,.78),pow(t,.65));              // exotic mint
}
vec3 stars(vec3 d){vec2 uv=vec2(atan(d.z,d.x)/6.2831853+.5,asin(clamp(d.y,-1.,1.))/PI+.5),cell=floor(uv*vec2(720,360));float n=hash21(cell),s=smoothstep(1.-.0022*u_star_density,1.,n);vec3 c=mix(vec3(.55,.7,1),vec3(1,.72,.45),hash21(cell+7.));float band=pow(max(0.,1.-abs(d.y+.15*sin(atan(d.z,d.x)*2.))/.18),3.);return c*s*s*(.65+.35*sin(u_seed+n*40.))*1.8+vec3(.025,.032,.055)*band*(.3+fbm(uv*18.));}
vec3 disk_color(vec3 p, float drama){float r=length(p.xy),a=atan(p.y,p.x),ph=a-u_rotation*u_time*.3/pow(max(r,1.1),1.5);float n=fbm(vec2(r*2.7+cos(ph)*5.,sin(ph)*5.+u_time*.08+u_seed*.01));n=mix(n,fbm(vec2(r*9.+cos(ph)*13.,sin(ph)*13.-u_time*.17)),.42);float edge=smoothstep(3.,3.7,r)*(1.-smoothstep(10.5,12.5,r));float heat=clamp(pow(3./max(r,3.),.75)*u_temperature,0.,1.),dop=clamp(1.+.55/sqrt(max(r,1.5))*sin(a)*1.5,.35,1.8),grav=sqrt(max(1.-1./max(r,1.001),.02));// A real disk-space hot sector also appears in the lensed disk images.
float sector=max(cos(a-(.22*u_time+.00001*u_seed)),0.);sector*=sector;sector*=sector;
float boost=1.+.65*drama*sector*(1.-smoothstep(4.,8.,r));
return palette(clamp(heat*dop/grav,0.,1.))*edge*(.32+1.2*n)*u_density*dop*dop*boost;}
// Zero velocity and acceleration at each envelope endpoint.
float ease5(float a,float b,float x){float t=clamp((x-a)/(b-a),0.,1.);return t*t*t*(t*(t*6.-15.)+10.);}
void main(){
 vec2 p=(2.*gl_FragCoord.xy-u_resolution)/u_resolution.y/u_radius;
 float phase=u_time*u_orbit_rate+u_seed*.000001,orbit,elev,dist,roll=0.,closeFX=0.,arrivalFX=0.;
 if(u_camera_mode<.5){
  // Diving arc: approach from above, skim the disk, then climb away.
  dist=10.5+(3.7+.4*abs(u_approach))*cos(phase);orbit=u_approach*(phase*1.18+.28*sin(phase*2.));elev=u_inclination+.58*sin(phase*.83);
 }else if(u_camera_mode<1.5){
  // Banking slingshot: asymmetric radius and a faster sweep at periapsis.
  dist=9.8+(3.3+.4*abs(u_approach))*sin(phase)+1.15*sin(phase*2.);orbit=u_approach*(phase*1.35-.34*cos(phase));elev=u_inclination+.48*cos(phase*1.17);
 }else if(u_camera_mode<2.5){
  // Polar pass: crosses from one face of the disk to the other.
  dist=10.8+(4.+.4*abs(u_approach))*cos(phase*.91);orbit=u_approach*(phase+.55*sin(phase*.72));elev=u_inclination+.78*sin(phase*.69);
 }else{
  // Fast arrival, held banked sweep, slower release. Only the envelopes
  // wrap: accumulated azimuth stays continuous and never backtracks.
  float tau=u_time*u_orbit_rate,cycles=tau/(2.*PI),lap=floor(cycles),q=fract(cycles);
  float arrival=ease5(.08,.40,q),departure=ease5(.60,.97,q);
  closeFX=arrival*(1.-departure);
  arrivalFX=4.*arrival*(1.-arrival);
  dist=mix(18.,u_periapsis,closeFX);
  elev=.635-.57*closeFX+.025*closeFX*sin(2.*PI*q);
  orbit=u_seed*.000001+u_approach*(.35*tau+2.7*(lap+ease5(.27,.73,q)));
  float bank=ease5(.16,.40,q)*(1.-ease5(.65,.96,q));
  roll=(u_approach<0.?-1.:1.)*2.25*bank;
 }
 dist=max(dist,5.4);
 vec3 cam=dist*vec3(cos(orbit)*cos(elev),sin(orbit)*cos(elev),sin(elev)),forward=normalize(-cam),baseRight=normalize(cross(forward,vec3(0,0,1))),baseUp=cross(baseRight,forward),right=cos(roll)*baseRight+sin(roll)*baseUp,up=-sin(roll)*baseRight+cos(roll)*baseUp;
 // One ray, with subtle arrival-only peripheral distortion and framing drift.
 float r2=dot(p,p),shot=fract(u_time*u_orbit_rate/(2.*PI));
 vec2 lensP=p*(1.+.035*arrivalFX*r2/(1.+r2));
 lensP+=closeFX*vec2((u_approach<0.?-1.:1.)*.12*sin(2.*PI*shot),.06);
 vec3 ray=normalize(forward+lensP.x*right+lensP.y*up);
 float impact=length(cross(cam,ray));bool captured=impact<2.598076;
 float u=1./length(cam),phi=0.;vec3 normal=normalize(cam),perp=cross(cross(normal,ray),normal);float plen=length(perp);vec3 tangent=plen>1e-6?perp/plen:right;float tang=dot(ray,tangent),du=abs(tang)>1e-6?-dot(ray,normal)/tang*u:200.*u;vec3 old=cam,pos=cam,color=vec3(0);float trans=1.;
 for(int i=0;i<260;i++){float step=.04*(1.-.62*exp(-12.*(u-.667)*(u-.667)));du+=.5*(-u+1.5*u*u)*step;u+=du*step;du+=.5*(-u+1.5*u*u)*step;phi+=step;if(u>=1.||u<=.0005)break;old=pos;pos=(cos(phi)*normal+sin(phi)*tangent)/u;if(old.z*pos.z<0.){vec3 x=mix(old,pos,-old.z/(pos.z-old.z));float r=length(x.xy);if(r>2.8&&r<13.){color+=trans*disk_color(x,closeFX);trans*=.72;}}}
 if(!captured){vec3 d=length(pos-old)>1e-5?normalize(pos-old):ray;color+=trans*(stars(d)+vec3(.002,.003,.007));}
 if(u_jet>0.){float axis=abs(dot(ray,vec3(0,0,1)));color+=u_jet*smoothstep(.975,.997,axis)*vec3(.18,.42,1)*(.45+.55*noise(p*45.+u_time*.2));}
 color=color/(1.+color);fragColor=vec4(pow(max(color,0.),vec3(1./2.2)),1);
}
