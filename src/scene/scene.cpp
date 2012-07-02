/*
 *  Albion 2 Engine "light"
 *  Copyright (C) 2004 - 2012 Florian Ziesche
 *  
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License only.
 *  
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "scene.h"
#include "particle/particle.h"

/*! scene constructor
 */
scene::scene(engine* e_) :
e(e_), s(e_->get_shader()), exts(e_->get_ext()), r(e_->get_rtt()), cl(e_->get_opencl()),
window_handler(this, &scene::window_event_handler)
{
	//
	stereo = e->get_stereo();
	
	recreate_buffers(frames[0], size2(e->get_width(), e->get_height()));
	
	e->get_event()->add_internal_event_handler(window_handler, EVENT_TYPE::WINDOW_RESIZE);
	
	// load light objects/models
	// TODO: !!! use simpler model!
	light_sphere = (a2estatic*)create_a2emodel<a2estatic>();
	light_sphere->load_model(e->data_path("light_sphere.a2m"));
}

/*! scene destructor
 */
scene::~scene() {
	a2e_debug("deleting scene object");
	
	e->get_event()->remove_event_handler(window_handler);

	a2e_debug("deleting models and lights");
	models.clear();
	lights.clear();

	//
	delete_buffers(frames[0]);
	delete light_sphere;

	a2e_debug("scene object deleted");
}

void scene::delete_buffers(frame_buffers& buffers) {
	if(buffers.scene_buffer != nullptr) r->delete_buffer(buffers.scene_buffer);
	if(buffers.fxaa_buffer != nullptr) r->delete_buffer(buffers.fxaa_buffer);
	if(buffers.g_buffer[0] != nullptr) r->delete_buffer(buffers.g_buffer[0]);
	if(buffers.l_buffer[0] != nullptr) r->delete_buffer(buffers.l_buffer[0]);
	if(buffers.g_buffer[1] != nullptr) r->delete_buffer(buffers.g_buffer[1]);
	if(buffers.l_buffer[1] != nullptr) r->delete_buffer(buffers.l_buffer[1]);
	
#if defined(A2E_INFERRED_RENDERING_CL)
	if(buffers.cl_normal_nuv_buffer[0] != nullptr) cl->delete_buffer(buffers.cl_normal_nuv_buffer[0]);
	if(buffers.cl_depth_buffer[0] != nullptr) cl->delete_buffer(buffers.cl_depth_buffer[0]);
	if(buffers.cl_light_buffer[0] != nullptr) cl->delete_buffer(buffers.cl_light_buffer[0]);
	if(buffers.cl_normal_nuv_buffer[1] != nullptr) cl->delete_buffer(buffers.cl_normal_nuv_buffer[1]);
	if(buffers.cl_depth_buffer[1] != nullptr) cl->delete_buffer(buffers.cl_depth_buffer[1]);
	if(buffers.cl_light_buffer[1] != nullptr) cl->delete_buffer(buffers.cl_light_buffer[1]);
#endif
}

