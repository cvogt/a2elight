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

#include "rtt.h"
#include "scene.h" // TODO: remove this again when cl inferred rendering is std
#include "engine.h"

const char* rtt::TEXTURE_ANTI_ALIASING_STR[] = {
	"NONE",
	"MSAA1",
	"MSAA2",
	"MSAA4",
	"MSAA8",
	"MSAA16",
	"MSAA32",
	"MSAA64",
	"CSAA8",
	"CSAA8Q",
	"CSAA16",
	"CSAA16Q",
	"CSAA32",
	"CSAA32Q",
	"2xSSAA",
	"4xSSAA",
	"FXAA",
	"4/3xSSAA + FXAA",
	"2xSSAA + FXAA",
};

/*! there is no function currently
 */
rtt::rtt(engine* e_, gfx* g_, ext* exts_, unsigned int screen_width_, unsigned int screen_height_) {
	// get classes
	rtt::e = e_;
	rtt::g = g_;
	rtt::exts = exts_;

	current_buffer = NULL;

	filter[0] = GL_NEAREST;
	filter[1] = GL_LINEAR;
	filter[2] = GL_LINEAR_MIPMAP_NEAREST;
	filter[3] = GL_LINEAR_MIPMAP_LINEAR;

	rtt::screen_width = screen_width_;
	rtt::screen_height = screen_height_;
}

/*! there is no function currently
 */
rtt::~rtt() {
	a2e_debug("deleting rtt object");

	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	for(rtt::fbo* buffer : buffers) {
		glDeleteTextures(buffer->attachment_count, buffer->tex_id);
		glDeleteFramebuffers(1, &buffer->fbo_id);
		delete buffer;
	}
	buffers.clear();

	a2e_debug("rtt object deleted");
}

rtt::fbo* rtt::add_buffer(unsigned int width, unsigned int height, GLenum target, texture_object::TEXTURE_FILTERING filtering, TEXTURE_ANTI_ALIASING taa, GLint wrap_s, GLint wrap_t, GLint internal_format, GLenum format, GLenum type, unsigned int attachment_count, rtt::DEPTH_TYPE depth_type) {
	GLenum* targets = new GLenum[attachment_count];
	texture_object::TEXTURE_FILTERING* filterings = new texture_object::TEXTURE_FILTERING[attachment_count];
	rtt::TEXTURE_ANTI_ALIASING* taas = new rtt::TEXTURE_ANTI_ALIASING[attachment_count];
	GLint* wraps_s = new GLint[attachment_count];
	GLint* wraps_t = new GLint[attachment_count];
	GLint* internal_formats = new GLint[attachment_count];
	GLenum* formats = new GLenum[attachment_count];
	GLenum* types = new GLenum[attachment_count];

	for(unsigned int i = 0; i < attachment_count; i++) {
		targets[i] = target;
		filterings[i] = filtering;
		taas[i] = taa;
		wraps_s[i] = wrap_s;
		wraps_t[i] = wrap_t;
		internal_formats[i] = internal_format;
		formats[i] = format;
		types[i] = type;
	}

	rtt::fbo* ret_buffer = add_buffer(width, height, targets, filterings, taas, wraps_s, wraps_t, internal_formats, formats, types, attachment_count, depth_type);

	delete [] targets;
	delete [] filterings;
	delete [] taas;
	delete [] wraps_s;
	delete [] wraps_t;
	delete [] internal_formats;
	delete [] formats;
	delete [] types;

	return ret_buffer;
}

