#pragma once
#include <mutex>
#include <shared_mutex>
namespace boost {
    template<typename M> using lock_guard        = std::lock_guard<M>;
    template<typename M> using unique_lock       = std::unique_lock<M>;
    template<typename M> using shared_lock       = std::shared_lock<M>;
}
