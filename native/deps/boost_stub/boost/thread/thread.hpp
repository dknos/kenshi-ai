#pragma once
#include <thread>
namespace boost {
    using thread = std::thread;
    namespace this_thread {
        using std::this_thread::get_id;
        using std::this_thread::sleep_for;
        using std::this_thread::yield;
    }
}
