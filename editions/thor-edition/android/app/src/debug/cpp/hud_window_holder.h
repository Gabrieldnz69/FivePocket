#pragma once
// hud2 (2026-10-22): the second screen's window (SecondScreen.java's Presentation) for a library that exports
// recomp_set_hud_window_v1 (tools/host_guest_trace/private_trace_hud_api.h). Same contract as PrivateFrameSurface's V6
// window: acquire returns a new reference (or null) and its generation; the generation changes at every attach, resize
// and detach. Nothing is drawn here: the library owns the window's swapchain while it has one.
#include <android/native_window.h>
#include <atomic>
#include <cstdint>
#include <mutex>
class HudWindowHolder final {
public:
    bool attach(ANativeWindow* owned){
        std::lock_guard lock(mutex_);
        if(!owned)return false;
        if(window_)ANativeWindow_release(window_);
        window_=owned;generation_.fetch_add(1,std::memory_order_acq_rel);return true;
    }
    void changed(){std::lock_guard lock(mutex_);if(window_)generation_.fetch_add(1,std::memory_order_acq_rel);}
    void detach(){
        std::lock_guard lock(mutex_);
        if(window_){ANativeWindow_release(window_);window_=nullptr;}
        generation_.fetch_add(1,std::memory_order_acq_rel);
    }
    ANativeWindow* acquire(std::uint64_t* generation){
        std::lock_guard lock(mutex_);
        if(generation)*generation=generation_.load(std::memory_order_acquire);
        if(!window_)return nullptr;
        ANativeWindow_acquire(window_);return window_;
    }
    static void release(ANativeWindow* window){if(window)ANativeWindow_release(window);}
    std::uint64_t generation() const noexcept {return generation_.load(std::memory_order_acquire);}
private:
    std::mutex mutex_;
    ANativeWindow* window_=nullptr;
    std::atomic<std::uint64_t> generation_{0};
};