void scene::recreate_buffers(frame_buffers& buffers, const size2 buffer_size, const bool create_alpha_buffer) {
	if(e->get_init_mode() != engine::GRAPHICAL) return;
	
	// check if buffers have already been created (and delete them, if so)
	delete_buffers(buffers);
	
	// create buffers used for inferred rendering
	
	// note: opaque and alpha/transparent geometry have their own buffers
	// use 1 + 2 16-bit RGBA float buffers for the g-buffer: normal+Nuv (both) and DSF (alpha only)
	// use 2 + 2 8-bit RGBA ubyte buffer for the l-buffer (separate diffuse + specular for both)
	// use 1 8-bit RGBA ubyte buffer for the (intermediate) fxaa buffer
	// use 1 8-bit RGBA ubyte buffer for the final buffer
	
	// TODO: add 4xSSAA support -> downscale in shader (+enable in engine)
	
	// figure out the target type (multi-sampled or not)
	GLenum target = GL_TEXTURE_2D;
	
	//
	GLint rgba16_float_internal_format = GL_RGBA16F;
	GLenum f16_format_type = GL_HALF_FLOAT;
	
	GLint internal_formats[] = { rgba16_float_internal_format, rgba16_float_internal_format };
	GLenum formats[] = { GL_RGBA, GL_RGBA };
	GLenum targets[] = { target, target };
	texture_object::TEXTURE_FILTERING filters[] = { texture_object::TF_POINT, texture_object::TF_POINT };
	const rtt::TEXTURE_ANTI_ALIASING taa = e->get_anti_aliasing();
	rtt::TEXTURE_ANTI_ALIASING taas[] = { taa, taa };
	GLint wraps[] = { GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE };
	GLenum types[] = { f16_format_type, f16_format_type };
	
	//
	float2 inferred_scale(float2(e->get_inferred_scale())*12.5f + 50.0f);
	inferred_scale *= float2(buffer_size);
	inferred_scale /= 100.0f;
	uint2 render_buffer_size = uint2(inferred_scale);
	if(render_buffer_size.x % 2 == 1) render_buffer_size.x++;
	if(render_buffer_size.y % 2 == 1) render_buffer_size.y++;
	const uint2 final_buffer_size = uint2(buffer_size);
	
	const float aa_scale = r->get_anti_aliasing_scale(e->get_anti_aliasing());
	
	// create geometry buffer
	// note that depth must be a 2d texture, because we will read it later inside a shader
	// also: opaque gbuffer doesn't need an additional id buffer
	buffers.g_buffer[0] = r->add_buffer(render_buffer_size.x, render_buffer_size.y, targets,
										  filters, taas, wraps, wraps, internal_formats, formats,
										  types, 1, rtt::DT_TEXTURE_2D);
	if(create_alpha_buffer) {
#if !defined(A2E_IOS) // TODO: think of a workaround for this
		buffers.g_buffer[1] = r->add_buffer(render_buffer_size.x, render_buffer_size.y, targets,
											filters, taas, wraps, wraps, internal_formats, formats,
											types, 2, rtt::DT_TEXTURE_2D);
#endif
	}
	else buffers.g_buffer[1] = nullptr;
	
	// create light buffer
	buffers.l_buffer[0] = r->add_buffer(render_buffer_size.x, render_buffer_size.y, GL_TEXTURE_2D, texture_object::TF_POINT, taa, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, 2, rtt::DT_NONE);
	if(create_alpha_buffer) {
#if !defined(A2E_IOS)
		buffers.l_buffer[1] = r->add_buffer(render_buffer_size.x, render_buffer_size.y, GL_TEXTURE_2D, texture_object::TF_POINT, taa, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, 2, rtt::DT_NONE);
#endif
	}
	else buffers.l_buffer[1] = nullptr;
	
	// scene/final buffer (material pass)
	if(final_buffer_size.x == render_buffer_size.x && final_buffer_size.y == render_buffer_size.y) {
		// reuse the g-buffer depth buffer (performance and memory!)
		buffers.scene_buffer = r->add_buffer(final_buffer_size.x, final_buffer_size.y, GL_TEXTURE_2D, (aa_scale > 1.0f ? texture_object::TF_LINEAR : texture_object::TF_POINT), taa, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE, GL_RGB8, GL_RGB, GL_UNSIGNED_BYTE, 1, rtt::DT_NONE);
		buffers.scene_buffer->depth_type = rtt::DT_TEXTURE_2D;
		buffers.scene_buffer->depth_buffer = buffers.g_buffer[0]->depth_buffer;
	}
	else {
		// sadly, the depth buffer optimization can't be used here, because the buffers are of a different size
		buffers.scene_buffer = r->add_buffer(final_buffer_size.x, final_buffer_size.y, GL_TEXTURE_2D, (aa_scale > 1.0f ? texture_object::TF_LINEAR : texture_object::TF_POINT), taa, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE, GL_RGB8, GL_RGB, GL_UNSIGNED_BYTE, 1, rtt::DT_RENDERBUFFER);
	}
	
	buffers.fxaa_buffer = r->add_buffer(final_buffer_size.x, final_buffer_size.y, GL_TEXTURE_2D, texture_object::TF_LINEAR, taa, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, 1, rtt::DT_NONE);
	
#if defined(A2E_INFERRED_RENDERING_CL)
	buffers.cl_normal_nuv_buffer[0] = cl->create_ogl_image2d_buffer(opencl::BT_READ, buffers.g_buffer[0]->tex[0]);
	buffers.cl_depth_buffer[0] = cl->create_ogl_image2d_buffer(opencl::BT_READ, buffers.g_buffer[0]->depth_buffer);
	buffers.cl_light_buffer[0] = cl->create_ogl_image2d_buffer(opencl::BT_WRITE, buffers.l_buffer[0]->tex[0]);
#if !defined(A2E_IOS)
	buffers.cl_normal_nuv_buffer[1] = cl->create_ogl_image2d_buffer(opencl::BT_READ, buffers.g_buffer[1]->tex[0]);
	buffers.cl_depth_buffer[1] = cl->create_ogl_image2d_buffer(opencl::BT_READ, buffers.g_buffer[1]->depth_buffer);
	buffers.cl_light_buffer[1] = cl->create_ogl_image2d_buffer(opencl::BT_WRITE, buffers.l_buffer[1]->tex[0]);
#endif
#endif
	
	a2e_debug("g/l-buffer @%v", size2(buffers.g_buffer[0]->width,
									  buffers.g_buffer[0]->height));
	a2e_debug("scene-buffer @%v", size2(buffers.scene_buffer->width,
										buffers.scene_buffer->height));
}

