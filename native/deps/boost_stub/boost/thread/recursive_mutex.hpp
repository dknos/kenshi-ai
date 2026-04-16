#pragma once
#include <mutex>
namespace boost {
    using recursive_mutex = std::recursive_mutex;
    using recursive_timed_mutex = std::recursive_timed_mutex;
}
