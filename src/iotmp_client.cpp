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

#include <thinger/iotmp/client.hpp>

#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/logging/log.h>
#include <zephyr/posix/sys/eventfd.h>

#include <cerrno>
#include <cstring>

LOG_MODULE_REGISTER(thinger_iotmp, CONFIG_THINGER_IOTMP_LOG_LEVEL);

// Client thread stack
K_THREAD_STACK_DEFINE(iotmp_client_stack, CONFIG_THINGER_IOTMP_STACK_SIZE);

namespace thinger::iotmp {

// ============================================================================
// Construction / Configuration
// ============================================================================

client::client() {
    k_mutex_init(&tx_mutex_);
    // Default port for Zephyr (TLS)
    port_ = 25206;
}

client::~client() {
    stop();
}

#ifdef CONFIG_THINGER_IOTMP_TLS
void client::set_tls_tag(sec_tag_t tag) {
    tls_tag_ = tag;
}
#endif

// ============================================================================
// CRTP transport implementation
// ============================================================================

bool client::send_bytes_impl(const void* buf, size_t len) {
    auto* ptr = static_cast<const uint8_t*>(buf);
    size_t remaining = len;

    while(remaining > 0) {
        ssize_t rc = zsock_send(sock_, ptr, remaining, 0);
        if(rc <= 0) return false;
        ptr += rc;
        remaining -= rc;
    }
    return true;
}

bool client::recv_bytes_impl(void* buf, size_t len) {
    auto* ptr = static_cast<uint8_t*>(buf);
    size_t remaining = len;

    while(remaining > 0) {
        ssize_t rc = zsock_recv(sock_, ptr, remaining, 0);
        if(rc <= 0) return false;
        ptr += rc;
        remaining -= rc;
    }
    return true;
}

bool client::is_connected_impl() const {
    return sock_ >= 0;
}

unsigned long client::get_millis() const {
    return static_cast<unsigned long>(k_uptime_get());
}

// ============================================================================
// CRTP connection implementation
// ============================================================================

bool client::connect_impl() {
    struct zsock_addrinfo hints = {};
    struct zsock_addrinfo* res = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%u", port_);

    int rc = zsock_getaddrinfo(host_, port_str, &hints, &res);
    if(rc != 0 || !res) {
        LOG_ERR("DNS resolve failed for %s: %d", host_, rc);
        return false;
    }

#ifdef CONFIG_THINGER_IOTMP_TLS
    sock_ = zsock_socket(res->ai_family, res->ai_socktype, IPPROTO_TLS_1_2);
#else
    sock_ = zsock_socket(res->ai_family, res->ai_socktype, res->ai_protocol);
#endif

    if(sock_ < 0) {
        LOG_ERR("Socket creation failed: %d", errno);
        zsock_freeaddrinfo(res);
        return false;
    }

#ifdef CONFIG_THINGER_IOTMP_TLS
    if(tls_tag_ >= 0) {
        sec_tag_t sec_tags[] = { tls_tag_ };
        rc = zsock_setsockopt(sock_, SOL_TLS, TLS_SEC_TAG_LIST, sec_tags, sizeof(sec_tags));
        if(rc < 0) {
            LOG_ERR("TLS sec tag failed: %d", errno);
            zsock_close(sock_);
            sock_ = -1;
            zsock_freeaddrinfo(res);
            return false;
        }

        rc = zsock_setsockopt(sock_, SOL_TLS, TLS_HOSTNAME, host_, strlen(host_) + 1);
        if(rc < 0) {
            LOG_WRN("TLS hostname failed: %d", errno);
        }
    }
#endif

    // Set receive timeout
    struct zsock_timeval tv = { .tv_sec = 10, .tv_usec = 0 };
    zsock_setsockopt(sock_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    rc = zsock_connect(sock_, res->ai_addr, res->ai_addrlen);
    zsock_freeaddrinfo(res);

    if(rc < 0) {
        LOG_ERR("Connect failed: %d", errno);
        zsock_close(sock_);
        sock_ = -1;
        return false;
    }

    return true;
}

void client::disconnect_impl() {
    if(sock_ >= 0) {
        zsock_close(sock_);
        sock_ = -1;
    }
}

bool client::data_available_impl() {
    if(sock_ < 0) return false;

    struct zsock_pollfd fds[2];
    fds[0].fd = sock_;
    fds[0].events = ZSOCK_POLLIN;
    fds[1].fd = event_fd_;
    fds[1].events = ZSOCK_POLLIN;

    int nfds = (event_fd_ >= 0) ? 2 : 1;
    int rc = zsock_poll(fds, nfds, 100); // 100ms blocking timeout

    // Handle TX queue wakeup
    if(nfds > 1 && (fds[1].revents & ZSOCK_POLLIN)) {
        eventfd_t val;
        eventfd_read(event_fd_, &val);
        flush_tx_queue();
    }

    // Socket error/hangup
    if(fds[0].revents & (ZSOCK_POLLHUP | ZSOCK_POLLERR)) {
        return false;
    }

    return rc > 0 && (fds[0].revents & ZSOCK_POLLIN);
}

// ============================================================================
// Lifecycle
// ============================================================================

int client::start() {
    if(running_) return -EALREADY;

    running_ = true;

    // Create eventfd for cross-thread wakeup
    event_fd_ = eventfd(0, 0);
    if(event_fd_ < 0) {
        LOG_ERR("Failed to create eventfd: %d", errno);
        running_ = false;
        return -errno;
    }

    thread_id_ = k_thread_create(
        &thread_data_,
        iotmp_client_stack,
        K_THREAD_STACK_SIZEOF(iotmp_client_stack),
        thread_entry,
        this, nullptr, nullptr,
        CONFIG_THINGER_IOTMP_PRIORITY, 0, K_NO_WAIT
    );

    k_thread_name_set(thread_id_, "iotmp");

    LOG_INF("IOTMP client started");
    return 0;
}

void client::stop() {
    if(!running_) return;

    running_ = false;

    // Wake poll loop so it exits
    if(event_fd_ >= 0) {
        eventfd_write(event_fd_, 1);
    }

    if(thread_id_) {
        k_thread_join(thread_id_, K_SECONDS(10));
        thread_id_ = nullptr;
    }

    disconnect();

    if(event_fd_ >= 0) {
        zsock_close(event_fd_);
        event_fd_ = -1;
    }

    LOG_INF("IOTMP client stopped");
}

// ============================================================================
// Thread
// ============================================================================

void client::thread_entry(void* p1, void* p2, void* p3) {
    auto* self = static_cast<client*>(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);
    self->run();
}

void client::run() {
    while(running_) {
        this->handle();
    }
}

// ============================================================================
// TX queue (for cross-thread message sending)
// ============================================================================

bool client::enqueue_message(iotmp_message& msg) {
    auto encoded = encode_message(msg);

    k_mutex_lock(&tx_mutex_, K_FOREVER);
    tx_queue_.push(std::move(encoded));
    k_mutex_unlock(&tx_mutex_);

    eventfd_write(event_fd_, 1);
    return true;
}

void client::flush_tx_queue() {
    k_mutex_lock(&tx_mutex_, K_FOREVER);
    while(!tx_queue_.empty()) {
        auto& data = tx_queue_.front();
        send_bytes(data.data(), data.size());
        tx_queue_.pop();
    }
    k_mutex_unlock(&tx_mutex_);
}

} // namespace thinger::iotmp