bool scene::window_event_handler(EVENT_TYPE type, shared_ptr<event_object> obj) {
	if(type == EVENT_TYPE::WINDOW_RESIZE) {
		const window_resize_event& evt = (const window_resize_event&)*obj;
		recreate_buffers(frames[0], evt.size);
	}
	return true;
}

/*! draws the scene
 */
void scene::draw() {
	// don't draw anything if scene isn't enabled
	if(!enabled) return;
	
	// scene setup (run particle systems, ...)
	setup_scene();
	
	// sort transparency/alpha objects (+assign mask ids)
	sort_alpha_objects();
	
	// TODO: stereo rendering
	
	// render env probes
	if(!env_probes.empty()) {
		e->push_modelview_matrix();
		e->push_projection_matrix();
		const float3 engine_pos(*e->get_position());
		const float3 engine_rot(*e->get_rotation());
		for(const auto& probe : env_probes) {
			bool render_probe = false;
			switch (probe->freq) {
				case env_probe::PROBE_FREQUENCY::ONCE:
					if(probe->frame_counter > 0) {
						probe->frame_counter--;
						render_probe = true;
					}
					break;
				case env_probe::PROBE_FREQUENCY::EVERY_FRAME:
					render_probe = true;
					break;
				case env_probe::PROBE_FREQUENCY::NTH_FRAME:
					probe->frame_counter--;
					if(probe->frame_counter == 0) {
						probe->frame_counter = probe->frame_freq;
						render_probe = true;
					}
					break;
			}
			if(render_probe) {
				e->set_position(-probe->position.x,
								-probe->position.y,
								-probe->position.z);
				e->set_rotation(probe->rotation.x, probe->rotation.y);
				geometry_pass(probe->buffers, DRAW_MODE::ENVIRONMENT_PASS);
				light_and_material_pass(probe->buffers, DRAW_MODE::ENVIRONMENT_PASS);
			}
		}
		e->pop_modelview_matrix();
		e->pop_projection_matrix();
		e->set_position(engine_pos.x, engine_pos.y, engine_pos.z);
		e->set_rotation(engine_rot.x, engine_rot.y);
	}
	
	// render to actual scene frame buffers
	geometry_pass(frames[0]);
	light_and_material_pass(frames[0]);
}

