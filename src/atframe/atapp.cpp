#include <assert.h>
#include <fstream>
#include <iostream>
#include <signal.h>

#include "std/foreach.h"

#include "atframe/atapp.h"

#include "common/string_oprs.h"

#include "cli/shell_font.h"

namespace atapp {
    app *app::last_instance_;

    app::app() : mode_(mode_t::CUSTOM) {
        last_instance_ = this;
        conf_.id = 0;
        conf_.execute_path = NULL;
        conf_.stop_timeout = 30000; // 30s
        conf_.tick_interval = 32;   // 32ms

        tick_timer_.sec_update = util::time::time_utility::raw_time_t::min();
        tick_timer_.sec = 0;
        tick_timer_.usec = 0;

        tick_timer_.tick_timer.is_activited = false;
        tick_timer_.timeout_timer.is_activited = false;
    }

    app::~app() {
        if (this == last_instance_) {
            last_instance_ = NULL;
        }

        owent_foreach(module_ptr_t & mod, modules_) {
            if (mod && mod->owner_ == this) {
                mod->owner_ = NULL;
            }
        }
    }

    int app::run(atbus::adapter::loop_t *ev_loop, int argc, const char **argv, void *priv_data) {
        if (check(flag_t::RUNNING)) {
            return 0;
        }

        // update time first
        util::time::time_utility::update();

        // step 1. bind default options
        // step 2. load options from cmd line
        setup_option(argc, argv, priv_data);
        setup_command();

        // step 3. if not in show mode, exit 0
        if (mode_t::INFO == mode_) {
            return 0;
        }

        util::cli::shell_stream ss(std::cerr);
        // step 4. load options from cmd line
        conf_.bus_conf.ev_loop = ev_loop;
        int ret = reload();
        if (ret < 0) {
            ss() << util::cli::shell_font_style::SHELL_FONT_COLOR_RED << "load configure failed" << std::endl;
            return ret;
        }

        // step 5. if not in start mode, send cmd
        switch (mode_) {
        case mode_t::START: {
            break;
        }
        case mode_t::CUSTOM:
        case mode_t::STOP:
        case mode_t::RELOAD: {
            return send_last_command(ev_loop);
        }
        default: { return 0; }
        }

        // step 6. setup log & signal
        ret = setup_log();
        if (ret < 0) {
            ss() << util::cli::shell_font_style::SHELL_FONT_COLOR_RED << "setup log failed" << std::endl;
            return ret;
        }

        ret = setup_signal();
        if (ret < 0) {
            ss() << util::cli::shell_font_style::SHELL_FONT_COLOR_RED << "setup signal failed" << std::endl;
            return ret;
        }

        ret = setup_atbus();
        if (ret < 0) {
            ss() << util::cli::shell_font_style::SHELL_FONT_COLOR_RED << "setup atbus failed" << std::endl;
            bus_node_.reset();
            return ret;
        }

        // step 7. all modules reload
        owent_foreach(module_ptr_t & mod, modules_) {
            if (mod->is_enabled()) {
                ret = mod->reload();
                if (ret < 0) {
                    ss() << util::cli::shell_font_style::SHELL_FONT_COLOR_RED << "load configure of " << mod->name() << " failed"
                        << std::endl;
                    return ret;
                }
            }
        }

        // step 8. all modules init
        owent_foreach(module_ptr_t & mod, modules_) {
            if (mod->is_enabled()) {
                ret = mod->init();
                if (ret < 0) {
                    ss() << util::cli::shell_font_style::SHELL_FONT_COLOR_RED << "initialze " << mod->name() << " failed" << std::endl;
                    return ret;
                }
            }
        }

        // callback of all modules inited
        if (evt_on_all_module_inited_) {
            evt_on_all_module_inited_(*this);
        }

        // step 9. set running
        return run_ev_loop(ev_loop);
    }

    int app::reload() {
        app_conf old_conf = conf_;
        util::cli::shell_stream ss(std::cerr);

        WLOGINFO("============ start to load configure ============");
        // step 1. reset configure
        cfg_loader_.clear();

        // step 2. reload from program configure file
        if (conf_.conf_file.empty()) {
            ss() << util::cli::shell_font_style::SHELL_FONT_COLOR_RED << "missing configure file" << std::endl;
            print_help();
            return -1;
        }
        if (cfg_loader_.load_file(conf_.conf_file.c_str(), false) < 0) {
            ss() << util::cli::shell_font_style::SHELL_FONT_COLOR_RED << "load configure file " << conf_.conf_file << " failed"
                 << std::endl;
            print_help();
            return -1;
        }

        // step 3. reload from external configure files
        // step 4. merge configure
        {
            std::vector<std::string> external_confs;
            cfg_loader_.dump_to("atapp.config.external", external_confs);
            owent_foreach(std::string & conf_fp, external_confs) {
                if (!conf_fp.empty()) {
                    if (cfg_loader_.load_file(conf_.conf_file.c_str(), true) < 0) {
                        if (check(flag_t::RUNNING)) {
                            WLOGERROR("load external configure file %s failed", conf_fp.c_str());
                        } else {
                            ss() << util::cli::shell_font_style::SHELL_FONT_COLOR_RED << "load external configure file " << conf_fp
                                 << " failed" << std::endl;
                            return 1;
                        }
                    }
                }
            }
        }

        // apply ini configure
        apply_configure();
        // reuse ev loop if not configued
        if (NULL == conf_.bus_conf.ev_loop) {
            conf_.bus_conf.ev_loop = old_conf.bus_conf.ev_loop;
        }

        // step 5. if not in start mode, return
        if (mode_t::START != mode_) {
            return 0;
        }

        if (check(flag_t::RUNNING)) {
            // step 6. reset log
            setup_log();

            // step 7. if inited, let all modules reload
            owent_foreach(module_ptr_t & mod, modules_) {
                if (mod->is_enabled()) {
                    mod->reload();
                }
            }

            // step 8. if running and tick interval changed, reset timer
            if (old_conf.tick_interval != conf_.tick_interval) {
                setup_timer();
            }
        }

        WLOGINFO("------------ load configure done ------------");
        return 0;
    }

    int app::stop() {
        WLOGINFO("============ receive stop signal and ready to stop all modules ============");
        // step 1. set stop flag.
        bool is_stoping = set_flag(flag_t::STOPING, true);

        // TODO stop reason = manual stop
        if (!is_stoping && bus_node_) {
            bus_node_->shutdown(0);
        }

        // step 2. stop libuv and return from uv_run
        // if (!is_stoping) {
        if (bus_node_) {
            uv_stop(bus_node_->get_evloop());
        }
        // }
        return 0;
    }

