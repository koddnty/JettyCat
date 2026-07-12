#include "init.hpp"

const std::string& getInstanceId() {
    static std::string s_instance_id = std::to_string(
        std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
        ).count());
    return s_instance_id;
}