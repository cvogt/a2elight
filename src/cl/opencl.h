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

#ifndef __A2E_OPENCL_H__
#define __A2E_OPENCL_H__

#include "global.h"
#include "core/file_io.h"
#include "core/core.h"
#include "core/vector2.h"

// necessary for now (when compiling with opencl 1.2+ headers)
#define CL_USE_DEPRECATED_OPENCL_1_1_APIS 1

#if defined(__APPLE__)
#include <OpenCL/OpenCL.h>
#include <OpenCL/cl.h>
#include <OpenCL/cl_platform.h>
#include <OpenCL/cl_ext.h>
#include <OpenCL/cl_gl.h>
#include <OpenGL/CGLContext.h>
#include <OpenGL/CGLCurrent.h>
#include <OpenGL/CGLDevice.h>
#else
#include <CL/cl.h>
#include <CL/cl_platform.h>
#include <CL/cl_ext.h>
#include <CL/cl_gl.h>
#endif

#define __CL_ENABLE_EXCEPTIONS
#include "cl/cl.hpp"

#define CLINFO_STR_SIZE 65536*2

/*! @class opencl
 *  @brief opencl routines
 */

class A2E_API opencl {
public:
	struct kernel_object;
	struct buffer_object;
	struct device_object;
	
	opencl& operator=(const opencl&) = delete;
	opencl(const opencl&) = delete;
	opencl(const char* kernel_path, file_io* f_, SDL_Window* wnd, const bool clear_cache);
	~opencl();
	
	bool is_supported() { return true; }
	bool is_cpu_support();
	bool is_gpu_support();
	
	enum OPENCL_DEVICE {
		NONE,
		FASTEST_GPU,
		FASTEST_CPU,
		ALL_GPU,
		ALL_CPU,
		ALL_DEVICES,
		GPU0,
		GPU1,
		GPU2,
		GPU4,
		GPU5,
		GPU6,
		GPU7,
		GPU255 = GPU0+254,
		CPU0,
		CPU1,
		CPU2,
		CPU3,
		CPU4,
		CPU5,
		CPU6,
		CPU7,
		CPU255 = CPU0+254
	};
	device_object* get_device(OPENCL_DEVICE device);
	device_object* get_active_device();
	
	enum OPENCL_VENDOR {
		CLV_NVIDIA,
		CLV_ATI,
		CLV_INTEL,
		CLV_AMD,
		CLV_UNKNOWN
	};
	
	//! buffer types/flags (associated kernel => buffer has been set as a kernel argument, at least once and the latest one for an index)
	enum EBUFFER_TYPE {
		BT_READ_WRITE		= 1,	//!< enum read and write buffer
		BT_READ				= 2,	//!< enum read only buffer
		BT_WRITE			= 3,	//!< enum write only buffer
		BT_INITIAL_COPY		= 4,	//!< enum the specified data will be copied to the buffer at creation time
		BT_COPY_ON_USE		= 8,	//!< enum the specified data will be copied to the buffer each time an associated kernel is being used (that is right before kernel execution)
		BT_USE_HOST_MEMORY	= 16,	//!< enum buffer memory will be allocated in host memory
		BT_READ_BACK_RESULT	= 32,	//!< enum every time an associated kernel has been executed, the result buffer data will be read back/copied to the specified pointer location
		BT_DELETE_AFTER_USE	= 64,	//!< enum the buffer will be deleted after its first use (after an associated kernel has been executed)
		BT_BLOCK_ON_READ	= 128,	//!< enum the read command is blocking, all data will be read/copied before program continuation
		BT_BLOCK_ON_WRITE	= 256,	//!< enum the write command is blocking, all data will be written before program continuation
		BT_OPENGL_BUFFER	= 512	//!< enum determines if a buffer is a shared opengl buffer/image/memory object
	};
	typedef unsigned int BUFFER_TYPE;
	
	void init(bool use_platform_devices = false, const size_t platform_index = 0);
	void reload_kernels();
	
	void use_kernel(const string& identifier);
	void run_kernel();
	void run_kernel(kernel_object* kernel_obj);
	void run_kernel(const char* kernel_identifier);
	kernel_object* get_cur_kernel() { return cur_kernel; }
	void finish();
	void flush();
	
	kernel_object* add_kernel_file(const string& identifier, const char* file_name, const string& func_name, const char* additional_options = NULL);
	kernel_object* add_kernel_src(const string& identifier, const string& src, const string& func_name, const char* additional_options = NULL);
	
	buffer_object* create_buffer(BUFFER_TYPE type, size_t size, void* data = NULL);
	buffer_object* create_image2d_buffer(BUFFER_TYPE type, cl_channel_order channel_order, cl_channel_type channel_type, size_t width, size_t height, void* data = NULL);
	buffer_object* create_image3d_buffer(BUFFER_TYPE type, cl_channel_order channel_order, cl_channel_type channel_type, size_t width, size_t height, size_t depth, void* data = NULL);
	buffer_object* create_ogl_buffer(BUFFER_TYPE type, GLuint ogl_buffer);
	buffer_object* create_ogl_image2d_buffer(BUFFER_TYPE type, GLuint texture, GLenum target = GL_TEXTURE_2D);
	buffer_object* create_ogl_image2d_renderbuffer(BUFFER_TYPE type, GLuint renderbuffer);
	void delete_buffer(buffer_object* buffer_obj);
	void write_buffer(buffer_object* buffer_obj, const void* src, const size_t offset = 0, const size_t size = 0);
	void write_image2d(buffer_object* buffer_obj, const void* src, size2 origin, size2 region);
	void write_image3d(buffer_object* buffer_obj, const void* src, size3 origin, size3 region);
	void read_buffer(void* dst, buffer_object* buffer_obj);
	void* map_buffer(buffer_object* buffer_obj, EBUFFER_TYPE access_type, bool blocking = true);
	void unmap_buffer(buffer_object* buffer_obj, void* map_ptr);
	void set_manual_gl_sharing(buffer_object* gl_buffer_obj, const bool state);
	
