#include "renderer.h"

#include <algorithm> //sort

#include "camera.h"
#include "../gfx/gfx.h"
#include "../gfx/shader.h"
#include "../gfx/mesh.h"
#include "../gfx/texture.h"
#include "../gfx/fbo.h"
#include "../pipeline/prefab.h"
#include "../pipeline/material.h"
#include "../pipeline/animation.h"
#include "../utils/utils.h"
#include "../extra/hdre.h"
#include "../core/ui.h"

#include "scene.h"

#define MAX_LIGHTS 5

using namespace SCN;

//some globals
GFX::Mesh sphere;

Renderer::Renderer(const char* shader_atlas_filename)
{
	render_wireframe = false;
	render_boundaries = false;
	render_mode = eRenderMode::LIGHTS;
	is_multipass = true;
	show_shadowmaps = false;
	show_shadows = false;
	scene = nullptr;
	skybox_cubemap = nullptr;

	if (!GFX::Shader::LoadAtlas(shader_atlas_filename))
		exit(1);
	GFX::checkGLErrors();

	sphere.createSphere(1.0f);
}

bool SortRenderCalls(RenderCall* first, RenderCall* second)
{
	return first->distance_to_camera > second->distance_to_camera;
}

void Renderer::setupScene(Camera* camera)
{
	if (scene->skybox_filename.size())
		skybox_cubemap = GFX::Texture::Get(std::string(scene->base_folder + "/" + scene->skybox_filename).c_str());
	else
		skybox_cubemap = nullptr;

	lights.clear();
	render_calls.clear();

	//process entities
	for (size_t i = 0; i < scene->entities.size(); i++)
	{
		BaseEntity* ent = scene->entities[i];
		if (!ent->visible)
			continue;

		//is a prefab!
		if (ent->getType() == eEntityType::PREFAB)
		{
			PrefabEntity* pent = (SCN::PrefabEntity*)ent;
			RenderCall* rc = new RenderCall();
			vec3 nodepos = pent->root.model.getTranslation();
			rc->ent = pent;
			rc->distance_to_camera = camera->eye.distance(nodepos);
			render_calls.push_back(rc);
		}
		else if (ent->getType() == eEntityType::LIGHT) 
		{
			lights.push_back((SCN::LightEntity*)ent);
		}
	}
	std::sort(render_calls.begin(), render_calls.end(), SortRenderCalls);

	generateShadowmaps();
}

void Renderer::renderScene(SCN::Scene* scene, Camera* camera)
{
	this->scene = scene;
	setupScene(camera);

	renderFrame(scene, camera);

	//debug
	if (show_shadowmaps)
		debugShadowmaps();
	
}

void Renderer::renderFrame(SCN::Scene* scene, Camera* camera)
{
	glDisable(GL_BLEND);
	glEnable(GL_DEPTH_TEST);

	camera->enable();

	//set the clear color (the background color)
	glClearColor(scene->background_color.x, scene->background_color.y, scene->background_color.z, 1.0);

	// Clear the color and the depth buffer
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	GFX::checkGLErrors();

	//render skybox
	if(skybox_cubemap && render_mode != eRenderMode::FLAT)
		renderSkybox(skybox_cubemap);

	//render entities in order
	for each (RenderCall* rc in render_calls)
	{
		if (!rc->ent->visible)
			continue;

		if (rc->ent->getType() == eEntityType::PREFAB)
		{
			PrefabEntity* pent = (SCN::PrefabEntity*)rc->ent;
			if (pent->prefab)
				renderNode(&pent->root, camera);
		}
	}
}


void Renderer::renderSkybox(GFX::Texture* cubemap)
{
	Camera* camera = Camera::current;

	glDisable(GL_BLEND);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	if (render_wireframe)
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

	GFX::Shader* shader = GFX::Shader::Get("skybox");
	if (!shader)
		return;
	shader->enable();

	Matrix44 m;
	m.setTranslation(camera->eye.x, camera->eye.y, camera->eye.z);
	m.scale(10, 10, 10);
	shader->setUniform("u_model", m);
	cameraToShader(camera, shader);
	shader->setUniform("u_texture", cubemap, 0);
	sphere.render(GL_TRIANGLES);
	shader->disable();
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	glEnable(GL_DEPTH_TEST);
}

