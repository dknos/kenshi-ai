#pragma once
// Minimal boost::unordered_set stub — wraps std::unordered_set
#include <unordered_set>
#include "unordered_map.hpp"  // pulls in boost::hash

namespace boost {

namespace unordered {
    template<typename K,
             typename H = boost::hash<K>,
             typename P = std::equal_to<K>,
             typename A = std::allocator<K>>
    using unordered_set = std::unordered_set<K, H, P, A>;
}

template<typename K,
         typename H = boost::hash<K>,
         typename P = std::equal_to<K>,
         typename A = std::allocator<K>>
using unordered_set = std::unordered_set<K, H, P, A>;

} // namespace boost
