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

#include "event.h"

event::event() : thread_base(),
#if !defined(GCC_LEGACY)
user_queue_lock()
#else
user_queue_lock(NULL)
#endif
{
	mouse_down_state[0] = mouse_down_state[1] = mouse_down_state[2] =
	mouse_up_state[0] = mouse_up_state[1] = mouse_up_state[2] =
		ipnt(-1, -1);
	
	lm_double_click_timer = SDL_GetTicks();
	rm_double_click_timer = SDL_GetTicks();
	mm_double_click_timer = SDL_GetTicks();
	
	ldouble_click_time = 200;
	rdouble_click_time = 200;
	mdouble_click_time = 200;

	shift = false;
	alt = false;
	
#if defined(GCC_LEGACY)
	user_queue_lock = SDL_CreateMutex();
#endif
	
	this->start();
}

event::~event() {
#if defined(GCC_LEGACY)
	SDL_DestroyMutex(user_queue_lock);
#endif
}

void event::run() {
	// user events are handled "asynchronously", so they don't
	// interfere with other (internal) events or engine code
	
	// copy/move user events to a processing queue, so the queue can
	// still be used in the other (main) event thread
#if !defined(GCC_LEGACY)
	user_queue_lock.lock();
	user_event_queue_processing.swap(user_event_queue);
	user_queue_lock.unlock();
#else
	if(SDL_mutexP(user_queue_lock) != -1) {
		user_event_queue_processing.swap(user_event_queue);
		SDL_mutexV(user_queue_lock);
	}
#endif
	
	handle_user_events();
}

/*! handles the sdl events
 */
void event::handle_events() {
	// internal engine event handler
	while(SDL_PollEvent(&event_handle)) {
		const unsigned int event_type = event_handle.type;
		const unsigned int cur_ticks = SDL_GetTicks();
		
		if(event_type == SDL_MOUSEBUTTONDOWN ||
		   event_type == SDL_MOUSEBUTTONUP) {
			// mouse event handling
			const int2 mouse_coord = int2(event_handle.button.x, event_handle.button.y);
			
			switch(event_type) {
				case SDL_MOUSEBUTTONDOWN: {
					switch(event_handle.button.button) {
						case SDL_BUTTON_LEFT:
							if(event_handle.button.state == SDL_PRESSED) {
								mouse_down_state[0] = mouse_coord;
								handle_event(EVENT_TYPE::MOUSE_LEFT_DOWN,
											 make_shared<mouse_left_down_event>(cur_ticks, mouse_coord));
							}
							break;
						case SDL_BUTTON_RIGHT:
							if(event_handle.button.state == SDL_PRESSED) {
								mouse_down_state[1] = mouse_coord;
								handle_event(EVENT_TYPE::MOUSE_RIGHT_DOWN,
											 make_shared<mouse_right_down_event>(cur_ticks, mouse_coord));
							}
							break;
						case SDL_BUTTON_MIDDLE:
							if(event_handle.button.state == SDL_PRESSED) {
								mouse_down_state[2] = mouse_coord;
								handle_event(EVENT_TYPE::MOUSE_MIDDLE_DOWN,
											 make_shared<mouse_middle_down_event>(cur_ticks, mouse_coord));
							}
							break;
					}
				}
				break;
				case SDL_MOUSEBUTTONUP: {
					switch(event_handle.button.button) {
						case SDL_BUTTON_LEFT:
							if(event_handle.button.state == SDL_RELEASED) {
								mouse_up_state[0] = mouse_coord;
								handle_event(EVENT_TYPE::MOUSE_LEFT_UP,
											 make_shared<mouse_left_up_event>(cur_ticks, mouse_coord));
								
								if(cur_ticks - lm_double_click_timer < ldouble_click_time) {
									// emit a double click event
									handle_event(EVENT_TYPE::MOUSE_LEFT_DOUBLE_CLICK,
												 make_shared<mouse_left_double_click_event>(
													cur_ticks,
													prev_events[EVENT_TYPE::MOUSE_LEFT_DOWN],
													prev_events[EVENT_TYPE::MOUSE_LEFT_UP]));
								}
								else {
									// only emit a normal click event
									handle_event(EVENT_TYPE::MOUSE_LEFT_CLICK,
												 make_shared<mouse_left_click_event>(
													cur_ticks,
													prev_events[EVENT_TYPE::MOUSE_LEFT_DOWN],
													prev_events[EVENT_TYPE::MOUSE_LEFT_UP]));
								}
								
								lm_double_click_timer = cur_ticks;
							}
							break;
						case SDL_BUTTON_RIGHT:
							if(event_handle.button.state == SDL_RELEASED) {
								mouse_up_state[1] = mouse_coord;
								handle_event(EVENT_TYPE::MOUSE_RIGHT_UP,
											 make_shared<mouse_right_up_event>(cur_ticks, mouse_coord));
								
								if(cur_ticks - rm_double_click_timer < rdouble_click_time) {
									// emit a double click event
									handle_event(EVENT_TYPE::MOUSE_RIGHT_DOUBLE_CLICK,
												 make_shared<mouse_right_double_click_event>(
													cur_ticks,
													prev_events[EVENT_TYPE::MOUSE_RIGHT_DOWN],
													prev_events[EVENT_TYPE::MOUSE_RIGHT_UP]));
								}
								else {
									// only emit a normal click event
									handle_event(EVENT_TYPE::MOUSE_RIGHT_CLICK,
												 make_shared<mouse_right_click_event>(
													cur_ticks,
													prev_events[EVENT_TYPE::MOUSE_RIGHT_DOWN],
													prev_events[EVENT_TYPE::MOUSE_RIGHT_UP]));
								}
								
								rm_double_click_timer = cur_ticks;
							}
							break;
						case SDL_BUTTON_MIDDLE:
							if(event_handle.button.state == SDL_RELEASED) {
								mouse_up_state[2] = mouse_coord;
								handle_event(EVENT_TYPE::MOUSE_MIDDLE_UP,
											 make_shared<mouse_middle_up_event>(cur_ticks, mouse_coord));
								
								if(SDL_GetTicks() - mm_double_click_timer < mdouble_click_time) {
									// emit a double click event
									handle_event(EVENT_TYPE::MOUSE_MIDDLE_DOUBLE_CLICK,
												 make_shared<mouse_middle_double_click_event>(
													cur_ticks,
													prev_events[EVENT_TYPE::MOUSE_MIDDLE_DOWN],
													prev_events[EVENT_TYPE::MOUSE_MIDDLE_UP]));
								}
								else {
									// only emit a normal click event
									handle_event(EVENT_TYPE::MOUSE_MIDDLE_CLICK,
												 make_shared<mouse_middle_click_event>(
													cur_ticks,
													prev_events[EVENT_TYPE::MOUSE_MIDDLE_DOWN],
													prev_events[EVENT_TYPE::MOUSE_MIDDLE_UP]));
								}
								
								mm_double_click_timer = cur_ticks;
							}
							break;
					}
				}
				break;
			}
		}
		else {
			// key, etc. event handling
			switch(event_type) {
				// TODO: use .unicode instead of .sym as soon as sdl really supports it
				case SDL_KEYUP:
					handle_event(EVENT_TYPE::KEY_UP,
								 make_shared<key_up_event>(cur_ticks, event_handle.key.keysym.sym));
					
					// TODO: !
					handle_event(EVENT_TYPE::KEY_PRESSED,
								 make_shared<key_up_event>(cur_ticks, event_handle.key.keysym.sym));
					break;
				case SDL_KEYDOWN:
					handle_event(EVENT_TYPE::KEY_DOWN,
								 make_shared<key_up_event>(cur_ticks, event_handle.key.keysym.sym));
					break;
				case SDL_TEXTINPUT:
					// TODO: !
					break;
				case SDL_QUIT:
					handle_event(EVENT_TYPE::QUIT, make_shared<quit_event>(cur_ticks));
					break;
				default: break;
			}
		}
	}
}