//renders a node of the prefab and its children
void Renderer::renderNode(SCN::Node* node, Camera* camera)
{
	if (!node->visible)
		return;

	//compute global matrix
	Matrix44 node_model = node->getGlobalMatrix(true);

	//does this node have a mesh? then we must render it
	if (node->mesh && node->material)
	{
		//compute the bounding box of the object in world space (by using the mesh bounding box transformed to world space)
		BoundingBox world_bounding = transformBoundingBox(node_model,node->mesh->box);
		
		//if bounding box is inside the camera frustum then the object is probably visible
		if (camera->testBoxInFrustum(world_bounding.center, world_bounding.halfsize) )
		{
			if(render_boundaries)
				node->mesh->renderBounding(node_model, true);
			switch (render_mode) {
				case FLAT: 
					renderMeshWithMaterialFlat(node_model, node->mesh, node->material); 
					break;
				case TEXTURED:
					renderMeshWithMaterial(node_model, node->mesh, node->material);
					break;
				case LIGHTS: 
					renderMeshWithMaterialLight(node_model, node->mesh, node->material); 
					break;
			}
			
		}
	}

	//iterate recursively with children
	for (int i = 0; i < node->children.size(); ++i)
		renderNode( node->children[i], camera);
}

void Renderer::renderMeshWithMaterial(const Matrix44 model, GFX::Mesh* mesh, SCN::Material* material)
{
	//in case there is nothing to do
	if (!mesh || !mesh->getNumVertices() || !material)
		return;
	assert(glGetError() == GL_NO_ERROR);

	//define locals to simplify coding
	GFX::Shader* shader = NULL;
	GFX::Texture* albedo_texture = NULL;
	GFX::Texture* emissive_texture = NULL;
	GFX::Texture* white = GFX::Texture::getWhiteTexture();
	Camera* camera = Camera::current;

	albedo_texture = material->textures[SCN::eTextureChannel::ALBEDO].texture;
	emissive_texture = material->textures[SCN::eTextureChannel::EMISSIVE].texture;
	//texture = material->metallic_roughness_texture;
	//texture = material->normal_texture;
	//texture = material->occlusion_texture;


	//select the blending
	if (material->alpha_mode == SCN::eAlphaMode::BLEND)
	{
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}
	else
		glDisable(GL_BLEND);

	//select if render both sides of the triangles
	if (material->two_sided)
		glDisable(GL_CULL_FACE);
	else
		glEnable(GL_CULL_FACE);
	assert(glGetError() == GL_NO_ERROR);

	glEnable(GL_DEPTH_TEST);

	//chose a shader
	shader = GFX::Shader::Get("texture");

	assert(glGetError() == GL_NO_ERROR);

	//no shader? then nothing to render
	if (!shader)
		return;
	shader->enable();

	//upload uniforms
	shader->setUniform("u_model", model);
	cameraToShader(camera, shader);
	float t = getTime();
	shader->setUniform("u_time", t);

	shader->setUniform("u_color", material->color);
	shader->setUniform("u_emissive_factor", material->emissive_factor);
	shader->setUniform("u_albedo_texture", albedo_texture ? albedo_texture : white, 0);
	shader->setUniform("u_emissive_texture", emissive_texture ? emissive_texture : white, 1);


	//this is used to say which is the alpha threshold to what we should not paint a pixel on the screen (to cut polygons according to texture alpha)
	shader->setUniform("u_alpha_cutoff", material->alpha_mode == SCN::eAlphaMode::MASK ? material->alpha_cutoff : 0.001f);

	if (render_wireframe)
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

	mesh->render(GL_TRIANGLES);



	//disable shader
	shader->disable();

	//set the render state as it was before to avoid problems with future renders
	glDisable(GL_BLEND);
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

//renders a mesh given its transform and material
void Renderer::renderMeshWithMaterialFlat(const Matrix44 model, GFX::Mesh* mesh, SCN::Material* material)
{
	//in case there is nothing to do
	if (!mesh || !mesh->getNumVertices() || !material )
		return;
    assert(glGetError() == GL_NO_ERROR);

	//define locals to simplify coding
	GFX::Shader* shader = NULL;
	Camera* camera = Camera::current;

	//select the blending
	if (material->alpha_mode == SCN::eAlphaMode::BLEND)
		return;
	glDisable(GL_BLEND);

	//select if render both sides of the triangles
	if(material->two_sided)
		glDisable(GL_CULL_FACE);
	else
		glEnable(GL_CULL_FACE);
    assert(glGetError() == GL_NO_ERROR);

	glEnable(GL_DEPTH_TEST);

	//chose a shader
	shader = GFX::Shader::Get("flat");

    assert(glGetError() == GL_NO_ERROR);

	//no shader? then nothing to render
	if (!shader)
		return;
	shader->enable();

	//upload uniforms
	shader->setUniform("u_model", model);
	cameraToShader(camera, shader);

	mesh->render(GL_TRIANGLES);
	
	//disable shader
	shader->disable();
	//set the render state as it was before to avoid problems with future renders
	glDisable(GL_BLEND);
	glPolygonMode( GL_FRONT_AND_BACK, GL_FILL );
}

void Renderer::renderMeshWithMaterialLight(const Matrix44 model, GFX::Mesh* mesh, SCN::Material* material)
{
	//in case there is nothing to do
	if (!mesh || !mesh->getNumVertices() || !material)
		return;
	assert(glGetError() == GL_NO_ERROR);

	//define locals to simplify coding
	GFX::Shader* shader = NULL;
	GFX::Texture* albedo_texture = NULL;
	GFX::Texture* emissive_texture = NULL;
	GFX::Texture* metallic_texture = NULL;
	GFX::Texture* normal_texture = NULL;
	GFX::Texture* occlusion_texture = NULL;
	GFX::Texture* white = GFX::Texture::getWhiteTexture();
	Camera* camera = Camera::current;

	albedo_texture = material->textures[SCN::eTextureChannel::ALBEDO].texture;
	emissive_texture = material->textures[SCN::eTextureChannel::EMISSIVE].texture;
	metallic_texture = material->textures[SCN::eTextureChannel::METALLIC_ROUGHNESS].texture;
	normal_texture = material->textures[SCN::eTextureChannel::NORMALMAP].texture;
	occlusion_texture = material->textures[SCN::eTextureChannel::OCCLUSION].texture;

	//select the blending
	if (material->alpha_mode == SCN::eAlphaMode::BLEND)
	{
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}
	else
		glDisable(GL_BLEND);

	//select if render both sides of the triangles
	if (material->two_sided)
		glDisable(GL_CULL_FACE);
	else
		glEnable(GL_CULL_FACE);
	assert(glGetError() == GL_NO_ERROR);

	glEnable(GL_DEPTH_TEST);

	//chose a shader

	std::string curr_shader = lights.size() == 0 ? "no_light"
		: is_multipass ? "light_multipass" : "light_singlepass";
	shader = GFX::Shader::Get(curr_shader.c_str());

	assert(glGetError() == GL_NO_ERROR);

	//no shader? then nothing to render
	if (!shader)
		return;
	shader->enable();

	//upload uniforms
	shader->setUniform("u_model", model);
	cameraToShader(camera, shader);
	float t = getTime();
	shader->setUniform("u_time", t);

	shader->setUniform("u_color", material->color);
	shader->setUniform("u_emissive_factor", material->emissive_factor);
	int curr_text = 0;
	shader->setUniform("u_albedo_texture", albedo_texture ? albedo_texture : white, curr_text++);
	shader->setUniform("u_emissive_texture", emissive_texture ? emissive_texture : white, curr_text++);
	shader->setUniform("u_metallic_texture", metallic_texture ? metallic_texture : white, curr_text++);
	if(normal_texture)
		shader->setUniform("u_normal_texture", normal_texture, curr_text++);
	shader->setUniform("u_has_normalmap", normal_texture ? 4.0f : 0.0f);
	if(occlusion_texture)
		shader->setUniform("u_occlusion_texture", occlusion_texture, curr_text++);


	//this is used to say which is the alpha threshold to what we should not paint a pixel on the screen (to cut polygons according to texture alpha)
	shader->setUniform("u_alpha_cutoff", material->alpha_mode == SCN::eAlphaMode::MASK ? material->alpha_cutoff : 0.001f);

	vec3 temp_ambient_light = scene->ambient_light;
	shader->setUniform("u_ambient_light", scene->ambient_light);

	if (render_wireframe)
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);


#pragma region MULTIPASS
	if (is_multipass)
	{
		glDepthFunc(GL_LEQUAL);
		if (lights.size() == 0)
		{
			mesh->render(GL_TRIANGLES);
		}
		else
		{
			for (size_t i = 0; i < lights.size(); i++)
			{
				LightEntity* light = lights[i];
				Matrix44 bounding_model = model;
				if (!BoundingBoxSphereOverlap(BoundingBox(bounding_model.getTranslation(), vec3(mesh->radius)),
					light->root.model.getTranslation(),
					light->max_distance * 2))
					continue;

				shader->setUniform( "u_light_position", light->root.model.getTranslation() );
				shader->setUniform( "u_light_front", light->root.model.rotateVector(vec3(0,0,1)) );
				shader->setUniform( "u_light_color", light->color * light->intensity );
				shader->setUniform( "u_light_info", vec4((int)light->light_type,(int)light->near_distance, (int)light->max_distance, 0) ); 

				shader->setUniform( "u_shadow_params", vec2(light->shadowmap?1.0f:0.0f, light->shadow_bias )); 
				if (light->shadowmap)
				{
					shader->setUniform( "u_shadowmap", light->shadowmap, 8 );
					shader->setUniform( "u_shadow_viewproj", light->shadow_viewproj ); 
				}

				if (light->light_type == eLightType::SPOT)
		  			shader->setUniform( "u_light_cone", vec2( cos(light->cone_info.x * DEG2RAD), cos(light->cone_info.y * DEG2RAD)));
			

				//do the draw call that renders the mesh into the screen
				mesh->render(GL_TRIANGLES);
				glEnable(GL_BLEND);
				glBlendFunc(GL_SRC_ALPHA, GL_ONE);

				temp_ambient_light = vec3(0.0);
				shader->setUniform( "u_ambient_light", temp_ambient_light );
				shader->setUniform( "u_emissive_factor", vec3(0.0) );
			}
		}

	}
#pragma endregion MULTIPASS
#pragma region SINGLEPASS
	else
	{
		vec3 light_position[MAX_LIGHTS];
		vec3 light_color[MAX_LIGHTS];
		vec3 light_front[MAX_LIGHTS];
		vec4 light_info[MAX_LIGHTS];

		vec2 light_cone[MAX_LIGHTS];

		vec2 shadow_params[MAX_LIGHTS];
		mat4 shadow_viewprojs[MAX_LIGHTS];
		GFX::Texture* shadowmaps[MAX_LIGHTS];

		

		int num_lights = lights.size();
		if (num_lights == 0)
		{
			shader->setUniform("u_light_type", 0);
			mesh->render(GL_TRIANGLES);
		}
		else
		{
			for (size_t i = 0; i < min(num_lights,MAX_LIGHTS); i++)
			{
				
				LightEntity* light = lights[i];
			
				light_position[i] = light->root.model.getTranslation();
				light_color[i] = light->color * light->intensity;
				light_front[i] = light->root.model.rotateVector(vec3(0, 0, 1));
				light_info[i] = vec4((int)light->light_type, (int)light->near_distance, (int)light->max_distance, 0);

				if(light->light_type == eLightType::SPOT)
					light_cone[i] = vec2(cos(light->cone_info.x * DEG2RAD), cos(light->cone_info.y * DEG2RAD));

				shadow_params[i] = vec2((light->shadowmap && show_shadows) ? 1 : 0, light->shadow_bias);
				if (light->shadowmap)
				{
					shadowmaps[i] = light->shadowmap;
					shadow_viewprojs[i] = light->shadow_viewproj;
				}
				else
				{
					shadowmaps[i] = nullptr;
					shadow_viewprojs[i] = mat4();
				}
				
			}

			shader->setUniform3Array("u_light_position", (float*)&light_position, min(num_lights, MAX_LIGHTS));
			shader->setUniform3Array("u_light_color", (float*)&light_color, min(num_lights, MAX_LIGHTS));
			shader->setUniform3Array("u_light_front", (float*)&light_front, min(num_lights, MAX_LIGHTS));
			shader->setUniform4Array("u_light_info", (float*)&light_info, min(num_lights, MAX_LIGHTS));
			shader->setUniform2Array("u_light_cone", (float*)&light_cone, min(num_lights, MAX_LIGHTS));
			shader->setUniform1("u_num_lights", min(num_lights, MAX_LIGHTS));


			shader->setUniform2Array("u_shadow_params", (float*)&shadow_params, min(num_lights, MAX_LIGHTS));
			if (show_shadows)
			{
				shader->setMatrix44Array("u_shadow_viewproj", shadow_viewprojs, min(num_lights, MAX_LIGHTS));
				int i = 1;
				for (size_t i = 0; i < min(num_lights, MAX_LIGHTS); i++)
				{
					if (i < num_lights)
					{
 						std::string uniform_string = "u_shadowmap[" + std::to_string(i) + "]";
						if(shadowmaps[i])
							shader->setUniform(uniform_string.c_str(), shadowmaps[i], 8 + i);
					}
				}
			}

			mesh->render(GL_TRIANGLES);
		}
	}
#pragma endregion SINGLEPASS
	
	//disable shader
	shader->disable();

	//set the render state as it was before to avoid problems with future renders
	glDisable(GL_BLEND);
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	glDepthFunc(GL_LESS);
}
void SCN::Renderer::cameraToShader(Camera* camera, GFX::Shader* shader)
{
	shader->setUniform("u_viewprojection", camera->viewprojection_matrix );
	shader->setUniform("u_camera_position", camera->eye);
}