rtt::fbo* rtt::add_buffer(unsigned int width, unsigned int height, GLenum* target, texture_object::TEXTURE_FILTERING* filtering, TEXTURE_ANTI_ALIASING* taa, GLint* wrap_s, GLint* wrap_t, GLint* internal_format, GLenum* format, GLenum* type, unsigned int attachment_count, rtt::DEPTH_TYPE depth_type) {
	buffers.push_back(*new rtt::fbo*());
	buffers.back() = new rtt::fbo();
	buffers.back()->width = width;
	buffers.back()->height = height;
	buffers.back()->draw_width = width;
	buffers.back()->draw_height = height;
	buffers.back()->attachment_count = attachment_count;
	buffers.back()->tex_id = new unsigned int[buffers.back()->attachment_count];
	buffers.back()->auto_mipmap = new bool[buffers.back()->attachment_count];
	buffers.back()->target = new GLenum[buffers.back()->attachment_count];
	buffers.back()->anti_aliasing = new rtt::TEXTURE_ANTI_ALIASING[buffers.back()->attachment_count];
	buffers.back()->depth_type = depth_type;
	buffers.back()->samples = 0;
	
	//
	const size_t max_tex_size = exts->get_max_texture_size();
	const float fmax_tex_size = max_tex_size;
	size2 orig_resolution = size2(width, height);
	size2 cur_resolution = orig_resolution;
	float ssaa = 0.0f;
	for(unsigned int i = 0; i < buffers.back()->attachment_count; i++) {
		buffers.back()->anti_aliasing[i] = taa[i];
		
		float ssaa_factor = get_anti_aliasing_scale(buffers.back()->anti_aliasing[i]);
		if(ssaa_factor <= 1.0f) continue;
		
		//
		// try lower ssaa setting
		float cur_ssaa_factor = ssaa_factor;
		while(cur_ssaa_factor >= 2.0f) {
			if(float(orig_resolution.x) * ssaa_factor > fmax_tex_size ||
			   float(orig_resolution.y) * ssaa_factor > fmax_tex_size) {
				cur_ssaa_factor -= 2.0f;
				continue;
			}
			else break;
		}
		
		if(cur_ssaa_factor <= 0.0f) {
			a2e_error("couldn't create a SSAA%u buffer (nor using a smaller SSAA setting)!", ssaa_factor);
			break; // break, since this won't work with any setting
		}
		
		if(cur_ssaa_factor < ssaa_factor) {
			a2e_error("couldn't create a SSAA%u buffer - using SSAA%u instead!", ssaa_factor, cur_ssaa_factor);
		}
		
		ssaa = std::max(ssaa, cur_ssaa_factor);
	}
	
	// apply ssaa
	if(ssaa > 0.0f) {
		const float2 ssaa_res = get_resolution_for_scale(ssaa, size2(width, height));
		width = (unsigned int)ssaa_res.x;
		height = (unsigned int)ssaa_res.y;
		buffers.back()->width = width;
		buffers.back()->height = height;
		buffers.back()->draw_width = width;
		buffers.back()->draw_height = height;
	}
	
	// check if a color or stencil should be created
	if(format[0] != GL_STENCIL_INDEX) {
		// color buffer
		buffers.back()->color = true;
		buffers.back()->stencil = false;
		
		glGenFramebuffers(1, &buffers.back()->fbo_id);
		glBindFramebuffer(GL_FRAMEBUFFER, buffers.back()->fbo_id);
		
		glGenTextures(attachment_count, buffers.back()->tex_id);
		for(unsigned int i = 0; i < buffers.back()->attachment_count; i++) {
			buffers.back()->target[i] = target[i];
			glBindTexture(buffers.back()->target[i], buffers.back()->tex_id[i]);
			
			glTexParameteri(buffers.back()->target[i], GL_TEXTURE_MAG_FILTER, (filtering[i] == 0 ? GL_NEAREST : GL_LINEAR));
			glTexParameteri(buffers.back()->target[i], GL_TEXTURE_MIN_FILTER, filter[filtering[i]]);
			glTexParameteri(buffers.back()->target[i], GL_TEXTURE_WRAP_S, wrap_s[i]);
			glTexParameteri(buffers.back()->target[i], GL_TEXTURE_WRAP_T, wrap_t[i]);
			if(exts->is_anisotropic_filtering_support() && filtering[i] >= texture_object::TF_BILINEAR) {
				glTexParameteri(buffers.back()->target[i], GL_TEXTURE_MAX_ANISOTROPY_EXT, exts->get_max_anisotropic_filtering());
			}
			
			switch(buffers.back()->target[i]) {
				case GL_TEXTURE_1D:
					glTexImage1D(buffers.back()->target[i], 0, internal_format[i], width, 0, format[i], type[i], NULL);
					break;
				case GL_TEXTURE_2D:
					glTexImage2D(buffers.back()->target[i], 0, internal_format[i], width, height, 0, format[i], type[i], NULL);
					break;
				/*case GL_TEXTURE_3D:
					// TODO: tex3d
					glTexImage3D(buffers.back()->target[i], 0, internal_format[i], width, height, 1, 0, format[i], type[i], NULL);
					break;*/
				case GL_TEXTURE_2D_MULTISAMPLE:
					glTexImage2DMultisample(buffers.back()->target[i], (GLsizei)get_sample_count(buffers.back()->anti_aliasing[0]), internal_format[i], width, height, false);
					break;
				/*case GL_TEXTURE_3D_MULTISAMPLE:
					// TODO: tex3d
					glTexImage3DMultisample(buffers.back()->target[i], samp, internal_format[i], width, height, 1, false);
					break;*/
				default:
					glTexImage2D(buffers.back()->target[i], 0, internal_format[i], width, height, 0, format[i], type[i], NULL);
					break;
			}
			
			if(filtering[i] > texture_object::TF_LINEAR) {
				buffers.back()->auto_mipmap[i] = true;
				//glGenerateMipmap(buffers.back()->target[i]);
			}
			else buffers.back()->auto_mipmap[i] = false;
			
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0+i, buffers.back()->target[i], buffers.back()->tex_id[i], 0);
			
#if A2E_DEBUG
			// TODO: fbo/texture checking
			GLint check_internal_format = 0, check_type = 0, check_size = 0;
			glGetTexLevelParameteriv(buffers.back()->target[i], 0, GL_TEXTURE_INTERNAL_FORMAT, &check_internal_format);
			glGetTexLevelParameteriv(buffers.back()->target[i], 0, GL_TEXTURE_RED_TYPE, &check_type);
			glGetTexLevelParameteriv(buffers.back()->target[i], 0, GL_TEXTURE_RED_SIZE, &check_size);
			//a2e_debug("FBO: iformat: %X, type: %X, size: %d", check_internal_format, check_type, check_size);
#endif
		}
		
		current_buffer = buffers.back();
		check_fbo(current_buffer);
		
		if(depth_type != rtt::DT_NONE) {
			// TODO: ?, this is currently a restriction
			/*if(depth_type == rtt::DT_TEXTURE_2D) {
				buffers.back()->anti_aliasing[0] = rtt::TAA_NONE;
			}*/
			
			switch(buffers.back()->anti_aliasing[0]) {
				case rtt::TAA_NONE:
				case rtt::TAA_SSAA_2:
				case rtt::TAA_SSAA_4:
				case rtt::TAA_FXAA:
				case rtt::TAA_SSAA_4_3_FXAA:
				case rtt::TAA_SSAA_2_FXAA:
					if(depth_type == rtt::DT_RENDERBUFFER) {
						glGenRenderbuffers(1, &buffers.back()->depth_buffer);
						glBindRenderbuffer(GL_RENDERBUFFER, buffers.back()->depth_buffer);
						glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
					}
					else if(depth_type == rtt::DT_TEXTURE_2D) {
						glGenTextures(1, &buffers.back()->depth_buffer);
						glBindTexture(GL_TEXTURE_2D, buffers.back()->depth_buffer);
						glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
						glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
						glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
						glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
						glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
						
						// apparently opencl/opengl depth texture sharing only works with a float format
#if !defined(A2E_INFERRED_RENDERING_CL)
						glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, width, height, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, 0);
#else
						glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, width, height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, 0);
#endif
						glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, buffers.back()->depth_buffer, 0);
					}
					
					check_fbo(current_buffer);
					break;
				case rtt::TAA_MSAA_2:
				case rtt::TAA_MSAA_4:
				case rtt::TAA_MSAA_8:
				case rtt::TAA_MSAA_16:
				case rtt::TAA_MSAA_32:
				case rtt::TAA_MSAA_64: {
					GLsizei samples = (GLsizei)get_sample_count(buffers.back()->anti_aliasing[0]);

					buffers.back()->resolve_buffer = new GLuint[attachment_count];
					glGenFramebuffers(attachment_count, buffers.back()->resolve_buffer);
					for(size_t i = 0; i < attachment_count; i++) {
						glBindFramebuffer(GL_FRAMEBUFFER, buffers.back()->resolve_buffer[i]);
						glFramebufferTexture2D(GL_FRAMEBUFFER, (GLenum)(GL_COLOR_ATTACHMENT0+i), target[i], buffers.back()->tex_id[i], 0);
					}
					check_fbo(current_buffer);
					
					glBindFramebuffer(GL_FRAMEBUFFER, buffers.back()->fbo_id);

					glGenRenderbuffers(1, &buffers.back()->color_buffer);
					glBindRenderbuffer(GL_RENDERBUFFER, buffers.back()->color_buffer);
					glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, format[0], buffers.back()->width, buffers.back()->height);
					glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, buffers.back()->color_buffer);
					check_fbo(current_buffer);
					
					if(depth_type == rtt::DT_RENDERBUFFER) {
						glGenRenderbuffers(1, &buffers.back()->depth_buffer);
						glBindRenderbuffer(GL_RENDERBUFFER, buffers.back()->depth_buffer);
						glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH_COMPONENT24, buffers.back()->width, buffers.back()->height);
						glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, buffers.back()->depth_buffer);
					}
					else if(depth_type == rtt::DT_TEXTURE_2D) {
						buffers.back()->samples = samples;

						glGenTextures(1, &buffers.back()->depth_buffer);
						glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, buffers.back()->depth_buffer);
						glTexParameteri(GL_TEXTURE_2D_MULTISAMPLE, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
						glTexParameteri(GL_TEXTURE_2D_MULTISAMPLE, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
						glTexParameteri(GL_TEXTURE_2D_MULTISAMPLE, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
						glTexParameteri(GL_TEXTURE_2D_MULTISAMPLE, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
						
						glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, samples, GL_DEPTH_COMPONENT24, width, height, false);
						glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D_MULTISAMPLE, buffers.back()->depth_buffer, 0);
					}
					
					check_fbo(current_buffer);
				}
				break;
				case rtt::TAA_CSAA_8:
				case rtt::TAA_CSAA_8Q:
				case rtt::TAA_CSAA_16:
				case rtt::TAA_CSAA_16Q:
				case rtt::TAA_CSAA_32:
				case rtt::TAA_CSAA_32Q: {
					int color_samples, coverage_samples;
					switch(buffers.back()->anti_aliasing[0]) {
						case rtt::TAA_CSAA_8:
							color_samples = 4;
							coverage_samples = 8;
							break;
						case rtt::TAA_CSAA_8Q:
							color_samples = 8;
							coverage_samples = 8;
							break;
						case rtt::TAA_CSAA_16:
							color_samples = 4;
							coverage_samples = 16;
							break;
						case rtt::TAA_CSAA_16Q:
							color_samples = 8;
							coverage_samples = 16;
							break;
						case rtt::TAA_CSAA_32: // TODO: ratio?
						case rtt::TAA_CSAA_32Q: // TODO: ratio?
						default:
							color_samples = 4;
							coverage_samples = 8;
							break;
					}
					
					glGenRenderbuffers(1, &buffers.back()->depth_buffer);
					glGenRenderbuffers(1, &buffers.back()->color_buffer);

					buffers.back()->resolve_buffer = new GLuint[1];
					glGenFramebuffers(1, &buffers.back()->resolve_buffer[0]);
					
					glBindFramebuffer(GL_FRAMEBUFFER, buffers.back()->resolve_buffer[0]);
					glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, buffers.back()->tex_id[0], 0);
					check_fbo(current_buffer);
					
					glBindFramebuffer(GL_FRAMEBUFFER, buffers.back()->fbo_id);
					glBindRenderbuffer(GL_RENDERBUFFER, buffers.back()->color_buffer);
					
					glRenderbufferStorageMultisampleCoverageNV(GL_RENDERBUFFER, coverage_samples, color_samples, format[0], buffers.back()->width, buffers.back()->height);
					glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, buffers.back()->color_buffer);
					check_fbo(current_buffer);
					
					glBindRenderbuffer(GL_RENDERBUFFER, buffers.back()->depth_buffer);
					glRenderbufferStorageMultisampleCoverageNV(GL_RENDERBUFFER, coverage_samples, color_samples, GL_DEPTH_COMPONENT24, buffers.back()->width, buffers.back()->height);
					glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, buffers.back()->depth_buffer);
					check_fbo(current_buffer);
					break;
				}
				default:
					break;
			}
		}
		
		glBindRenderbuffer(GL_RENDERBUFFER, 0);
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}
	else  {
		// stencil buffer
		// TODO: this is defined per spec standard, but doesn't work on current graphic cards
		buffers.back()->color = false;
		buffers.back()->stencil = true;
		
		glGenFramebuffers(1, &buffers.back()->fbo_id);
		glBindFramebuffer(GL_FRAMEBUFFER, buffers.back()->fbo_id);
		glGenRenderbuffers(1, &buffers.back()->stencil_buffer);
		glBindRenderbuffer(GL_RENDERBUFFER, buffers.back()->stencil_buffer);
		glRenderbufferStorage(GL_RENDERBUFFER, internal_format[0], width, height);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, buffers.back()->stencil_buffer);
		
		check_fbo(buffers.back());
		
		glBindRenderbuffer(GL_RENDERBUFFER, 0);
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}
	
	return buffers.back();
}

