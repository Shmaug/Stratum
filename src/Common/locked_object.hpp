#pragma once

#include "common.hpp"

namespace stm {

template <typename T>
class locked_object {
private:
	T mObject;
	mutable mutex mMutex;
public:

	class locked_ptr_t {
	private:
		T* mObject;
		scoped_lock<mutex> mLock;
	public:
		inline locked_ptr_t(T* object, mutex& m) : mObject(object), mLock(scoped_lock<mutex>(m)) {};
		inline T& operator*() { return *mObject; }
		inline T* operator->() { return mObject; }
	};
	class locked_cptr_t {
	private:
		const T* mObject;
		scoped_lock<mutex> mLock;
	public:
		inline locked_cptr_t(const T* object, mutex& m) : mObject(object), mLock(scoped_lock<mutex>(m)) {};
		inline const T& operator*() { return *mObject; }
		inline const T* operator->() { return mObject; }
	};

	inline auto lock() { return locked_ptr_t(&mObject, mMutex); }
	inline auto lock() const { return locked_cptr_t(&mObject, mMutex); }
	inline mutex& m() const { return mMutex; }
};

}