void Renderer::debugShadowmaps()
{
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
	int x = 300;
	for (auto light : lights)
	{
		if (!light->shadowmap)
			continue;
		GFX::Shader* shader = GFX::Shader::getDefaultShader("linear_depth");
		shader->enable();
		shader->setUniform("u_camera_nearfar", vec2(light->near_distance, light->max_distance));
		glViewport(x, 100, 256, 256);
		light->shadowmap->toViewport( shader );
		shader->disable();
		x += 260;
	}

	vec2 size = CORE::getWindowSize();
	glViewport(0, 0, size.x, size.y);
}

bool checkVectors(vec3 vector1, vec3 vector2)
{
	return vector1.x == vector2.x && vector1.y == vector2.y && vector1.z == vector2.z;
}

void Renderer::generateShadowAtlas()
{
	Camera camera;
	GFX::startGPULabel("Shadow atlas");

	eRenderMode prev = render_mode;
	render_mode = eRenderMode::FLAT;

	vec2 size = vec2(1024, 1024);
	int i = 0;
	int j = 0;
	for (auto light : lights)
	{
		if (!light->cast_shadows)
			continue;

		//check if light inside camera
		//TODO

		if (!light->shadowmap_fbo)
		{
			light->shadowmap_fbo = new GFX::FBO();
			light->shadowmap_fbo->setDepthOnly(1024, 1024);
			light->shadowmap = light->shadowmap_fbo->depth_texture;
		}

		vec3 pos = light->root.model.getTranslation();
		vec3 front = light->root.model.rotateVector(vec3(0, 0, -1));
		vec3 up = checkVectors(front, vec3(0, -1, 0)) ? vec3(0, -1, 0) : vec3(0, 1, 0);

		camera.lookAt(pos, pos + front, up);

		float aspect = 1.0;
		if (light->light_type == eLightType::SPOT)
			camera.setPerspective(light->cone_info.y * 2, aspect, light->near_distance, light->max_distance);
		else if (light->light_type == eLightType::DIRECTIONAL)
		{
			float halfarea = light->area / 2;
			camera.setOrthographic(-halfarea, halfarea, halfarea * aspect, -halfarea * aspect, 0.1, light->max_distance);
		}

		light->shadowmap_fbo->bind();
		{
			vec2 region = vec2(i * (size.x * 0.25), j * (size.y * 0.25));
			vec4 shadow_region(region.x, region.y, size.x * 0.25, size.y * 0.25);


			glViewport(shadow_region.x, shadow_region.y,
				shadow_region.z, shadow_region.w);
			glScissor(shadow_region.x, shadow_region.y,
				shadow_region.z, shadow_region.w);
			glEnable(GL_SCISSOR_TEST);

			//render shadowmap...
			renderFrame(scene, &camera);
			glClear(GL_DEPTH_TEST);
		}

		light->shadowmap_fbo->unbind();

		light->shadow_viewproj = camera.viewprojection_matrix;
		i++;
		if (i % 2 == 0)
		{
			j++;
			i = 0;
		}
	}

	render_mode = prev;

	GFX::endGPULabel();
}