	void set_active_device(OPENCL_DEVICE dev);
	bool set_kernel_argument(unsigned int index, opencl::buffer_object* arg);
	bool set_kernel_argument(unsigned int index, const opencl::buffer_object* arg);
	template<typename T> bool set_kernel_argument(unsigned int index, T arg);
	bool set_kernel_argument(unsigned int index, size_t size, void* arg);
	void set_kernel_range(const cl::NDRange& global, const cl::NDRange& local);
	size_t get_kernel_work_group_size();
	cl::NDRange compute_local_kernel_range(const unsigned int dimensions);
	
	//! this is for manual handling only
	void acquire_gl_object(buffer_object* gl_buffer_obj);
	void release_gl_object(buffer_object* gl_buffer_obj);
	
	struct kernel_object {
		cl::Kernel* kernel;
		cl::Program* program;
		cl::NDRange* global;
		cl::NDRange* local;
		unsigned int arg_count;
		vector<bool> args_passed;
		map<unsigned int, buffer_object*> buffer_args;
		bool has_ogl_buffers;
		string kernel_name;
		
		kernel_object() : kernel(), program(), global(NULL), local(NULL), arg_count(0), args_passed(), buffer_args(), has_ogl_buffers(false), kernel_name("") {}
		~kernel_object() {
			if(global != NULL) delete global;
			if(local != NULL) delete local;
			for(auto& ba : buffer_args) {
				ba.second->associated_kernels.erase(this);
			}
			delete kernel;
			delete program;
		}
	};
	
	struct buffer_object {
		cl::Buffer* buffer;
		cl::Image* image_buffer;
		GLuint ogl_buffer;
		bool manual_gl_sharing;
		void* data;
		size_t size;
		unsigned int type;
		cl_mem_flags flags;
		cl::ImageFormat format;
		size3 origin;
		size3 region;
		map<kernel_object*, vector<unsigned int> > associated_kernels; // kernels + argument numbers
		
		buffer_object() : buffer(NULL), image_buffer(NULL), ogl_buffer(0), manual_gl_sharing(false), data(NULL), size(0), type(0), format(0, 0), associated_kernels() {}
	};
	
	struct device_object {
		cl::Device* device;
		OPENCL_DEVICE type;
		OPENCL_VENDOR vendor_type;
		unsigned int units;
		unsigned int clock;
		cl_ulong mem_size;
		cl_device_type internal_type;
		string name;
		string vendor;
		string version;
		string driver_version;
		string extensions;
		
		cl_ulong max_alloc;
		size_t max_wg_size;
		size2 max_img_2d;
		size3 max_img_3d;
		bool img_support;
		
		device_object() : device(NULL), type((OPENCL_DEVICE)0), units(0), clock(0), mem_size(0), internal_type(0), name(""), vendor(""), version(""), driver_version(""), extensions("") {}
	};

	inline const char* make_kernel_path(const char* file_name) {
		tmp_path = kernel_path_str + string(file_name);
		return tmp_path.c_str();
	}
	
protected:
	file_io* f;
	SDL_Window* sdl_wnd;
	
	string build_options;
	string nv_build_options;
	string kernel_path_str;
	string tmp_path;
	
	stringstream* buffer;
	
	buffer_object* create_buffer_object(BUFFER_TYPE type, void* data = NULL);
	void load_internal_kernels();
	void destroy_kernels();
	void check_compilation(const bool ret, const string& filename);
	void log_program_binary(const kernel_object* kernel, const string& options);
	
	bool has_vendor_device(OPENCL_VENDOR vendor_type);
	
	const char* error_code_to_string(cl_int error_code);
	
	cl::Context* context;
	cl::Platform* platform;
	vector<cl::Platform> platforms;
	vector<cl::Device> internal_devices;
	vector<device_object*> devices;
	device_object* active_device;
	device_object* fastest_cpu;
	device_object* fastest_gpu;
	vector<cl::ImageFormat> ro_formats;
	vector<cl::ImageFormat> wo_formats;
	vector<cl::ImageFormat> rw_formats;
	cl_int ierr;
	bool successful_internal_compilation;
	
	vector<buffer_object*> buffers;
	map<string, kernel_object*> kernels;
	kernel_object* cur_kernel;
	
	map<cl::Device*, cl::CommandQueue*> queues;
	
};

template<typename T> bool opencl::set_kernel_argument(unsigned int index, T arg) {
	try {
		cur_kernel->kernel->setArg(index, arg);
		cur_kernel->args_passed[index] = true;
		
		// remove "references" of the last used buffer for this kernel and argument index (if there is one)
		if(cur_kernel->buffer_args.count(index) > 0) {
			vector<unsigned int>* buf_associated_kernels = &cur_kernel->buffer_args[index]->associated_kernels[cur_kernel];
			buf_associated_kernels->erase(find(buf_associated_kernels->begin(), buf_associated_kernels->end(), index));
			cur_kernel->buffer_args.erase(index);
		}
		return true;
	}
	catch(cl::Error err) {
		a2e_error("%s (%d: %s)!", err.what(), err.err(), error_code_to_string(err.err()));
		return false;
	}
	catch(...) {
		a2e_error("unknown error!");
		return false;
	}
	return false;
}

#endif // __OPENCL_H__