void rtt::delete_buffer(rtt::fbo* buffer) {
	glDeleteTextures(buffer->attachment_count, buffer->tex_id);
	glDeleteFramebuffers(1, &buffer->fbo_id);
	delete buffer;
	buffers.remove(buffer);
}

void rtt::start_draw(rtt::fbo* buffer) {
	current_buffer = buffer;
	if(buffer->anti_aliasing[0] == rtt::TAA_NONE ||
	   buffer->anti_aliasing[0] == rtt::TAA_SSAA_2 ||
	   buffer->anti_aliasing[0] == rtt::TAA_SSAA_4 ||
	   buffer->anti_aliasing[0] == rtt::TAA_FXAA ||
	   buffer->anti_aliasing[0] == rtt::TAA_SSAA_4_3_FXAA ||
	   buffer->anti_aliasing[0] == rtt::TAA_SSAA_2_FXAA) {
		glBindFramebuffer(GL_FRAMEBUFFER, buffer->fbo_id);
		for(unsigned int i = 0; i < buffer->attachment_count; i++) {
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0+i, buffer->target[i], buffer->tex_id[i], 0);
		}
		
		if(buffer->depth_type == DT_RENDERBUFFER) {
			glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, buffer->depth_buffer);
		}
		else if(buffer->depth_type == DT_TEXTURE_2D) {
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, (buffer->samples == 0 ? GL_TEXTURE_2D : GL_TEXTURE_2D_MULTISAMPLE), buffer->depth_buffer, 0);
		}
		else glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, 0);
	}
	else {
		glBindFramebuffer(GL_FRAMEBUFFER, buffer->fbo_id);
	}
	glViewport(0, 0, buffer->draw_width, buffer->draw_height);
}

