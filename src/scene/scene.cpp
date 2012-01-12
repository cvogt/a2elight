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

static opencl* _cl = NULL;
static map<string, size_t> _timer_bucket;
static map<string, size_t> _timer_bucket_count;
void _start_timer();
void _stop_timer(const string& name);

#if 1
void _start_timer() {
}
void _stop_timer(const string& name) {
}
#endif

/*! scene constructor
 */
scene::scene(engine* e_) {
	scene::is_light = false;
	
	_dbg_projs = NULL;
	_dbg_proj_count = 0;
	
	_cl = e_->get_opencl();
	
	//
	blur_buffer1 = NULL;
	blur_buffer2 = NULL;
	blur_buffer3 = NULL;
	average_buffer = NULL;
	exposure_buffer[0] = NULL;
	exposure_buffer[1] = NULL;

	// get classes
	scene::e = e_;
	scene::s = e->get_shader();
	scene::exts = e->get_ext();
	scene::r = e->get_rtt();
	scene::g = e->get_gfx();
	scene::cl = e->get_opencl();

	stereo = e->get_stereo();
	eye_distance = -0.3f; // 1.5f?

	if(e->get_init_mode() == engine::GRAPHICAL) {
		scene::skybox_tex = 0;
		scene::max_value = 0.0f;
		scene::render_skybox = false;

		cur_exposure = 0;
		fframe_time = 0.0f;
		iframe_time = SDL_GetTicks();

		float screen_w = (float)e->get_width();
		float screen_h = (float)e->get_height();
		unsigned int q = 5;
		float xInc = 1.0f / screen_w;
		float yInc = 1.0f / screen_h;
		tcs_line_h = new float[q*2];
		tcs_line_v = new float[q*2];
		// h
		for(unsigned int i = 0; i < q; i++) {
			tcs_line_h[i*2+0] = (-2.0f * xInc) + ((float)i * xInc);
			tcs_line_h[i*2+1] = 0.0f;
		}
		// v
		for(unsigned int i = 0; i < q; i++) {
			tcs_line_v[i*2+0] = 0.0f;
			tcs_line_v[i*2+1] = (-2.0f * yInc) + ((float)i * yInc);
		}
		
		// create buffers used for inferred rendering
		
		// note: opaque and alpha/transparent geometry have their own buffers
		// use 1 + 2 16-bit RGBA float buffers for the g-buffer: normal+Nuv (both) and DSF (alpha only)
		// use 1 + 1 16-bit RGBA float buffer for the l-buffer
		// use 1 8-bit RGBA ubyte buffer for the (intermediate) fxaa buffer
		// use 1 8-bit RGBA ubyte buffer for the final buffer
		
		// TODO: add 4xSSAA support -> downscale in shader (+enable in engine)
		
		// figure out the target type (multi-sampled or not)
		GLenum target = GL_TEXTURE_2D;
		
		// figure out the best 16-bit float format
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
		inferred_scale *= float2(e->get_width(), e->get_height());
		inferred_scale /= 100.0f;
		uint2 render_buffer_size = uint2(inferred_scale);
		if(render_buffer_size.x % 2 == 1) render_buffer_size.x++;
		if(render_buffer_size.y % 2 == 1) render_buffer_size.y++;
		const uint2 final_buffer_size = uint2(e->get_width(), e->get_height());
		
		const float aa_scale = r->get_anti_aliasing_scale(e->get_anti_aliasing());
		
		cur_frame = 0;
		for(size_t i = 0; i < A2E_CONCURRENT_FRAMES; i++) {
			// create geometry buffer
			// note that depth must be a 2d texture, because we will read it later inside a shader
			// also: opaque gbuffer doesn't need an additional id buffer
			frames[i].g_buffer[0] = r->add_buffer(render_buffer_size.x, render_buffer_size.y, targets,
												  filters, taas, wraps, wraps, internal_formats, formats,
												  types, 1, rtt::DT_TEXTURE_2D);
			frames[i].g_buffer[1] = r->add_buffer(render_buffer_size.x, render_buffer_size.y, targets,
												  filters, taas, wraps, wraps, internal_formats, formats,
												  types, 2, rtt::DT_TEXTURE_2D);
			
			// create light buffer
			frames[i].l_buffer[0] = r->add_buffer(render_buffer_size.x, render_buffer_size.y, GL_TEXTURE_2D, texture_object::TF_POINT, taa, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, 1, rtt::DT_NONE);
			frames[i].l_buffer[1] = r->add_buffer(render_buffer_size.x, render_buffer_size.y, GL_TEXTURE_2D, texture_object::TF_POINT, taa, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, 1, rtt::DT_NONE);
			
			// scene/final buffer (material pass)
			if(final_buffer_size.x == render_buffer_size.x && final_buffer_size.y == render_buffer_size.y) {
				// reuse the g-buffer depth buffer (performance and memory!)
				frames[i].scene_buffer = r->add_buffer(final_buffer_size.x, final_buffer_size.y, GL_TEXTURE_2D, (aa_scale > 1.0f ? texture_object::TF_LINEAR : texture_object::TF_POINT), taa, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE, GL_RGB8, GL_RGB, GL_UNSIGNED_BYTE, 1, rtt::DT_NONE);
				frames[i].scene_buffer->depth_type = rtt::DT_TEXTURE_2D;
				frames[i].scene_buffer->depth_buffer = frames[i].g_buffer[0]->depth_buffer;
			}
			else {
				// sadly, the depth buffer optimization can't be used here, because the buffers are of a different size
				frames[i].scene_buffer = r->add_buffer(final_buffer_size.x, final_buffer_size.y, GL_TEXTURE_2D, (aa_scale > 1.0f ? texture_object::TF_LINEAR : texture_object::TF_POINT), taa, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE, GL_RGB8, GL_RGB, GL_UNSIGNED_BYTE, 1, rtt::DT_RENDERBUFFER);
			}
			
			frames[i].fxaa_buffer = r->add_buffer(final_buffer_size.x, final_buffer_size.y, GL_TEXTURE_2D, texture_object::TF_LINEAR, taa, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, 1, rtt::DT_NONE);
			
#if defined(A2E_INFERRED_RENDERING_CL)
			frames[i].cl_normal_nuv_buffer[0] = cl->create_ogl_image2d_buffer(opencl::BT_READ, frames[i].g_buffer[0]->tex_id[0]);
			frames[i].cl_depth_buffer[0] = cl->create_ogl_image2d_buffer(opencl::BT_READ, frames[i].g_buffer[0]->depth_buffer);
			frames[i].cl_light_buffer[0] = cl->create_ogl_image2d_buffer(opencl::BT_WRITE, frames[i].l_buffer[0]->tex_id[0]);
			frames[i].cl_normal_nuv_buffer[1] = cl->create_ogl_image2d_buffer(opencl::BT_READ, frames[i].g_buffer[1]->tex_id[0]);
			frames[i].cl_depth_buffer[1] = cl->create_ogl_image2d_buffer(opencl::BT_READ, frames[i].g_buffer[1]->depth_buffer);
			frames[i].cl_light_buffer[1] = cl->create_ogl_image2d_buffer(opencl::BT_WRITE, frames[i].l_buffer[1]->tex_id[0]);
#endif
		}
		
		a2e_debug("g/l-buffer @%v", size2(frames[0].g_buffer[0]->width,
										  frames[0].g_buffer[0]->height));
		a2e_debug("scene-buffer @%v", size2(frames[0].scene_buffer->width,
											frames[0].scene_buffer->height));

		// load light objects/models
		// TODO: !!! use simpler model!
		light_sphere = (a2estatic*)create_a2emodel<a2estatic>();
		light_sphere->load_model(e->data_path("light_sphere.a2m"));
	}
}

