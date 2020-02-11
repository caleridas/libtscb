/* -*- C++ -*-
 * (c) 2020 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1.
 * Refer to the file "COPYING" for details.
 */

#ifndef TSCB_DETAIL_INTRUSIVE_PTR_H
#define TSCB_DETAIL_INTRUSIVE_PTR_H

namespace tscb {
namespace detail {

/* This is an intrusive_ptr (refcounted objects, refcounter part of
 * object itself) compatible with boost::intrusive_ptr: Objects may
 * be tracked by both and share refcounter in the same way. Reimplemeted
 * here to avoid depending on entirety of boost just for this simple
 * class. Please use boost::intrusive_ptr directly outside the scope
 * of this library. */

template<typename T>
class intrusive_ptr {
public:
	inline ~intrusive_ptr() noexcept
	{
		reset();
	}

	inline constexpr intrusive_ptr() noexcept : repr_(nullptr) { }

	inline explicit
	intrusive_ptr(T* ptr, bool take_reference=true) noexcept
		: repr_(ptr)
	{
		if (repr_ && take_reference) {
			intrusive_ptr_add_ref(ptr);
		}
	}

	inline
	intrusive_ptr(const intrusive_ptr& other) noexcept
		: intrusive_ptr(other.get())
	{}

	inline
	intrusive_ptr(intrusive_ptr&& other) noexcept
		: repr_(nullptr)
	{
		swap(other);
	}

	template<typename O>
	inline
	intrusive_ptr(const intrusive_ptr<O>& other) noexcept
		: intrusive_ptr(other.get())
	{
	}

	inline intrusive_ptr&
	operator=(const intrusive_ptr& other) noexcept
	{
		intrusive_ptr tmp(other);
		swap(tmp);
		return *this;
	}

	inline intrusive_ptr&
	operator=(intrusive_ptr&& other) noexcept
	{
		swap(other);
		return *this;
	}

	inline void
	reset(T* ptr=nullptr, bool take_reference=true)
	{
		if (repr_) {
			intrusive_ptr_release(repr_);
		}
		repr_ = ptr;
		if (repr_ && take_reference) {
			intrusive_ptr_add_ref(repr_);
		}
	}

	inline void
	swap(intrusive_ptr& other) noexcept
	{
		T* tmp = other.repr_;
		other.repr_ = repr_;
		repr_ = tmp;
	}

	inline operator bool() const noexcept { return repr_; }

	inline T* get() const noexcept { return repr_; }

	inline T* operator->() const noexcept { return repr_; }
	inline T& operator*() const noexcept { return *repr_; }

	inline T* detach() noexcept
	{
		T* ptr = repr_;
		repr_ = nullptr;
		return ptr;
	}

private:
	T* repr_;
};

}
}

#endif