void scene::sort_alpha_objects() {
	// sort transparency/alpha objects + assign mask ids
	
	// first, sort objects from front to back
	struct alpha_object_cmp {
		const float3 position;
		alpha_object_cmp(const float3& cam_position) : position(cam_position) {}
		bool operator()(const pair<const extbbox*, size_t>& lhs, const pair<const extbbox*, size_t>& rhs) const {
			float3 lmid((lhs.first->min + lhs.first->max) / 2.0f);
			float3 rmid((rhs.first->min + rhs.first->max) / 2.0f);
			return (position - lhs.first->pos - lmid).length() < (position - rhs.first->pos - rmid).length();
		}
	} alpha_object_cmp_obj(-(*e->get_position())); // create cmp obj and as usual, flip pos
	std::sort(sorted_alpha_objects.begin(), sorted_alpha_objects.end(), alpha_object_cmp_obj);
	
	// second, compute the transformed corners of each bbox
	const size_t obj_count = sorted_alpha_objects.size();
	float3 (*bbox_corners)[8] = new float3[obj_count][8];
	size_t cur_obj = 0;
	for(const auto& obj_iter : sorted_alpha_objects) {
		const extbbox& box = *(obj_iter.first);
		
		// compute bbox corners and transform them
		float3 corners[8] = {
			box.min,
			box.max,
			float3(box.min.x, box.min.y, box.max.z),
			float3(box.min.x, box.max.y, box.min.z),
			float3(box.max.x, box.min.y, box.min.z),
			float3(box.max.x, box.min.y, box.max.z),
			float3(box.max.x, box.max.y, box.min.z),
			float3(box.min.x, box.max.y, box.max.z),
		};
		
		for(size_t i = 0; i < 8; i++) {
			bbox_corners[cur_obj][i] = (corners[i] * box.mview) + box.pos;
		}
		cur_obj++;
	}
	
	// third, project corners onto current near plane
	ipnt (*bbox_proj)[8] = new ipnt[obj_count][8];
	int4 viewport(0, 0, (int)e->get_width(), (int)e->get_height());
	matrix4f mviewt = e->get_modelview_matrix();
	matrix4f mprojt = e->get_projection_matrix();
	for(size_t i = 0; i < obj_count; i++) {
		for(size_t j = 0; j < 8; j++) {
			bbox_proj[i][j] = core::get_2d_from_3d(bbox_corners[i][j], mviewt, mprojt, viewport);
		}
	}
	
	// fourth, check projected bbox overlap, TODO: use polygon/polygon intersection/overlap test
	// TODO: http://stackoverflow.com/questions/115426/algorithm-to-detect-intersection-of-two-rectangles
	// for the moment, do a simple rectangle/rectangle overlap test
	ipnt screen_dim(e->get_width(), e->get_height());
	ipnt (*bbox_rects)[2] = new ipnt[obj_count][2];
	for(size_t i = 0; i < obj_count; i++) {
		// compute rectangle
		bbox_rects[i][0] = ipnt(numeric_limits<int>::max()); // pmin
		bbox_rects[i][1] = ipnt(numeric_limits<int>::min()); // pmax
		for(size_t j = 0; j < 8; j++) {
			if(bbox_proj[i][j].x == numeric_limits<int>::min()) continue;
			bbox_rects[i][0] = ipnt::min(bbox_rects[i][0], bbox_proj[i][j]);
			bbox_rects[i][1] = ipnt::max(bbox_rects[i][1], bbox_proj[i][j]);
		}
		if(bbox_rects[i][0].x == numeric_limits<int>::max() || bbox_rects[i][1].x == numeric_limits<int>::min()) {
			// invisible
			continue;
		}
		
		// clamp to screen
		bbox_rects[i][0].clamp(ipnt(0, 0), screen_dim);
		bbox_rects[i][1].clamp(ipnt(0, 0), screen_dim);
	}
	for(size_t i = 0; i < obj_count; i++) {
		if(bbox_rects[i][0].x == numeric_limits<int>::max() || bbox_rects[i][1].x == numeric_limits<int>::min()) {
			sorted_alpha_objects[i].second = numeric_limits<int>::max();
			continue;
		}
		
		vector<size_t> intersections;
		for(size_t j = 0; j < obj_count; j++) {
			if(i == j) continue;
			if(bbox_rects[i][0].x < bbox_rects[j][1].x &&
			   bbox_rects[i][1].x > bbox_rects[j][0].x &&
			   bbox_rects[i][0].y < bbox_rects[j][1].y &&
			   bbox_rects[i][1].y > bbox_rects[j][0].y) {
				intersections.push_back(j);
			}
		}
		
		if(intersections.size() == 0) {
			// if we have no intersections, simply assign mask id #1
			sorted_alpha_objects[i].second = 1;
		}
		else {
			// else, check which bbox is the closest and assign ids
			// TODO: dependency list + resolving?
			size_t mask_id = 1;
			for(const auto& inter_iter : intersections) {
				if(inter_iter < i) mask_id++;
				else if(inter_iter > i) {
					sorted_alpha_objects[i].second = std::min(mask_id, (size_t)A2E_MAX_MASK_ID);
					break;
				}
			}
		}
	}
	
	// cleanup
	delete [] bbox_corners;
	delete [] bbox_proj;
	delete [] bbox_rects;
}
void scene::add_alpha_object(const extbbox* bbox, const size_t& sub_object_id, a2emodel::draw_callback* cb) {
	delete_alpha_object(bbox); // clean up old data if there is any
	alpha_objects[bbox] = make_pair(sub_object_id, cb);
	sorted_alpha_objects.push_back(make_pair(bbox, 0));
}

void scene::add_alpha_objects(const size_t count, const extbbox** bboxes, const size_t* sub_object_ids, a2emodel::draw_callback* cbs) {
	// TODO: improve this?
	for(size_t i = 0; i < count; i++) {
		add_alpha_object(bboxes[i], sub_object_ids[i], &cbs[i]);
	}
}

void scene::delete_alpha_object(const extbbox* bbox) {
	if(alpha_objects.count(bbox) != 0) {
		delete alpha_objects[bbox].second; // delete functor
		alpha_objects.erase(bbox);
		// O(n) ... TODO: improve this
		for(auto objiter = sorted_alpha_objects.begin(); objiter != sorted_alpha_objects.end(); objiter++) {
			if(objiter->first == bbox) {
				sorted_alpha_objects.erase(objiter);
				break;
			}
		}
	}
}