void rtt::stop_draw() {
	check_fbo(current_buffer);
	
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	if(current_buffer->anti_aliasing[0] != rtt::TAA_NONE &&
	   current_buffer->anti_aliasing[0] != rtt::TAA_SSAA_2 &&
	   current_buffer->anti_aliasing[0] != rtt::TAA_SSAA_4 &&
	   current_buffer->anti_aliasing[0] != rtt::TAA_FXAA &&
	   current_buffer->anti_aliasing[0] != rtt::TAA_SSAA_4_3_FXAA &&
	   current_buffer->anti_aliasing[0] != rtt::TAA_SSAA_2_FXAA) {
		if(current_buffer->depth_type == DT_RENDERBUFFER ||
		   current_buffer->depth_type == DT_TEXTURE_2D) {
			glBindFramebuffer(GL_FRAMEBUFFER, 0);
		
			glBindFramebuffer(GL_READ_FRAMEBUFFER, current_buffer->fbo_id);
			for(size_t i = 0; i < current_buffer->attachment_count; i++) {
				glBindFramebuffer(GL_DRAW_FRAMEBUFFER, current_buffer->resolve_buffer[i]);
				glBlitFramebuffer(0, 0, current_buffer->width, current_buffer->height, 0, 0, current_buffer->width, current_buffer->height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
			}
		
			glBindFramebuffer(GL_FRAMEBUFFER, 0);
		}
	}
	
	for(unsigned int i = 0; i < current_buffer->attachment_count; i++) {
		if(current_buffer->auto_mipmap[i]) {
			// TODO: fix this
			//glBindTexture(current_buffer->target[i], current_buffer->tex_id[i]);
			//glGenerateMipmap(current_buffer->target[i]);
		}
	}
	glBindTexture(current_buffer->target[0], 0);
	
	glViewport(0, 0, rtt::screen_width, rtt::screen_height);
}

void rtt::start_2d_draw() {
	e->start_2d_draw(current_buffer->draw_width, current_buffer->draw_height, true);
}

void rtt::stop_2d_draw() {
	e->stop_2d_draw();
}

void rtt::clear(const unsigned int and_mask) {
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	if(current_buffer->depth_type != DT_NONE) {
		glClear((GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT) & and_mask);
	}
	else {
		glClear(GL_COLOR_BUFFER_BIT & and_mask);
	}
}

void rtt::check_fbo(rtt::fbo* buffer) {
	GLint status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	if(status != GL_FRAMEBUFFER_COMPLETE) {
		a2e_error("framebuffer (size: %u*%upx; depth: %i; tex id: %u; fbo id: %u) didn't pass status check!",
				  buffer->width, buffer->height, buffer->depth_type, buffer->tex_id, buffer->fbo_id);
	}
	
	switch(status) {
		case GL_FRAMEBUFFER_COMPLETE:
			break;
		case GL_FRAMEBUFFER_UNSUPPORTED:
			a2e_error("unsupported framebuffer (%u)!", status);
			break;
		case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:
			a2e_error("incomplete framebuffer attachement (%u)!", status);
			break;
		case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT:
			a2e_error("missing framebuffer attachement (%u)!", status);
			break;
		case GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER:
			a2e_error("incomplete framebuffer draw buffer (%u)!", status);
			break;
		case GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER:
			a2e_error("incomplete framebuffer read buffer (%u)!", status);
			break;
		case GL_FRAMEBUFFER_INCOMPLETE_LAYER_TARGETS:
			a2e_error("incomplete framebuffer layer targets (%u)!", status);
			break;
		case GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE:
			a2e_error("incomplete framebuffer multisample (%u)!", status);
			break;
		default:
			a2e_error("unknown framebuffer error (%u)!", status);
			break;
	}
}

size_t rtt::get_sample_count(const rtt::TEXTURE_ANTI_ALIASING& taa) const {
	switch(taa) {
		case TAA_NONE: return 0;
		case TAA_MSAA_1: return 1;
		case TAA_MSAA_2: return 2;
		case TAA_MSAA_4: return 4;
		case TAA_MSAA_8: return 8;
		case TAA_MSAA_16: return 16;
		case TAA_MSAA_32: return 32;
		case TAA_MSAA_64: return 64;
		case TAA_CSAA_8: return 0;
		case TAA_CSAA_8Q: return 0;
		case TAA_CSAA_16: return 0;
		case TAA_CSAA_16Q: return 0;
		case TAA_CSAA_32: return 0;
		case TAA_CSAA_32Q: return 0;
		case TAA_SSAA_2: return 0;
		case TAA_SSAA_4: return 0;
		case TAA_FXAA: return 0;
		case TAA_SSAA_4_3_FXAA: return 0;
		case TAA_SSAA_2_FXAA: return 0;
	}
	assert(false && "invalid anti-aliasing mode");
	return 0;
}

const rtt::fbo* rtt::get_current_buffer() const {
	return current_buffer;
}

float rtt::get_anti_aliasing_scale(const TEXTURE_ANTI_ALIASING& taa) const {
	switch(taa) {
		case rtt::TAA_SSAA_2: return 2.0f;
		case rtt::TAA_SSAA_4: return 4.0f;
		case rtt::TAA_SSAA_4_3_FXAA: return 4.0f/3.0f;
		case rtt::TAA_SSAA_2_FXAA: return 2.0f;
		default: break;
	}
	return 1.0f;
}

float2 rtt::get_resolution_for_scale(const float& scale, const size2& res) const {
	if(scale == 1.0f) return float2(res);
	
	float2 ret_res(ceilf(scale * float(res.x)), ceilf(scale * float(res.y)));
	
	// always return a resolution that is dividable by 2
	ret_res.x += (fmod(ret_res.x, 2.0f) != 0.0f ? 1.0f : 0.0f);
	ret_res.y += (fmod(ret_res.y, 2.0f) != 0.0f ? 1.0f : 0.0f);
	return ret_res;
}