/*! scene destructor
 */
scene::~scene() {
	a2e_debug("deleting scene object");

	a2e_debug("deleting models and lights");
	models.clear();
	lights.clear();

	// if hdr is supported, than fbo's are supported too, so we don't need an extra delete in the "hdr fbo delete branch"
	if(e->get_init_mode() == engine::GRAPHICAL) {
		for(size_t i = 0; i < A2E_CONCURRENT_FRAMES; i++) {
			if(frames[i].scene_buffer != NULL) r->delete_buffer(frames[i].scene_buffer);
			if(frames[i].fxaa_buffer != NULL) r->delete_buffer(frames[i].fxaa_buffer);
			if(frames[i].g_buffer[0] != NULL) r->delete_buffer(frames[i].g_buffer[0]);
			if(frames[i].l_buffer[0] != NULL) r->delete_buffer(frames[i].l_buffer[0]);
			if(frames[i].g_buffer[1] != NULL) r->delete_buffer(frames[i].g_buffer[1]);
			if(frames[i].l_buffer[1] != NULL) r->delete_buffer(frames[i].l_buffer[1]);
			
#if defined(A2E_INFERRED_RENDERING_CL)
			if(frames[i].cl_normal_nuv_buffer[0] != NULL) cl->delete_buffer(frames[i].cl_normal_nuv_buffer[0]);
			if(frames[i].cl_depth_buffer[0] != NULL) cl->delete_buffer(frames[i].cl_depth_buffer[0]);
			if(frames[i].cl_light_buffer[0] != NULL) cl->delete_buffer(frames[i].cl_light_buffer[0]);
			if(frames[i].cl_normal_nuv_buffer[1] != NULL) cl->delete_buffer(frames[i].cl_normal_nuv_buffer[1]);
			if(frames[i].cl_depth_buffer[1] != NULL) cl->delete_buffer(frames[i].cl_depth_buffer[1]);
			if(frames[i].cl_light_buffer[1] != NULL) cl->delete_buffer(frames[i].cl_light_buffer[1]);
#endif
		}
		
		if(blur_buffer1 != NULL) r->delete_buffer(blur_buffer1);
		if(blur_buffer2 != NULL) r->delete_buffer(blur_buffer2);
		if(blur_buffer3 != NULL) r->delete_buffer(blur_buffer3);
		if(average_buffer != NULL) r->delete_buffer(average_buffer);
		if(exposure_buffer[0] != NULL) r->delete_buffer(exposure_buffer[0]);
		if(exposure_buffer[1] != NULL) r->delete_buffer(exposure_buffer[1]);
		
		delete light_sphere;
	}

	delete [] tcs_line_h;
	delete [] tcs_line_v;

	a2e_debug("scene object deleted");
}

