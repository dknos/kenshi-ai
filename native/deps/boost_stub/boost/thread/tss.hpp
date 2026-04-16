#pragma once
#include <memory>
namespace boost {
    // Minimal stub — one thread_local per type sufficient for compilation
    template<typename T>
    class thread_specific_ptr {
        static thread_local T* s_val;
    public:
        T* get() const   { return s_val; }
        T* operator->() const { return s_val; }
        T& operator*()  const { return *s_val; }
        void reset(T* p = nullptr) { delete s_val; s_val = p; }
        T* release()     { T* p = s_val; s_val = nullptr; return p; }
    };
    template<typename T> thread_local T* thread_specific_ptr<T>::s_val = nullptr;
}
