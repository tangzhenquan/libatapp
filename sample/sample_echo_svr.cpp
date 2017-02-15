
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <vector>

#include <uv.h>

#include <atframe/atapp.h>
#include <time/time_utility.h>


class echo_module : public atapp::module_impl {
public:
    virtual int init() {
        WLOGINFO("echo module init");
        return 0;
    };

    virtual int reload() {
        WLOGINFO("echo module reload");
        return 0;
    }

    virtual int stop() {
        WLOGINFO("echo module stop");
        return 0;
    }

    virtual int timeout() {
        WLOGINFO("echo module timeout");
        return 0;
    }

    virtual const char *name() const { return "echo_module"; }

    virtual int tick() {
        time_t cur_print = util::time::time_utility::get_now() / 20;
        static time_t print_per_sec = cur_print;
        if (print_per_sec != cur_print) {
            WLOGINFO("echo module tick");
            print_per_sec = cur_print;
        }

        return 0;
    }
};


static int app_command_handler_echo(util::cli::callback_param params) {
    std::stringstream ss;
    for (size_t i = 0; i < params.get_params_number(); ++i) {
        ss << " " << params[i]->to_cpp_string();
    }

    WLOGINFO("echo commander:%s", ss.str().c_str());
    return 0;
}

struct app_command_handler_transfer {
    atapp::app *app_;
    app_command_handler_transfer(atapp::app &a) : app_(&a) {}

    int operator()(util::cli::callback_param params) {
        if (params.get_params_number() < 2) {
            WLOGERROR("transfer command require at least 2 parameters");
            return 0;
        }

        int type = 0;
        if (params.get_params_number() > 2) {
            type = params[2]->to_int();
        }
        app_->get_bus_node()->send_data(params[0]->to_uint64(), type, params[1]->to_cpp_string().c_str(), params[1]->to_cpp_string().size(), false);
        return 0;
    }
};

static int app_option_handler_echo(util::cli::callback_param params) {
    std::stringstream ss;
    for (size_t i = 0; i < params.get_params_number(); ++i) {
        ss << " " << params[i]->to_cpp_string();
    }

    std::cout << "echo option: " << ss.str() << std::endl;
    return 0;
}

static int app_handle_on_msg(atapp::app &app, const atapp::app::msg_t &msg, const void *buffer, size_t len) {
    std::string data;
    data.assign(reinterpret_cast<const char *>(buffer), len);
    WLOGINFO("receive a message(from 0x%llx, type=%d) %s", static_cast<unsigned long long>(msg.head.src_bus_id), msg.head.type, data.c_str());

    if (NULL != msg.body.forward && 0 != msg.body.forward->from) {
        return app.get_bus_node()->send_data(msg.body.forward->from, msg.head.type, buffer, len);
    }

    return 0;
}

static int app_handle_on_send_fail(atapp::app &app, atapp::app::app_id_t src_pd, atapp::app::app_id_t dst_pd, const atbus::protocol::msg &m) {
    WLOGERROR("send data from 0x%llx to 0x%llx failed", static_cast<unsigned long long>(src_pd), static_cast<unsigned long long>(dst_pd));
    return 0;
}

static int app_handle_on_connected(atapp::app &app, atbus::endpoint &ep, int status) {
    WLOGINFO("app 0x%llx connected, status: %d", static_cast<unsigned long long>(ep.get_id()), status);
    return 0;
}

static int app_handle_on_disconnected(atapp::app &app, atbus::endpoint &ep, int status) {
    WLOGINFO("app 0x%llx disconnected, status: %d", static_cast<unsigned long long>(ep.get_id()), status);
    return 0;
}

int main(int argc, char *argv[]) {
    atapp::app app;

    // setup module
    app.add_module(std::make_shared<echo_module>());
    // setup cmd
    util::cli::cmd_option_ci::ptr_type cmgr = app.get_command_manager();
    cmgr->bind_cmd("echo", app_command_handler_echo);
    cmgr->bind_cmd("transfer", app_command_handler_transfer(app));

    // setup options
    util::cli::cmd_option::ptr_type opt_mgr = app.get_option_manager();
    // show help and exit
    opt_mgr->bind_cmd("-echo", app_option_handler_echo)->set_help_msg("-echo [text]                           echo a message.");

    // setup message handle
    app.set_evt_on_recv_msg(app_handle_on_msg);
    app.set_evt_on_send_fail(app_handle_on_send_fail);
    app.set_evt_on_app_connected(app_handle_on_connected);
    app.set_evt_on_app_disconnected(app_handle_on_disconnected);

    // run
    return app.run(uv_default_loop(), argc, (const char **)argv, NULL);
}
