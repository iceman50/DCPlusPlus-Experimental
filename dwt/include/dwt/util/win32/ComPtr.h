/*
  DC++ Widget Toolkit

  Copyright (C) 2026 iceman50

  All rights reserved.

  Redistribution and use in source and binary forms, with or without modification,
  are permitted provided that the following conditions are met:

      * Redistributions of source code must retain the above copyright notice,
        this list of conditions and the following disclaimer.
      * Redistributions in binary form must reproduce the above copyright notice,
        this list of conditions and the following disclaimer in the documentation
        and/or other materials provided with the distribution.
      * Neither the name of the DWT nor the names of its contributors
        may be used to endorse or promote products derived from this software
        without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
*/

#ifndef DWT_UTIL_WIN32_COMPTR_H
#define DWT_UTIL_WIN32_COMPTR_H

namespace dwt { namespace util { namespace win32 {

/** Owns a COM interface reference. Copies AddRef; moves transfer ownership.
 * put() releases the current reference before receiving an owned result;
 * detach() transfers the reference to the caller without releasing it. */
template<typename T>
class ComPtr {
public:
	ComPtr() : value(nullptr) { }

	explicit ComPtr(T* ptr, bool addRef = true) : value(ptr) {
		if(value && addRef) {
			value->AddRef();
		}
	}

	ComPtr(const ComPtr& rhs) : value(rhs.value) {
		if(value) {
			value->AddRef();
		}
	}

	ComPtr(ComPtr&& rhs) noexcept : value(rhs.value) {
		rhs.value = nullptr;
	}

	~ComPtr() {
		reset();
	}

	ComPtr& operator=(const ComPtr& rhs) {
		if(this != &rhs) {
			reset(rhs.value);
		}
		return *this;
	}

	ComPtr& operator=(ComPtr&& rhs) noexcept {
		if(this != &rhs) {
			reset();
			value = rhs.value;
			rhs.value = nullptr;
		}
		return *this;
	}

	T* get() const { return value; }
	T** put() {
		reset();
		return &value;
	}
	T* operator->() const { return value; }
	explicit operator bool() const { return value != nullptr; }

	void reset(T* ptr = nullptr, bool addRef = true) {
		if(ptr && addRef) {
			ptr->AddRef();
		}
		if(value) {
			value->Release();
		}
		value = ptr;
	}

	T* detach() {
		auto result = value;
		value = nullptr;
		return result;
	}

private:
	T* value;
};

} } }

#endif
