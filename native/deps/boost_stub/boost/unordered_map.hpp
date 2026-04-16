#pragma once
// Minimal boost::unordered_map stub — wraps std::unordered_map
#include <unordered_map>
#include <functional>

namespace boost {

template<typename T>
struct hash {
    size_t operator()(const T& v) const { return std::hash<T>{}(v); }
};
// pointer specialisation
template<typename T>
struct hash<T*> {
    size_t operator()(T* p) const { return std::hash<T*>{}(p); }
};

namespace unordered {
    template<typename K, typename V,
             typename H = boost::hash<K>,
             typename P = std::equal_to<K>,
             typename A = std::allocator<std::pair<const K, V>>>
    using unordered_map = std::unordered_map<K, V, H, P, A>;
}

template<typename K, typename V,
         typename H = boost::hash<K>,
         typename P = std::equal_to<K>,
         typename A = std::allocator<std::pair<const K, V>>>
using unordered_map = std::unordered_map<K, V, H, P, A>;

} // namespace boost
