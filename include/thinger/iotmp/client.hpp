// The MIT License (MIT)
//
// Copyright (c) INTERNET OF THINGER SL
// Author: alvarolb@gmail.com (Alvaro Luis Bustamante)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#ifndef THINGER_IOTMP_CLIENT_HPP
#define THINGER_IOTMP_CLIENT_HPP

// Map IOTMP logging to Zephyr LOG_MODULE — must be defined before core headers
#include <zephyr/logging/log.h>

// Use LOG_MODULE_DECLARE so LOG_* macros work in headers included from any .cpp
// The actual LOG_MODULE_REGISTER is in iotmp_client.cpp
#ifndef THINGER_LOG_ERROR
#define THINGER_LOG_ERROR(fmt, ...)   do { LOG_MODULE_DECLARE(thinger_iotmp, CONFIG_THINGER_IOTMP_LOG_LEVEL); LOG_ERR(fmt, ##__VA_ARGS__); } while(0)
#endif
#ifndef THINGER_LOG_WARNING
#define THINGER_LOG_WARNING(fmt, ...) do { LOG_MODULE_DECLARE(thinger_iotmp, CONFIG_THINGER_IOTMP_LOG_LEVEL); LOG_WRN(fmt, ##__VA_ARGS__); } while(0)
#endif
#ifndef THINGER_LOG_INFO
#define THINGER_LOG_INFO(fmt, ...)    do { LOG_MODULE_DECLARE(thinger_iotmp, CONFIG_THINGER_IOTMP_LOG_LEVEL); LOG_INF(fmt, ##__VA_ARGS__); } while(0)
#endif
#ifndef THINGER_LOG_DEBUG
#define THINGER_LOG_DEBUG(fmt, ...)   do { LOG_MODULE_DECLARE(thinger_iotmp, CONFIG_THINGER_IOTMP_LOG_LEVEL); LOG_DBG(fmt, ##__VA_ARGS__); } while(0)
#endif

#include <thinger/iotmp/iotmp.hpp>

#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>

#include <string>
#include <queue>

#ifdef CONFIG_THINGER_IOTMP_TLS
#include <zephyr/net/tls_credentials.h>
#endif

namespace thinger::iotmp {

    // ----------------------------------------------------------------
    // Zephyr IOTMP client.
    //
    // Inherits all protocol and connection-lifecycle logic from
    // iotmp_client_base via CRTP.  This class only provides transport
    // primitives, Zephyr threading, and a cross-thread TX queue.
    //
    // Call start() to spawn the client thread — it calls handle()
    // in a loop, which manages connection, authentication, keepalive,
    // streams, and reconnection automatically.
    // ----------------------------------------------------------------
    class client : public iotmp_client_base<client> {
    public:
        client();
        ~client();

#ifdef CONFIG_THINGER_IOTMP_TLS
        void set_tls_tag(sec_tag_t tag);
#endif

        // Lifecycle
        int start();
        void stop();

        // ----- CRTP transport implementation -------------------------

        bool send_bytes_impl(const void* data, size_t len);
        bool recv_bytes_impl(void* buf, size_t len);
        bool is_connected_impl() const;
        bool connect_impl();
        void disconnect_impl();
        bool data_available_impl();
        unsigned long get_millis() const;

        // Send message from any thread (queues + wakes poll)
        bool enqueue_message(iotmp_message& msg);

    private:
        // Thread entry point
        static void thread_entry(void* p1, void* p2, void* p3);

        // Main loop (runs in client thread)
        void run();

        // Flush TX queue (called from client thread)
        void flush_tx_queue();

        // Socket
        int sock_ = -1;
        int event_fd_ = -1;

        // Thread
        struct k_thread thread_data_;
        k_tid_t thread_id_ = nullptr;
        volatile bool running_ = false;

        // TX queue (for cross-thread message sending)
        struct k_mutex tx_mutex_;
        std::queue<std::string> tx_queue_;

#ifdef CONFIG_THINGER_IOTMP_TLS
        sec_tag_t tls_tag_ = -1;
#endif
    };

}

#endif
