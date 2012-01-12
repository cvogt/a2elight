/*  Albion 2 Engine "light"
 *  Copyright (C) 2004 - 2012 Florian Ziesche
 *  
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License only
 *  
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "engine.h"
#include "a2e_version.h"
#include "rendering/shader.h"
#include "cl/opencl.h"

// dll main for windows dll export
#ifdef __WINDOWS__
BOOL APIENTRY DllMain(HANDLE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
	switch(ul_reason_for_call) {
		case DLL_PROCESS_ATTACH:
		case DLL_THREAD_ATTACH:
		case DLL_THREAD_DETACH:
		case DLL_PROCESS_DETACH:
			break;
	}
    return TRUE;
}
#endif // __WINDOWS__

/*! this is used to set an absolute data path depending on call path (path from where the binary is called/started),
 *! which is mostly needed when the binary is opened via finder under os x or any file manager under linux
 */
engine::engine(const char* callpath_, const char* datapath_) {
	logger::init();
	
	engine::callpath = callpath_;
	engine::datapath = callpath_;
	engine::rel_datapath = datapath_;

#ifndef __WINDOWS__
	const char dir_slash = '/';
#else
	const char dir_slash = '\\';
#endif
	engine::datapath = datapath.substr(0, datapath.rfind(dir_slash)+1) + datapath_;

#ifdef CYGWIN
	engine::callpath = "./";
	engine::datapath = callpath_;
	engine::datapath = datapath.substr(0, datapath.rfind("/")+1) + datapath_;
#endif
	
	create();
}

/*! there is no function currently
 */
engine::~engine() {
	a2e_debug("deleting engine object");

	for(map<string, SDL_Cursor*>::iterator citer = cursors.begin(); citer != cursors.end(); citer++) {
		if(citer->first != "STANDARD") {
			SDL_FreeCursor(citer->second);
		}
	}
	cursors.clear();

	if(c != NULL) delete c;
	if(f != NULL) delete f;
	if(e != NULL) delete e;
	if(g != NULL) delete g;
	if(t != NULL) delete t;
	if(exts != NULL) delete exts;
	if(x != NULL) delete x;
	if(u != NULL) delete u;
	if(ocl != NULL) delete ocl;
	if(shd != NULL) delete shd;

	a2e_debug("engine object deleted");
	
	SDL_GL_DeleteContext(config.ctx);
	SDL_DestroyWindow(config.wnd);
	SDL_Quit();
	
	logger::destroy();
}

