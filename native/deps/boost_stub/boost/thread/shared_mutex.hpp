#pragma once
// Minimal stub — aliases std equivalents for compilation only
#include <shared_mutex>
namespace boost {
    using shared_mutex      = std::shared_mutex;
    using upgrade_mutex     = std::shared_mutex;
    using shared_timed_mutex = std::shared_timed_mutex;
    template<typename M> using shared_lock    = std::shared_lock<M>;
    template<typename M> using upgrade_lock   = std::shared_lock<M>;
}
