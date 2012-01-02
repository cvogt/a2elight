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

/*! @brief platform header
 *  @author flo
 *
 *  do/include platform specific stuff here
 */

///////////////////////////////////////////////////////////////
// Windows
#if (defined(WIN32) || defined(_WIN32) || defined(WIN64) || defined(_WIN64) || defined(__WINDOWS__) || defined(MINGW)) && !defined(CYGWIN)

#include <windows.h>
#include <winnt.h>
#include <io.h>
#include <direct.h>

// defines
#ifndef __WINDOWS__
#define __WINDOWS__ 1
#endif

#ifndef strtof
#define strtof(arg1, arg2) ((float)strtod(arg1, arg2))
#endif

#ifndef __func__
#define __func__ __FUNCTION__
#endif

#ifndef __FLT_MAX__
#define __FLT_MAX__ FLT_MAX
#endif

#ifndef SIZE_T_MAX
#ifdef MAXSIZE_T
#define SIZE_T_MAX MAXSIZE_T
#else
#define SIZE_T_MAX (~((size_t)0))
#endif
#endif

#undef getcwd
#define getcwd _getcwd

#pragma warning(disable: 4251)
#pragma warning(disable: 4290) // unnecessary exception throw warning
#pragma warning(disable: 4503) // srsly microsoft? this ain't the '80s ...

// Mac OS X
#elif __APPLE__
#include <dirent.h>
#define A2E_API

// everything else (Linux, *BSD, ...)
#else
#define A2E_API
#include <dirent.h>

#ifndef SIZE_T_MAX
#define SIZE_T_MAX (~((size_t)0))
#endif

#endif // Windows


// general includes
#ifdef A2E_USE_OPENMP
#include <omp.h>
#endif

#ifdef __APPLE__
#include <SDL/SDL.h>
#include <SDL/SDL_thread.h>
#include <SDL/SDL_cpuinfo.h>
#include <SDL/SDL_platform.h>
#include <SDL/SDL_syswm.h>
#include <SDL_image/SDL_image.h>
#include <SDL_net/SDL_net.h>
#include <OpenGL/gl3.h>
#include <OpenGL/gl3ext.h>
#include <sys/types.h>
#include <pwd.h>
#include <unistd.h>

#elif defined(__WINDOWS__) && !defined(WIN_UNIXENV)
#include <SDL/SDL.h>
#include <SDL/SDL_thread.h>
#include <SDL/SDL_cpuinfo.h>
#include <SDL/SDL_platform.h>
#include <SDL/SDL_syswm.h>
#include <SDL/SDL_image.h>
#include <SDL/SDL_net.h>
#include <GL/gl3.h>
#include <GL/wglext.h>

#elif defined(MINGW)
#include <SDL/SDL.h>
#include <SDL/SDL_thread.h>
#include <SDL/SDL_cpuinfo.h>
#include <SDL/SDL_platform.h>
#include <SDL/SDL_syswm.h>
#include <SDL/SDL_image.h>
#include <SDL/SDL_net.h>
#define GL3_PROTOTYPES
#include <GL/gl3.h>

#else
#include <SDL.h>
#include <SDL_thread.h>
#include <SDL_cpuinfo.h>
#include <SDL_image.h>
#include <SDL_net.h>
#include <SDL_platform.h>
#include <SDL_syswm.h>
#include <GL/gl3.h>
#ifndef WIN_UNIXENV
#include <GL/glx.h>
#include <GL/glxext.h>
#endif
#include <sys/types.h>
#include <pwd.h>
#include <unistd.h>
#endif

#if (__GNUC__ == 4 && __GNUC_MINOR__ == 6)
// TODO: remove all the gcc workarounds ...
#define GCC_LEGACY 1
#endif

// c++ headers
#include "core/cpp_headers.h"

// a2e logger
#include "core/logger.h"

#ifndef __has_feature
#define __has_feature(x) 0
#endif