void engine::create() {
#if !defined(__WINDOWS__) && !defined(CYGWIN)
	if(datapath.size() > 0 && datapath[0] == '.') {
		// strip leading '.' from datapath if there is one
		datapath.erase(0, 1);
		
		char working_dir[8192];
		memset(working_dir, 0, 8192);
		getcwd(working_dir, 8192);
		
		datapath = working_dir + datapath;
	}
#elif defined(CYGWIN)
	// do nothing
#else
	char working_dir[8192];
	memset(working_dir, 0, 8192);
	getcwd(working_dir, 8192);

	size_t strip_pos = datapath.find("\\.\\");
	if(strip_pos != string::npos) {
		datapath.erase(strip_pos, 3);
	}
	
	bool add_bin_path = (working_dir == datapath.substr(0, datapath.length()-1)) ? false : true;
	if(!add_bin_path) datapath = working_dir + string("\\") + (add_bin_path ? datapath : "");
	else {
		if(datapath[datapath.length()-1] == '/') {
			datapath = datapath.substr(0, datapath.length()-1);
		}
		datapath += string("\\");
	}

#endif
	
#ifdef __APPLE__
	// check if datapath contains a 'MacOS' string (indicates that the binary is called from within an OS X .app or via complete path from the shell)
	if(datapath.find("MacOS") != string::npos) {
		// if so, add "../../../" to the datapath, since we have to relocate the datapath if the binary is inside an .app
		datapath.insert(datapath.find("MacOS")+6, "../../../");
	}
#endif
	
	// condense datapath
	datapath = core::strip_path(datapath);
	
	shaderpath = "shader/";
	kernelpath = "kernels/";
	cursor_visible = true;
	
	c = NULL;
	f = NULL;
	e = NULL;
	g = NULL;
	t = NULL;
	exts = NULL;
	x = NULL;
	u = NULL;
	ocl = NULL;
	shd = NULL;
	
	fps = 0;
	fps_counter = 0;
	fps_time = 0;
	frame_time = 0.0f;
	frame_time_sum = 0;
	frame_time_counter = 0;
	new_fps_count = false;
	standard_cursor = NULL;
	cursor_data = NULL;
	cursor_mask = NULL;
	
	u = new unicode();
	f = new file_io();
	c = new core();
	x = new xml(this);
	e = new event();
	g = new gfx(this);
	ocl = NULL;
	
	AtomicSet(&reload_shaders_flag, 0);
	AtomicSet(&reload_kernels_flag, 0);
	
	// print out engine info
	a2e_debug("%s", (A2E_VERSION_STRING).c_str());
	
	// load config
	config_doc = x->process_file(data_path("config.xml"));
	if(config_doc.valid) {
		config.width = config_doc.get<size_t>("config.screen.width", 640);
		config.height = config_doc.get<size_t>("config.screen.height", 480);
		config.fullscreen = config_doc.get<bool>("config.screen.fullscreen", false);
		config.vsync = config_doc.get<bool>("config.screen.vsync", false);
		config.stereo = config_doc.get<bool>("config.screen.stereo", false);
		
		config.fov = config_doc.get<float>("config.projection.fov", 72.0f);
		config.near_far_plane.x = config_doc.get<float>("config.projection.near", 1.0f);
		config.near_far_plane.y = config_doc.get<float>("config.projection.far", 1000.0f);
		
		config.key_repeat = config_doc.get<size_t>("config.input.key_repeat", 200);
		config.ldouble_click_time = config_doc.get<size_t>("config.input.ldouble_click_time", 200);
		config.mdouble_click_time = config_doc.get<size_t>("config.input.mdouble_click_time", 200);
		config.rdouble_click_time = config_doc.get<size_t>("config.input.rdouble_click_time", 200);
		
		config.fps_limit = config_doc.get<size_t>("config.sleep.time", 0);
		
		config.server.port = (unsigned short int)config_doc.get<size_t>("config.server.port", 0);
		config.server.max_clients = (unsigned int)config_doc.get<size_t>("config.server.max_clients", 0);
		
		config.client.client_name = config_doc.get<string>("config.client.name", "");
		config.client.server_name = config_doc.get<string>("config.client.server", "");
		config.client.port = (unsigned short int)config_doc.get<size_t>("config.client.port", 0);
		config.client.lis_port = (unsigned short int)config_doc.get<size_t>("config.client.lis_port", 0);
		
		string filtering_str = config_doc.get<string>("config.graphic.filtering", "");
		if(filtering_str == "POINT") config.filtering = texture_object::TF_POINT;
		else if(filtering_str == "LINEAR") config.filtering = texture_object::TF_LINEAR;
		else if(filtering_str == "BILINEAR") config.filtering = texture_object::TF_BILINEAR;
		else if(filtering_str == "TRILINEAR") config.filtering = texture_object::TF_TRILINEAR;
		else config.filtering = texture_object::TF_POINT;
		
		config.anisotropic = config_doc.get<size_t>("config.graphic.anisotropic", 0);
		
		string anti_aliasing_str = config_doc.get<string>("config.graphic.anti_aliasing", "");
		if(anti_aliasing_str == "NONE") config.anti_aliasing = rtt::TAA_NONE;
		/*else if(anti_aliasing_str == "MSAA1") config.anti_aliasing = rtt::TAA_MSAA_1;
		else if(anti_aliasing_str == "MSAA2") config.anti_aliasing = rtt::TAA_MSAA_2;
		else if(anti_aliasing_str == "MSAA4") config.anti_aliasing = rtt::TAA_MSAA_4;
		else if(anti_aliasing_str == "MSAA8") config.anti_aliasing = rtt::TAA_MSAA_8;
		else if(anti_aliasing_str == "MSAA16") config.anti_aliasing = rtt::TAA_MSAA_16;
		else if(anti_aliasing_str == "MSAA32") config.anti_aliasing = rtt::TAA_MSAA_32;
		else if(anti_aliasing_str == "MSAA64") config.anti_aliasing = rtt::TAA_MSAA_64;
		else if(anti_aliasing_str == "CSAA8") config.anti_aliasing = rtt::TAA_CSAA_8;
		else if(anti_aliasing_str == "CSAA8Q") config.anti_aliasing = rtt::TAA_CSAA_8Q;
		else if(anti_aliasing_str == "CSAA16") config.anti_aliasing = rtt::TAA_CSAA_16;
		else if(anti_aliasing_str == "CSAA16Q") config.anti_aliasing = rtt::TAA_CSAA_16Q;
		else if(anti_aliasing_str == "CSAA32") config.anti_aliasing = rtt::TAA_CSAA_32;
		else if(anti_aliasing_str == "CSAA32Q") config.anti_aliasing = rtt::TAA_CSAA_32Q;*/
		else if(anti_aliasing_str == "FXAA") config.anti_aliasing = rtt::TAA_FXAA;
		else if(anti_aliasing_str == "2xSSAA") config.anti_aliasing = rtt::TAA_SSAA_2;
		//else if(anti_aliasing_str == "4xSSAA") config.anti_aliasing = rtt::TAA_SSAA_4;
		else if(anti_aliasing_str == "4/3xSSAA+FXAA") config.anti_aliasing = rtt::TAA_SSAA_4_3_FXAA;
		else if(anti_aliasing_str == "2xSSAA+FXAA") config.anti_aliasing = rtt::TAA_SSAA_2_FXAA;
		else config.anti_aliasing = rtt::TAA_NONE;
		
		config.disabled_extensions = config_doc.get<string>("config.graphic_device.disabled_extensions", "");
		config.force_device = config_doc.get<string>("config.graphic_device.force_device", "");
		config.force_vendor = config_doc.get<string>("config.graphic_device.force_vendor", "");
		
		config.inferred_scale = config_doc.get<size_t>("config.inferred.scale", 4);
		
		config.opencl_platform = config_doc.get<size_t>("config.opencl.platform", 0);
		config.clear_cache = config_doc.get<bool>("config.opencl.clear_cache", false);
	}
}

