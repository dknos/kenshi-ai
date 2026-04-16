#pragma once
#include <condition_variable>
namespace boost {
    using condition_variable = std::condition_variable;
    using condition_variable_any = std::condition_variable_any;
    using condition = std::condition_variable_any;
    using condition_variable_base = std::condition_variable_any;
}
