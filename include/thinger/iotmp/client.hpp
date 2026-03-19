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

#include <thinger/iotmp/core/iotmp_types.hpp>
#include <thinger/iotmp/core/iotmp_message.hpp>
#include <thinger/iotmp/core/iotmp_encoder.hpp>
#include <thinger/iotmp/core/iotmp_decoder.hpp>
#include <thinger/iotmp/core/iotmp_resource.hpp>

#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>

#include <string>
#include <map>
#include <queue>

#ifdef CONFIG_THINGER_IOTMP_TLS
#include <zephyr/net/tls_credentials.h>
#endif

namespace thinger::iotmp {

    enum class client_state {
        DISCONNECTED,
        CONNECTING,
        CONNECTED,
        AUTHENTICATING,
        AUTHENTICATED,
        AUTH_FAILED,
        CONNECTION_ERROR,
        READY
    };

    struct stream_config {
        iotmp_resource* resource = nullptr;
        unsigned int interval_ms = 0;
        int64_t last_streaming = 0;
    };

    class client {
    public:
        client();
        ~client();

        // Configuration
        void set_credentials(const char* username, const char* device_id, const char* credentials);
        void set_host(const char* host, uint16_t port = 0);

#ifdef CONFIG_THINGER_IOTMP_TLS
        void set_tls_tag(sec_tag_t tag);
#endif

        // Resource registration
        iotmp_resource& operator[](const char* name);

        // Lifecycle
        int start();
        void stop();

        // State
        bool is_connected() const { return connected_; }
        client_state get_state() const { return state_; }

        // State callback
        using state_callback_t = std::function<void(client_state)>;
        void set_state_callback(state_callback_t cb) { state_callback_ = std::move(cb); }

        // Server API (blocking, thread-safe — call from any thread)
        bool set_property(const char* property_id, json_t data);
        bool get_property(const char* property_id, json_t& data);
        bool write_bucket(const char* bucket_id, json_t data);
        bool call_endpoint(const char* endpoint_name);
        bool call_endpoint(const char* endpoint_name, json_t data);

        // Manually stream a resource's current value
        bool stream(const char* resource_name);

    private:
        // Thread entry point
        static void thread_entry(void* p1, void* p2, void* p3);

        // Main loop (runs in client thread)
        void run();

        // Connection lifecycle
        int do_connect();
        void do_disconnect();
        bool do_authenticate();

        // I/O helpers
        bool socket_read(void* buf, size_t len);
        bool socket_write(const void* buf, size_t len);
        bool read_varint(uint32_t& value);

        // Message handling
        bool read_message(iotmp_message& msg);
        bool write_message(iotmp_message& msg);
        void handle_message(iotmp_message& msg);
        void handle_resource_request(iotmp_message& request);

        // Send message from client thread (direct write)
        void send_message(iotmp_message& msg);

        // Send message from any thread (queues + wakes poll)
        bool enqueue_message(iotmp_message& msg);
        void flush_tx_queue();

        // Streaming helpers
        bool stream_resource(iotmp_resource& resource, uint16_t stream_id);
        void check_stream_intervals();

        // Server API helper (sends RUN message and waits for response)
        bool server_request(iotmp_message& msg, json_t* response_payload = nullptr);

        // State
        void notify_state(client_state state);

        // Resource matching
        iotmp_resource* find_resource(const std::string& path);

        // Configuration
        std::string host_ = "iot.thinger.io";
        uint16_t port_ = 25206;
        std::string username_;
        std::string device_id_;
        std::string credentials_;
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
        state_callback_t state_callback_;

        // TX queue (for cross-thread message sending)
        struct k_mutex tx_mutex_;
        std::queue<std::string> tx_queue_;

        // Resources and streams
        std::map<std::string, iotmp_resource> resources_;
        std::map<uint16_t, stream_config> streams_;

        // Read buffer
        uint8_t read_buffer_[CONFIG_THINGER_IOTMP_MAX_MESSAGE_SIZE];
    };

}

#endif