/*! initializes the engine in console + graphical or console only mode
 *  @param console the initialization mode (false = gfx/console, true = console only)
 *  @param width the window width
 *  @param height the window height
 *  @param depth the color depth of the window (16, 24 or 32)
 *  @param fullscreen bool if the window is drawn in fullscreen mode
 */
void engine::init(bool console, unsigned int width, unsigned int height,
				  bool fullscreen, bool vsync, const char* ico) {
	if(console == true) {
	    engine::mode = engine::CONSOLE;
		// create extension class object
		exts = new ext(engine::mode, &config.disabled_extensions, &config.force_device, &config.force_vendor);
		a2e_debug("initializing albion 2 engine in console only mode");
	}
	else {
		config.width = width;
		config.height = height;
		config.fullscreen = fullscreen;
		config.vsync = vsync;

		engine::init(ico);
	}
}

/*! initializes the engine in console + graphical mode
 */
void engine::init(const char* ico) {
	engine::mode = engine::GRAPHICAL;
	a2e_debug("initializing albion 2 engine in console + graphical mode");

	// initialize sdl
	if(SDL_Init(SDL_INIT_VIDEO) == -1) {
		a2e_error("can't init SDL: %s", SDL_GetError());
		exit(1);
	}
	else {
		a2e_debug("sdl initialized");
	}
	atexit(SDL_Quit);

	// set some flags
	config.flags |= SDL_WINDOW_OPENGL;
	config.flags |= SDL_WINDOW_SHOWN;
	config.flags |= SDL_WINDOW_INPUT_FOCUS;
	config.flags |= SDL_WINDOW_MOUSE_FOCUS;

	if(config.fullscreen) {
		config.flags |= SDL_WINDOW_FULLSCREEN;
		a2e_debug("fullscreen enabled");
	}
	else a2e_debug("fullscreen disabled");

	a2e_debug("vsync %s", config.vsync ? "enabled" : "disabled");
	
	// gl attributes
	SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_BUFFER_SIZE, 32);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
	SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);

	// ... load icon before SDL_SetVideoMode
	if(ico != NULL) load_ico(ico);

	// create screen
	config.wnd = SDL_CreateWindow("A2E", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, (unsigned int)config.width, (unsigned int)config.height, config.flags);
	if(config.wnd == NULL) {
		a2e_error("can't create window: %s", SDL_GetError());
		exit(1);
	}
	else {
		SDL_GetWindowSize(config.wnd, (int*)&config.width, (int*)&config.height);
		a2e_debug("video mode set: w%u h%u", config.width, config.height);
	}
	config.ctx = SDL_GL_CreateContext(config.wnd);
	if(config.ctx == NULL) {
		a2e_error("can't create opengl context: %s", SDL_GetError());
		exit(1);
	}
	SDL_GL_SetSwapInterval(config.vsync ? 1 : 0); // has to be set after context creation
	
	// TODO: this is only a rudimentary solution, think of or wait for a better one ...
	ocl = new opencl(core::strip_path(string(datapath + kernelpath)).c_str(), f, config.wnd, config.clear_cache); // use absolute path
	
	// enable multi-threaded opengl context when on os x
