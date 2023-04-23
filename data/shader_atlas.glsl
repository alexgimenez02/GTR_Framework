//example of some shaders compiled
flat basic.vs flat.fs
texture basic.vs texture.fs
light_multipass basic.vs light_multi.fs
light_singlepass basic.vs light_single.fs
skybox basic.vs skybox.fs
depth quad.vs depth.fs
multi basic.vs multi.fs

\basic.vs

#version 330 core

in vec3 a_vertex;
in vec3 a_normal;
in vec2 a_coord;
in vec4 a_color;

uniform vec3 u_camera_pos;

uniform mat4 u_model;
uniform mat4 u_viewprojection;

//this will store the color for the pixel shader
out vec3 v_position;
out vec3 v_world_position;
out vec3 v_normal;
out vec2 v_uv;
out vec4 v_color;

uniform float u_time;

void main()
{	
	//calcule the normal in camera space (the NormalMatrix is like ViewMatrix but without traslation)
	v_normal = (u_model * vec4( a_normal, 0.0) ).xyz;
	
	//calcule the vertex in object space
	v_position = a_vertex;
	v_world_position = (u_model * vec4( v_position, 1.0) ).xyz;
	
	//store the color in the varying var to use it from the pixel shader
	v_color = a_color;

	//store the texture coordinates
	v_uv = a_coord;

	//calcule the position of the vertex using the matrices
	gl_Position = u_viewprojection * vec4( v_world_position, 1.0 );
}

\quad.vs

#version 330 core

in vec3 a_vertex;
in vec2 a_coord;
out vec2 v_uv;

void main()
{	
	v_uv = a_coord;
	gl_Position = vec4( a_vertex, 1.0 );
}


\flat.fs

#version 330 core

uniform vec4 u_color;

out vec4 FragColor;

void main()
{
	FragColor = u_color;
}


\texture.fs

#version 330 core

in vec3 v_position;
in vec3 v_world_position;
in vec3 v_normal;
in vec2 v_uv;
in vec4 v_color;

uniform vec4 u_color;
uniform sampler2D u_texture;
uniform float u_time;
uniform float u_alpha_cutoff;

out vec4 FragColor;

void main()
{
	vec2 uv = v_uv;
	vec4 color = u_color;
	color *= texture( u_texture, v_uv );

	if(color.a < u_alpha_cutoff)
		discard;

	FragColor = color;
}

\light_multi.fs

#version 330 core

#define NOLIGHTS 0 
#define POINT_LIGHT 1
#define SPOT_LIGHT 2
#define DIRECTIONAL_LIGHT 3 

in vec3 v_position;
in vec3 v_world_position;
in vec3 v_normal;
in vec2 v_uv;
in vec4 v_color;

//material properties
uniform vec4 u_color;
uniform sampler2D u_albedo_texture;
uniform sampler2D u_emissive_texture;
uniform sampler2D u_metallic_texture;
uniform sampler2D u_normal_texture;
uniform sampler2D u_occlusion_texture;
uniform vec3 u_emissive_factor;

//global properties
uniform float u_time;
uniform float u_alpha_cutoff;

uniform vec3 u_ambient_light;

uniform vec3 u_light_position;
uniform vec3 u_light_front;
uniform vec3 u_light_color;
uniform vec4 u_light_info; //(light_type, near, far, xxx)
uniform vec2 u_light_cone;

out vec4 FragColor;

void main()
{
	vec2 uv = v_uv;
	vec4 albedo = u_color;
	vec4 occlusion = vec4(0.0);
	vec4 metalic = vec4(0.0);
	vec4 normalmap = vec4(0.0);
	albedo *= texture( u_albedo_texture, v_uv );
	occlusion = texture( u_occlusion_texture, v_uv );
	metalic = texture( u_metallic_texture, v_uv );
	normalmap = texture( u_normal_texture, v_uv );
	//Discard as soon as possible
	if(albedo.a < u_alpha_cutoff)
		discard;


	vec3 N = normalize( v_normal );
	
	vec3 light = vec3(0.0);
	light += (u_ambient_light * occlusion.r );

	if( int(u_light_info.x) == DIRECTIONAL_LIGHT )
	{
		float NdotL = dot(N,u_light_front);
		light += max( NdotL, 0.0 ) * u_light_color;
	}
	else if( int(u_light_info.x) == POINT_LIGHT || int(u_light_info.x) == SPOT_LIGHT )
	{
		vec3 L = u_light_position - v_world_position;
		float dist = length(L);
		L /= dist;
		float NdotL = dot(N,L);
		float att = max(0.0, (u_light_info.z - dist) / u_light_info.z);
		
		if( int(u_light_info.x) == SPOT_LIGHT )
		{
			float cos_angle = dot( u_light_front, L);
			if( cos_angle < u_light_cone.y )
				att = 0.0;
			else if( cos_angle < u_light_cone.x) 
				att *= 1.0 - (cos_angle - u_light_cone.x) / (u_light_cone.y - u_light_cone.x);
		} 

		light += max( NdotL, 0.0 ) * u_light_color * att;
	}

	vec3 color = albedo.xyz * light;
	color += u_emissive_factor * texture(u_emissive_texture, v_uv).xyz;
	
	FragColor = vec4(color, albedo.a);
}


\light_single.fs

#version 330 core

