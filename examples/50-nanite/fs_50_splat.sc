$input v_texcoord0

/*
 * Copyright 2011-2025 Branimir Karadzic. All rights reserved.
 * License: https://github.com/bkaradzic/bgfx/blob/master/LICENSE
 */

#include "../common/common.sh"

SAMPLER2D(sourceSampler, 0);

void main()
{
	vec4 base  = texture2D(sourceSampler, v_texcoord0);
	gl_FragColor = base;
}
