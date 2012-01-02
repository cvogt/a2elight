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

#ifndef __SCENE_H__
#define __SCENE_H__

#define A2E_CONCURRENT_FRAMES 1

#include "global.h"

#include "core/core.h"
#include "engine.h"
#include "scene/model/a2estatic.h"
#include "scene/model/a2emodel.h"
#include "scene/light.h"
#include "rendering/shader.h"
#include "core/matrix4.h"
#include "rendering/rtt.h"
#include "cl/opencl.h"

/*! @class scene
 *  @brief a2e scene manager
 */

class particle_manager;

class A2E_API scene {
protected:
	// draw callback handler, used to register draw functions and let them get called by the scene render function
	class draw_handler {
	public:
		virtual void draw(const DRAW_MODE draw_mode) = 0;
		virtual ~draw_handler() {}
	};
	
	template<class C> class draw_handler_obj : public draw_handler {
	private:
		C* p_class;
		void (C::*func)(const DRAW_MODE);
	public:
		draw_handler_obj(C* p_classx, void (C::*funcx)(const DRAW_MODE)) : p_class(p_classx), func(funcx) {}
		virtual ~draw_handler_obj() {}
		
		virtual void draw(const DRAW_MODE draw_mode) {
			(p_class->*func)(draw_mode);
		}
		
		typedef void (C::*fp_func)();
		C* get_class() { return p_class; }
		fp_func get_func() { return func; }
	};
	
	map<string, draw_handler*> draw_callbacks;
	
public:
	scene(engine* e);
	~scene();

	void draw();
	
	template<typename T> T* create_a2emodel();
	void add_model(a2emodel* model);
	void delete_model(a2emodel* model);
	void add_light(light* light);
	void delete_light(light* light);
	void add_particle_manager(particle_manager* pm);
	void delete_particle_manager(particle_manager* pm);

	void set_position(float x, float y, float z);
	void set_light(bool state);

	float3* get_position();
	bool get_light();

	void set_skybox_texture(unsigned int tex);
	unsigned int get_skybox_texture();
	void set_render_skybox(bool state);
	bool get_render_skybox();
	
	template<class C> void add_draw_callback(const char* name, C* p_class, void (C::*fp_draw_callback)(const DRAW_MODE)) {
		draw_callbacks[name] = new draw_handler_obj<C>(p_class, fp_draw_callback);
	}
	void delete_draw_callback(const char* name) {
		if(draw_callbacks.count(name)) {
			delete draw_callbacks[name];
			draw_callbacks.erase(name);
		}
	}
	
	float get_eye_distance();
	void set_eye_distance(float distance);
	
	//
	void add_alpha_object(const extbbox* bbox, const size_t& sub_object_id, draw_callback* cb);
	void add_alpha_objects(const size_t count, const extbbox** bboxes, const size_t* sub_object_ids, draw_callback* cbs);
	void delete_alpha_object(const extbbox* bbox);
	void delete_alpha_objects(const size_t count, const extbbox** bboxes);
	
	// for debugging purposes:
	const rtt::fbo* _get_g_buffer(const size_t type = 0) const { return frames[cur_frame].g_buffer[type]; }
	const rtt::fbo* _get_l_buffer(const size_t type = 0) const { return frames[cur_frame].l_buffer[type]; }
	const rtt::fbo* _get_fxaa_buffer() const { return frames[cur_frame].fxaa_buffer; }
	const rtt::fbo* _get_scene_buffer() const { return frames[cur_frame].scene_buffer; }

protected:
	engine* e;
	shader* s;
	ext* exts;
	rtt* r;
	gfx* g;
	opencl* cl;
	
	void setup_scene();
	void geometry_pass();
	void light_and_material_pass();
	void postprocess();
	void sort_alpha_objects();

	// vars
	float3 position;

	set<a2emodel*> models;
	set<light*> lights;
	set<particle_manager*> particle_managers;
	
	// <bbox*, <sub-object id, draw functor*>>
	map<const extbbox*, pair<size_t, draw_callback*>> alpha_objects;
	// <bbox*, mask id>, mask id: 0 (invalid), {1, 2, 3}
	vector<pair<const extbbox*, size_t>> sorted_alpha_objects;
	ipnt** _dbg_projs;
	size_t _dbg_proj_count;

	//! specifies if lighting is enabled in this scene
	bool is_light;

	unsigned int skybox_tex;
	float max_value;
	bool render_skybox;

	a2estatic* light_sphere;

	// render and scene buffer
	struct frame_buffers {
		rtt::fbo* scene_buffer; // final output buffer
		rtt::fbo* fxaa_buffer; // fxaa
		rtt::fbo* g_buffer[2]; // opaque + alpha
		rtt::fbo* l_buffer[2]; // opaque + alpha
	
#if defined(A2E_INFERRED_RENDERING_CL)
		opencl::buffer_object* cl_normal_nuv_buffer[2];
		opencl::buffer_object* cl_depth_buffer[2];
		opencl::buffer_object* cl_light_buffer[2];
#endif
		frame_buffers() :
		scene_buffer(NULL), fxaa_buffer(NULL)
		{
			g_buffer[0] = g_buffer[1] = NULL;
			l_buffer[0] = l_buffer[1] = NULL;
#if defined(A2E_INFERRED_RENDERING_CL)
			cl_normal_nuv_buffer[0] = cl_normal_nuv_buffer[1] = NULL;
			cl_depth_buffer[0] = cl_depth_buffer[1] = NULL;
			cl_light_buffer[0] = cl_light_buffer[1] = NULL;
#endif
		}
	};
	frame_buffers frames[A2E_CONCURRENT_FRAMES];
	size_t cur_frame;

	// hdr buffer
	rtt::fbo* blur_buffer1;
	rtt::fbo* blur_buffer2;
	rtt::fbo* blur_buffer3;
	rtt::fbo* average_buffer;
	rtt::fbo* exposure_buffer[2];

	int cur_exposure;
	float fframe_time;
	int iframe_time;
	float* tcs_line_h;
	float* tcs_line_v;
	
	float eye_distance;

	bool stereo;

};

/*! creates an a2emodel object and returns it
 */
template<typename T> T* scene::create_a2emodel() {
	return new T(e, s, this);
}

#endif
