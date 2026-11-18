// re2DJ built-in post-processing shader: crt.
// Written for rePIU (task 768) and brought to re2DJ (task 455) by the same
// author; BSD 3-Clause License, see LICENSE.
//
// An arcade-monitor look in one pass: barrel curvature, Gaussian scanline
// beams that widen with brightness, an aperture-grille mask, and a soft
// vignette. Work is done in linear light. LogicalSize carries the game's
// logical resolution, so each beam is one original scanline.

#pragma parameter CRT_CURVATURE "Curvature" 0.01 0.0 0.30 0.01
#pragma parameter CRT_BEAM_MIN "Beam width (dark)" 0.22 0.05 0.60 0.01
#pragma parameter CRT_BEAM_MAX "Beam width (bright)" 0.38 0.05 0.80 0.01
#pragma parameter CRT_MASK_STRENGTH "Mask strength" 0.30 0.0 1.0 0.05
#pragma parameter CRT_BRIGHTNESS "Brightness" 1.35 0.5 2.5 0.05
#pragma parameter CRT_VIGNETTE "Vignette" 0.15 0.0 1.0 0.05

#if defined(VERTEX)

attribute vec4 VertexCoord;
attribute vec4 TexCoord;
uniform mat4 MVPMatrix;
varying vec2 tex_coord;

void main()
{
    gl_Position = MVPMatrix * VertexCoord;
    tex_coord = TexCoord.xy;
}

#elif defined(FRAGMENT)

#ifdef GL_ES
precision mediump float;
#endif

uniform sampler2D Texture;
uniform vec2 TextureSize;
uniform vec2 LogicalSize;
uniform vec2 OutputSize;
varying vec2 tex_coord;

#ifdef PARAMETER_UNIFORM
uniform float CRT_CURVATURE;
uniform float CRT_BEAM_MIN;
uniform float CRT_BEAM_MAX;
uniform float CRT_MASK_STRENGTH;
uniform float CRT_BRIGHTNESS;
uniform float CRT_VIGNETTE;
#else
#define CRT_CURVATURE 0.01
#define CRT_BEAM_MIN 0.22
#define CRT_BEAM_MAX 0.38
#define CRT_MASK_STRENGTH 0.30
#define CRT_BRIGHTNESS 1.35
#define CRT_VIGNETTE 0.15
#endif

const float kInputGamma = 2.4;
const float kOutputGamma = 2.2;

vec2 Warp(vec2 position)
{
    vec2 centred = position * 2.0 - 1.0;
    centred *= vec2(1.0 + centred.y * centred.y * CRT_CURVATURE,
                    1.0 + centred.x * centred.x * CRT_CURVATURE);
    return centred * 0.5 + 0.5;
}

vec3 SampleLinear(vec2 position)
{
    return pow(texture2D(Texture, position).rgb, vec3(kInputGamma));
}

// Weight of a beam `offset` rows from its centre; brighter beams spread.
vec3 Beam(float offset, vec3 color)
{
    vec3 width = mix(vec3(CRT_BEAM_MIN), vec3(CRT_BEAM_MAX), color);
    vec3 scaled = vec3(offset) / width;
    return exp(-0.5 * scaled * scaled);
}

vec3 ApertureMask(float x)
{
    float phase = fract(x / 3.0);
    vec3 mask = vec3(1.0 - CRT_MASK_STRENGTH);
    if (phase < 0.3333)
    {
        mask.r = 1.0;
    }
    else if (phase < 0.6667)
    {
        mask.g = 1.0;
    }
    else
    {
        mask.b = 1.0;
    }
    return mask;
}

void main()
{
    vec2 position = Warp(tex_coord);
    vec2 inside = step(vec2(0.0), position) * step(position, vec2(1.0));
    if (inside.x * inside.y == 0.0)
    {
        gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    // The two logical rows around this point, sampled at their centres. The
    // half-output-pixel shift puts a beam centre on a pixel centre at integer
    // scales; without it the two rows of a line at 2x sit symmetrically about
    // the beam and read equally bright, so no scanline shows.
    float row = position.y * LogicalSize.y - 0.5 +
        0.5 * LogicalSize.y / OutputSize.y;
    float base = floor(row);
    float fraction = row - base;
    vec3 upper = SampleLinear(vec2(position.x, (base + 0.5) / LogicalSize.y));
    vec3 lower = SampleLinear(vec2(position.x, (base + 1.5) / LogicalSize.y));
    vec3 color = upper * Beam(fraction, upper) + lower * Beam(1.0 - fraction, lower);

    color *= ApertureMask(gl_FragCoord.x);
    color *= CRT_BRIGHTNESS;

    vec2 edge = position * (1.0 - position);
    float vignette = clamp(16.0 * edge.x * edge.y, 0.0001, 1.0);
    color *= pow(vignette, CRT_VIGNETTE);

    gl_FragColor = vec4(pow(clamp(color, 0.0, 1.0), vec3(1.0 / kOutputGamma)),
                        1.0);
}

#endif