void scene::delete_alpha_objects(const size_t count, const extbbox** bboxes) {
	// TODO: improve this?
	for(size_t i = 0; i < count; i++) {
		delete_alpha_object(bboxes[i]);
	}
}

void scene::setup_scene() {
	// run particle managers
	for(const auto& pm : particle_managers) {
		pm->run();
	}
}

/*! starts drawing the scene
 */
void scene::geometry_pass(frame_buffers& buffers, const DRAW_MODE draw_mode_or_mask) {
	const DRAW_MODE geom_pass_masked = (DRAW_MODE)((unsigned int)DRAW_MODE::GEOMETRY_PASS | (unsigned int)draw_mode_or_mask);
	const DRAW_MODE geom_alpha_pass_masked = (DRAW_MODE)((unsigned int)DRAW_MODE::GEOMETRY_ALPHA_PASS | (unsigned int)draw_mode_or_mask);
	
	// normal rendering using a fbo
	r->start_draw(buffers.g_buffer[0]);
	r->clear();
	
#if !defined(A2E_IOS)
	static const GLenum draw_buffers[] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
	glDrawBuffers(1, draw_buffers);
#endif
	
	// render models (opaque)
	for(const auto& iter : models) {
		if(iter->get_visible()) iter->draw(geom_pass_masked);
	}
	
	// render skybox
	if(render_skybox) {
		// TODO: GL3 (-> freealbion code)
	}

	// render physical objects
	for(const auto& model : models) {
		if(model->get_draw_phys_obj()) model->draw_phys_obj();
	}
	
	// render callbacks (opaque)
	for(const auto& draw_cb : draw_callbacks) {
		(*draw_cb.second)(geom_pass_masked);
	}
	
	r->stop_draw();
	
	if(buffers.g_buffer[1] != nullptr) {
#if !defined(A2E_IOS)
		// render models (transparent/alpha)
		r->start_draw(buffers.g_buffer[1]);
		r->clear();
		glDrawBuffers(2, draw_buffers);
		
		for(auto iter = sorted_alpha_objects.crbegin(); iter != sorted_alpha_objects.crend(); iter++) {
			const pair<size_t, a2emodel::draw_callback*>& obj = alpha_objects[iter->first];
			(*obj.second)(geom_alpha_pass_masked, obj.first, iter->second);
		}
		
		// render callbacks (alpha)
		for(const auto& draw_cb : draw_callbacks) {
			(*draw_cb.second)(geom_alpha_pass_masked);
		}
		
		r->stop_draw();
#else
		// TODO: render transparent models in iOS/GLES2.0
#endif
	}
}