#define NOLIGHTS 0 
#define POINT_LIGHT 1
#define SPOT_LIGHT 2
#define DIRECTIONAL_LIGHT 3 
#define MAX_LIGHTS 5

in vec3 v_position;
in vec3 v_world_position;
in vec3 v_normal;
in vec2 v_uv;
in vec4 v_color;

//material properties
uniform vec4 u_color;
uniform sampler2D u_albedo_texture;
uniform sampler2D u_emissive_texture;
uniform vec3 u_emissive_factor;

//global properties
uniform float u_time;
uniform float u_alpha_cutoff;

uniform vec3 u_ambient_light;

uniform vec3 u_light_position[MAX_LIGHTS];
uniform vec3 u_light_front[MAX_LIGHTS];
uniform vec3 u_light_color[MAX_LIGHTS];
uniform vec4 u_light_info[MAX_LIGHTS]; //(light_type, near, far, xxx)
uniform vec2 u_light_cone[MAX_LIGHTS];
uniform int u_num_lights;

out vec4 FragColor;

void main()
{
	vec2 uv = v_uv;
	vec4 albedo = u_color;
	albedo *= texture( u_albedo_texture, v_uv );
	//Discard as soon as possible
	if(albedo.a < u_alpha_cutoff)
		discard;

	vec3 N = normalize( v_normal );
	
	vec3 light = vec3(0.0);
	light += u_ambient_light;

	for	( int i = 0; i < MAX_LIGHTS; i++){
		if (i < u_num_lights){
			vec4 current_light_info = u_light_info[i];
			
			if( int(current_light_info.x) == DIRECTIONAL_LIGHT )
			{
				float NdotL = dot(N,u_light_front[i]);
				light += max( NdotL, 0.0 ) * u_light_color[i];
			}
			else if( int(current_light_info.x) == POINT_LIGHT || int(current_light_info.x) == SPOT_LIGHT )
			{
				vec3 L = u_light_position[i] - v_world_position;
				float dist = length(L);
				L /= dist;
				float NdotL = dot(N,L);
				
				float att = max(0.0, (current_light_info.z - dist) / current_light_info.z);
				
				if( int(current_light_info.x) == SPOT_LIGHT )
				{
					vec2 current_light_cone = u_light_cone[i];
					float cos_angle = dot( u_light_front[i], L);
					if( cos_angle < current_light_cone.y )
						att = 0.0;
					else if( cos_angle < current_light_cone.x) 
						att *= 1.0 - (cos_angle - current_light_cone.x) / (current_light_cone.y - current_light_cone.x);
				} 

				light += max( NdotL, 0.0 ) * u_light_color[i] * att;
			}
			
		}
	}

	

	vec3 color = albedo.xyz * light;
	color += u_emissive_factor * texture(u_emissive_texture, v_uv).xyz;
	
	FragColor = vec4(color, albedo.a);
}

\skybox.fs

#version 330 core

in vec3 v_position;
in vec3 v_world_position;

uniform samplerCube u_texture;
uniform vec3 u_camera_position;
out vec4 FragColor;

void main()
{
	vec3 E = v_world_position - u_camera_position;
	vec4 color = texture( u_texture, E );
	FragColor = color;
}


\multi.fs

#version 330 core

in vec3 v_position;
in vec3 v_world_position;
in vec3 v_normal;
in vec2 v_uv;

uniform vec4 u_color;
uniform sampler2D u_texture;
uniform float u_time;
uniform float u_alpha_cutoff;

layout(location = 0) out vec4 FragColor;
layout(location = 1) out vec4 NormalColor;

void main()
{
	vec2 uv = v_uv;
	vec4 color = u_color;
	color *= texture( u_texture, uv );

	if(color.a < u_alpha_cutoff)
		discard;

	vec3 N = normalize(v_normal);

	FragColor = color;
	NormalColor = vec4(N,1.0);
}


\depth.fs

#version 330 core

uniform vec2 u_camera_nearfar;
uniform sampler2D u_texture; //depth map
in vec2 v_uv;
out vec4 FragColor;

void main()
{
	float n = u_camera_nearfar.x;
	float f = u_camera_nearfar.y;
	float z = texture2D(u_texture,v_uv).x;
	if( n == 0.0 && f == 1.0 )
		FragColor = vec4(z);
	else
		FragColor = vec4( n * (z + 1.0) / (f + n - z * (f - n)) );
}


\instanced.vs

#version 330 core

in vec3 a_vertex;
in vec3 a_normal;
in vec2 a_coord;

in mat4 u_model;

uniform vec3 u_camera_pos;

uniform mat4 u_viewprojection;

//this will store the color for the pixel shader
out vec3 v_position;
out vec3 v_world_position;
out vec3 v_normal;
out vec2 v_uv;

void main()
{	
	//calcule the normal in camera space (the NormalMatrix is like ViewMatrix but without traslation)
	v_normal = (u_model * vec4( a_normal, 0.0) ).xyz;
	
	//calcule the vertex in object space
	v_position = a_vertex;
	v_world_position = (u_model * vec4( a_vertex, 1.0) ).xyz;
	
	//store the texture coordinates
	v_uv = a_coord;

	//calcule the position of the vertex using the matrices
	gl_Position = u_viewprojection * vec4( v_world_position, 1.0 );
}