void Renderer::generateShadowmaps()
{
	Camera camera;
	Camera* scene_cam = Camera::current;
	GFX::startGPULabel("Shadowmaps");

	eRenderMode prev = render_mode;
	render_mode = eRenderMode::FLAT;

	for (auto light : lights)
	{
		if (!light->cast_shadows)
			continue;

		//check if light inside camera
		//TODO
			
		if (!light->shadowmap_fbo)
		{
			light->shadowmap_fbo = new GFX::FBO();
			light->shadowmap_fbo->setDepthOnly(1024, 1024);
			light->shadowmap = light->shadowmap_fbo->depth_texture;
		}

		vec3 pos = light->root.model.getTranslation();
		vec3 front = light->root.model.rotateVector(vec3(0, 0, -1));
		vec3 up = checkVectors(front,vec3(0,-1,0)) ? vec3(0, -1, 0) : vec3(0, 1, 0);
		
		camera.lookAt( pos,  pos + front, up);

		float aspect = 1.0;
		if (light->light_type == eLightType::SPOT)
			camera.setPerspective(light->cone_info.y * 2, aspect, light->near_distance, light->max_distance);
		else if (light->light_type == eLightType::DIRECTIONAL)
		{
			float halfarea = light->area / 2;
			camera.setOrthographic(-halfarea, halfarea, halfarea * aspect, -halfarea * aspect, 0.1, light->max_distance);
		}
		
		light->shadowmap_fbo->bind();

		renderFrame(scene, &camera);

		light->shadowmap_fbo->unbind();

		light->shadow_viewproj = camera.viewprojection_matrix;
	}

	render_mode = prev;

	GFX::endGPULabel();

}



