#include "kernel_process_type.hpp"
#include <atomic>
#include <cassert>
#include <stdexcept>
#include <thread>

int main() {
    assert(dotm::ke_get_current_process_type() == 1U);
    dotm::ke_set_current_process_type(2U);
    assert(dotm::ke_get_current_process_type() == 2U);
    std::atomic<unsigned> worker_initial{99U}, worker_after{99U};
    std::thread worker([&] {
        worker_initial = dotm::ke_get_current_process_type();
        dotm::ke_set_current_process_type(0U);
        worker_after = dotm::ke_get_current_process_type();
    });
    worker.join();
    assert(worker_initial == 1U && worker_after == 0U);
    assert(dotm::ke_get_current_process_type() == 2U);
    bool invalid = false;
    try { dotm::ke_set_current_process_type(3U); }
    catch (const std::invalid_argument&) { invalid = true; }
    assert(invalid && dotm::ke_get_current_process_type() == 2U);
    dotm::ke_set_current_process_type(1U);
    assert(dotm::ke_get_current_process_type() == 1U);
}