/*#ifdef __APPLE__
	CGLError cgl_err = CGLEnable(CGLGetCurrentContext(), kCGLCEMPEngine);
	if(cgl_err != kCGLNoError) {
		a2e_error("unable to set multi-threaded opengl context!");
	}
	else {
		a2e_debug("multi-threaded opengl context enabled!");
	}
#endif*/
	
	// make an early clear
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
	swap();

	// create extension class object
	exts = new ext(engine::mode, &config.disabled_extensions, &config.force_device, &config.force_vendor);
	
	// capability test
	if(!exts->is_gl_version(3, 2)) { // TODO: check for shader support! (use recognized gl version)
		a2e_error("A2E doesn't support your graphic device! OpenGL 3.2 is the minimum requirement.");
		SDL_Delay(10000);
		exit(1);
	}
	
	int tmp = 0;
	SDL_GL_GetAttribute(SDL_GL_DOUBLEBUFFER, &tmp);
	a2e_debug("double buffering %s", tmp == 1 ? "enabled" : "disabled");

	// print out some opengl informations
	a2e_debug("vendor: %s", glGetString(GL_VENDOR));
	a2e_debug("renderer: %s", glGetString(GL_RENDERER));
	a2e_debug("version: %s", glGetString(GL_VERSION));
	
	if(SDL_GetCurrentVideoDriver() == NULL) {
		a2e_error("couldn't get video driver: %s!", SDL_GetError());
	}
	else a2e_debug("video driver: %s", SDL_GetCurrentVideoDriver());
	
	// enable key repeat
	if((SDL_EnableKeyRepeat((int)config.key_repeat, SDL_DEFAULT_REPEAT_INTERVAL))) {
		a2e_debug("setting keyboard repeat failed: %s", SDL_GetError());
		exit(1);
	}
	else {
		a2e_debug("keyboard repeat set");
	}
	
	e->set_ldouble_click_time((unsigned int)config.ldouble_click_time);
	e->set_rdouble_click_time((unsigned int)config.rdouble_click_time);
	e->set_mdouble_click_time((unsigned int)config.mdouble_click_time);
	
	// enable unicode key input
	SDL_EnableUNICODE(1);
	
	// initialize ogl
	init_gl();
	a2e_debug("opengl initialized");

	// resize stuff
	resize_window();

	// reserve memory for position and rotation 
	engine::position = new float3();
	engine::rotation = new float3();

	// check which anti-aliasing modes are supported (ranging from worst to best)
	supported_aa_modes.push_back(rtt::TAA_NONE);
	supported_aa_modes.push_back(rtt::TAA_FXAA);
	supported_aa_modes.push_back(rtt::TAA_SSAA_2);
	supported_aa_modes.push_back(rtt::TAA_SSAA_4);
	supported_aa_modes.push_back(rtt::TAA_SSAA_4_3_FXAA);
	supported_aa_modes.push_back(rtt::TAA_SSAA_2_FXAA);
	/*if(exts->get_max_samples() >= 1) supported_aa_modes.push_back(rtt::TAA_MSAA_1);
	if(exts->get_max_samples() >= 2) supported_aa_modes.push_back(rtt::TAA_MSAA_2);
	if(exts->get_max_samples() >= 4) supported_aa_modes.push_back(rtt::TAA_MSAA_4);
	if(exts->is_fbo_multisample_coverage_mode_support(8, 4)) supported_aa_modes.push_back(rtt::TAA_CSAA_8);
	if(exts->is_fbo_multisample_coverage_mode_support(8, 8)) supported_aa_modes.push_back(rtt::TAA_CSAA_8Q);
	if(exts->get_max_samples() >= 8) supported_aa_modes.push_back(rtt::TAA_MSAA_8);
	if(exts->is_fbo_multisample_coverage_mode_support(16, 4)) supported_aa_modes.push_back(rtt::TAA_CSAA_16);
	if(exts->is_fbo_multisample_coverage_mode_support(16, 8)) supported_aa_modes.push_back(rtt::TAA_CSAA_16Q);
	if(exts->get_max_samples() >= 16) supported_aa_modes.push_back(rtt::TAA_MSAA_16);
	if(exts->get_max_samples() >= 32) supported_aa_modes.push_back(rtt::TAA_MSAA_32);
	if(exts->get_max_samples() >= 64) supported_aa_modes.push_back(rtt::TAA_MSAA_64);*/
	
	bool chosen_aa_mode_supported = false;
	for(vector<rtt::TEXTURE_ANTI_ALIASING>::iterator aaiter = supported_aa_modes.begin(); aaiter != supported_aa_modes.end(); aaiter++) {
		if(*aaiter == config.anti_aliasing) chosen_aa_mode_supported = true;
	}

	// if the chosen anti-aliasing mode isn't supported, use the next best one
	if(!chosen_aa_mode_supported) {
		config.anti_aliasing = supported_aa_modes.back();
		a2e_error("your chosen anti-aliasing mode isn't supported by your graphic card - using \"%s\" instead!", rtt::TEXTURE_ANTI_ALIASING_STR[config.anti_aliasing]);
	}
	else a2e_debug("using \"%s\" anti-aliasing", rtt::TEXTURE_ANTI_ALIASING_STR[config.anti_aliasing]);

	// check anisotropic
	if(config.anisotropic > exts->get_max_anisotropic_filtering()) {
		config.anisotropic = exts->get_max_anisotropic_filtering();
		a2e_error("your chosen anisotropic-filtering value isn't supported by your graphic card - using \"%u\" instead!", config.anisotropic);
	}
	else a2e_debug("using \"%ux\" anisotropic-filtering", config.anisotropic);
	
	// create texture manager and render to texture object
	t = new texman(f, u, exts, datapath, config.anisotropic);
	r = new rtt(this, g, exts, (unsigned int)config.width, (unsigned int)config.height);

	// print out informations about additional threads