void scene::light_and_material_pass(frame_buffers& buffers, const DRAW_MODE draw_mode_or_mask) {
	//
	rtt::fbo* scene_buffer = buffers.scene_buffer;
	rtt::fbo* fxaa_buffer = buffers.fxaa_buffer;
	rtt::fbo* l_buffer = buffers.l_buffer[0];
#if defined(A2E_INFERRED_RENDERING_CL)
	opencl::buffer_object** cl_normal_nuv_buffer = buffers.cl_normal_nuv_buffer;
	opencl::buffer_object** cl_depth_buffer = buffers.cl_depth_buffer;
	opencl::buffer_object** cl_light_buffer = buffers.cl_light_buffer;
#endif
	
	// some parameters required both by the shader and the opencl version
	// compute projection constants (necessary to reconstruct world pos)
	const float2 near_far_plane = e->get_near_far_plane();
	const float2 projection_ab = float2(near_far_plane.y / (near_far_plane.y - near_far_plane.x),
										(-near_far_plane.y * near_far_plane.x) / (near_far_plane.y - near_far_plane.x));
	const float3 cam_position = -float3(*e->get_position());
	const float2 screen_size = float2(float(l_buffer->width), float(l_buffer->height));
#if !defined(A2E_IOS)
	const bool light_alpha_objects = (!alpha_objects.empty() &&
									  buffers.l_buffer[0] != nullptr);
#else
	const bool light_alpha_objects = false; // TODO: iOS: implement this!
#endif
	
	// TODO: cleanup
	const matrix4f projection_matrix = *e->get_projection_matrix();
	const matrix4f modelview_matrix = *e->get_modelview_matrix();
	const matrix4f mvpm = modelview_matrix * projection_matrix;
	const matrix4f inv_modelview_matrix = matrix4f(modelview_matrix).invert();
	
#if !defined(A2E_INFERRED_RENDERING_CL)
	/////////////////////////////////////////////////////
	// light pass - using shaders
	// light 1st: opaque geometry, 2nd: alpha geometry
	
	// set blend mode (add all light results)
	glEnable(GL_BLEND);
	glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_COLOR);
	
	for(size_t light_pass = 0; light_pass < (light_alpha_objects ? 2 : 1); light_pass++) {
		r->start_draw(buffers.l_buffer[light_pass]);
		r->clear();
		static const GLenum draw_buffers[] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
		glDrawBuffers(2, draw_buffers);
		
		// shader init
		//gl3shader ir_lighting = s->get_gl3shader("IR_LP_PHONG");
		gl3shader ir_lighting = s->get_gl3shader("IR_LP_ASHIKHMIN_SHIRLEY");
		for(size_t light_type = 0; light_type < 2; light_type++) {
			if(light_type == 0) {
				ir_lighting->use("#");
				ir_lighting->uniform("mvpm", mvpm);
			}
			else if(light_type == 1) ir_lighting->use("directional");
			
			ir_lighting->uniform("imvm", inv_modelview_matrix);
			ir_lighting->uniform("cam_position", cam_position);
			ir_lighting->uniform("screen_size", screen_size);
			ir_lighting->uniform("projection_ab", projection_ab);
			ir_lighting->texture("normal_nuv_buffer",
								 buffers.g_buffer[light_pass]->tex[0], GL_TEXTURE_2D);
			
			//
			ir_lighting->texture("depth_buffer", buffers.g_buffer[light_pass]->depth_buffer, GL_TEXTURE_2D);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
			
			// first: all point and spot (TODO) lights
			if(light_type == 0) {
				for(const auto& li : lights) {
					if(!li->is_enabled()) continue;
					if(li->get_type() != light::LT_POINT) continue;
					
					// TODO: light visibility test? (not visible if: player !facing light && dist(player, light) > r)
					const float radius = li->get_radius();
					//const float half_far_plane = e->get_near_far_plane().y - 0.1f;
					//const float ls_radius = (radius < 0.0f || radius > half_far_plane ?
					//						 half_far_plane : radius); // clamp to far plane - small offset
					// radius + near plane, b/c we have to account for that too
					const float light_dist = (cam_position - li->get_position()).length() - e->get_near_far_plane().x;
					//if(light_dist <= ls_radius) {
					if(light_dist <= radius) {
						glFrontFace(GL_CW);
						glDepthFunc(GL_GREATER);
					}
					else {
						glFrontFace(GL_CCW);
						glDepthFunc(GL_LEQUAL);
					}
					
					ir_lighting->uniform("light_position", float4(li->get_position(), li->get_radius()));
					ir_lighting->uniform("light_color", float4(li->get_color(), li->get_inv_sqr_radius()));
					ir_lighting->attribute_array("in_vertex", light_sphere->get_vbo_vertices(), 3, GL_FLOAT);
					
					glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, light_sphere->get_vbo_indices(0));
					glDrawElements(GL_TRIANGLES, (GLsizei)light_sphere->get_index_count(0) * 3, GL_UNSIGNED_INT, nullptr);
					
				}
				glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
			}
			// second: all directional lights
			else if(light_type == 1) {
				glFrontFace(GL_CCW);
				for(const auto& li : lights) {
					if(!li->is_enabled()) continue;
					if(li->get_type() != light::LT_DIRECTIONAL) continue;
					
					ir_lighting->uniform("light_position", float4(li->get_position(), 0.0f));
					ir_lighting->uniform("light_color", float4(li->get_color(), 0.0f));
					ir_lighting->uniform("light_ambient", float4(li->get_ambient(), 0.0f));
					
					ir_lighting->attribute_array("in_vertex", gfx2d::get_fullscreen_quad_vbo(), 2, GL_FLOAT);
					glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
				}
			}
			
			ir_lighting->disable();
		}
		
		// done
		r->stop_draw();
	}
	
	glDepthFunc(GL_LEQUAL);
	glDisable(GL_BLEND);
	
#else // defined(A2E_INFERRED_RENDERING_CL)
	/////////////////////////////////////////////////////
