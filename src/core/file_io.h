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

#ifndef __FILE_IO_H__
#define __FILE_IO_H__

#include "global.h"

#include "gui/unicode.h"

/*! @class file_io
 *  @brief file input/output
 */

class A2E_API file_io
{
public:
	file_io();
	~file_io();

	enum FIO_OPEN_TYPE {
		OT_READ,
		OT_READWRITE,
		OT_WRITE,
		OT_READ_BINARY,
		OT_READWRITE_BINARY,
		OT_WRITE_BINARY,
		OT_APPEND,
		OT_APPEND_BINARY,
		OT_APPEND_READ,
		OT_APPEND_READ_BINARY
	};
	
	enum FILE_TYPE {
		FT_NONE,
		FT_DIR,
		FT_ALL,
		FT_IMAGE,
		FT_A2E_MODEL,
		FT_A2E_ANIMATION,
		FT_A2E_MATERIAL,
		FT_A2E_MAP,
		FT_A2E_UI,
		FT_A2E_LIST,
		FT_A2E_SHADER,
		FT_XML,
		FT_TEXT,
		FT_OPENCL
	};

	bool open_file(const string& filename, FIO_OPEN_TYPE open_type);
	bool file_to_buffer(const string& filename, stringstream& buffer);
	void close_file();
	uint64_t get_filesize();
	void read_file(stringstream* buffer);
	void get_line(char* finput, unsigned int length);
	void get_block(char* data, size_t size);
	void get_terminated_block(string* str, char terminator);
	char get_char();
	unsigned short int get_usint();
	unsigned int get_uint();
	float get_float();
	fstream* get_filestream();
	void seek(size_t offset);
	streampos get_current_offset();
	
	void write_block(const char* data, size_t size, bool check_size = false);
	void write_terminated_block(string* str, char terminator);


	bool is_file(const char* filename);
	bool eof();

protected:
	fstream filestream;

	unsigned char tmp[8];

	bool check_open();

};

#endif