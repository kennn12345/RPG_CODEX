// stars, nebulas and galaxies: irregular spherical scatter, wisps and gentle scintillation
float deep = smoothstep(0.23, 0.52, -h);
float starFade = saturate((-h - 0.05) / 0.25);
float3 nightSky = 0;
if (Night > 0.01 && V.z > -0.05)
{
    c *= lerp(1.0, 0.25, deep);
    // A less inflated domain leaves connected wisps rather than isolated round clouds.
    float3 warp = float3(w.fbm3(V * 1.7 + 11.0), w.fbm3(V * 1.7 + 23.0), w.fbm3(V * 1.7 + 37.0));
    float3 q = V * 3.1 + (warp - 0.5) * 1.25;
    float n1 = w.fbm3(q), n2 = w.fbm3(q * 2.3 + 5.0), lace = w.fbm3(q * 6.0 + 9.0);
    float fine = w.fbm3(V * 48.0 + q * 0.6 + 17.0);
    float dust = w.fbm3(V * 17.0 + q * 0.8 + 31.0);
    // Noise-shaped coverage avoids the circular spotlight borders of direction lobes.
    float a1 = smoothstep(0.36,0.68,warp.x + (n2-0.5)*0.3);
    float a2 = smoothstep(0.36,0.67,warp.y + (dust-0.5)*0.25);
    float a3 = smoothstep(0.45,0.73,warp.z);
    float a4 = smoothstep(0.48,0.76,1.0-warp.x) * smoothstep(0.42,0.68,warp.z);
    float body = smoothstep(0.43,0.68,n1);
    float filaments = smoothstep(0.43,0.56,lace) * (1.0 - smoothstep(0.58,0.72,lace)) * 0.28;
    float knots = pow(saturate(fine),4.0) * body;
    float3 neb = a1 * (float3(0.025,0.16,0.95) * body * 1.8 + float3(0.12,0.42,0.9) * filaments * 0.95 + float3(0.38,0.58,1.0) * knots * 2.3);
    neb += a2 * (float3(0.31,0.055,0.68) * body * 1.3 + float3(0.5,0.12,0.64) * filaments * 0.65 + float3(0.42,0.48,1.0) * knots * 1.8);
    neb += a3 * (float3(0.025,0.32,0.46) * body + float3(0.12,0.25,0.7) * filaments);
    neb += a4 * (float3(0.11,0.10,0.62) * body + float3(0.32,0.12,0.6) * filaments * 0.55);
    neb *= 1.0 - 0.84 * smoothstep(0.48,0.68,n2) * smoothstep(0.42,0.64,dust);

    // The Milky Way bends into a narrow, broken double ribbon with dark branching lanes.
    float3 mwN = normalize(float3(0.38,0.77,0.51));
    float dz = dot(V,mwN) + (n1 - 0.5) * 0.38 + (n2 - 0.5) * 0.16;
    float band = exp(-dz * dz / lerp(0.009,0.032,saturate(dust)));
    float core = exp(-dz * dz / lerp(0.0014,0.0045,saturate(lace)));
    float offsetLane = dz + (dust - 0.5) * 0.09;
    float darkLane = exp(-offsetLane * offsetLane / 0.00065) * smoothstep(0.40,0.62,dust);
    float cloudlets = smoothstep(0.40,0.72,lace);
    float3 mw = band * (float3(0.025,0.09,0.36) * (0.15 + 0.85 * cloudlets)
        + float3(0.13,0.25,0.66) * pow(saturate(fine),3.0) * 1.9);
    mw += core * (float3(0.16,0.24,0.6) * smoothstep(0.42,0.72,fine) * 1.2
        + float3(0.48,0.30,0.23) * smoothstep(0.54,0.73,n2) * 0.22);
    mw *= 1.0 - 0.91 * darkLane;
    mw *= 1.0 - 0.55 * smoothstep(0.55,0.72,n2) * smoothstep(0.48,0.66,dust);
    float3 glowLow = float3(0.04,0.24,0.26) * pow(saturate(1.0 - V.z),7.0);

    // Two distinct galaxies. The first is now visible beside the overhead moon view.
    float3 gal = 0;
    for (int g = 0; g < 2; g++)
    {
        float3 Gd = normalize(g == 0 ? float3(-0.62,-0.25,0.74) : float3(0.84,-0.27,0.46));
        if (dot(V,Gd) > 0.78)
        {
            float gs = g == 0 ? 0.23 : 0.155;
            float3 Gx = normalize(cross(Gd,float3(0.2,0.3,1.0))), Gy = cross(Gd,Gx);
            float2 p = float2(dot(V,Gx),dot(V,Gy)) / gs;
            float tilt = g == 0 ? 0.46 : 0.57, turn = g == 0 ? 0.4 : -0.65;
            p = float2(p.x * cos(turn) - p.y * sin(turn),p.x * sin(turn) + p.y * cos(turn));
            p.y /= 1.0 - tilt;
            float r = length(p), th = atan2(p.y,p.x);
            float mott = w.fbm3(float3(p * 19.0,float(g) * 7.0 + 2.0));
            float phase = (g == 0 ? 3.0 : 2.0) * th - 6.2 * log(r + 0.07) + (mott - 0.5) * 0.55;
            float arms = pow(saturate(cos(phase) * 0.5 + 0.5),3.2);
            float spiralDust = pow(saturate(cos(phase + 1.15) * 0.5 + 0.5),4.0);
            float disc2 = exp(-r * 2.8), inner = 1.0 - exp(-r * r * 100.0);
            float clumps = smoothstep(0.48,0.73,mott);
            float3 armsC = float3(0.13,0.28,0.9) * disc2 * arms * (0.35 + mott * 1.25) * inner * 3.8;
            armsC += float3(0.48,0.65,1.0) * disc2 * arms * clumps * inner * 2.0;
            float3 bulge = float3(0.65,0.47,0.36) * exp(-r * 6.0) * (0.7 + mott * 0.5);
            float3 centerG = float3(1.0,0.88,0.7) * exp(-r * r * 130.0) * 3.8;
            gal += (armsC + bulge + centerG) * (1.0 - 0.72 * spiralDust * smoothstep(0.08,0.35,r)) * (g == 0 ? 1.0 : 0.65);
        }
    }

    // Jittered 2D face samples are projected onto the sphere. Intersecting a 3D
    // lattice with the dome made visible latitude rings; there are no depth slices here.
    // Independent fixed rotations hide face alignment between layers. Nothing drifts.
    float3 stars = 0;
    for (int l = 0; l < 4; l++)
    {
        float angle = 0.47 + float(l)*1.137;
        float3 D = float3(V.x*cos(angle)-V.y*sin(angle), V.x*sin(angle)+V.y*cos(angle), V.z);
        float lean = 0.63 + float(l)*0.719;
        D = float3(D.x,D.y*cos(lean)-D.z*sin(lean),D.y*sin(lean)+D.z*cos(lean));
        float grid = l == 0 ? 32.0 : l == 1 ? 70.0 : l == 2 ? 145.0 : 300.0;
        float major = max(abs(D.x),max(abs(D.y),abs(D.z)));
        // Evaluate both sides near a face boundary so stars straddling it stay whole.
        for (int axis = 0; axis < 3; axis++)
        {
            float denom = axis == 0 ? abs(D.x) : axis == 1 ? abs(D.y) : abs(D.z);
            if (denom < major*0.975) continue;
            float2 uv = (axis == 0 ? D.yz : axis == 1 ? D.zx : D.xy) / max(denom,0.0001);
            float signD = axis == 0 ? D.x : axis == 1 ? D.y : D.z;
            float face = float(axis)*2.0 + (signD > 0.0 ? 1.0 : 0.0);
            float2 sv = uv*grid, cell = floor(sv), f = frac(sv);
            float3 id = float3(cell,face*19.7+float(l)*43.1+3.17);
            float seed = w.h3(id);
            // Correct cubemap area distortion, maintaining similar density at edges.
            float area = pow(1.0+dot(uv,uv),-1.5);
            float density = min(0.92,(l == 0 ? 0.14 : l == 1 ? 0.28 : l == 2 ? 0.48 : 0.62)
                * area * (1.0+1.6*band) * (0.8+0.5*smoothstep(0.35,0.65,n2)));
            if (seed >= density * 0.4) continue; // Half the accepted November 7 star population.
            if (l >= 2 && w.h3(id + 21.47) >= 0.5) continue; // Thin only the two smallest layers by half.
            float2 at = 0.18 + 0.64*float2(w.h3(id+1.3),w.h3(id+5.1));
            float2 centerUV = (cell+at)/grid;
            if (max(abs(centerUV.x),abs(centerUV.y)) > 1.0) continue;
            float d = length(f-at);
            float kind = w.h3(id+7.9), power = w.h3(id+9.1);
            float3 tint = kind < 0.6 ? float3(0.68,0.82,1.0) : kind < 0.8 ? float3(1.0,0.98,0.94) : kind < 0.94 ? float3(1.0,0.82,0.5) : float3(1.0,0.55,0.3);
            float size = l == 0 ? 0.075 : l == 1 ? 0.105 : l == 2 ? 0.145 : 0.17;
            // Angular footprint stays consistent across the six projections.
            d /= sqrt(1.0+dot(uv,uv));
            float point2 = smoothstep(size,0.0,d);
            float halo = l == 0 ? exp(-d*d*380.0)*0.075 : 0.0;
            float gain = (l == 0 ? 3.2 : l == 1 ? 1.3 : l == 2 ? 0.6 : 0.24)*(0.3+0.7*power*power);
            float phaseS = seed*400.0+kind*17.0;
            float amplitude = l < 2 ? 0.20+0.18*power : 0.10+0.12*power;
            float twinkle = 0.85*(1.0+amplitude*(0.68*sin(T*(0.85+1.6*kind)+phaseS)
                +0.32*sin(T*(2.1+1.7*power)+phaseS*1.618)));
            stars += tint*(point2*point2+halo)*gain*twinkle;
        }
    }
    // Keep most of the dome clear: faint wisps gather near the galactic ribbon.
    // Stars retain their brightness and scintillation above the restrained haze.
    nightSky = (neb * 0.00096 * (0.35 + 0.65 * band) + mw * 0.00256 + gal * 0.0028 + glowLow * 0.0048) * deep + stars * StarBright * starFade;
    nightSky *= Night * above;
}
