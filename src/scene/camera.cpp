/*
 *  Albion 2 Engine "light"
 *  Copyright (C) 2004 - 2014 Florian Ziesche
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

#include "camera.hpp"
#include "engine.hpp"
#include <floor/core/event.hpp>

camera::camera() : evt(floor::get_event()),
keyboard_handler(bind(&camera::key_handler, this, placeholders::_1, placeholders::_2)) {
	evt->add_event_handler(keyboard_handler, EVENT_TYPE::KEY_DOWN, EVENT_TYPE::KEY_UP);
}

camera::~camera() {
	evt->remove_event_handler(keyboard_handler);
}

/*! runs the camera (expected to be called every draw)
 */
void camera::run() {
	if(keyboard_input) {
		// ... recalculate the cameras position
		if(key_state[0]) {
			position.x += (float)sin((rotation.y - 90.0) * const_math::PI_DIV_180<double>) * cam_speed;
			position.z -= (float)cos((rotation.y - 90.0) * const_math::PI_DIV_180<double>) * cam_speed;
		}
		if(key_state[1]) {
			position.x -= (float)sin((rotation.y - 90.0) * const_math::PI_DIV_180<double>) * cam_speed;
			position.z += (float)cos((rotation.y - 90.0) * const_math::PI_DIV_180<double>) * cam_speed;
		}
		if(key_state[2]) {
			position.x -= (float)sin(rotation.y * const_math::PI_DIV_180<double>) * cam_speed;
			position.y += (float)sin(rotation.x * const_math::PI_DIV_180<double>) * cam_speed;
			position.z += (float)cos(rotation.y * const_math::PI_DIV_180<double>) * cam_speed;
		}
		if(key_state[3]) {
			position.x += (float)sin(rotation.y * const_math::PI_DIV_180<double>) * cam_speed;
			position.y -= (float)sin(rotation.x * const_math::PI_DIV_180<double>) * cam_speed;
			position.z -= (float)cos(rotation.y * const_math::PI_DIV_180<double>) * cam_speed;
		}
	}

	if(mouse_input) {
		// calculate the rotation via the current mouse cursor position
		int cursor_pos_x = 0;
		int cursor_pos_y = 0;
	
////////////////////////////////
// linux/windows version
#if !defined(__APPLE__)
		SDL_GetMouseState(&cursor_pos_x, &cursor_pos_y);

		double xpos = (1.0 / (double)floor::get_width()) * (double)cursor_pos_x;
		double ypos = (1.0 / (double)floor::get_height()) * (double)cursor_pos_y;
		
		if(xpos != 0.5 || ypos != 0.5) {
			rotation.x -= (0.5 - ypos) * (double)rotation_speed;
			rotation.y -= (0.5 - xpos) * (double)rotation_speed;
			SDL_WarpMouseInWindow(floor::get_window(), (int)(floor::get_width()/2), (int)(floor::get_height()/2));
		}
////////////////////////////////
// os x version
#else
		SDL_GetRelativeMouseState(&cursor_pos_x, &cursor_pos_y);
		
		double xpos = (1.0 / (double)floor::get_width()) * (double)-cursor_pos_x;
		double ypos = (1.0 / (double)floor::get_height()) * (double)-cursor_pos_y;
		
		if(xpos != 0.0 || ypos != 0.0) {
			if(!ignore_next_rotation) {
				rotation.x -= ypos * (double)rotation_speed;
				rotation.y -= xpos * (double)rotation_speed;
			}
			else ignore_next_rotation--;
			
			const float2 center_point(float2(floor::get_width(), floor::get_height()) * 0.5f);
			SDL_WarpMouseInWindow(floor::get_window(), (int)roundf(center_point.x), (int)roundf(center_point.y));
		}
#endif
////////////////////////////////
	}

	// wrap around 360°
	rotation.x = const_math::wrap(rotation.x, 360.0f);
	rotation.y = const_math::wrap(rotation.y, 360.0f);

	// inform engine about position and rotation
	engine::set_position(position.x, position.y, position.z);
	engine::set_rotation(rotation.x, rotation.y);
}

/*! sets the position of the camera
 *  @param x x coordinate
 *  @param y y coordinate
 *  @param z z coordinate
 */
void camera::set_position(const float& x, const float& y, const float& z) {
	position.set(x, y, z);
}
void camera::set_position(const float3& pos) {
	position.set(pos);
}

/*! sets the rotation of the camera
 *  @param x x rotation
 *  @param y y rotation
 *  @param z z rotation
 */
void camera::set_rotation(const float& x, const float& y, const float& z) {
	rotation.set(x, y, z);
}
void camera::set_rotation(const float3& rot) {
	rotation.set(rot);
}

/*! returns the position of the camera
 */