#ifdef A2E_USE_OPENMP
	omp_set_num_threads(config.thread_count);
	config.thread_count = omp_get_max_threads();
#endif
	
	// if GL_RENDERER is that damn m$ gdi driver, exit a2e 
	// no official support for this crappy piece of software 
	if(strcmp((const char*)glGetString(GL_RENDERER), "GDI Generic") == 0) {
		a2e_error("A2E doesn't support the MS GDI Generic driver!\nGo and install one of these (that match your grapic card):\nhttp://www.ati.com  http://www.nvidia.com  http://www.intel.com");
		SDL_Delay(10000);
		exit(1);
	}

	// set standard texture filtering + anisotropic filtering
	t->set_filtering(config.filtering);

	// get standard (sdl internal) cursor and create cursor data
	standard_cursor = SDL_GetCursor();
	cursors["STANDARD"] = standard_cursor;

	// seed
	const unsigned int rseed = ((unsigned int)time(NULL)/SDL_GetTicks())*((unsigned int)time(NULL)%SDL_GetTicks());
	srand(rseed >> 1);
	
	// init/create shaders, init gfx
	shd = new shader(this);
	g->init();
	
	// init opencl
	ocl->init(false, config.opencl_platform);
}

/*! sets the windows width
 *  @param width the window width
 */
void engine::set_width(unsigned int width) {
	config.width = width;
	// TODO: ...
}

/*! sets the window height
 *  @param height the window height
 */
void engine::set_height(unsigned int height) {
	config.height = height;
	// TODO: ...
}

/*! starts drawing the window
 */
void engine::start_draw() {
	// draws ogl stuff
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glViewport(0, 0, (unsigned int)config.width, (unsigned int)config.height);
	
	// clear the color and depth buffers
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
	
	// reset model view matrix
	modelview_matrix.identity();
	translation_matrix.identity();
	rotation_matrix.identity();
	mvp_matrix = projection_matrix;
	
	//
	static bool vao_init = false;
	static GLuint global_vao = 0;
	if(!vao_init) {
		vao_init = true;
		
		glGenVertexArrays(1, &global_vao);
	}
	glBindVertexArray(global_vao);
}

/*! stops drawing the window
 */
void engine::stop_draw() {
	glBindVertexArray(0);
	swap();
	
	GLenum error = glGetError();
	switch(error) {
		case GL_NO_ERROR:
			break;
		case GL_INVALID_ENUM:
			a2e_error("OpenGL error: invalid enum!");
			break;
		case GL_INVALID_VALUE:
			a2e_error("OpenGL error: invalid value!");
			break;
		case GL_INVALID_OPERATION:
			a2e_error("OpenGL error: invalid operation!");
			break;
		case GL_OUT_OF_MEMORY:
			a2e_error("OpenGL error: out of memory!");
			break;
		case GL_INVALID_FRAMEBUFFER_OPERATION:
			a2e_error("OpenGL error: invalid framebuffer operation!");
			break;
		default:
			a2e_error("unknown OpenGL error: %u!");
			break;
	}
	
	frame_time_sum += SDL_GetTicks() - frame_time_counter;

	// fps "limiter"
	if(config.fps_limit != 0) SDL_Delay((unsigned int)config.fps_limit);

	// handle fps count
	fps_counter++;
	if(SDL_GetTicks() - fps_time > 1000) {
		fps = fps_counter;
		new_fps_count = true;
		fps_counter = 0;
		fps_time = SDL_GetTicks();
		
		frame_time = (float)frame_time_sum / (float)fps;
		frame_time_sum = 0;
	}
	frame_time_counter = SDL_GetTicks();
	
	// check for shader/kernel reload (this is safe to do here)
	if(AtomicGet(&reload_shaders_flag) == 1) {
		AtomicSet(&reload_shaders_flag, 0);
		glFlush();
		glFinish();
		ocl->flush();
		ocl->finish();
		shd->reload_shaders();
	}
	if(AtomicGet(&reload_kernels_flag) == 1) {
		AtomicSet(&reload_kernels_flag, 0);
		glFlush();
		glFinish();
		ocl->flush();
		ocl->finish();
		ocl->reload_kernels();
	}
}