    int app::tick() {
        int active_count;
        util::time::time_utility::update();
        // record start time point
        util::time::time_utility::raw_time_t start_tp = util::time::time_utility::now();
        util::time::time_utility::raw_time_t end_tp = start_tp;
        do {
            if (tick_timer_.sec != util::time::time_utility::get_now()) {
                tick_timer_.sec = util::time::time_utility::get_now();
                tick_timer_.usec = 0;
                tick_timer_.sec_update = util::time::time_utility::now();
            } else {
                tick_timer_.usec = static_cast<time_t>(
                    std::chrono::duration_cast<std::chrono::microseconds>(util::time::time_utility::now() - tick_timer_.sec_update)
                        .count());
            }

            active_count = 0;
            int res;
            // step 1. proc available modules
            owent_foreach(module_ptr_t & mod, modules_) {
                if (mod->is_enabled()) {
                    res = mod->tick();
                    if (res < 0) {
                        WLOGERROR("module %s run tick and return %d", mod->name(), res);
                    } else {
                        active_count += res;
                    }
                }
            }

            // step 2. proc atbus
            res = bus_node_->proc(tick_timer_.sec, tick_timer_.usec);
            if (res < 0) {
                WLOGERROR("atbus run tick and return %d", res);
            } else {
                active_count += res;
            }

            // only tick time less than tick interval will run loop again
            util::time::time_utility::update();
            end_tp = util::time::time_utility::now();
        } while (active_count > 0 && (end_tp - start_tp) >= std::chrono::milliseconds(conf_.tick_interval));
        return 0;
    }

    app::app_id_t app::get_id() const { return conf_.id; }

    bool app::check(flag_t::type f) const {
        if (f < 0 || f >= flag_t::FLAG_MAX) {
            return false;
        }

        return flags_.test(f);
    }

    void app::add_module(module_ptr_t module) {
        if (this == module->owner_) {
            return;
        }

        assert(NULL == module->owner_);

        modules_.push_back(module);
        module->owner_ = this;
    }

    util::cli::cmd_option_ci::ptr_type app::get_command_manager() {
        if (!cmd_handler_) {
            return cmd_handler_ = util::cli::cmd_option_ci::create();
        }

        return cmd_handler_;
    }

    util::cli::cmd_option::ptr_type app::get_option_manager() {
        if (!app_option_) {
            return app_option_ = util::cli::cmd_option::create();
        }

        return app_option_;
    }

    void app::set_app_version(const std::string &ver) { conf_.app_version = ver; }

    const std::string &app::get_app_version() const { return conf_.app_version; }

    atbus::node::ptr_t app::get_bus_node() { return bus_node_; }
    const atbus::node::ptr_t app::get_bus_node() const { return bus_node_; }

    util::config::ini_loader& app::get_configure() { return cfg_loader_; }
    const util::config::ini_loader& app::get_configure() const { return cfg_loader_; }

    void app::set_evt_on_recv_msg(callback_fn_on_msg_t fn) { evt_on_recv_msg_ = fn; }
    void app::set_evt_on_send_fail(callback_fn_on_send_fail_t fn) { evt_on_send_fail_ = fn; }
    void app::set_evt_on_app_connected(callback_fn_on_connected_t fn) { evt_on_app_connected_ = fn; }
    void app::set_evt_on_app_disconnected(callback_fn_on_disconnected_t fn) { evt_on_app_disconnected_ = fn; }
    void app::set_evt_on_all_module_inited(callback_fn_on_all_module_inited_t fn) { evt_on_all_module_inited_ = fn; }

    const app::callback_fn_on_msg_t &app::get_evt_on_recv_msg() const { return evt_on_recv_msg_; }
    const app::callback_fn_on_send_fail_t &app::get_evt_on_send_fail() const { return evt_on_send_fail_; }
    const app::callback_fn_on_connected_t &app::get_evt_on_app_connected() const { return evt_on_app_connected_; }
    const app::callback_fn_on_disconnected_t &app::get_evt_on_app_disconnected() const { return evt_on_app_disconnected_; }
    const app::callback_fn_on_all_module_inited_t &app::get_evt_on_all_module_inited() const { return evt_on_all_module_inited_; }

    void app::ev_stop_timeout(uv_timer_t *handle) {
        assert(handle && handle->data);

        if (NULL != handle && NULL != handle->data) {
            app *self = reinterpret_cast<app *>(handle->data);
            self->set_flag(flag_t::TIMEOUT, true);
        }

        if (NULL != handle) {
            uv_stop(handle->loop);
        }
    }

    bool app::set_flag(flag_t::type f, bool v) {
        if (f < 0 || f >= flag_t::FLAG_MAX) {
            return false;
        }

        bool ret = flags_.test(f);
        flags_.set(f, v);
        return ret;
    }

    bool app::check_flag(flag_t::type f) const {
        if (f < 0 || f >= flag_t::FLAG_MAX) {
            return false;
        }

        return flags_.test(f);
    }

    int app::apply_configure() {
        // id
        if (0 == conf_.id) {
            cfg_loader_.dump_to("atapp.id", conf_.id);
        }

        // hostname
        {
            std::string hostname;
            cfg_loader_.dump_to("atapp.hostname", hostname);
            if (!hostname.empty()) {
                atbus::node::set_hostname(hostname);
            }
        }

        conf_.bus_listen.clear();
        cfg_loader_.dump_to("atapp.bus.listen", conf_.bus_listen);

        // conf_.stop_timeout = 30000; // use last available value
        cfg_loader_.dump_to("atapp.timer.stop_timeout", conf_.stop_timeout);

        // conf_.tick_interval = 32; // use last available value
        cfg_loader_.dump_to("atapp.timer.tick_interval", conf_.tick_interval);

        // atbus configure
        atbus::node::default_conf(&conf_.bus_conf);

        cfg_loader_.dump_to("atapp.bus.children_mask", conf_.bus_conf.children_mask);
        {
            bool optv = false;
            cfg_loader_.dump_to("atapp.bus.options.global_router", optv);
            conf_.bus_conf.flags.set(atbus::node::conf_flag_t::EN_CONF_GLOBAL_ROUTER, optv);
        }

        cfg_loader_.dump_to("atapp.bus.proxy", conf_.bus_conf.father_address);
        cfg_loader_.dump_to("atapp.bus.loop_times", conf_.bus_conf.loop_times);
        cfg_loader_.dump_to("atapp.bus.ttl", conf_.bus_conf.ttl);
        cfg_loader_.dump_to("atapp.bus.backlog", conf_.bus_conf.backlog);
        cfg_loader_.dump_to("atapp.bus.first_idle_timeout", conf_.bus_conf.first_idle_timeout);
        cfg_loader_.dump_to("atapp.bus.ping_interval", conf_.bus_conf.ping_interval);
        cfg_loader_.dump_to("atapp.bus.retry_interval", conf_.bus_conf.retry_interval);
        cfg_loader_.dump_to("atapp.bus.fault_tolerant", conf_.bus_conf.fault_tolerant);
        cfg_loader_.dump_to("atapp.bus.msg_size", conf_.bus_conf.msg_size);
        cfg_loader_.dump_to("atapp.bus.recv_buffer_size", conf_.bus_conf.recv_buffer_size);
        cfg_loader_.dump_to("atapp.bus.send_buffer_size", conf_.bus_conf.send_buffer_size);
        cfg_loader_.dump_to("atapp.bus.send_buffer_number", conf_.bus_conf.send_buffer_number);

        return 0;
    }

