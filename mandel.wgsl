//------------------------------------------------------------------------------
//  Copyright (c) 2025 Michele Morrone
//  All rights reserved.
//
//  https://michelemorrone.eu - https://brutpitt.com
//
//  X: https://x.com/BrutPitt - GitHub: https://github.com/BrutPitt
//
//  direct mail: brutpitt(at)gmail.com - me(at)michelemorrone.eu
//
//  This software is distributed under the terms of the BSD 2-Clause license
//------------------------------------------------------------------------------
R"(
    struct shaderData {
        mScale      : vec2f,
        mTransp     : vec2f,
        wSize       : vec2f,
        iterations  : i32,
        nColors     : i32,
        shift       : f32,
    };
    @group(0) @binding(0) var<uniform> sd : shaderData;

    @vertex fn vs(@builtin(vertex_index) VertexIndex : u32) -> @builtin(position) vec4f
    {
        // use "in-place" position (w/o vetrex buffer): 4 vetex / triangleStrip
        var pos = array( vec2f(-1.0,  1.0),
                         vec2f(-1.0, -1.0),
                         vec2f( 1.0,  1.0),
                         vec2f( 1.0, -1.0)  );
        return vec4f(pos[VertexIndex], 0, 1);
    }

    fn hsl2rgb(hsl: vec3f) -> vec3f
    {
        let H: f32 = fract(hsl.x);
        let rgb: vec3f = clamp(vec3f(abs(H * 6. - 3.) - 1., 2. - abs(H * 6. - 2.), 2. - abs(H * 6. - 4.)), vec3f(0.0), vec3f(1.0));
        let C: f32 = (1. - abs(2. * hsl.z - 1.)) * hsl.y;
        return (rgb - 0.5) * C + hsl.z;
    }

    @fragment fn fs(@builtin(position) position: vec4f) -> @location(0) vec4f
    {
        let c: vec2f = sd.mTransp - sd.mScale + position.xy / sd.wSize * (sd.mScale * 2.);
        var z: vec2f = vec2f(0.);
        var clr: f32 = 0.;

        for (var i: i32 = 1; i < sd.iterations; i = i + 1) {
            z = vec2f(z.x * z.x - z.y * z.y, 2. * z.x * z.y) + c;
            if (dot(z, z) > 16.) {
                clr = f32(i) / f32(sd.nColors);
                break;
            }
        }

        if (clr > 0.0) { return vec4f(hsl2rgb(vec3f(sd.shift + clr, 1., 0.5)), 1.); }
        else           { return vec4f(0.); }
    }
)"