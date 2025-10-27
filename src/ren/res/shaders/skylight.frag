
#define PI 3.141592
#define iSteps 16
#define jSteps 8

vec2 rsi(vec3 r0, vec3 rd, float sr) {
  // ray-sphere intersection that assumes
  // the sphere is centered at the origin.
  // No intersection when result.x > result.y
  float a = dot(rd, rd);
  float b = 2.0 * dot(rd, r0);
  float c = dot(r0, r0) - (sr * sr);
  float d = (b * b) - 4.0 * a * c;
  if (d < 0.0) return vec2(1e5, -1e5);
  return vec2((-b - sqrt(d)) / (2.0 * a), (-b + sqrt(d)) / (2.0 * a));
}

vec3 atmosphere(vec3 r, vec3 r0, vec3 pSun, float iSun, float rPlanet, float rAtmos, vec3 kRlh,
                float kMie, float shRlh, float shMie, float g) {
  // Normalize the sun and view directions.
  pSun = normalize(pSun);
  r = normalize(r);

  // Calculate the step size of the primary ray.
  vec2 p = rsi(r0, r, rAtmos);
  if (p.x > p.y) return vec3(0, 0, 0);
  p.y = min(p.y, rsi(r0, r, rPlanet).x);
  float iStepSize = (p.y - p.x) / float(iSteps);

  // Initialize the primary ray time.
  float iTime = 0.0;

  // Initialize accumulators for Rayleigh and Mie scattering.
  vec3 totalRlh = vec3(0, 0, 0);
  vec3 totalMie = vec3(0, 0, 0);

  // Initialize optical depth accumulators for the primary ray.
  float iOdRlh = 0.0;
  float iOdMie = 0.0;

  // Calculate the Rayleigh and Mie phases.
  float mu = dot(r, pSun);
  float mumu = mu * mu;
  float gg = g * g;
  float pRlh = 3.0 / (16.0 * PI) * (1.0 + mumu);
  float pMie = 3.0 / (8.0 * PI) * ((1.0 - gg) * (mumu + 1.0)) /
               (pow(1.0 + gg - 2.0 * mu * g, 1.5) * (2.0 + gg));

  // Sample the primary ray.
  for (int i = 0; i < iSteps; i++) {
    // Calculate the primary ray sample position.
    vec3 iPos = r0 + r * (iTime + iStepSize * 0.5);

    // Calculate the height of the sample.
    float iHeight = length(iPos) - rPlanet;

    // Calculate the optical depth of the Rayleigh and Mie scattering for this step.
    float odStepRlh = exp(-iHeight / shRlh) * iStepSize;
    float odStepMie = exp(-iHeight / shMie) * iStepSize;

    // Accumulate optical depth.
    iOdRlh += odStepRlh;
    iOdMie += odStepMie;

    // Calculate the step size of the secondary ray.
    float jStepSize = rsi(iPos, pSun, rAtmos).y / float(jSteps);

    // Initialize the secondary ray time.
    float jTime = 0.0;

    // Initialize optical depth accumulators for the secondary ray.
    float jOdRlh = 0.0;
    float jOdMie = 0.0;

    // Sample the secondary ray.
    for (int j = 0; j < jSteps; j++) {
      // Calculate the secondary ray sample position.
      vec3 jPos = iPos + pSun * (jTime + jStepSize * 0.5);

      // Calculate the height of the sample.
      float jHeight = length(jPos) - rPlanet;

      // Accumulate the optical depth.
      jOdRlh += exp(-jHeight / shRlh) * jStepSize;
      jOdMie += exp(-jHeight / shMie) * jStepSize;

      // Increment the secondary ray time.
      jTime += jStepSize;
    }

    // Calculate attenuation.
    vec3 attn = exp(-(kMie * (iOdMie + jOdMie) + kRlh * (iOdRlh + jOdRlh)));

    // Accumulate scattering.
    totalRlh += odStepRlh * attn;
    totalMie += odStepMie * attn;

    // Increment the primary ray time.
    iTime += iStepSize;
  }

  // Calculate and return the final color.
  return iSun * (pRlh * kRlh * totalRlh + pMie * kMie * totalMie);
}




// Simple hash function for pseudorandom star generation
float hash(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }

// Generate a star at the given direction
// Returns the brightness of the star at this position
float generateStar(vec3 rayDir, vec2 starSeed) {
  // Create a grid of potential stars
  vec3 normalized = normalize(rayDir);

  // Map to a grid for star placement
  vec2 starGrid = floor(normalized.xz * starSeed);
  vec2 gridFrac = fract(normalized.xz * starSeed);

  // Hash to decide if this grid cell has a star
  float hasStarHash = hash(starGrid);

  // Only ~20% of grid cells have stars
  if (hasStarHash > 0.2) return 0.0;

  // Determine star position within the cell
  float hashX = hash(starGrid + vec2(1.0, 0.0));
  float hashY = hash(starGrid + vec2(0.0, 1.0));
  vec2 starPos = vec2(hashX, hashY) * 2.0 - 1.0;
  starPos *= 0.3;  // Stars are somewhat clustered in cells

  // Distance from current position to star
  float dist = length(gridFrac - (starPos * 0.5 + 0.5));

  // Star size (small circles)
  float starSize = 0.02 + hash(starGrid + vec2(0.0, 1.0)) * 0.03;

  // Soft circle with falloff
  float star = smoothstep(starSize, 0.0, dist);

  // Vary star brightness
  float brightness = 0.5 + hash(starGrid + vec2(1.0, 1.0)) * 0.5;

  return star * brightness;
}