    int app::run_ev_loop(atbus::adapter::loop_t *ev_loop) {
        bool keep_running = true;
        util::cli::shell_stream ss(std::cerr);

        set_flag(flag_t::RUNNING, true);

        // TODO if atbus is reset, init it again

        if (setup_timer() < 0) {
            set_flag(flag_t::RUNNING, false);
            return -1;
        }

        // write pid file
        if (!conf_.pid_file.empty()) {
            std::fstream pid_file;
            pid_file.open(conf_.pid_file.c_str(), std::ios::out);
            if (!pid_file.is_open()) {
                ss() << util::cli::shell_font_style::SHELL_FONT_COLOR_RED << "open and write pif file " << conf_.pid_file << " failed"
                     << std::endl;

                // failed and skip running
                keep_running = false;
            } else {
                pid_file << atbus::node::get_pid();
                pid_file.close();
            }
        }

        while (keep_running && bus_node_) {
            // step X. loop uv_run util stop flag is set
            uv_run(bus_node_->get_evloop(), UV_RUN_DEFAULT);
            if (check_flag(flag_t::STOPING)) {
                keep_running = false;

                if (check_flag(flag_t::TIMEOUT)) {
                    // step X. notify all modules timeout
                    owent_foreach(module_ptr_t & mod, modules_) {
                        if (mod->is_enabled()) {
                            WLOGERROR("try to stop module %s but timeout", mod->name());
                            mod->timeout();
                            mod->disable();
                        }
                    }
                } else {
                    // step X. notify all modules to finish and wait for all modules stop
                    owent_foreach(module_ptr_t & mod, modules_) {
                        if (mod->is_enabled()) {
                            int res = mod->stop();
                            if (0 == res) {
                                mod->disable();
                            } else if (res < 0) {
                                mod->disable();
                                WLOGERROR("try to stop module %s but failed and return %d", mod->name(), res);
                            } else {
                                // any module stop running will make app wait
                                keep_running = true;
                            }
                        }
                    }

                    // step X. if stop is blocked and timeout not triggered, setup stop timeout and waiting for all modules finished
                    if (false == tick_timer_.timeout_timer.is_activited) {
                        uv_timer_init(bus_node_->get_evloop(), &tick_timer_.timeout_timer.timer);
                        tick_timer_.timeout_timer.timer.data = this;

                        int res = uv_timer_start(&tick_timer_.timeout_timer.timer, ev_stop_timeout, conf_.stop_timeout, 0);
                        if (0 == res) {
                            tick_timer_.timeout_timer.is_activited = true;
                        } else {
                            WLOGERROR("setup stop timeout failed, res: %d", res);
                            set_flag(flag_t::TIMEOUT, false);
                        }
                    }
                }
            }

            // if atbus is at shutdown state, loop
            if (keep_running && bus_node_->check(atbus::node::flag_t::EN_FT_SHUTDOWN)) {
                uv_run(bus_node_->get_evloop(), UV_RUN_DEFAULT);
            }
        }

        // close timer
        close_timer(tick_timer_.tick_timer);
        close_timer(tick_timer_.timeout_timer);

        // not running now
        set_flag(flag_t::RUNNING, false);

        return 0;
    }

    // graceful Exits
    void app::_app_setup_signal_term(int signo) {
        if (NULL != app::last_instance_) {
            app::last_instance_->stop();
        }
    }

    int app::setup_signal() {
        // block signals
        app::last_instance_ = this;
        signal(SIGTERM, _app_setup_signal_term);

#ifndef WIN32
        signal(SIGHUP, SIG_IGN);  // lost parent process
        signal(SIGPIPE, SIG_IGN); // close stdin, stdout or stderr
        signal(SIGTSTP, SIG_IGN); // close tty
        signal(SIGTTIN, SIG_IGN); // tty input
        signal(SIGTTOU, SIG_IGN); // tty output
#endif

        return 0;
    }