#endif
	
	/////////////////////////////////////////////////////
	// model material pass
	const DRAW_MODE mat_pass_masked = (DRAW_MODE)((unsigned int)DRAW_MODE::MATERIAL_PASS | (unsigned int)draw_mode_or_mask);
	const DRAW_MODE mat_alpha_pass_masked = (DRAW_MODE)((unsigned int)DRAW_MODE::MATERIAL_ALPHA_PASS | (unsigned int)draw_mode_or_mask);
	
	// note: this reuses the g-buffer depth buffer
	// -> anything that has not equal depth will be culled/discarded early
	// (this gives a nice speed boost and also saves memory)
	r->start_draw(scene_buffer);
	if(scene_buffer->depth_type == rtt::DT_TEXTURE_2D) {
		r->clear(GL_COLOR_BUFFER_BIT); // only clear color, keep depth
		glDepthFunc(GL_EQUAL);
	}
	else r->clear();
	
	// render models (opaque)
	for(const auto& iter : models) {
		if(iter->get_visible()) {
			iter->set_ir_buffers(buffers.g_buffer[0], buffers.l_buffer[0],
								 buffers.g_buffer[1], buffers.l_buffer[1]);
			iter->draw(mat_pass_masked);
		}
	}
	
	// render callbacks (opaque pass)
	for(const auto& draw_cb : draw_callbacks) {
		(*draw_cb.second)(mat_pass_masked);
	}
	
	// for alpha objects and particles rendering, switch back to LEQUAL,
	// b/c they are not contained in the current depth buffer
	glDepthFunc(GL_LEQUAL);
	
#if !defined(A2E_IOS)
	if(sorted_alpha_objects.size() > 0 || draw_callbacks.size() > 0) {
		// render models (transparent/alpha)
		glEnable(GL_BLEND);
		glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA); // pre-multiplied alpha blending
		for(auto iter = sorted_alpha_objects.crbegin(); iter != sorted_alpha_objects.crend(); iter++) {
			const pair<size_t, a2emodel::draw_callback*>& obj = alpha_objects[iter->first];
			(*obj.second)(mat_alpha_pass_masked, obj.first, iter->second);
		}
		// render callbacks (alpha pass)
		for(const auto& draw_cb : draw_callbacks) {
			(*draw_cb.second)(mat_alpha_pass_masked);
		}
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glDisable(GL_BLEND);
	}
#else
	// TODO: render transparent models in iOS/GLES2.0
#endif
	
	// render/draw particle managers
	for(const auto& pm : particle_managers) {
		pm->draw(buffers.g_buffer[0]);
	}
	
	r->stop_draw();
	
	// FXAA
	const auto cur_aa = e->get_anti_aliasing();
	const bool is_fxaa = (cur_aa == rtt::TAA_FXAA ||
						  cur_aa == rtt::TAA_SSAA_4_3_FXAA ||
						  cur_aa == rtt::TAA_SSAA_2_FXAA);
	const bool is_post_processing = !pp_handlers.empty();
	
	if(is_fxaa) {
		//
		r->start_draw(fxaa_buffer);
		r->start_2d_draw();
		
		gl3shader luma_shd = s->get_gl3shader("LUMA");
		luma_shd->texture("src_buffer", scene_buffer->tex[0]);
		gfx2d::draw_fullscreen_triangle();
		luma_shd->disable();
		
		r->stop_2d_draw();
		r->stop_draw();
		
		// render to scene buffer again (-> correct filtering!)
		r->start_draw(scene_buffer);
		r->start_2d_draw();
		
		gl3shader fxaa_shd = s->get_gl3shader("FXAA");
		fxaa_shd->texture("src_buffer", fxaa_buffer->tex[0]);
		fxaa_shd->uniform("texel_size",
						  float2(1.0f) / float2(fxaa_buffer->width, fxaa_buffer->height));
		
		glFrontFace(GL_CW);
		gfx2d::draw_fullscreen_triangle();
		glFrontFace(GL_CCW);
		
		fxaa_shd->disable();
		
		r->stop_2d_draw();
		r->stop_draw();
	}
	
	// apply post processing
	if(is_post_processing) {
		for(const auto& pph : pp_handlers) {
			(*pph)(scene_buffer);
		}
	}
}

/*! adds a model to the scene
 *  @param model pointer to the model
 */
void scene::add_model(a2emodel* model) {
	models.push_back(model);
}

/*! removes a model from the scene
 *  @param model pointer to the model
 */
void scene::delete_model(a2emodel* model) {
	const auto iter = find(models.begin(), models.end(), model);
	if(iter == models.end()) {
		a2e_error("can't delete model: model doesn't exist!");
		return;
	}
	models.erase(iter);
}

/*! adds a light to the scene
 *  @param new_light pointer to the light
 */
void scene::add_light(light* new_light) {
	// check if light already exists ...
	const auto iter = find(lights.begin(), lights.end(), new_light);
	if(iter != lights.end()) {
		a2e_error("light already exists in this scene!");
		return;
	}
	lights.push_back(new_light);
}

/*! delets a light of the scene
 *  @param del_light pointer to the light
 */
