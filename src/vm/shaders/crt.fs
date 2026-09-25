#version 330

// The screen's looks for the 256 x 256 picture. `mode` picks one; the
// strengths, each 0 to 1, tune it, so every effect can be off alone.
// Presentation only: the bytes the GPU composed are the texture this
// reads, and nothing here writes back.
//
//   1 sharp smooth    pixels stay square; only the edges between them
//                     are blended, so a non-integer scale does not shimmer
//   2 classic         the soft CRT the machine always had
//   3 shadow mask     a round beam per row and a triad mask, in the
//                     manner of Timothy Lottes' shader
//   4 aperture grille vertical phosphor stripes and sharp scanlines, a
//                     Trinitron
//   5 slot mask       stripes broken into staggered slots, a TV
//   6 LCD             a grid between the pixels, a handheld
//   7 composite       colour that bleeds sideways, a TV over an aerial
//   8 smooth          Scale2x: diagonal steps are rounded off
//
// Mode 0, sharp pixels, never reaches the shader.

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;
uniform vec2 screenSize;   // the source texture size, 256 x 256
uniform vec2 outputSize;   // the drawn picture in framebuffer pixels
uniform int mode;
uniform float scanlines;   // 0 off, 1 dark gaps between rows
uniform float curvature;   // 0 flat, 1 a pronounced barrel
uniform float blur;        // 0 sharp, 1 a wide soft blur
uniform float bloom;       // 0 none, 1 bright pixels glow
uniform float vignette;    // 0 none, 1 dark corners
uniform float mask;        // 0 none, 1 the full phosphor pattern

out vec4 finalColor;

vec2 curve(vec2 uv) {
  uv = uv * 2.0 - 1.0;
  vec2 offset = abs(uv.yx) / vec2(6.0, 4.0);
  uv = uv + uv * offset * offset * curvature;
  return uv * 0.5 + 0.5;
}

vec3 sampleBlurred(vec2 uv, float radius) {
  vec2 px = 1.0 / screenSize;
  vec3 sum = texture(texture0, uv).rgb * 4.0;
  sum += texture(texture0, uv + vec2(px.x, 0.0) * radius).rgb;
  sum += texture(texture0, uv - vec2(px.x, 0.0) * radius).rgb;
  sum += texture(texture0, uv + vec2(0.0, px.y) * radius).rgb;
  sum += texture(texture0, uv - vec2(0.0, px.y) * radius).rgb;
  return sum / 8.0;
}

// Sharp bilinear: inside a pixel the colour is flat, and only a band as
// wide as one output pixel at the border is blended. Needs linear filtering.
vec3 sharpSmooth(vec2 uv) {
  vec2 scale = max(outputSize / screenSize, vec2(1.0));
  vec2 texel = uv * screenSize;
  vec2 centre = fract(texel) - 0.5;
  vec2 range = 0.5 - 0.5 / scale;
  vec2 f = (centre - clamp(centre, -range, range)) * scale + 0.5;
  return texture(texture0, (floor(texel) + f) / screenSize).rgb;
}

// One row's colour: sharp bilinear along the row, softened toward plain
// bilinear by the blur. Needs linear filtering.
vec3 rowColour(vec2 uv, float row) {
  float scale = max(outputSize.x / screenSize.x, 1.0);
  float x = uv.x * screenSize.x;
  float c = fract(x) - 0.5;
  float range = 0.5 - 0.5 / scale;
  float f = (c - clamp(c, -range, range)) * scale + 0.5;
  float y = (row + 0.5) / screenSize.y;
  vec3 sharp = texture(texture0, vec2((floor(x) + f) / screenSize.x, y)).rgb;
  vec3 soft = texture(texture0, vec2(uv.x, y)).rgb;
  return mix(sharp, soft, blur);
}

// The beam across rows: each row lights a Gaussian band, wider where it is
// brighter, and the two nearest rows add up.
vec3 beam(vec2 uv) {
  float y = uv.y * screenSize.y - 0.5;
  float row = floor(y);
  float d = y - row;
  vec3 a = rowColour(uv, row);
  vec3 b = rowColour(uv, min(row + 1.0, screenSize.y - 1.0));
  float sharp = mix(0.0, 7.0, scanlines);
  vec3 wa = exp(-sharp * d * d / (0.35 + 0.65 * a));
  vec3 wb = exp(-sharp * (1.0 - d) * (1.0 - d) / (0.35 + 0.65 * b));
  return a * wa + b * wb;
}

// The framebuffer pixel the fragment is. The masks repeat every few
// pixels, so where they start does not matter.
vec2 outPixel() {
  return floor(gl_FragCoord.xy);
}

vec3 shadowMask() {
  vec2 p = outPixel();
  float i = mod(p.x + mod(p.y, 2.0) * 1.5, 3.0);
  vec3 m = vec3(i < 1.0 ? 1.0 : 0.0, i >= 1.0 && i < 2.0 ? 1.0 : 0.0, i >= 2.0 ? 1.0 : 0.0);
  return mix(vec3(1.0), m * 2.2 + 0.25, mask);
}