#ifndef SKIP_IMGUI

void TogglePassMode(const char* str_id, bool* v)
{
	ImGui::Text("Singlepass");
	ImGui::SameLine();
	ImVec4* colors = ImGui::GetStyle().Colors;
	ImVec2 p = ImGui::GetCursorScreenPos();
	ImDrawList* draw_list = ImGui::GetWindowDrawList();

	float height = ImGui::GetFrameHeight();
	float width = height * 1.55f;
	float radius = height * 0.50f;

	
	ImGui::SameLine();
	ImGui::InvisibleButton(str_id, ImVec2(width, height));
	if (ImGui::IsItemClicked()) *v = !*v;
	ImGuiContext& gg = *GImGui;
	float ANIM_SPEED = 0.085f;
	if (gg.LastActiveId == gg.CurrentWindow->GetID(str_id))// && g.LastActiveIdTimer < ANIM_SPEED)
		float t_anim = ImSaturate(gg.LastActiveIdTimer / ANIM_SPEED);
	if (ImGui::IsItemHovered())
		draw_list->AddRectFilled(p, ImVec2(p.x + width, p.y + height), ImGui::GetColorU32(*v ? colors[ImGuiCol_ButtonActive] : ImVec4(0.78f, 0.78f, 0.78f, 1.0f)), height * 0.5f);
	else
		draw_list->AddRectFilled(p, ImVec2(p.x + width, p.y + height), ImGui::GetColorU32(*v ? colors[ImGuiCol_Button] : ImVec4(0.85f, 0.85f, 0.85f, 1.0f)), height * 0.50f);
	draw_list->AddCircleFilled(ImVec2(p.x + radius + (*v ? 1 : 0) * (width - radius * 2.0f), p.y + radius), radius - 1.5f, IM_COL32(255, 255, 255, 255));

	ImGui::SameLine();
	ImGui::Text("Multipass");
	ImGui::NewLine();
}

