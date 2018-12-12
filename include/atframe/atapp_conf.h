/**
 * atapp_conf.h
 *
 *  Created on: 2016年04月23日
 *      Author: owent
 */
#ifndef LIBATAPP_ATAPP_CONF_H
#define LIBATAPP_ATAPP_CONF_H

#pragma once

#include "libatbus.h"
#include <string>
#include <vector>

#include "atapp_version.h"

namespace atapp {
    struct app_conf {
        // bus configure
        atbus::node::bus_id_t id;
        std::string conf_file;
        std::string pid_file;
        const char *execute_path;
        bool resume_mode;

        std::vector<std::string> bus_listen;
        atbus::node::conf_t bus_conf;
        std::string app_version;

        // app configure
        uint64_t stop_timeout;  // module timeout when receive a stop message, libuv use uint64_t
        uint64_t tick_interval; // tick interval, libuv use uint64_t

        atbus::node::bus_id_t type_id;
        std::string type_name;
        std::string name;
        std::string hash_code;
    };
} // namespace atapp

#endif