/*! draws the scene
 */
void scene::draw() {
	//cout << "######" << endl;
	
	_start_timer();
	// scene setup (lights, run particle systems, ...)
	setup_scene();
	_stop_timer("setup");
	
	_start_timer();
	// sort transparency/alpha objects (+assign mask ids)
	sort_alpha_objects();
	_stop_timer("sorting");
	
	// TODO: stereo rendering
	
	_start_timer();
	geometry_pass();
	_stop_timer("geometry pass");
	
	light_and_material_pass();
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
	for(auto obj_iter = sorted_alpha_objects.begin(); obj_iter != sorted_alpha_objects.end(); obj_iter++, cur_obj++) {
		const extbbox& box = *(obj_iter->first);
		
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
			for(auto inter_iter = intersections.begin(); inter_iter != intersections.end(); inter_iter++) {
				if(*inter_iter < i) mask_id++;
				else if(*inter_iter > i) {
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
void scene::add_alpha_object(const extbbox* bbox, const size_t& sub_object_id, draw_callback* cb) {
	delete_alpha_object(bbox); // clean up old data if there is any
	alpha_objects[bbox] = make_pair(sub_object_id, cb);
	sorted_alpha_objects.push_back(make_pair(bbox, 0));
}

void scene::add_alpha_objects(const size_t count, const extbbox** bboxes, const size_t* sub_object_ids, draw_callback* cbs) {
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
	for(auto& pm : particle_managers) {
		pm->run();
	}
}

/*! starts drawing the scene
 */
void scene::geometry_pass() {
	// normal rendering using a fbo
	r->start_draw(frames[0].g_buffer[0]);
	r->clear();
	
	static const GLenum draw_buffers[] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
	glDrawBuffers(1, draw_buffers);
	
	// render models (opaque)
	for(auto iter = models.begin(); iter != models.end(); iter++) {
		if((*iter)->get_visible()) (*iter)->draw(DRAW_MODE::GEOMETRY_PASS);
	}
	
	// render skybox
	if(render_skybox) {
		// TODO: GL3 (-> freealbion code)
	}

	// render physical objects
	for(auto& model : models) {
		if(model->get_draw_phys_obj()) model->draw_phys_obj();
	}
	
	// render callbacks (opaque)
	for(map<string, draw_handler*>::iterator draw_iter = draw_callbacks.begin(); draw_iter != draw_callbacks.end(); draw_iter++) {
		draw_iter->second->draw(DRAW_MODE::GEOMETRY_PASS);
	}
	
	// render/draw particle managers (TODO: PARTICLE TODO)
	/*for(auto& pm : particle_managers) {
		pm->draw();
	}*/
	
	r->stop_draw();
	
	// render models (transparent/alpha)
	r->start_draw(frames[0].g_buffer[1]);
	r->clear();
	glDrawBuffers(2, draw_buffers);
	
	for(auto iter = sorted_alpha_objects.rbegin(); iter != sorted_alpha_objects.rend(); iter++) {
		const pair<size_t, draw_callback*>& obj = alpha_objects[iter->first];
		(*obj.second)(DRAW_MODE::GEOMETRY_ALPHA_PASS, obj.first, iter->second);
	}
	
	// render callbacks (alpha)
	for(map<string, draw_handler*>::iterator draw_iter = draw_callbacks.begin(); draw_iter != draw_callbacks.end(); draw_iter++) {
		draw_iter->second->draw(DRAW_MODE::GEOMETRY_ALPHA_PASS);
	}
	
	r->stop_draw();
}

void scene::light_and_material_pass() {
	_start_timer();
	
	//
	rtt::fbo* scene_buffer = frames[0].scene_buffer;
	rtt::fbo* fxaa_buffer = frames[0].fxaa_buffer;
	rtt::fbo* l_buffer = frames[0].l_buffer[0];
#if defined(A2E_INFERRED_RENDERING_CL)
	opencl::buffer_object** cl_normal_nuv_buffer = frames[0].cl_normal_nuv_buffer;
	opencl::buffer_object** cl_depth_buffer = frames[0].cl_depth_buffer;
	opencl::buffer_object** cl_light_buffer = frames[0].cl_light_buffer;
#endif
	
	// some parameters required both by the shader and the opencl version
	// compute projection constants (necessary to reconstruct world pos)
	const float2 near_far_plane = e->get_near_far_plane();
	const float2 projection_ab = float2(near_far_plane.y / (near_far_plane.y - near_far_plane.x),
										(-near_far_plane.y * near_far_plane.x) / (near_far_plane.y - near_far_plane.x));
	const float3 cam_position = -float3(*e->get_position());
	const float2 screen_size = float2(float(l_buffer->width), float(l_buffer->height));
	const bool light_alpha_objects = !alpha_objects.empty();
	
	// TODO: cleanup
	const matrix4f projection_matrix = *e->get_projection_matrix();
	const matrix4f modelview_matrix = *e->get_modelview_matrix();
	const matrix4f mvpm = modelview_matrix * projection_matrix;
	const matrix4f inv_modelview_matrix = matrix4f(modelview_matrix).invert();
	
#if !defined(A2E_INFERRED_RENDERING_CL)
	/////////////////////////////////////////////////////
	// light pass - using shaders
	// light 1st: opaque geometry, 2nd: alpha geometry
	for(size_t light_pass = 0; light_pass < (light_alpha_objects ? 2 : 1); light_pass++) {
		r->start_draw(frames[0].l_buffer[light_pass]);
		r->clear();
		
		// set blend mode (add all light results)
		glEnable(GL_BLEND);
		glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_COLOR);
		
		// shader init
		gl3shader ir_lighting = s->get_gl3shader("IR_LP_PHONG");
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
								 frames[0].g_buffer[light_pass]->tex_id[0], GL_TEXTURE_2D);
			
			//
			ir_lighting->texture("depth_buffer",
								 frames[0].g_buffer[light_pass]->depth_buffer, GL_TEXTURE_2D);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
			
			// first: all point and spot (TODO) lights
			if(light_type == 0) {
				for(const auto& li : lights) {
					if(!li->is_enabled()) continue;
					if(li->get_type() != light::LT_POINT) continue;
					
					// TODO: light visibility test? (not visible if: player !facing light && dist(player, light) > r)
					
					ir_lighting->uniform("light_position", float4(li->get_position(), li->get_sqr_radius()));
					ir_lighting->uniform("light_color", float4(li->get_color(), li->get_inv_sqr_radius()));
					
					const float radius = li->get_radius();
					// TODO: add max distance config setting
					const float half_far_plane = e->get_near_far_plane().y - 0.1f;
					const float ls_radius = (radius < 0.0f || radius > half_far_plane ? half_far_plane : radius); // clamp to 499.9 (2*r=far plane)
					// radius + 1, b/c we have to account for the near plane (= 1.0)
					if(float3(cam_position - li->get_position()).length() <= (ls_radius + 1.0f)) {
						glFrontFace(GL_CW);
						glDepthFunc(GL_GREATER);
					}
					else {
						glFrontFace(GL_CCW);
						glDepthFunc(GL_LEQUAL);
					}
					
					ir_lighting->uniform("light_radius", ls_radius);
					ir_lighting->attribute_array("in_vertex", light_sphere->get_vbo_vertices(), 3, GL_FLOAT);
					
					glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, light_sphere->get_vbo_indices(0));
					
					glDrawElements(GL_TRIANGLES, (GLsizei)light_sphere->get_index_count(0) * 3, GL_UNSIGNED_INT, NULL);
					
					glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
				}
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
					
					ir_lighting->attribute_array("in_vertex", g->get_fullscreen_quad_vbo(), 2, GL_FLOAT);
					glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
				}
			}
			
			ir_lighting->disable();
		}
		glDepthFunc(GL_LEQUAL);
		glDisable(GL_BLEND);
		
		// done
		r->stop_draw();
	}