void engine::push_ogl_state() {
	// make a full soft-context-switch
	glGetIntegerv(GL_BLEND_SRC, &pushed_blend_src);
	glGetIntegerv(GL_BLEND_DST, &pushed_blend_dst);
	glGetIntegerv(GL_BLEND_SRC_RGB, &pushed_blend_src_rgb);
	glGetIntegerv(GL_BLEND_DST_RGB, &pushed_blend_dst_rgb);
	glGetIntegerv(GL_BLEND_SRC_ALPHA, &pushed_blend_src_alpha);
	glGetIntegerv(GL_BLEND_DST_ALPHA, &pushed_blend_dst_alpha);
}

void engine::pop_ogl_state() {
	// make a full soft-context-switch, restore all values
	
	glBlendFunc(pushed_blend_src, pushed_blend_dst);
	glBlendFuncSeparate(pushed_blend_src_rgb, pushed_blend_dst_rgb,
						pushed_blend_src_alpha, pushed_blend_dst_alpha);
}

/*! sets the window caption
 *  @param caption the window caption
 */
void engine::set_caption(const char* caption) {
	SDL_SetWindowTitle(config.wnd, caption);
}

/*! returns the window caption
 */
const char* engine::get_caption() {
	return SDL_GetWindowTitle(config.wnd);
}

/*! opengl initialization function
 */
bool engine::init_gl() {
	// set clear color
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	// depth buffer setup
	glClearDepth(1.0f);
	// enable depth testing
	glEnable(GL_DEPTH_TEST);
	// less/equal depth test
	glDepthFunc(GL_LEQUAL);
	// enable backface culling
	glCullFace(GL_BACK);
	glEnable(GL_CULL_FACE);
	glFrontFace(GL_CCW);

	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	return true;
}

/* function to reset our viewport after a window resize
 */
bool engine::resize_window() {
	// set the viewport
	glViewport(0, 0, (GLsizei)config.width, (GLsizei)config.height);

	// projection matrix
	// set perspective with fov (default = 72) and near/far plane value (default = 1.0f/1000.0f)
	projection_matrix.perspective(config.fov, (float)config.width / (float)config.height,
								  config.near_far_plane.x, config.near_far_plane.y);

	// model view matrix
	modelview_matrix.identity();
	translation_matrix.identity();
	rotation_matrix.identity();
	mvp_matrix = projection_matrix;

	return true;
}

/*! sets the position of the user/viewer
 *  @param xpos x coordinate
 *  @param ypos y coordinate
 *  @param zpos z coordinate
 */
void engine::set_position(float xpos, float ypos, float zpos) {
	position.set(xpos, ypos, zpos);
	
	translation_matrix = matrix4f().translate(xpos, ypos, zpos);
	modelview_matrix = translation_matrix * rotation_matrix;
	mvp_matrix = modelview_matrix * projection_matrix;
}

/*! sets the rotation of the user/viewer
 *  @param xrot x rotation
 *  @param yrot y rotation
 */
void engine::set_rotation(float xrot, float yrot) {
	rotation.x = xrot;
	rotation.y = yrot;
	
	rotation_matrix = matrix4f().rotate_y(yrot) * matrix4f().rotate_x(xrot);
	modelview_matrix = translation_matrix * rotation_matrix;
	mvp_matrix = modelview_matrix * projection_matrix;
}

/*! returns the position of the user
 */
float3* engine::get_position() {
	return &position;
}

/*! returns the rotation of the user
 */
float3* engine::get_rotation() {
	return &rotation;
}

/*! starts drawing the 2d elements and initializes the opengl functions for that
 */
void engine::start_2d_draw() {
	start_2d_draw((unsigned int)config.width, (unsigned int)config.height, false);
}

void engine::start_2d_draw(const unsigned int width, const unsigned int height, const bool fbo) {
	glViewport(0, 0, width, height);

	// we need an orthogonal view (2d) for drawing 2d elements
	push_projection_matrix();
	if(!fbo) projection_matrix.ortho(0, width, height, 0, -1.0, 1.0);
	else projection_matrix.ortho(0, width, 0, height, -1.0, 1.0);

	push_modelview_matrix();
	modelview_matrix.identity();
	
	glFrontFace(GL_CW);
	mvp_matrix = projection_matrix;
	glDisable(GL_CULL_FACE); // TODO: GL3, remove again
	
	// shaders are using pre-multiplied alpha
	glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
	glDisable(GL_BLEND);
}

/*! stops drawing the 2d elements
 */
void engine::stop_2d_draw() {
	pop_projection_matrix();
	pop_modelview_matrix();
	glFrontFace(GL_CCW);
	glEnable(GL_CULL_FACE); // TODO: GL3, remove again
}

