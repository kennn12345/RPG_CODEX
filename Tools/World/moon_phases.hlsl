// moon: calendar-driven illumination of the same softly textured surface
float cm = dot(V, Lm);
float md = smoothstep(cos(MoonSize), cos(MoonSize * 0.97), cm);
float3 tx = normalize(cross(Lm,abs(Lm.z)>.99?float3(1,0,0):float3(0,0,1))), ty = cross(tx,Lm);
float2 uv = float2(dot(V,tx),dot(V,ty))/sin(MoonSize);
float limb = sqrt(saturate(1.0-dot(uv,uv)));
float maria = smoothstep(.42,.62,w.fbm3(float3(uv*2.2,3.7)));
float craters = w.fbm3(float3(uv*9.0,1.3));
float mtex = (.95-.35*maria)*(.85+.3*craters)*(.55+.45*limb);
float phaseAngle = Phase*6.283185307;
float3 moonNormal = float3(uv,limb);
// Project sunlight into the moon's local frame so the bright edge faces the sun.
float3 phaseLight = float3(dot(Ls,tx),dot(Ls,ty),-dot(Ls,Lm));
// Relief breaks the perfectly ruled quarter line; grazing light falls off gradually.
float relief = (craters-.5)*.035+(maria-.5)*.012;
float grazing = dot(moonNormal,phaseLight)+relief;
float terminator = smoothstep(-.06,.10,grazing);
terminator *= lerp(.65,1.0,smoothstep(0.0,.35,grazing));
float illumination = .5+.5*cos(phaseAngle);
float clarity = 1-.38*Haze;
float3 moonTint = lerp(float3(.98,.95,.88),float3(1.0,.79,.53),MoonWarm);
float moonVisibility = lerp(.06,1.0,Night)*smoothstep(-.03,.035,Lm.z);
float3 moonC = (md*mtex*moonTint*MoonBright*.45*terminator*clarity
    +pow(saturate(cm),lerp(250.0,110.0,Haze))*(1-md)*float3(.55,.65,.95)*MoonGlow*.35*pow(illumination,3)*(1+Haze*.5))*moonVisibility;

// Moving irregular cloud cover, driven by the same weather state as world lighting.
float3 cloudDomain = V*5.0+float3(T*(.003+Wind*.012),T*.0015,13.7);
float cloudNoise = w.fbm3(cloudDomain)+.12*w.fbm3(cloudDomain*3.1+7.0);
float cloudMask = smoothstep(1.0-Clouds*.96,1.20-Clouds*.81,cloudNoise)*smoothstep(-.02,.10,V.z)*saturate(Clouds*5);
cloudMask *= 1-saturate(VolumeClouds); // The volumetric component supplies depth, self-shadowing and sunlight.
float cloudTransmission = exp(-cloudMask*(2.0+Clouds*4.0));
float hazeTransmission = exp(-Haze*(.35+.7*pow(1-saturate(V.z),2)));
float3 cloudColor = lerp(float3(.13,.16,.20)*SkyBright,float3(.0005,.0007,.0011),Night);
cloudColor += Flash*float3(.14,.18,.26);
// The complete moon occludes distant stars, including its unlit hemisphere.
// Darken the night background rather than showing stars through the shadow.
float skyDarkening=lerp(1.0,.06,smoothstep(.1,.9,Night));
// Foreground atmospheric haze remains in front of the shadowed surface; stars do not.
float3 clearSky = c*skyDarkening*(1.0-disc*above*smoothstep(-.05,.02,h)*(1.0-smoothstep(.12,.45,h)))*(1.0-md*terminator*moonVisibility*(.2+.8*Night))
    +nightSky*(1.0-md)+(sunC+moonC)*above;
float3 weatherSky = clearSky*cloudTransmission*hazeTransmission+cloudColor*cloudMask;
weatherSky += Flash*float3(.012,.017,.025)*above;
return weatherSky;
