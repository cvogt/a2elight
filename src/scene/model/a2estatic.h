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

#ifndef __A2ESTATIC_H__
#define __A2ESTATIC_H__

#include "global.h"

#include "core/core.h"
#include "core/vector3.h"
#include "core/file_io.h"
#include "engine.h"
#include "scene/model/a2ematerial.h"
#include "rendering/shader.h"
#include "core/matrix4.h"
#include "scene/light.h"
#include "scene/model/a2emodel.h"

/*! @class a2estatic
 *  @brief class for loading and displaying an a2e static model
 */

class A2E_API a2estatic : public a2emodel {
public:
	a2estatic(engine* e, shader* s, scene* sce);
	virtual ~a2estatic();

	virtual void draw(const DRAW_MODE draw_mode);
	void load_model(const string& filename);
	void load_from_memory(unsigned int object_count, unsigned int vertex_count,
						  float3* vertices, coord* tex_coords,
						  unsigned int* index_count, index3** indices);
	
	void set_hard_scale(float x, float y, float z);
	void set_hard_position(float x, float y, float z);
	void scale_tex_coords(float su, float sv);

	float3* get_col_vertices();
	index3* get_col_indices();
	unsigned int get_col_vertex_count();
	unsigned int get_col_index_count();

	//
	GLuint get_vbo_vertices() const { return vbo_vertices_id; };
	GLuint get_vbo_tex_coords() const { return vbo_tex_coords_id; };
	GLuint get_vbo_indices(const size_t& sub_object) const { return vbo_indices_ids[sub_object]; };
	GLuint get_vbo_normals() const { return vbo_normals_id; };
	GLuint get_vbo_binormals() const { return vbo_binormals_id; };
	GLuint get_vbo_tangents() const { return vbo_tangents_id; };

protected:
	float3* vertices;
	coord* tex_coords;
	index3** indices;
	index3** tex_indices;
	unsigned int vertex_count;
	unsigned int tex_coord_count;
	unsigned int* index_count;
	unsigned int* min_index;
	unsigned int* max_index;
	GLuint vbo_vertices_id;
	GLuint vbo_tex_coords_id;
	GLuint* vbo_indices_ids;
	GLuint vbo_normals_id;
	GLuint vbo_binormals_id;
	GLuint vbo_tangents_id;

	float3* normals;
	float3* binormals;
	float3* tangents;

	// normal stuff
	struct nlist {
		unsigned int* obj_num;
		unsigned int* num;
		unsigned int count;
	};
	nlist* normal_list;
	
	void reorganize_model_data();

	// used for parallax mapping
	void generate_normals();
	
	virtual void pre_draw_setup(const ssize_t sub_object_num = -1);
	virtual void post_draw_setup(const ssize_t sub_object_num = -1);
	
};

#endif