/**
 * atapp.h
 *
 *  Created on: 2016年04月23日
 *      Author: owent
 */
#ifndef LIBATAPP_ATAPP_H_
#define LIBATAPP_ATAPP_H_

#pragma once

#include <map>
#include <string>
#include <vector>

#include "libatbus.h"

#include "std/functional.h"

#include <bitset>

#include "cli/cmd_option.h"
#include "time/time_utility.h"

#include "atapp_conf.h"
#include "atapp_log_sink_maker.h"
#include "atapp_module_impl.h"

#define LIBATAPP_VERSION "0.1.0.0"

namespace atapp {
    class app {
    public:
        typedef atbus::node::bus_id_t app_id_t;
        typedef atbus::protocol::msg msg_t;
        typedef std::shared_ptr<module_impl> module_ptr_t;

        struct flag_t {
            enum type { RUNNING = 0, STOPING, TIMEOUT, FLAG_MAX };
        };

        struct mode_t {
            enum type {
                CUSTOM = 0, // custom command
                START,      // start server
                STOP,       // send a stop command
                RELOAD,     // send a reload command
                INFO,       // show information and exit
                MODE_MAX
            };
        };

        // return > 0 means busy and will enter tick again as soon as possiable
        typedef std::function<int()> tick_handler_t;
        // parameters is (message head, buffer address, buffer size)
        typedef std::function<int(const msg_t &, const void *, size_t)> msg_handler_t;

        struct timer_info_t {
            bool is_activited;
            uv_timer_t timer;
        };

        struct tick_timer_t {
            util::time::time_utility::raw_time_t sec_update;
            time_t sec;
            time_t usec;

            timer_info_t tick_timer;
            timer_info_t timeout_timer;
        };


        typedef std::function<int(app &, const msg_t &, const void *, size_t)> callback_fn_on_msg_t;
        typedef std::function<int(app &, app_id_t src_pd, app_id_t dst_pd, const atbus::protocol::msg &m)> callback_fn_on_send_fail_t;
        typedef std::function<int(app &, atbus::endpoint &, int)> callback_fn_on_connected_t;
        typedef std::function<int(app &, atbus::endpoint &, int)> callback_fn_on_disconnected_t;
        typedef std::function<int(app &)> callback_fn_on_all_module_inited_t;

    public:
        app();
        ~app();

        int run(atbus::adapter::loop_t *ev_loop, int argc, const char **argv, void *priv_data = NULL);

        int reload();

        int stop();

        int tick();

        app_id_t get_id() const;

        bool check(flag_t::type f) const;

        /**
         * @brief add a new module
         */
        void add_module(module_ptr_t module);

        /**
         * @brief convert module type and add a new module
         */
        template <typename TModPtr>
        void add_module(TModPtr module) {
            add_module(std::dynamic_pointer_cast<module_impl>(module));
        }

        // api: add custom log type
        // api: add custom module
        // api: add custom command callback
        util::cli::cmd_option_ci::ptr_type get_command_manager();

        // api: add custem program options
        util::cli::cmd_option::ptr_type get_option_manager();

        void set_app_version(const std::string &ver);

        const std::string &get_app_version() const;

        atbus::node::ptr_t get_bus_node();
        const atbus::node::ptr_t get_bus_node() const;

        util::config::ini_loader &get_configure();
        const util::config::ini_loader &get_configure() const;

        void set_evt_on_recv_msg(callback_fn_on_msg_t fn);
        void set_evt_on_send_fail(callback_fn_on_send_fail_t fn);
        void set_evt_on_app_connected(callback_fn_on_connected_t fn);
        void set_evt_on_app_disconnected(callback_fn_on_disconnected_t fn);
        void set_evt_on_all_module_inited(callback_fn_on_all_module_inited_t fn);

        const callback_fn_on_msg_t &get_evt_on_recv_msg() const;
        const callback_fn_on_send_fail_t &get_evt_on_send_fail() const;
        const callback_fn_on_connected_t &get_evt_on_app_connected() const;
        const callback_fn_on_disconnected_t &get_evt_on_app_disconnected() const;
        const callback_fn_on_all_module_inited_t &get_evt_on_all_module_inited() const;

