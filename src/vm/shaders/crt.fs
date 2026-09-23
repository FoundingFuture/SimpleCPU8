#version 330

// The CRT look for the 256x256 screen: scanlines, a little curvature, a
// soft blur and bloom, a vignette. Every effect has its own strength, 0 to
// 1, so each can be off alone. Presentation only: the bytes the GPU
// composed are the texture this reads and nothing here writes back.

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;
uniform vec2 screenSize;   // the source texture size, 256 x 256
uniform float scanlines;   // 0 off, 1 full dark gaps between rows
uniform float curvature;   // 0 flat, 1 a pronounced barrel
uniform float blur;        // 0 sharp, 1 a wide soft blur
uniform float bloom;       // 0 none, 1 bright pixels glow
uniform float vignette;    // 0 none, 1 dark corners

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

void main() {
  vec2 uv = curve(fragTexCoord);
  if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
    finalColor = vec4(0.0, 0.0, 0.0, 1.0);
    return;
  }

  vec3 sharp = texture(texture0, uv).rgb;
  vec3 soft = sampleBlurred(uv, 0.5 + blur * 1.5);
  vec3 color = mix(sharp, soft, blur);

  // Bloom: the blurred image, added where it is bright.
  vec3 glow = sampleBlurred(uv, 2.0);
  color += glow * glow * bloom * 0.6;

  // Scanlines follow the source rows, so they stay put at any window size.
  float row = uv.y * screenSize.y;
  float line = 0.5 + 0.5 * cos(row * 6.28318);
  color *= 1.0 - scanlines * 0.45 * line;

  // Vignette.
  vec2 v = uv * (1.0 - uv);
  float vig = pow(v.x * v.y * 16.0, 0.25);
  color *= mix(1.0, vig, vignette);

  finalColor = vec4(color, 1.0) * fragColor;
}