void scene::delete_light(light* del_light) {
	lights.erase(remove(lights.begin(), lights.end(), del_light), end(lights));
}

/*! sets the skybox texture
 *  @param tex the texture
 */
void scene::set_skybox_texture(a2e_texture tex) {
	skybox_tex = tex;
}

/*! returns the skybox texture
 */
const a2e_texture& scene::get_skybox_texture() const {
	return skybox_tex;
}

/*! sets the flag if a skybox is rendered
 *  @param state the new state
 */
void scene::set_render_skybox(const bool state) {
	render_skybox = state;
}

/*! returns the render skybox flag
 */
bool scene::get_render_skybox() const {
	return render_skybox;
}

/*! scene draw postprocessing
 */
void scene::postprocess() {
	// TODO: hdr rendering
	
	// normal rendering using a fbo
	r->stop_draw();
}

void scene::set_eye_distance(const float& distance) {
	eye_distance = distance;
}

const float& scene::get_eye_distance() const {
	return eye_distance;
}

void scene::add_particle_manager(particle_manager* pm) {
	particle_managers.push_back(pm);
}

void scene::delete_particle_manager(particle_manager* pm) {
	particle_managers.erase(remove(particle_managers.begin(), particle_managers.end(), pm), end(particle_managers));
}

void scene::add_post_processing(post_processing_handler* pph) {
	if(pph == nullptr) return;
	const auto iter = find(pp_handlers.begin(), pp_handlers.end(), pph);
	if(iter != pp_handlers.end()) {
		return; // already in container
	}
	pp_handlers.push_back(pph);
}

void scene::delete_post_processing(const post_processing_handler* pph) {
	if(pph == nullptr) return;
	const auto iter = find(pp_handlers.begin(), pp_handlers.end(), pph);
	if(iter == pp_handlers.end()) {
		return; // already in container
	}
	pp_handlers.erase(iter);
}

void scene::set_enabled(const bool& status) {
	if(status != enabled && !status) {
		// clear scene buffers (so they are fully transparent)
		for(size_t i = 0; i < A2E_CONCURRENT_FRAMES; i++) {
			r->start_draw(frames[i].scene_buffer);
			r->clear();
			r->stop_draw();
		}
	}
	enabled = status;
}

bool scene::is_enabled() const {
	return enabled;
}

void scene::add_draw_callback(const string& name, draw_callback& cb) {
	if(draw_callbacks.count(name) > 0) {
		a2e_error("this scene draw callback already exists!");
		return;
	}
	//draw_callbacks.emplace(name, &cb); // TODO: use this, when gcc finally decides to correctly implement c++11
	draw_callbacks.insert(make_pair(name, &cb));
}

void scene::delete_draw_callback(draw_callback& cb) {
	for(auto iter = draw_callbacks.begin(); iter != draw_callbacks.end(); iter++) {
		if(iter->second == &cb) {
			draw_callbacks.erase(iter);
			return;
		}
	}
	a2e_error("no such scene draw callback does exist!");
}

void scene::delete_draw_callback(const string& name) {
	const auto iter = draw_callbacks.find(name);
	if(iter == draw_callbacks.end()) {
		a2e_error("no such scene draw callback does exist!");
		return;
	}
	draw_callbacks.erase(iter);
}

scene::env_probe* scene::add_environment_probe(const float3& pos, const float2& rot, const size2 buffer_size, const bool capture_alpha) {
	const size2 dual_buffer_size(buffer_size.x * 2, buffer_size.y);
	env_probe* probe = new env_probe(pos, rot, dual_buffer_size, capture_alpha);
	recreate_buffers(probe->buffers, probe->buffer_size, probe->capture_alpha);
	add_environment_probe(probe);
	return probe;
}

void scene::add_environment_probe(scene::env_probe* probe) {
	if(probe == nullptr) return;
	env_probes.insert(probe);
}

void scene::delete_environment_probe(env_probe* probe) {
	delete_buffers(probe->buffers);
	env_probes.erase(probe);
	if(probe != nullptr) delete probe;
}

scene::env_probe::env_probe(const float3& pos_, const float2& rot_, const size2 buffer_size_, const bool capture_alpha_) :
position(pos_), rotation(rot_), buffer_size(buffer_size_), capture_alpha(capture_alpha_)
{
}

scene::env_probe::~env_probe() {
}

const vector<a2emodel*>& scene::get_models() const {
	return models;
}

const vector<light*>& scene::get_lights() const {
	return lights;
}

const vector<particle_manager*>& scene::get_particle_managers() const {
	return particle_managers;
}

const set<scene::env_probe*>& scene::get_env_probes() const {
	return env_probes;
}