    int app::setup_log() {
        util::cli::shell_stream ss(std::cerr);

        // register inner log module
        if (log_reg_.find(log_sink_maker::get_file_sink_name()) == log_reg_.end()) {
            log_reg_[log_sink_maker::get_file_sink_name()] = log_sink_maker::get_file_sink_reg();
        }

        if (log_reg_.find(log_sink_maker::get_stdout_sink_name()) == log_reg_.end()) {
            log_reg_[log_sink_maker::get_stdout_sink_name()] = log_sink_maker::get_stdout_sink_reg();
        }

        if (log_reg_.find(log_sink_maker::get_stderr_sink_name()) == log_reg_.end()) {
            log_reg_[log_sink_maker::get_stderr_sink_name()] = log_sink_maker::get_stderr_sink_reg();
        }

        // load configure
        uint32_t log_cat_number = LOG_WRAPPER_CATEGORIZE_SIZE;
        cfg_loader_.dump_to("atapp.log.cat.number", log_cat_number);
        if (log_cat_number > LOG_WRAPPER_CATEGORIZE_SIZE) {
            ss() << util::cli::shell_font_style::SHELL_FONT_COLOR_RED << "log categorize should not be greater than "
                 << LOG_WRAPPER_CATEGORIZE_SIZE << ". you can define LOG_WRAPPER_CATEGORIZE_SIZE to a greater number and rebuild atapp."
                 << std::endl;
            log_cat_number = LOG_WRAPPER_CATEGORIZE_SIZE;
        }
        int log_level_id = util::log::log_wrapper::level_t::LOG_LW_INFO;
        cfg_loader_.dump_to("atapp.log.level", log_level_id);

        char log_path[256] = {0};

        for (uint32_t i = 0; i < log_cat_number; ++i) {
            std::string log_name, log_prefix;
            UTIL_STRFUNC_SNPRINTF(log_path, sizeof(log_path), "atapp.log.cat.%u.name", i);
            cfg_loader_.dump_to(log_path, log_name);

            if (log_name.empty()) {
                continue;
            }

            UTIL_STRFUNC_SNPRINTF(log_path, sizeof(log_path), "atapp.log.cat.%u.prefix", i);
            cfg_loader_.dump_to(log_path, log_prefix);

            // init and set prefix
            WLOG_INIT(i, WLOG_LEVELID(log_level_id));
            if (!log_prefix.empty()) {
                WLOG_GETCAT(i)->set_prefix_format(log_prefix);
            }

            // FIXME: For now, log can not be reload. we may make it available someday in the future
            if (!WLOG_GETCAT(i)->get_sinks().empty()) {
                continue;
            }

            // register log handles
            for (uint32_t j = 0;; ++j) {
                std::string sink_type;
                UTIL_STRFUNC_SNPRINTF(log_path, sizeof(log_path), "atapp.log.%s.%u.type", log_name.c_str(), j);
                cfg_loader_.dump_to(log_path, sink_type);

                if (sink_type.empty()) {
                    break;
                }

                // already read log cat name, sink type name
                int log_handle_min = util::log::log_wrapper::level_t::LOG_LW_FATAL,
                    log_handle_max = util::log::log_wrapper::level_t::LOG_LW_DEBUG;
                UTIL_STRFUNC_SNPRINTF(log_path, sizeof(log_path), "atapp.log.%s.%u", log_name.c_str(), j);
                util::config::ini_value &cfg_set = cfg_loader_.get_node(log_path);

                UTIL_STRFUNC_SNPRINTF(log_path, sizeof(log_path), "atapp.log.%s.%u.level.min", log_name.c_str(), j);
                cfg_loader_.dump_to(log_path, log_handle_min);

                UTIL_STRFUNC_SNPRINTF(log_path, sizeof(log_path), "atapp.log.%s.%u.level.max", log_name.c_str(), j);
                cfg_loader_.dump_to(log_path, log_handle_max);

                // register log sink
                std::map<std::string, log_sink_maker::log_reg_t>::iterator iter = log_reg_.find(sink_type);
                if (iter != log_reg_.end()) {
                    util::log::log_wrapper::log_handler_t log_handler = iter->second(log_name, *WLOG_GETCAT(i), j, cfg_set);
                    WLOG_GETCAT(i)
                        ->add_sink(log_handler, static_cast<util::log::log_wrapper::level_t::type>(log_handle_min),
                                   static_cast<util::log::log_wrapper::level_t::type>(log_handle_max));
                } else {
                    ss() << util::cli::shell_font_style::SHELL_FONT_COLOR_RED << "unavailable log type " << sink_type
                         << ", you can add log type register handle before init." << std::endl;
                }
            }
        }

        return 0;
    }

