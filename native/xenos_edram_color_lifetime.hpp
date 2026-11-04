#pragma once

namespace dotm {

// A guest VdSwap is NOT a clear of the original persistent EDRAM color.
// RB_COPY can read pixels rendered before the latest presentation. A newly
// allocated host attachment is invalid until a genuine guest DRAW executes.
class XenosEdramColorLifetime {
public:
    void on_render_target_created() noexcept { has_original_draw_ = false; }
    void on_original_draw_submitted() noexcept { has_original_draw_ = true; }
    void on_vd_swap() noexcept {
        // Present does not invalidate the guest EDRAM attachment.
    }
    bool can_resolve() const noexcept { return has_original_draw_; }
private:
    bool has_original_draw_ = false;
};

} // namespace dotm