    private:
        static void ev_stop_timeout(uv_timer_t *handle);

        bool set_flag(flag_t::type f, bool v);
        bool check_flag(flag_t::type f) const;

        int apply_configure();

        int run_ev_loop(atbus::adapter::loop_t *ev_loop);

        int setup_signal();

        void setup_option(int argc, const char *argv[], void *priv_data);

        void setup_command();

        int setup_log();

        int setup_atbus();

        void close_timer(timer_info_t &t);

        int setup_timer();

        int send_last_command(atbus::adapter::loop_t *ev_loop);

        void print_help();
        // ============ inner functional handlers ============
    private:
        int prog_option_handler_help(util::cli::callback_param params, util::cli::cmd_option *opt_mgr, util::cli::cmd_option_ci *cmd_mgr);
        int prog_option_handler_version(util::cli::callback_param params);
        int prog_option_handler_set_id(util::cli::callback_param params);
        int prog_option_handler_set_conf_file(util::cli::callback_param params);
        int prog_option_handler_set_pid(util::cli::callback_param params);
        int prog_option_handler_resume_mode(util::cli::callback_param params);
        int prog_option_handler_start(util::cli::callback_param params);
        int prog_option_handler_stop(util::cli::callback_param params);
        int prog_option_handler_reload(util::cli::callback_param params);
        int prog_option_handler_run(util::cli::callback_param params);

        int command_handler_start(util::cli::callback_param params);
        int command_handler_stop(util::cli::callback_param params);
        int command_handler_reload(util::cli::callback_param params);
        int command_handler_invalid(util::cli::callback_param params);

    private:
        int bus_evt_callback_on_recv_msg(const atbus::node &, const atbus::endpoint *, const atbus::connection *, const msg_t &, const void *, size_t);
        int bus_evt_callback_on_send_failed(const atbus::node &, const atbus::endpoint *, const atbus::connection *, const atbus::protocol::msg *m);
        int bus_evt_callback_on_error(const atbus::node &, const atbus::endpoint *, const atbus::connection *, int, int);
        int bus_evt_callback_on_reg(const atbus::node &, const atbus::endpoint *, const atbus::connection *, int);
        int bus_evt_callback_on_shutdown(const atbus::node &, int);
        int bus_evt_callback_on_available(const atbus::node &, int);
        int bus_evt_callback_on_invalid_connection(const atbus::node &, const atbus::connection *, int);
        int bus_evt_callback_on_custom_cmd(const atbus::node &, const atbus::endpoint *, const atbus::connection *, atbus::node::bus_id_t,
                                           const std::vector<std::pair<const void *, size_t> > &);
        int bus_evt_callback_on_add_endpoint(const atbus::node &, atbus::endpoint *, int);
        int bus_evt_callback_on_remove_endpoint(const atbus::node &, atbus::endpoint *, int);


        /** this function should always not be used outside atapp.cpp **/
        static void _app_setup_signal_term(int signo);

    private:
        static app *last_instance_;
        util::config::ini_loader cfg_loader_;
        util::cli::cmd_option::ptr_type app_option_;
        util::cli::cmd_option_ci::ptr_type cmd_handler_;
        std::vector<std::string> last_command_;

        app_conf conf_;

        atbus::node::ptr_t bus_node_;
        std::bitset<flag_t::FLAG_MAX> flags_;
        mode_t::type mode_;
        tick_timer_t tick_timer_;

        std::vector<module_ptr_t> modules_;
        std::map<std::string, log_sink_maker::log_reg_t> log_reg_; // log reg will not changed or be checked outside the init, so std::map is enough

        // callbacks
        callback_fn_on_msg_t evt_on_recv_msg_;
        callback_fn_on_send_fail_t evt_on_send_fail_;
        callback_fn_on_connected_t evt_on_app_connected_;
        callback_fn_on_disconnected_t evt_on_app_disconnected_;
        callback_fn_on_all_module_inited_t evt_on_all_module_inited_;

        // stat
        typedef struct {
            uv_rusage_t last_checkpoint_usage;
            time_t last_checkpoint_min;
        } stat_data_t;
        stat_data_t stat_;
    };
} // namespace atapp

#endif