    int app::setup_atbus() {
        int ret = 0, res = 0;
        if (bus_node_) {
            bus_node_->reset();
            bus_node_.reset();
        }

        atbus::node::ptr_t connection_node = atbus::node::create();
        if (!connection_node) {
            WLOGERROR("create bus node failed.");
            return -1;
        }

        ret = connection_node->init(conf_.id, &conf_.bus_conf);
        if (ret < 0) {
            WLOGERROR("init bus node failed. ret: %d", ret);
            return -1;
        }

        // TODO if not in resume mode, destroy shm
        // if (false == conf_.resume_mode) {}

        // init listen
        for (size_t i = 0; i < conf_.bus_listen.size(); ++i) {
            res = connection_node->listen(conf_.bus_listen[i].c_str());
            if (res < 0) {
#ifdef _WIN32
                if (EN_ATBUS_ERR_SHM_GET_FAILED == res) {
                    WLOGERROR("Using global shared memory require SeCreateGlobalPrivilege, try to run as Administrator.\nWe will ignore %s this time.", conf_.bus_listen[i].c_str());
                    util::cli::shell_stream ss(std::cerr);
                    ss() << util::cli::shell_font_style::SHELL_FONT_COLOR_RED <<
                        "Using global shared memory require SeCreateGlobalPrivilege, try to run as Administrator." << std::endl <<
                        "We will ignore " << conf_.bus_listen[i] << " this time." << std::endl;

                    // res = 0; // Value stored to 'res' is never read
                } else {
#endif
                    WLOGERROR("bus node listen %s failed. res: %d", conf_.bus_listen[i].c_str(), res);
                    ret = res;
#ifdef _WIN32
                }
#endif
            }
        }

        if (ret < 0) {
            WLOGERROR("bus node listen failed");
            return ret;
        }

        // start
        ret = connection_node->start();
        if (ret < 0) {
            WLOGERROR("bus node start failed, ret: %d", ret);
            return ret;
        }

        // if has father node, block and connect to father node
        if (atbus::node::state_t::CONNECTING_PARENT == connection_node->get_state() ||
            atbus::node::state_t::LOST_PARENT == connection_node->get_state()) {
            // setup timeout and waiting for parent connected
            if (false == tick_timer_.timeout_timer.is_activited) {
                uv_timer_init(connection_node->get_evloop(), &tick_timer_.timeout_timer.timer);
                tick_timer_.timeout_timer.timer.data = this;

                res = uv_timer_start(&tick_timer_.timeout_timer.timer, ev_stop_timeout, conf_.stop_timeout, 0);
                if (0 == res) {
                    tick_timer_.timeout_timer.is_activited = true;
                } else {
                    WLOGERROR("setup stop timeout failed, res: %d", res);
                    set_flag(flag_t::TIMEOUT, false);
                }

                while (NULL == connection_node->get_parent_endpoint()) {
                    if (check_flag(flag_t::TIMEOUT)) {
                        WLOGERROR("connection to parent node %s timeout", conf_.bus_conf.father_address.c_str());
                        ret = -1;
                        break;
                    }

                    uv_run(connection_node->get_evloop(), UV_RUN_ONCE);
                }

                // if connected, do not trigger timeout
                close_timer(tick_timer_.timeout_timer);

                if (ret < 0) {
                    WLOGERROR("connect to parent node failed");
                    return ret;
                }
            }
        }

        // setup all callbacks
        connection_node->set_on_recv_handle(std::bind(&app::bus_evt_callback_on_recv_msg, this, std::placeholders::_1,
                                                      std::placeholders::_2, std::placeholders::_3, std::placeholders::_4,
                                                      std::placeholders::_5, std::placeholders::_6));

        connection_node->set_on_send_data_failed_handle(std::bind(&app::bus_evt_callback_on_send_failed, this, std::placeholders::_1,
                                                                  std::placeholders::_2, std::placeholders::_3, std::placeholders::_4));

        connection_node->set_on_error_handle(std::bind(&app::bus_evt_callback_on_error, this, std::placeholders::_1, std::placeholders::_2,
                                                       std::placeholders::_3, std::placeholders::_4, std::placeholders::_5));

        connection_node->set_on_register_handle(std::bind(&app::bus_evt_callback_on_reg, this, std::placeholders::_1, std::placeholders::_2,
                                                          std::placeholders::_3, std::placeholders::_4));

        connection_node->set_on_shutdown_handle(
            std::bind(&app::bus_evt_callback_on_shutdown, this, std::placeholders::_1, std::placeholders::_2));

        connection_node->set_on_available_handle(
            std::bind(&app::bus_evt_callback_on_available, this, std::placeholders::_1, std::placeholders::_2));

        connection_node->set_on_invalid_connection_handle(std::bind(&app::bus_evt_callback_on_invalid_connection, this,
                                                                    std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

        connection_node->set_on_custom_cmd_handle(std::bind(&app::bus_evt_callback_on_custom_cmd, this, std::placeholders::_1,
                                                            std::placeholders::_2, std::placeholders::_3, std::placeholders::_4,
                                                            std::placeholders::_5));
        connection_node->set_on_add_endpoint_handle(
            std::bind(&app::bus_evt_callback_on_add_endpoint, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

        connection_node->set_on_remove_endpoint_handle(std::bind(&app::bus_evt_callback_on_remove_endpoint, this, std::placeholders::_1,
                                                                 std::placeholders::_2, std::placeholders::_3));

        bus_node_ = connection_node;

        return 0;
    }

    void app::close_timer(timer_info_t &t) {
        if (t.is_activited) {
            uv_timer_stop(&t.timer);
            uv_close(reinterpret_cast<uv_handle_t *>(&t.timer), NULL);
            t.is_activited = false;
        }
    }

    static void _app_tick_timer_handle(uv_timer_t *handle) {
        assert(handle && handle->data);

        if (NULL != handle && NULL != handle->data) {
            app *self = reinterpret_cast<app *>(handle->data);
            self->tick();
        }
    }

    int app::setup_timer() {
        close_timer(tick_timer_.tick_timer);

        if (conf_.tick_interval < 4) {
            conf_.tick_interval = 4;
            WLOGWARNING("tick interval can not smaller than 4ms, we use 4ms now.");
        }

        uv_timer_init(bus_node_->get_evloop(), &tick_timer_.tick_timer.timer);
        tick_timer_.tick_timer.timer.data = this;

        int res = uv_timer_start(&tick_timer_.tick_timer.timer, _app_tick_timer_handle, conf_.tick_interval, conf_.tick_interval);
        if (0 == res) {
            tick_timer_.tick_timer.is_activited = true;
        } else {
            WLOGERROR("setup tick timer failed, res: %d", res);
            return -1;
        }

        return 0;
    }

    void app::print_help() {
        util::cli::shell_stream shls(std::cout);

        shls() << util::cli::shell_font_style::SHELL_FONT_COLOR_YELLOW << util::cli::shell_font_style::SHELL_FONT_SPEC_BOLD <<
            "Usage: " << conf_.execute_path << " <options> <command> [command paraters...]" << std::endl;
        shls() << get_option_manager()->get_help_msg() << std::endl << std::endl;

        shls() << util::cli::shell_font_style::SHELL_FONT_COLOR_YELLOW << util::cli::shell_font_style::SHELL_FONT_SPEC_BOLD <<
            "Custom command help:" << std::endl;
        shls() << get_command_manager()->get_help_msg() << std::endl;
    }

    int app::prog_option_handler_help(util::cli::callback_param params, 
        util::cli::cmd_option *opt_mgr, util::cli::cmd_option_ci* cmd_mgr) {
        assert(opt_mgr);
        mode_ = mode_t::INFO;
        util::cli::shell_stream shls(std::cout);

        shls() << util::cli::shell_font_style::SHELL_FONT_COLOR_YELLOW << util::cli::shell_font_style::SHELL_FONT_SPEC_BOLD <<
            "Usage: " << conf_.execute_path << " <options> <command> [command paraters...]" << std::endl;
        shls() << opt_mgr->get_help_msg() << std::endl << std::endl;

        shls() << util::cli::shell_font_style::SHELL_FONT_COLOR_YELLOW << util::cli::shell_font_style::SHELL_FONT_SPEC_BOLD <<
            "Custom command help:" << std::endl;
        shls() << cmd_mgr->get_help_msg() << std::endl;
        return 0;
    }

    int app::prog_option_handler_version(util::cli::callback_param params) {
        mode_ = mode_t::INFO;
        if (get_app_version().empty()) {
            printf("1.0.0.0 - based on libatapp %s\n", LIBATAPP_VERSION);
        } else {
            printf("%s - based on libatapp %s\n", get_app_version().c_str(), LIBATAPP_VERSION);
        }
        return 0;
    }

    int app::prog_option_handler_set_id(util::cli::callback_param params) {
        if (params.get_params_number() > 0) {
            util::string::str2int(conf_.id, params[0]->to_string());
        } else {
            util::cli::shell_stream ss(std::cerr);
            ss() << util::cli::shell_font_style::SHELL_FONT_COLOR_RED << "-id require 1 parameter" << std::endl;
        }

        return 0;
    }

    int app::prog_option_handler_set_conf_file(util::cli::callback_param params) {
        if (params.get_params_number() > 0) {
            conf_.conf_file = params[0]->to_cpp_string();
        } else {
            util::cli::shell_stream ss(std::cerr);
            ss() << util::cli::shell_font_style::SHELL_FONT_COLOR_RED << "-c, --conf, --config require 1 parameter" << std::endl;
        }

        return 0;
    }

    int app::prog_option_handler_set_pid(util::cli::callback_param params) {
        if (params.get_params_number() > 0) {
            conf_.pid_file = params[0]->to_cpp_string();
        } else {
            util::cli::shell_stream ss(std::cerr);
            ss() << util::cli::shell_font_style::SHELL_FONT_COLOR_RED << "-p, --pid require 1 parameter" << std::endl;
        }

        return 0;
    }

    int app::prog_option_handler_resume_mode(util::cli::callback_param params) {
        conf_.resume_mode = true;
        return 0;
    }

    int app::prog_option_handler_start(util::cli::callback_param params) {
        mode_ = mode_t::START;
        return 0;
    }

    int app::prog_option_handler_stop(util::cli::callback_param params) {
        mode_ = mode_t::STOP;
        last_command_.clear();
        last_command_.push_back("stop");
        return 0;
    }

    int app::prog_option_handler_reload(util::cli::callback_param params) {
        mode_ = mode_t::RELOAD;
        last_command_.clear();
        last_command_.push_back("reload");
        return 0;
    }

    int app::prog_option_handler_run(util::cli::callback_param params) {
        mode_ = mode_t::CUSTOM;
        for (size_t i = 0; i < params.get_params_number(); ++i) {
            last_command_.push_back(params[i]->to_cpp_string());
        }

        if (0 == params.get_params_number()) {
            mode_ = mode_t::INFO;
            util::cli::shell_stream ss(std::cerr);
            ss() << util::cli::shell_font_style::SHELL_FONT_COLOR_RED << "run must follow a command" << std::endl;
        }
        return 0;
    }

    void app::setup_option(int argc, const char *argv[], void *priv_data) {
        assert(argc > 0);

        util::cli::cmd_option::ptr_type opt_mgr = get_option_manager();
        util::cli::cmd_option_ci::ptr_type cmd_mgr = get_command_manager();
        // show help and exit
        opt_mgr->bind_cmd("-h, --help, help", &app::prog_option_handler_help, this, opt_mgr.get(), cmd_mgr.get())
            ->set_help_msg("-h. --help, help                       show this help message.");

        // show version and exit
        opt_mgr->bind_cmd("-v, --version", &app::prog_option_handler_version, this)
            ->set_help_msg("-v, --version                          show version and exit.");

        // set app bus id
        opt_mgr->bind_cmd("-id", &app::prog_option_handler_set_id, this)
            ->set_help_msg("-id <bus id>                           set app bus id.");

        // set configure file path
        opt_mgr->bind_cmd("-c, --conf, --config", &app::prog_option_handler_set_conf_file, this)
            ->set_help_msg("-c, --conf, --config <file path>       set configure file path.");

        // set app pid file
        opt_mgr->bind_cmd("-p, --pid", &app::prog_option_handler_set_pid, this)
            ->set_help_msg("-p, --pid <pid file>                   set where to store pid.");

        // set configure file path
        opt_mgr->bind_cmd("-r, --resume", &app::prog_option_handler_resume_mode, this)
            ->set_help_msg("-r, --resume                           try to resume when start.");

        // start server
        opt_mgr->bind_cmd("start", &app::prog_option_handler_start, this)
            ->set_help_msg("start                                  start mode.");

        // stop server
        opt_mgr->bind_cmd("stop", &app::prog_option_handler_stop, this)
            ->set_help_msg("stop                                   send stop command to server.");

        // reload all configures
        opt_mgr->bind_cmd("reload", &app::prog_option_handler_reload, this)
            ->set_help_msg("reload                                 send reload command to server.");

        // run custom command
        opt_mgr->bind_cmd("run", &app::prog_option_handler_run, this)
            ->set_help_msg("run <command> [parameters...]          send custom command and parameters to server.");

        conf_.execute_path = argv[0];
        opt_mgr->start(argc - 1, &argv[1], false, priv_data);
    }

    int app::app::command_handler_start(util::cli::callback_param params) {
        // do nothing
        return 0;
    }

    int app::command_handler_stop(util::cli::callback_param params) {
        WLOGINFO("app node 0x%llx run stop command", static_cast<unsigned long long>(get_id()));
        return stop();
    }

    int app::command_handler_reload(util::cli::callback_param params) {
        WLOGINFO("app node 0x%llx run reload command", static_cast<unsigned long long>(get_id()));
        return reload();
    }

    int app::command_handler_invalid(util::cli::callback_param params) {
        WLOGERROR("receive invalid command %s", params.get("@Cmd")->to_string());
        return 0;
    }

    int app::bus_evt_callback_on_recv_msg(const atbus::node &, const atbus::endpoint *, const atbus::connection *,
        const msg_t & msg, const void *buffer, size_t len) {
        // call recv callback
        if (evt_on_recv_msg_) {
            return evt_on_recv_msg_(std::ref(*this), std::cref(msg), buffer, len);
        }

        return 0;
    }

    int app::bus_evt_callback_on_send_failed(const atbus::node &, const atbus::endpoint *, const atbus::connection *,
                                             const atbus::protocol::msg *m) {
        // call failed callback if it's message transfer
        if (NULL == m) {
            WLOGERROR("app 0x%llx receive a send failure without message", static_cast<unsigned long long>(get_id()));
            return -1;
        }

        WLOGERROR("app 0x%llx receive a send failure from 0x%llx, message cmd: %d, type: %d, ret: %d, sequence: %u", static_cast<unsigned long long>(get_id()),
                  static_cast<unsigned long long>(m->head.src_bus_id), static_cast<int>(m->head.cmd), m->head.type, m->head.ret, m->head.sequence);

        if ((ATBUS_CMD_DATA_TRANSFORM_REQ == m->head.cmd || ATBUS_CMD_DATA_TRANSFORM_RSP == m->head.cmd) && evt_on_send_fail_) {
            return evt_on_send_fail_(std::ref(*this), m->body.forward->from, m->body.forward->to, std::cref(*m));
        }

        return 0;
    }

    int app::bus_evt_callback_on_error(const atbus::node &n, const atbus::endpoint *ep, const atbus::connection *conn, int status,
                                       int errcode) {

        // meet eof or reset by peer is not a error
        if (UV_EOF == errcode || UV_ECONNRESET == errcode) {
            const char* msg = UV_EOF == errcode ? "got EOF" : "reset by peer";
            if (NULL != conn) {
                if (NULL != ep) {
                    WLOGINFO("bus node 0x%llx endpoint 0x%llx connection %s closed: %s", static_cast<unsigned long long>(n.get_id()), 
                        static_cast<unsigned long long>(ep->get_id()), conn->get_address().address.c_str(), msg);
                } else {
                    WLOGINFO("bus node 0x%llx connection %s closed: %s", static_cast<unsigned long long>(n.get_id()), conn->get_address().address.c_str(), msg);
                }

            } else {
                if (NULL != ep) {
                    WLOGINFO("bus node 0x%llx endpoint 0x%llx closed: %s", static_cast<unsigned long long>(n.get_id()), static_cast<unsigned long long>(ep->get_id()), msg);
                } else {
                    WLOGINFO("bus node 0x%llx closed: %s", static_cast<unsigned long long>(n.get_id()), msg);
                }
            }
            return 0;
        }

        if (NULL != conn) {
            if (NULL != ep) {
                WLOGERROR("bus node 0x%llx endpoint 0x%llx connection %s error, status: %d, error code: %d", static_cast<unsigned long long>(n.get_id()), 
                    static_cast<unsigned long long>(ep->get_id()), conn->get_address().address.c_str(), status, errcode);
            } else {
                WLOGERROR("bus node 0x%llx connection %s error, status: %d, error code: %d", static_cast<unsigned long long>(n.get_id()), 
                    conn->get_address().address.c_str(), status, errcode);
            }

        } else {
            if (NULL != ep) {
                WLOGERROR("bus node 0x%llx endpoint 0x%llx error, status: %d, error code: %d", static_cast<unsigned long long>(n.get_id()), 
                    static_cast<unsigned long long>(ep->get_id()), status, errcode);
            } else {
                WLOGERROR("bus node 0x%llx error, status: %d, error code: %d", static_cast<unsigned long long>(n.get_id()), status, errcode);
            }
        }

        return 0;
    }

    int app::bus_evt_callback_on_reg(const atbus::node &n, const atbus::endpoint *ep, const atbus::connection *conn, int res) {
        if (NULL != conn) {
            if (NULL != ep) {
                WLOGINFO("bus node 0x%llx endpoint 0x%llx connection %s registered, res: %d", static_cast<unsigned long long>(n.get_id()), 
                    static_cast<unsigned long long>(ep->get_id()), conn->get_address().address.c_str(), res);
            } else {
                WLOGINFO("bus node 0x%llx connection %s registered, res: %d", static_cast<unsigned long long>(n.get_id()), conn->get_address().address.c_str(), res);
            }

        } else {
            if (NULL != ep) {
                WLOGINFO("bus node 0x%llx endpoint 0x%llx registered, res: %d", static_cast<unsigned long long>(n.get_id()), 
                    static_cast<unsigned long long>(ep->get_id()), res);
            } else {
                WLOGINFO("bus node 0x%llx registered, res: %d", static_cast<unsigned long long>(n.get_id()), res);
            }
        }

        return 0;
    }

    int app::bus_evt_callback_on_shutdown(const atbus::node &n, int reason) {
        WLOGINFO("bus node 0x%llx shutdown, reason: %d", static_cast<unsigned long long>(n.get_id()), reason);
        return stop();
    }

    int app::bus_evt_callback_on_available(const atbus::node &n, int res) {
        WLOGINFO("bus node 0x%llx initialze done, res: %d", static_cast<unsigned long long>(n.get_id()), res);
        return res;
    }

    int app::bus_evt_callback_on_invalid_connection(const atbus::node &n, const atbus::connection *conn, int res) {
        if (NULL == conn) {
            WLOGERROR("bus node 0x%llx recv a invalid NULL connection , res: %d", static_cast<unsigned long long>(n.get_id()), res);
        } else {
            // already disconncted finished.
            if (atbus::connection::state_t::DISCONNECTED != conn->get_status()) {
                WLOGERROR("bus node 0x%llx make connection to %s done, res: %d", static_cast<unsigned long long>(n.get_id()), 
                    conn->get_address().address.c_str(), res);
            }
        }
        return 0;
    }

    int app::bus_evt_callback_on_custom_cmd(const atbus::node &, const atbus::endpoint *, const atbus::connection *,
                                            atbus::node::bus_id_t src_id, const std::vector<std::pair<const void *, size_t> > &args) {
        if (args.empty()) {
            return 0;
        }

        std::vector<std::string> args_str;
        args_str.resize(args.size());

        for (size_t i = 0; i < args_str.size(); ++i) {
            args_str[i].assign(reinterpret_cast<const char *>(args[i].first), args[i].second);
        }

        util::cli::cmd_option_ci::ptr_type cmd_mgr = get_command_manager();
        cmd_mgr->start(args_str, true, this);
        return 0;
    }

    int app::bus_evt_callback_on_add_endpoint(const atbus::node &n, atbus::endpoint *ep, int res) {
        if (NULL == ep) {
            WLOGERROR("bus node 0x%llx make connection to NULL, res: %d", static_cast<unsigned long long>(n.get_id()), res);
        } else {
            WLOGINFO("bus node 0x%llx make connection to 0x%llx done, res: %d", static_cast<unsigned long long>(n.get_id()), 
                static_cast<unsigned long long>(ep->get_id()), res);

            if (evt_on_app_connected_) {
                evt_on_app_connected_(std::ref(*this), std::ref(*ep), res);
            }
        }
        return 0;
    }

    int app::bus_evt_callback_on_remove_endpoint(const atbus::node &n, atbus::endpoint *ep, int res) {
        if (NULL == ep) {
            WLOGERROR("bus node 0x%llx release connection to NULL, res: %d", static_cast<unsigned long long>(n.get_id()), res);
        } else {
            WLOGINFO("bus node 0x%llx release connection to 0x%llx done, res: %d", static_cast<unsigned long long>(n.get_id()), static_cast<unsigned long long>(ep->get_id()), res);

            if (evt_on_app_disconnected_) {
                evt_on_app_disconnected_(std::ref(*this), std::ref(*ep), res);
            }
        }
        return 0;
    }

    void app::setup_command() {
        util::cli::cmd_option_ci::ptr_type cmd_mgr = get_command_manager();
        // dump all connected nodes to default log collector
        // cmd_mgr->bind_cmd("dump");
        // dump all nodes to default log collector
        // cmd_mgr->bind_cmd("dump");
        // dump state

        // start server
        cmd_mgr->bind_cmd("start", &app::app::command_handler_start, this);
        // stop server
        cmd_mgr->bind_cmd("stop", &app::app::command_handler_stop, this);
        // reload all configures
        cmd_mgr->bind_cmd("reload", &app::app::command_handler_reload, this);

        // invalid command
        cmd_mgr->bind_cmd("@OnError", &app::command_handler_invalid, this);
    }

    int app::send_last_command(atbus::adapter::loop_t *ev_loop) {
        util::cli::shell_stream ss(std::cerr);

        if (last_command_.empty()) {
            ss() << util::cli::shell_font_style::SHELL_FONT_COLOR_RED << "command is empty." << std::endl;
            return -1;
        }

        // step 1. using the fastest way to connect to server
        int use_level = 0;
        bool is_sync_channel = false;
        atbus::channel::channel_address_t use_addr;

        for (size_t i = 0; i < conf_.bus_listen.size(); ++i) {
            atbus::channel::channel_address_t parsed_addr;
            make_address(conf_.bus_listen[i].c_str(), parsed_addr);
            int parsed_level = 0;
            if (0 == UTIL_STRFUNC_STRNCASE_CMP("shm", parsed_addr.scheme.c_str(), 3)) {
                parsed_level = 5;
                is_sync_channel = true;
            } else if (0 == UTIL_STRFUNC_STRNCASE_CMP("unix", parsed_addr.scheme.c_str(), 4)) {
                parsed_level = 4;
            } else if (0 == UTIL_STRFUNC_STRNCASE_CMP("ipv6", parsed_addr.scheme.c_str(), 4)) {
                parsed_level = 3;
            } else if (0 == UTIL_STRFUNC_STRNCASE_CMP("ipv4", parsed_addr.scheme.c_str(), 4)) {
                parsed_level = 2;
            } else if (0 == UTIL_STRFUNC_STRNCASE_CMP("dns", parsed_addr.scheme.c_str(), 3)) {
                parsed_level = 1;
            }

            if (parsed_level > use_level) {
#ifdef _WIN32
                // On windows, shared memory must be load in the same directory, so use IOS first
                // Use Global\\ prefix requires the SeCreateGlobalPrivilege privilege 
                if (5 == parsed_level && 0 != use_level) {
                    continue;
                }
#endif
                use_addr = parsed_addr;
                use_level = parsed_level;
            }

        }

        if (0 == use_level) {
            ss() << util::cli::shell_font_style::SHELL_FONT_COLOR_RED << "there is no available listener address to send command."
                 << std::endl;
            return -1;
        }

        if (!bus_node_) {
            bus_node_ = atbus::node::create();
        }

        // command mode , must no concurrence
        if (!bus_node_) {
            ss() << util::cli::shell_font_style::SHELL_FONT_COLOR_RED << "create bus node failed" << std::endl;
            return -1;
        }

        // no need to connect to parent node
        conf_.bus_conf.father_address.clear();

        // using 0 for command sender
        int ret = bus_node_->init(0, &conf_.bus_conf);
        if (ret < 0) {
            ss() << util::cli::shell_font_style::SHELL_FONT_COLOR_RED << "init bus node failed. ret: " << ret << std::endl;
            return ret;
        }

        ret = bus_node_->start();
        if (ret < 0) {
            ss() << util::cli::shell_font_style::SHELL_FONT_COLOR_RED << "start bus node failed. ret: " << ret << std::endl;
            return ret;
        }

        // step 2. connect failed return error code
        atbus::endpoint *ep = NULL;
        if (is_sync_channel) {
            // preallocate endpoint when using shared memory channel, because this channel can not be connected without endpoint
            atbus::endpoint::ptr_t new_ep = atbus::endpoint::create(bus_node_.get(), conf_.id, conf_.bus_conf.children_mask, 0, "");
            ret = bus_node_->add_endpoint(new_ep);
            if (ret < 0) {
                ss() << util::cli::shell_font_style::SHELL_FONT_COLOR_RED << "connect to " << use_addr.address << " failed. ret: " << ret
                    << std::endl;
                return ret;
            }

            ret = bus_node_->connect(use_addr.address.c_str(), new_ep.get());
            if (ret >= 0) {
                ep = new_ep.get();
            }
        } else {
            ret = bus_node_->connect(use_addr.address.c_str());
        }
        
        if (ret < 0) {
            ss() << util::cli::shell_font_style::SHELL_FONT_COLOR_RED << "connect to " << use_addr.address << " failed. ret: " << ret
                 << std::endl;
            return ret;
        }

        // step 3. setup timeout timer
        if (false == tick_timer_.timeout_timer.is_activited) {
            uv_timer_init(ev_loop, &tick_timer_.timeout_timer.timer);
            tick_timer_.timeout_timer.timer.data = this;
        
            int res = uv_timer_start(&tick_timer_.timeout_timer.timer, ev_stop_timeout, conf_.stop_timeout, 0);
            if (0 == res) {
                tick_timer_.timeout_timer.is_activited = true;
            } else {
                ss() << util::cli::shell_font_style::SHELL_FONT_COLOR_RED << "setup timeout timer failed, res: " << res << std::endl;
                set_flag(flag_t::TIMEOUT, false);
            }
        }

        // step 4. waiting for connect success
        while (NULL == ep) {
            uv_run(ev_loop, UV_RUN_ONCE);

            if (check_flag(flag_t::TIMEOUT)) {
                break;
            }
            ep = bus_node_->get_endpoint(conf_.id);
        }

        if (NULL == ep) {
            close_timer(tick_timer_.timeout_timer);
            ss() << util::cli::shell_font_style::SHELL_FONT_COLOR_RED << "connect to " << use_addr.address << " timeout." << std::endl;
            return -1;
        }

        // step 5. send data
        std::vector<const void *> arr_buff;
        std::vector<size_t> arr_size;
        arr_buff.resize(last_command_.size());
        arr_size.resize(last_command_.size());
        for (size_t i = 0; i < last_command_.size(); ++i) {
            arr_buff[i] = last_command_[i].data();
            arr_size[i] = last_command_[i].size();
        }

        ret = bus_node_->send_custom_cmd(ep->get_id(), &arr_buff[0], &arr_size[0], last_command_.size());
        if (ret < 0) {
            ss() << util::cli::shell_font_style::SHELL_FONT_COLOR_RED << "send command failed. ret: " << ret << std::endl;
            return ret;
        }

        // step 6. waiting for send done(for shm, no need to wait, for io_stream fd, waiting write callback)
        if (!is_sync_channel) {
            do {
                size_t start_times = ep->get_stat_push_start_times();
                size_t end_times = ep->get_stat_push_success_times() + ep->get_stat_push_failed_times();
                if (end_times >= start_times) {
                    break;
                }

                uv_run(ev_loop, UV_RUN_ONCE);
                if (check_flag(flag_t::TIMEOUT)) {
                    ss() << util::cli::shell_font_style::SHELL_FONT_COLOR_RED << "send command timeout" << std::endl;
                    ret = -1;
                    break;
                }
            } while (true);
        }

        close_timer(tick_timer_.timeout_timer);

        if (bus_node_) {
            bus_node_->reset();
            bus_node_.reset();
        }
        return ret;
    }
}
