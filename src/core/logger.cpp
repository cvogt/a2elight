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


#include "logger.h"

#if !defined(A2E_IOS)
#define A2E_LOG_FILENAME "log.txt"
#else
#define A2E_LOG_FILENAME "/tmp/a2e_log.txt"
#endif

ofstream logger::log_file(A2E_LOG_FILENAME);
atomic_t logger::err_counter;
SDL_SpinLock logger::slock;

void logger::init() {
	logger::err_counter.value = 0;
	if(!log_file.is_open()) {
		cout << "LOG ERROR: couldn't open log file!" << endl;
	}
}

void logger::destroy() {
	log_file.close();
}

void logger::prepare_log(stringstream& buffer, const LOG_TYPE& type, const char* file, const char* func) {
	if(type != logger::LT_NONE) {
		switch(type) {
			case LT_ERROR:
				buffer << "\033[31m";
				break;
			case LT_DEBUG:
				buffer << "\033[32m";
				break;
			case LT_MSG:
				buffer << "\033[34m";
				break;
			default: break;
		}
		buffer << logger::type_to_str(type);
		switch(type) {
			case LT_ERROR:
			case LT_DEBUG:
			case LT_MSG:
				buffer << "\E[m";
				break;
			default: break;
		}
		if(type == logger::LT_ERROR) buffer << " #" << AtomicFetchThenIncrement(&err_counter) << ":";
		buffer << " ";
		// prettify file string (aka strip path)
		string file_str = file;
		size_t slash_pos = string::npos;
		if((slash_pos = file_str.rfind("/")) == string::npos) slash_pos = file_str.rfind("\\");
		file_str = (slash_pos != string::npos ? file_str.substr(slash_pos+1, file_str.size()-slash_pos-1) : file_str);
		buffer << file_str;
		buffer << ": " << func << "(): ";
	}
}

void logger::_log(stringstream& buffer, const char* str) {
	// this is the final log function
	while(*str) {
		if(*str == '%' && *(++str) != '%') {
			cout << "LOG ERROR: invalid log format, missing arguments!" << endl;
		}
		buffer << *str++;
	}
	buffer << endl;
	
	// finally: output
	SDL_AtomicLock(&slock);
	string bstr(buffer.str());
	cout << bstr;
	if(bstr[0] != 0x1B) {
		log_file << bstr;
	}
	else {
		bstr.erase(0, 5);
		bstr.erase(7, 3);
		log_file << bstr;
	}
	log_file.flush();
	SDL_AtomicUnlock(&slock);
}

