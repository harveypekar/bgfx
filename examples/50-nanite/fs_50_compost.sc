$input v_texcoord0

/*
 * Copyright 2011-2025 Branimir Karadzic. All rights reserved.
 * License: https://github.com/bkaradzic/bgfx/blob/master/LICENSE
 */

#include "../common/common.sh"

SAMPLER2D(baseSampler, 0);
SAMPLER2D(naniteSampler,  1);

void main()
{
	vec3 luminance = vec3(.3, .59, .11);
	vec4 base  = texture2D(baseSampler, v_texcoord0);
	vec4 nanite  = texture2D(naniteSampler,  v_texcoord0);

	vec4 color = 0;
	color.r = dot(luminance, base.rgb);
	color.g = dot(luminance, nanite.rgb);

	color.a = 1.0;
	gl_FragColor = color;
}