/*! sets the cursors visibility to state
 *  @param state the cursor visibility state
 */
void engine::set_cursor_visible(bool state) {
	engine::cursor_visible = state;
	SDL_ShowCursor(engine::cursor_visible);
}

/*! returns the cursor visibility stateo
 */
bool engine::get_cursor_visible() {
	return engine::cursor_visible;
}

/*! sets the fps limit (max. fps = 1000 / ms)
 *! note that a value of 0 increases the cpu usage to 100%
 *  @param ms how many milliseconds the engine should "sleep" after a frame is rendered
 */
void engine::set_fps_limit(unsigned int ms) {
	config.fps_limit = ms;
}

/*! returns how many milliseconds the engine is "sleeping" after a frame is rendered
 */
unsigned int engine::get_fps_limit() {
	return (unsigned int)config.fps_limit;
}

/*! returns the type of the initialization (0 = GRAPHICAL, 1 = CONSOLE)
 */
unsigned int engine::get_init_mode() {
	return engine::mode;
}

/*! returns a pointer to the core class
 */
core* engine::get_core() {
	return engine::c;
}

/*! returns a pointer to the file_io class
 */
file_io* engine::get_file_io() {
	return engine::f;
}

/*! returns a pointer to the file_io class
 */
event* engine::get_event() {
	return engine::e;
}

/*! returns the gfx class
 */
gfx* engine::get_gfx() {
	return engine::g;
}

/*! returns the texman class
 */
texman* engine::get_texman() {
	return engine::t;
}

/*! returns the extensions class
 */
ext* engine::get_ext() {
	return engine::exts;
}

/*! returns the xml class
 */
xml* engine::get_xml() {
	return engine::x;
}

/*! returns the rtt class
 */
rtt* engine::get_rtt() {
	return engine::r;
}

/*! returns the unicode class
 */
unicode* engine::get_unicode() {
	return engine::u;
}

/*! returns the opencl class
 */
opencl* engine::get_opencl() {
	return engine::ocl;
}

/*! returns the shader class
 */
shader* engine::get_shader() {
	return engine::shd;
}

/*! sets the data path
 *  @param data_path the data path
 */
void engine::set_data_path(const char* data_path) {
	engine::datapath = data_path;
}

/*! returns the data path
 */
string engine::get_data_path() const {
	return datapath;
}

/*! returns the call path
 */
string engine::get_call_path() const {
	return callpath;
}

/*! returns the shader path
 */
string engine::get_shader_path() const {
	return shaderpath;
}

/*! returns the kernel path
 */
string engine::get_kernel_path() const {
	return kernelpath;
}

/*! returns data path + str
 *  @param str str we want to "add" to the data path
 */
string engine::data_path(const string& str) const {
	if(str.length() == 0) return datapath;
	return datapath + str;
}

/*! returns data path + shader path + str
 *  @param str str we want to "add" to the data + shader path
 */
string engine::shader_path(const string& str) const {
	if(str.length() == 0) return datapath + shaderpath;
	return datapath + shaderpath + str;
}

/*! returns data path + kernel path + str
 *  @param str str we want to "add" to the data + kernel path
 */
string engine::kernel_path(const string& str) const {
	if(str.length() == 0) return datapath + kernelpath;
	return datapath + kernelpath + str;
}

/*! strips the data path from a string
 *  @param str str we want strip the data path from
 */
string engine::strip_data_path(const string& str) const {
	if(str.length() == 0) return "";
	return core::find_and_replace(str, datapath, "");
}

engine::server_data* engine::get_server_data() {
	return &config.server;
}

engine::client_data* engine::get_client_data() {
	return &config.client;
}

void engine::load_ico(const char* ico) {
	SDL_SetWindowIcon(config.wnd, IMG_Load(data_path(ico).c_str()));
}

texture_object::TEXTURE_FILTERING engine::get_filtering() {
	return config.filtering;
}

size_t engine::get_anisotropic() {
	return config.anisotropic;
}

rtt::TEXTURE_ANTI_ALIASING engine::get_anti_aliasing() {
	return config.anti_aliasing;
}

matrix4f* engine::get_projection_matrix() {
	return &(engine::projection_matrix);
}

matrix4f* engine::get_modelview_matrix() {
	return &(engine::modelview_matrix);
}

matrix4f* engine::get_mvp_matrix() {
	return &(engine::mvp_matrix);
}

matrix4f* engine::get_translation_matrix() {
	return &(engine::translation_matrix);
}

matrix4f* engine::get_rotation_matrix() {
	return &(engine::rotation_matrix);
}

unsigned int engine::get_fps() {
	new_fps_count = false;
	return engine::fps;
}

float engine::get_frame_time() {
	return engine::frame_time;
}