#else // defined(A2E_INFERRED_RENDERING_CL)
	/////////////////////////////////////////////////////
#endif
	_start_timer();
	
	/////////////////////////////////////////////////////
	// model material pass
	// for the moment, only render models in the scene (later: TODO: use start/stop_draw using another render buffer)
	
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
	for(auto iter = models.begin(); iter != models.end(); iter++) {
		if((*iter)->get_visible()) {
			(*iter)->set_ir_buffers(frames[0].g_buffer[0], frames[0].l_buffer[0],
									frames[0].g_buffer[1], frames[0].l_buffer[1]);
			(*iter)->draw(DRAW_MODE::MATERIAL_PASS);
		}
	}
	
	// render callbacks (opaque pass)
	for(map<string, draw_handler*>::iterator draw_iter = draw_callbacks.begin(); draw_iter != draw_callbacks.end(); draw_iter++) {
		draw_iter->second->draw(DRAW_MODE::MATERIAL_PASS);
	}
	
	// for alpha objects and particles rendering, switch back to LEQUAL,
	// b/c they are not contained in the current depth buffer
	glDepthFunc(GL_LEQUAL);
	
	if(sorted_alpha_objects.size() > 0) {
		// render models (transparent/alpha)
		glEnable(GL_BLEND);
		glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA); // pre-multiplied alpha blending
		for(auto iter = sorted_alpha_objects.rbegin(); iter != sorted_alpha_objects.rend(); iter++) {
			const pair<size_t, draw_callback*>& obj = alpha_objects[iter->first];
			(*obj.second)(DRAW_MODE::MATERIAL_ALPHA_PASS, obj.first, iter->second);
		}
		// render callbacks (alpha pass)
		for(map<string, draw_handler*>::iterator draw_iter = draw_callbacks.begin(); draw_iter != draw_callbacks.end(); draw_iter++) {
			draw_iter->second->draw(DRAW_MODE::MATERIAL_ALPHA_PASS);
		}
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glDisable(GL_BLEND);
	}
	
	// render/draw particle managers
	for(auto& pm : particle_managers) {
		pm->draw(frames[0].g_buffer[0]);
	}
	
	r->stop_draw();
	_stop_timer("material pass");
	_start_timer();
	
	// FXAA
	const auto cur_aa = e->get_anti_aliasing();
	const bool is_fxaa = (cur_aa == rtt::TAA_FXAA ||
						  cur_aa == rtt::TAA_SSAA_4_3_FXAA ||
						  cur_aa == rtt::TAA_SSAA_2_FXAA);
	const bool is_ssaa_fxaa = (cur_aa == rtt::TAA_SSAA_4_3_FXAA ||
							   cur_aa == rtt::TAA_SSAA_2_FXAA);
	if(is_fxaa) {
		//
		r->start_draw(fxaa_buffer);
		r->start_2d_draw();
		
		gl3shader luma_shd = s->get_gl3shader("LUMA");
		luma_shd->texture("src_buffer", scene_buffer->tex_id[0]);
		g->draw_fullscreen_triangle();
		luma_shd->disable();
		
		r->stop_2d_draw();
		r->stop_draw();
		
		//
		if(is_ssaa_fxaa) {
			// render to scene buffer again (-> correct filtering!)
			r->start_draw(scene_buffer);
			r->start_2d_draw();
		}
		else e->start_2d_draw();
		
		gl3shader fxaa_shd = s->get_gl3shader("FXAA");
		fxaa_shd->texture("src_buffer", fxaa_buffer->tex_id[0]);
		fxaa_shd->uniform("texel_size",
						  float2(1.0f) / float2(fxaa_buffer->width, fxaa_buffer->height));
		
		glFrontFace(GL_CW);
		g->draw_fullscreen_triangle();
		glFrontFace(GL_CCW);
		
		fxaa_shd->disable();
		
		if(is_ssaa_fxaa) {
			r->stop_2d_draw();
			r->stop_draw();
		}
		else e->stop_2d_draw();
	}
	
	if(!is_fxaa || is_ssaa_fxaa) {
		// draw to back buffer
		e->start_2d_draw();
		static const gfx::rect fs_rect(0, 0, e->get_width(), e->get_height());
		g->draw_textured_rectangle(fs_rect,
								   coord(0.0f, 1.0f), coord(1.0f, 0.0f),
								   scene_buffer->tex_id[0]);
		e->stop_2d_draw();
	}
	
	_stop_timer("fxaa+composite");
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
	// enable light automatically if we had no light before
	if(lights.size() == 0) scene::is_light = true;

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
	const auto iter = find(lights.begin(), lights.end(), del_light);
	if(iter == lights.end()) return;
	lights.erase(iter);

	// disable light automatically if there are no lights any more
	if(lights.size() == 0) scene::is_light = false;
}

