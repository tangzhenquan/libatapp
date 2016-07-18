#include <iostream>
#include <signal.h>
#include <typeinfo>

#include "atframe/atapp.h"

#include "cli/shell_font.h"


namespace atapp {
    module_impl::module_impl() : enabled_(true), owner_(NULL) {}
    module_impl::~module_impl() {}

    int module_impl::reload() { return 0; }

    int module_impl::stop() { return 0; }

    int module_impl::timeout() { return 0; }

    int module_impl::tick() { return 0; }

    const char *module_impl::name() const {
        const char *ret = typeid(*this).name();
        if (NULL == ret) {
            return "RTTI Unavailable";
        }

        // some compiler will generate number to mark the type
        while (ret && *ret >= '0' && *ret <= '9') {
            ++ret;
        }
        return ret;
    }

    bool module_impl::enable() {
        bool ret = enabled_;
        enabled_ = true;
        return ret;
    }

    bool module_impl::disable() {
        bool ret = enabled_;
        enabled_ = false;
        return ret;
    }

    app *module_impl::get_app() { return owner_; }

    const app *module_impl::get_app() const { return owner_; }
}