void ToggleShadows(const char* str_id, bool* v)
{
	ImGui::Text("Enable Shadows");
	ImGui::SameLine();
	ImVec4* colors = ImGui::GetStyle().Colors;
	ImVec2 p = ImGui::GetCursorScreenPos();
	ImDrawList* draw_list = ImGui::GetWindowDrawList();

	float height = ImGui::GetFrameHeight();
	float width = height * 1.55f;
	float radius = height * 0.50f;

	ImGui::InvisibleButton(str_id, ImVec2(width, height));
	if (ImGui::IsItemClicked()) *v = !*v;
	ImGuiContext& gg = *GImGui;
	float ANIM_SPEED = 0.085f;
	if (gg.LastActiveId == gg.CurrentWindow->GetID(str_id))// && g.LastActiveIdTimer < ANIM_SPEED)
		float t_anim = ImSaturate(gg.LastActiveIdTimer / ANIM_SPEED);
	if (ImGui::IsItemHovered())
		draw_list->AddRectFilled(p, ImVec2(p.x + width, p.y + height), ImGui::GetColorU32(*v ? colors[ImGuiCol_ButtonActive] : ImVec4(0.78f, 0.78f, 0.78f, 1.0f)), height * 0.5f);
	else
		draw_list->AddRectFilled(p, ImVec2(p.x + width, p.y + height), ImGui::GetColorU32(*v ? colors[ImGuiCol_Button] : ImVec4(0.85f, 0.85f, 0.85f, 1.0f)), height * 0.50f);
	draw_list->AddCircleFilled(ImVec2(p.x + radius + (*v ? 1 : 0) * (width - radius * 2.0f), p.y + radius), radius - 1.5f, IM_COL32(255, 255, 255, 255));
	
}