bool engine::is_new_fps_count() {
	return engine::new_fps_count;
}

SDL_Cursor* engine::add_cursor(const char* name, const char** raw_data, unsigned int xsize, unsigned int ysize, unsigned int hotx, unsigned int hoty) {
	if(cursors.count(name)) {
		a2e_error("cursor with such a name (%s) already exists!", name);
		return NULL;
	}

	if((xsize != 16 || ysize != 16) && (xsize != 32 || ysize != 32)) {
		a2e_error("invalid cursor size (%ux%u) - must be either 16x16 or 32x32!", xsize, ysize);
		return NULL;
	}

	if(xsize == 16) {
		cursor_data = cursor_data16;
		cursor_mask = cursor_mask16;
		memset(cursor_data, 0, 2*16);
		memset(cursor_mask, 0, 2*16);
	}
	else {
		cursor_data = cursor_data32;
		cursor_mask = cursor_mask32;
		memset(cursor_data, 0, 4*32);
		memset(cursor_mask, 0, 4*32);
	}

	unsigned int data_byte = 0;
	unsigned int data_bit = 0;
	for(unsigned int cy = 0; cy < xsize; cy++) {
		for(unsigned int cx = 0; cx < ysize; cx++) {
			data_byte = cy*(xsize/8) + cx/8;
			data_bit = 7 - (cx - (cx/8)*8);
			switch(raw_data[cy][cx]) {
				case ' ':
					// nothing ...
					break;
				case 'X':
					cursor_data[data_byte] |= (1 << data_bit);
					cursor_mask[data_byte] |= (1 << data_bit);
					break;
				case '.':
					cursor_mask[data_byte] |= (1 << data_bit);
					break;
				case 'I':
					cursor_data[data_byte] |= (1 << data_bit);
					break;
				default:
					break;
			}
		}
	}
	cursors[name] = SDL_CreateCursor(cursor_data, cursor_mask, (int)xsize, (int)ysize, (int)hotx, (int)hoty);
	return cursors[name];
}

void engine::set_cursor(SDL_Cursor* cursor) {
	SDL_SetCursor(cursor);
}

SDL_Cursor* engine::get_cursor(const char* name) {
	if(!cursors.count(name)) {
		a2e_error("no cursor with such a name (%s) exists!", name);
		return NULL;
	}
	return cursors[name];
}

bool engine::get_fullscreen() {
	return config.fullscreen;
}

bool engine::get_vsync() {
	return config.vsync;
}

bool engine::get_stereo() {
	return config.stereo;
}

unsigned int engine::get_width() {
	return (unsigned int)config.width;
}

unsigned int engine::get_height() {
	return (unsigned int)config.height;
}

unsigned int engine::get_key_repeat() {
	return (unsigned int)config.key_repeat;
}

unsigned int engine::get_ldouble_click_time() {
	return (unsigned int)config.ldouble_click_time;
}

unsigned int engine::get_mdouble_click_time() {
	return (unsigned int)config.mdouble_click_time;
}

unsigned int engine::get_rdouble_click_time() {
	return (unsigned int)config.rdouble_click_time;
}

string* engine::get_disabled_extensions() {
	return &config.disabled_extensions;
}

string* engine::get_force_device() {
	return &config.force_device;
}

string* engine::get_force_vendor() {
	return &config.force_vendor;
}

SDL_Window* engine::get_window() {
	return config.wnd;
}

size_t engine::get_inferred_scale() const {
	return config.inferred_scale;
}

const string engine::get_version() const {
	return A2E_VERSION_STRING;
}

void engine::swap() {
	SDL_GL_SwapWindow(config.wnd);
}

void engine::push_projection_matrix() {
	projm_stack.push_back(new matrix4f(projection_matrix));
}

void engine::pop_projection_matrix() {
	matrix4f* pmat = projm_stack.back();
	projection_matrix = *pmat;
	delete pmat;
	projm_stack.pop_back();
}

void engine::push_modelview_matrix() {
	mvm_stack.push_back(new matrix4f(modelview_matrix));
}

void engine::pop_modelview_matrix() {
	matrix4f* pmat = mvm_stack.back();
	modelview_matrix = *pmat;
	mvp_matrix = modelview_matrix * projection_matrix;
	delete pmat;
	mvm_stack.pop_back();
}

void engine::reload_shaders() {
	AtomicSet(&reload_shaders_flag, 1);
}

void engine::reload_kernels() {
	AtomicSet(&reload_kernels_flag, 1);
}

const float& engine::get_fov() const {
	return config.fov;
}

const float2& engine::get_near_far_plane() const {
	return config.near_far_plane;
}

const xml::xml_doc& engine::get_config_doc() const {
	return config_doc;
}