vec3 apertureGrille() {
  float i = mod(outPixel().x, 3.0);
  vec3 m = vec3(i < 1.0 ? 1.0 : 0.0, i >= 1.0 && i < 2.0 ? 1.0 : 0.0, i >= 2.0 ? 1.0 : 0.0);
  return mix(vec3(1.0), m * 2.4 + 0.2, mask);
}

vec3 slotMask() {
  vec2 p = outPixel();
  float col = floor(p.x / 3.0);
  float i = mod(p.x, 3.0);
  vec3 m = vec3(i < 1.0 ? 1.0 : 0.0, i >= 1.0 && i < 2.0 ? 1.0 : 0.0, i >= 2.0 ? 1.0 : 0.0);
  // Every slot is four pixels tall with a dark gap, staggered by column.
  float gap = mod(p.y + mod(col, 2.0) * 2.0, 4.0) < 1.0 ? 0.35 : 1.0;
  return mix(vec3(1.0), (m * 2.4 + 0.2) * gap, mask);
}

// Scale2x on the source grid: each pixel splits into four, and a corner
// takes a neighbour's colour when the two sides that meet there agree.
vec3 scale2x(vec2 uv) {
  vec2 texel = uv * screenSize;
  vec2 base = floor(texel);
  vec2 q = step(0.5, fract(texel));
  vec2 px = 1.0 / screenSize;
  vec2 c = (base + 0.5) * px;
  vec3 P = texture(texture0, c).rgb;
  vec3 A = texture(texture0, c - vec2(0.0, px.y)).rgb;
  vec3 B = texture(texture0, c + vec2(px.x, 0.0)).rgb;
  vec3 C = texture(texture0, c - vec2(px.x, 0.0)).rgb;
  vec3 D = texture(texture0, c + vec2(0.0, px.y)).rgb;
  vec3 v = q.y < 0.5 ? A : D;  // the neighbour above or below
  vec3 h = q.x < 0.5 ? C : B;  // the neighbour left or right
  vec3 vo = q.y < 0.5 ? D : A;
  vec3 ho = q.x < 0.5 ? B : C;
  if (h == v && h != vo && v != ho) return h;
  return P;
}

vec3 vignetted(vec3 color, vec2 uv) {
  vec2 v = uv * (1.0 - uv);
  float vig = pow(v.x * v.y * 16.0, 0.25);
  return color * mix(1.0, vig, vignette);
}

void main() {
  vec2 uv = curve(fragTexCoord);
  if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
    finalColor = vec4(0.0, 0.0, 0.0, 1.0);
    return;
  }

  vec3 color;
  if (mode == 1) {
    color = sharpSmooth(uv);
  } else if (mode == 3 || mode == 4 || mode == 5) {
    color = beam(uv);
    color *= mode == 3 ? shadowMask() : mode == 4 ? apertureGrille() : slotMask();
    color += sampleBlurred(uv, 2.0) * sampleBlurred(uv, 2.0) * bloom * 0.5;
  } else if (mode == 6) {
    vec2 f = fract(uv * screenSize);
    vec2 scale = outputSize / screenSize;
    // The gap is one output pixel wide at any size, shaded by the strength.
    vec2 edge = step(f, vec2(1.0) - 1.0 / max(scale, vec2(1.0)));
    color = texture(texture0, uv).rgb;
    color *= mix(1.0, edge.x * edge.y, mask * 0.6);
    color = mix(color, sampleBlurred(uv, 1.0), blur * 0.5);
  } else if (mode == 7) {
    // Chroma spreads wide along the row, brightness stays sharper.
    vec2 px = vec2(1.0 / screenSize.x, 0.0);
    float spread = 1.0 + blur * 3.0;
    vec3 wide = (texture(texture0, uv - px * spread).rgb + texture(texture0, uv).rgb * 2.0 +
                 texture(texture0, uv + px * spread).rgb) / 4.0;
    vec3 near = texture(texture0, uv).rgb;
    const vec3 luma = vec3(0.299, 0.587, 0.114);
    color = wide + (dot(near, luma) - dot(wide, luma));
    float line = 0.5 + 0.5 * cos(uv.y * screenSize.y * 6.28318);
    color *= 1.0 - scanlines * 0.35 * line;
    color += sampleBlurred(uv, 2.0) * sampleBlurred(uv, 2.0) * bloom * 0.4;
  } else if (mode == 8) {
    color = scale2x(uv);
  } else {
    vec3 sharp = texture(texture0, uv).rgb;
    vec3 soft = sampleBlurred(uv, 0.5 + blur * 1.5);
    color = mix(sharp, soft, blur);
    // Bloom: the blurred image, added where it is bright.
    vec3 glow = sampleBlurred(uv, 2.0);
    color += glow * glow * bloom * 0.6;
    // Scanlines follow the source rows, so they stay put at any window size.
    float row = uv.y * screenSize.y;
    float line = 0.5 + 0.5 * cos(row * 6.28318);
    color *= 1.0 - scanlines * 0.45 * line;
  }

  finalColor = vec4(vignetted(color, uv), 1.0) * fragColor;
}