void Renderer::showUI()
{
		
	ImGui::Checkbox("Wireframe", &render_wireframe);
	ImGui::Checkbox("Boundaries", &render_boundaries);

	ImGui::Combo("Render Mode", (int*)&render_mode, "FLAT\0TEXTURED\0LIGHTS\0", 3);
	if (render_mode == eRenderMode::LIGHTS)
	{
		TogglePassMode("", &is_multipass);
		ImGui::Checkbox("Show shadowmaps", &show_shadowmaps);
		if (!is_multipass)
			ToggleShadows("", &show_shadows);
	}

}

//Switch widget extracted from: https://github.com/ocornut/imgui/issues/1537#issuecomment-780262461
//void ToggleButton(const char* str_id, bool* v)
//{
//	ImVec4* colors = ImGui::GetStyle().Colors;
//	ImVec2 p = ImGui::GetCursorScreenPos();
//	ImDrawList* draw_list = ImGui::GetWindowDrawList();
//
//	float height = ImGui::GetFrameHeight();
//	float width = height * 1.55f;
//	float radius = height * 0.50f;
//
//	ImGui::InvisibleButton(str_id, ImVec2(width, height));
//	if (ImGui::IsItemClicked()) *v = !*v;
//	ImGuiContext& gg = *GImGui;
//	float ANIM_SPEED = 0.085f;
//	if (gg.LastActiveId == gg.CurrentWindow->GetID(str_id))// && g.LastActiveIdTimer < ANIM_SPEED)
//		float t_anim = ImSaturate(gg.LastActiveIdTimer / ANIM_SPEED);
//	if (ImGui::IsItemHovered())
//		draw_list->AddRectFilled(p, ImVec2(p.x + width, p.y + height), ImGui::GetColorU32(*v ? colors[ImGuiCol_ButtonActive] : ImVec4(0.78f, 0.78f, 0.78f, 1.0f)), height * 0.5f);
//	else
//		draw_list->AddRectFilled(p, ImVec2(p.x + width, p.y + height), ImGui::GetColorU32(*v ? colors[ImGuiCol_Button] : ImVec4(0.85f, 0.85f, 0.85f, 1.0f)), height * 0.50f);
//	draw_list->AddCircleFilled(ImVec2(p.x + radius + (*v ? 1 : 0) * (width - radius * 2.0f), p.y + radius), radius - 1.5f, IM_COL32(255, 255, 255, 255));
//}

#else
void Renderer::showUI() {}
#endif