float3& camera::get_position() {
	return position;
}
const float3& camera::get_position() const {
	return position;
}

/*! returns the rotation of the camera
 */
float3& camera::get_rotation() {
	return rotation;
}
const float3& camera::get_rotation() const {
	return rotation;
}

/*! if cam_input is set true then arrow key input and (auto-)reposition 
 *! stuff is done automatically in the camera class. otherwise you have
 *! to do it yourself
 *  @param state the cam_input state
 */
void camera::set_keyboard_input(const bool& state) {
	keyboard_input = state;
	
	// reset key state, in case a move key is still held
	key_state = { { false, false, false, false } };
}

/*! if mouse_input is set true then the cameras rotation is controlled via
 *! the mouse - furthermore the mouse cursor is reset to (0.5, 0.5) every cycle
 *! if it's set to false nothing (no rotation) happens
 *  @param state the cam_input state
 */
void camera::set_mouse_input(const bool& state) {
	// grab input
	SDL_SetWindowGrab(floor::get_window(), (state ? SDL_TRUE : SDL_FALSE));
	
#if defined(__APPLE__)
	// this effictively calls CGAssociateMouseAndMouseCursorPosition (which will lock the cursor to the window)
	// and subsequently handles all mouse moves in relative/delta mode
	SDL_SetRelativeMouseMode(state ? SDL_TRUE : SDL_FALSE);
	
	// this fixes some weird mouse positioning when switching from grab to non-grab mode
	if(mouse_input && !state) {
		const float2 center_point(float2(floor::get_width(), floor::get_height()) * 0.5f);
		SDL_WarpMouseInWindow(floor::get_window(), (int)roundf(center_point.x), (int)roundf(center_point.y));
	}
#endif
	
	ignore_next_rotation = 2;
	
	mouse_input = state;
}

/*! returns the cam_input bool
 */
bool camera::get_keyboard_input() const {
	return keyboard_input;
}

/*! returns the mouse_input bool
 */
bool camera::get_mouse_input() const {
	return mouse_input;
}

void camera::set_wasd_input(const bool& state) {
	wasd_input = state;
	
	// reset key state, in case a wasd key triggered it before
	key_state = { { false, false, false, false } };
}

bool camera::get_wasd_input() const {
	return wasd_input;
}

/*! sets the cameras rotation speed to speed
 *  @param speed the new rotation speed
 */
void camera::set_rotation_speed(const float& speed) {
	rotation_speed = speed;
}

/*! returns cameras rotation speed
 */
float camera::get_rotation_speed() const {
	return rotation_speed;
}

/*! sets the cameras speed to speed
 *  @param speed the new camera speed
 */
void camera::set_cam_speed(const float& speed) {
	cam_speed = speed;
}

/*! returns cameras speed
 */
float camera::get_cam_speed() const {
	return cam_speed;
}

/*! returns the cameras direction
 */
const float3& camera::get_direction() {
	direction.x = (float)-sin(rotation.y * const_math::PI_DIV_180<double>);
	direction.y = (float)sin(rotation.x * const_math::PI_DIV_180<double>);
	direction.z = (float)cos(rotation.y * const_math::PI_DIV_180<double>);
	return direction;
}

bool camera::key_handler(EVENT_TYPE type, shared_ptr<event_object> obj) {
	// if keyboard input flag is not set, return
	if(!keyboard_input) return false;
	
	if(type == EVENT_TYPE::KEY_DOWN) {
		const key_down_event& key_evt = (const key_down_event&)*obj;
		
		switch(key_evt.key) {
			case SDLK_RIGHT: key_state[0] = true; break;
			case SDLK_LEFT: key_state[1] = true; break;
			case SDLK_UP: key_state[2] = true; break;
			case SDLK_DOWN: key_state[3] = true; break;
		}
		
		if(wasd_input) {
			switch(key_evt.key) {
				case SDLK_d: key_state[0] = true; break;
				case SDLK_a: key_state[1] = true; break;
				case SDLK_w: key_state[2] = true; break;
				case SDLK_s: key_state[3] = true; break;
			}
		}
	}
	else { // EVENT_TYPE::KEY_UP
		const key_up_event& key_evt = (const key_up_event&)*obj;
		
		switch(key_evt.key) {
			case SDLK_RIGHT: key_state[0] = false; break;
			case SDLK_LEFT: key_state[1] = false; break;
			case SDLK_UP: key_state[2] = false; break;
			case SDLK_DOWN: key_state[3] = false; break;
		}
		
		if(wasd_input) {
			switch(key_evt.key) {
				case SDLK_d: key_state[0] = false; break;
				case SDLK_a: key_state[1] = false; break;
				case SDLK_w: key_state[2] = false; break;
				case SDLK_s: key_state[3] = false; break;
			}
		}
	}
	
	return true;
}