/*! sets the scenes position
 *  @param x x coordinate
 *  @param y y coordinate
 *  @param z z coordinate
 */
void scene::set_position(float x, float y, float z) {
	for(auto& model : models) {
		// subtract old position and add new one
		model->set_position(model->get_position()->x - position.x + x,
							model->get_position()->y - position.y + y,
							model->get_position()->z - position.z + z);
	}
	
	position.set(x, y, z);
}

/*! sets the light flag
 *  @param state the state of the light flag we want to set
 */
void scene::set_light(bool state) {
	scene::is_light = state;
}

/*! returns the scenes position
 */
float3* scene::get_position() {
	return &position;
}

/*! returns true if the light delete flag is set
 */
bool scene::get_light() {
	return scene::is_light;
}

/*! sets the skybox texture
 *  @param tex the texture id
 */
void scene::set_skybox_texture(unsigned int tex) {
	scene::skybox_tex = tex;
	max_value = e->get_texman()->get_texture(tex)->max_value;
}

/*! returns the skybox texture id
 */
unsigned int scene::get_skybox_texture() {
	return scene::skybox_tex;
}

/*! sets the flag if a skybox is rendered
 *  @param state the new state
 */
void scene::set_render_skybox(bool state) {
	scene::render_skybox = state;
}

/*! returns the render skybox flag
 */
bool scene::get_render_skybox() {
	return scene::render_skybox;
}

/*! scene draw postprocessing
 */
void scene::postprocess() {
	// TODO: hdr rendering
	
	// normal rendering using a fbo
	r->stop_draw();
}

float scene::get_eye_distance() {
	return eye_distance;
}

void scene::set_eye_distance(float distance) {
	eye_distance = distance;
}

void scene::add_particle_manager(particle_manager* pm) {
	particle_managers.push_back(pm);
}

void scene::delete_particle_manager(particle_manager* pm) {
	const auto iter = find(particle_managers.begin(), particle_managers.end(), pm);
	if(iter == particle_managers.end()) return;
	particle_managers.erase(iter);
}