// Calculate star visibility based on sky conditions
float getStarVisibility(vec3 rayDir, vec3 sunDir, vec3 skyColor) {
  // Only show stars in the top hemisphere
  if (rayDir.y <= 0.0) return 0.0;

  // Calculate sky brightness (luminance)
  float skyBrightness = dot(skyColor, vec3(0.299, 0.587, 0.114));

  // Inverse: stars visible when sky is very dark only
  // Much narrower range: only show stars when sky is very dark
  float starVis = smoothstep(0.15, 0.02, skyBrightness);

  // Apply nonlinear (cubic) fadeout for aggressive cutoff
  starVis = pow(starVis, 3.0);

  // Sun angle factor: stars fade in when sun goes below horizon
  // When sun.y is positive (above horizon), stars are suppressed
  float sunFactor = smoothstep(0.2, -0.5, sunDir.y);

  // Combine brightness and sun angle factors
  starVis *= sunFactor;

  return clamp(starVis, 0.0, 1.0);
}

// Render stars for a given ray direction
vec3 renderStars(vec3 rayDir, vec3 sunDir, vec3 skyColor) {
  float starVis = getStarVisibility(rayDir, sunDir, skyColor);

  if (starVis < 0.01) return vec3(0.0);  // Early exit if stars not visible

  // Generate multiple stars with different grid scales for variety
  float star1 = generateStar(rayDir, vec2(50.0, 50.0));
  float star2 = generateStar(rayDir, vec2(37.0, 42.0));
  float star3 = generateStar(rayDir, vec2(61.0, 55.0));

  // Combine stars
  float totalStar = max(star1, max(star2, star3));

  // Make stars white/slightly bluish
  vec3 starColor = vec3(0.95, 0.97, 1.0) * totalStar * starVis;

  return starColor;
}


vec3 computeSkyColorComplex(vec3 rayDir, vec3 sunDir) {
  vec3 color = atmosphere(normalize(rayDir),               // normalized ray direction
                          vec3(0, 6372e3, 0),              // ray origin
                          sunDir,                          // position of the sun
                          22.0,                            // intensity of the sun
                          6371e3,                          // radius of the planet in meters
                          6471e3,                          // radius of the atmosphere in meters
                          vec3(5.5e-6, 13.0e-6, 22.4e-6),  // Rayleigh scattering coefficient
                          21e-6,                           // Mie scattering coefficient
                          8e3,                             // Rayleigh scale height
                          1.2e3,                           // Mie scale height
                          0.758                            // Mie preferred scattering direction
  );



  // Render and blend stars based on sky brightness
  vec3 stars = renderStars(rayDir, sunDir, color);
  color = mix(color, color + stars, 1.0);  // Add stars additively

  return color;
}

vec3 computeSkyColorSimple(vec3 rayDir, vec3 sunDir) {
  float height = rayDir.y;

  if (height >= 0.0) {
    // Minecraft sky: very uniform light blue, barely any gradient
    vec3 skyColor = vec3(0.52, 0.73, 1.0);  // #85B8FF - Minecraft day sky

    // Very subtle darkening toward horizon
    float heightFactor = smoothstep(0.0, 0.3, height);
    skyColor = mix(vec3(0.68, 0.83, 1.0), skyColor, heightFactor);

    // Minecraft sun: bright white circle, hard edge
    float sundot = dot(rayDir, sunDir);

    // Sharp sun disc
    if (sundot > 0.9998) {  // ~1 degree radius
      return vec3(1.0);     // Pure white sun
    }

    // Subtle sun glow (much subtler than realistic)
    float sunGlow = pow(clamp(sundot, 0.0, 1.0), 512.0);
    skyColor += vec3(0.8, 0.8, 0.6) * sunGlow * 0.3;

    return skyColor;
  } else {
    // Minecraft void: dark, desaturated fog color
    float groundDepth = -height;

    // Start with fog color at horizon
    vec3 horizonFog = vec3(0.68, 0.83, 1.0);  // Light blue fog
    vec3 voidColor = vec3(0.17, 0.17, 0.17);  // Dark gray void

    // Sharp transition - Minecraft doesn't blend much
    return mix(horizonFog, voidColor, pow(groundDepth, 1));
  }
}


vec3 computeSkyColor(vec3 rayDir, vec3 sunDir) {
  // return computeSkyColorSimple(rayDir, sunDir);
  return computeSkyColorComplex(rayDir, sunDir);
}