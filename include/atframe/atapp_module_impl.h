/**
 * atapp_module_impl.h
 *
 *  Created on: 2016年05月18日
 *      Author: owent
 */
#ifndef LIBATAPP_ATAPP_MODULE_IMPL_H_
#define LIBATAPP_ATAPP_MODULE_IMPL_H_

#pragma once

#include "std/explicit_declare.h"
#include "std/smart_ptr.h"

namespace atapp {
    class app;

    class module_impl {
    protected:
        module_impl();
        ~module_impl();

    private:
        module_impl(const module_impl &) FUNC_DELETE;
        const module_impl &operator=(const module_impl &) FUNC_DELETE;

    public:
        virtual int init() = 0;
        virtual int reload();

        /**
         * @brief try to stop a module
         * @return if can't be stoped immadiately, return > 0, if there is a error, return < 0, otherwise 0
         * @note may be called more than once, when the first return <= 0, this module will be disabled.
         */
        virtual int stop();

        virtual int timeout();

        virtual const char *name() const;

        /**
         * @brief run tick handle and return active action number
         * @return active action number or error code
         */
        virtual int tick();

    protected:
        /**
         * @brief get owner atapp object
         * @return return owner atapp object, NULL if not added
         */
        app *get_app();

        /**
         * @brief get owner atapp object
         * @return return owner atapp object, NULL if not added
         */
        const app *get_app() const;

    protected:
        inline bool is_enabled() const { return enabled_; }

        bool enable();

        bool disable();

    private:
        bool enabled_;
        app *owner_;

        friend class app;
    };
}

#endif
