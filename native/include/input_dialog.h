#pragma once
#include <functional>
#include <string>

namespace InputDialog {
    void Init();
    // Show the input window; onSubmit fires from the window thread with the typed text.
    void Show(std::function<void(std::string)> onSubmit);
}
