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

#ifndef THINGER_LOG_ERROR
#define THINGER_LOG_ERROR(fmt, ...)   LOG_ERR(fmt, ##__VA_ARGS__)
#endif
#ifndef THINGER_LOG_WARNING
#define THINGER_LOG_WARNING(fmt, ...) LOG_WRN(fmt, ##__VA_ARGS__)
#endif
#ifndef THINGER_LOG_INFO
#define THINGER_LOG_INFO(fmt, ...)    LOG_INF(fmt, ##__VA_ARGS__)
#endif
#ifndef THINGER_LOG_DEBUG
#define THINGER_LOG_DEBUG(fmt, ...)   LOG_DBG(fmt, ##__VA_ARGS__)
#endif

#include <thinger/iotmp/iotmp.hpp>

#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>

#include <string>
#include <queue>
#include <functional>

#ifdef CONFIG_THINGER_IOTMP_TLS
#include <zephyr/net/tls_credentials.h>
#endif

namespace thinger::iotmp {

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

        // State
        bool is_connected() const { return connected_; }
        client_state get_state() const { return state_; }

        // Server API (blocking, thread-safe — call from any thread)
        bool set_property(const char* property_id, json_t data);
        bool get_property(const char* property_id, json_t& data);
        bool write_bucket(const char* bucket_id, json_t data);
        bool call_endpoint(const char* endpoint_name);
        bool call_endpoint(const char* endpoint_name, json_t data);

        // ----- CRTP transport implementation -------------------------

        bool send_bytes_impl(const void* data, size_t len);
        bool recv_bytes_impl(void* buf, size_t len);
        bool is_connected_impl() const;
        unsigned long get_millis() const;
        void on_disconnect();

        // Send message from any thread (queues + wakes poll)
        bool enqueue_message(iotmp_message& msg);

    private:
        // Thread entry point
        static void thread_entry(void* p1, void* p2, void* p3);

        // Main loop (runs in client thread)
        void run();

        // Connection lifecycle
        int do_connect();
        void do_disconnect();

        // Flush TX queue (called from client thread)
        void flush_tx_queue();

        // Server API helper (sends RUN message and waits for response)
        bool server_request(iotmp_message& msg, json_t* response_payload = nullptr);

        // State management (also notifies via base class callback)
        void set_state(client_state state);

        // Configuration (host_/port_/username_/device_id_/credential_ are in base)
        bool use_tls_ = true;

#ifdef CONFIG_THINGER_IOTMP_TLS
        sec_tag_t tls_tag_ = -1;
#endif

        // Socket
        int sock_ = -1;
        int event_fd_ = -1;

        // Thread
        struct k_thread thread_data_;
        k_tid_t thread_id_ = nullptr;
        volatile bool running_ = false;
        volatile bool connected_ = false;
        client_state state_ = client_state::DISCONNECTED;

        // TX queue (for cross-thread message sending)
        struct k_mutex tx_mutex_;
        std::queue<std::string> tx_queue_;

        // Read buffer
        uint8_t read_buffer_[CONFIG_THINGER_IOTMP_MAX_MESSAGE_SIZE];
    };

}

#endif