/*! gets the mouses position (pnt)
 */
pnt event::get_mouse_pos() const {
	pnt pos;
	SDL_GetMouseState((int*)&pos.x, (int*)&pos.y);
	return pos;
}

void event::set_ldouble_click_time(unsigned int dctime) {
	ldouble_click_time = dctime;
}

void event::set_rdouble_click_time(unsigned int dctime) {
	rdouble_click_time = dctime;
}

void event::set_mdouble_click_time(unsigned int dctime) {
	mdouble_click_time = dctime;
}

void event::add_event_handler(handler& handler_, EVENT_TYPE type) {
	handlers.insert(pair<EVENT_TYPE, handler&>(type, handler_));
}

void event::handle_event(const EVENT_TYPE& type, shared_ptr<event_object> obj) {
	// set/override last event for this type
	prev_events[type] = obj;
	
	// call internal event handlers directly
	const auto& range = internal_handlers.equal_range(type);
	for(auto iter = range.first; iter != range.second; iter++) {
		// ignore return value for now (TODO: actually use this?)
		iter->second(type, obj);
	}
	
	// push to user event queue (these will be handled later on)
#if !defined(GCC_LEGACY)
	user_queue_lock.lock();
	user_event_queue.push(make_pair(type, obj));
	user_queue_lock.unlock();
#else
	if(SDL_mutexP(user_queue_lock) != -1) {
		user_event_queue.push(make_pair(type, obj));
		SDL_mutexV(user_queue_lock);
	}
#endif
}

void event::handle_user_events() {
	while(!user_event_queue_processing.empty()) {
		// pop next event
		pair<EVENT_TYPE, shared_ptr<event_object>> evt = user_event_queue_processing.front();
		user_event_queue_processing.pop();
		
		// call user event handlers
		const auto& range = handlers.equal_range(evt.first);
		for(auto iter = range.first; iter != range.second; iter++) {
			iter->second(evt.first, evt.second);
		}
	}
}