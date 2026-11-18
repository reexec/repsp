// re2DJ built-in post-processing shader: scanline.
// Written for rePIU (task 768) and brought to re2DJ (task 455) by the same
// author; Copyright (c) 2026, re2DJ contributors; BSD 3-Clause License,
// see THIRD_PARTY_NOTICES.md.
//
// Darkens the second half of every logical scanline. LogicalSize carries the
// logical resolution, so the lines follow the original 272-line output at any
// window scale: at 2x every other output row is a gap. The gap's edges are
// one output pixel soft so fractional scales do not alias, and at 1x, where a
// line is a single row, the effect fades out rather than dimming everything.

#pragma parameter SCANLINE_STRENGTH "Scanline strength" 0.45 0.0 1.0 0.05
#pragma parameter SCANLINE_BRIGHTNESS "Brightness" 1.20 0.5 2.0 0.05

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
uniform float SCANLINE_STRENGTH;
uniform float SCANLINE_BRIGHTNESS;
#else
#define SCANLINE_STRENGTH 0.45
#define SCANLINE_BRIGHTNESS 1.20
#endif

void main()
{
    vec3 color = texture2D(Texture, tex_coord).rgb;
    // Output rows per logical line, and where this pixel's centre sits in
    // its line (0 at one edge, 1 at the other).
    float rows = max(OutputSize.y / LogicalSize.y, 1.0);
    float phase = fract(tex_coord.y * LogicalSize.y);
    float soft = 0.5 / rows;
    float gap = smoothstep(0.5 - soft, 0.5 + soft, phase);
    float fade = clamp(rows - 1.0, 0.0, 1.0);
    float weight = 1.0 - SCANLINE_STRENGTH * gap * fade;
    float boost = mix(1.0, SCANLINE_BRIGHTNESS, fade);
    gl_FragColor = vec4(clamp(color * weight * boost, 0.0, 1.0), 1.0);
}

#endif
