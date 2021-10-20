/* viridis colormap originally from https://www.shadertoy.com/view/XtGGzG and
 * remains under its original license */

float saturate( float x ) { return clamp( x, 0.0, 1.0 ); }

vec3 viridis_quintic( float x )
{
	x = saturate( x );
	vec4 x1 = vec4( 1.0, x, x * x, x * x * x ); // 1 x x2 x3
	vec4 x2 = x1 * x1.w * x; // x4 x5 x6 x7
	return vec3(
		dot( x1.xyzw, vec4( +0.280268003, -0.143510503, +2.225793877, -14.815088879 ) ) + dot( x2.xy, vec2( +25.212752309, -11.772589584 ) ),
		dot( x1.xyzw, vec4( -0.002117546, +1.617109353, -1.909305070, +2.701152864 ) ) + dot( x2.xy, vec2( -1.685288385, +0.178738871 ) ),
		dot( x1.xyzw, vec4( +0.300805501, +2.614650302, -12.019139090, +28.933559110 ) ) + dot( x2.xy, vec2( -33.491294770, +13.762053843 ) ) );
}

float tri( float x ) { return 1.0 - abs( fract( x * 0.5 ) - 0.5 ) * 2.0; }
vec3 smoothstep_unchecked( vec3 x ) { return ( x * x ) * ( 3.0 - x * 2.0 ); }
vec3 smoothbump( vec3 a, vec3 r, vec3 x ) { return 1.0 - smoothstep_unchecked( min( abs( x - a ), r ) / r ); }
