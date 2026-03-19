# IOTMP-Zephyr

[![Build](https://github.com/thinger-io/IOTMP-Zephyr/actions/workflows/build.yml/badge.svg)](https://github.com/thinger-io/IOTMP-Zephyr/actions/workflows/build.yml)

[IOTMP](https://docs.thinger.io) client module for [Zephyr RTOS](https://zephyrproject.org), enabling devices to connect to the [Thinger.io](https://thinger.io) IoT platform.

Built on the [IOTMP-Embedded](https://github.com/thinger-io/IOTMP-Embedded) protocol core.

## Features

- Dedicated client thread with `poll()`-based event loop
- Automatic reconnection with exponential backoff
- Keep-alive management
- Resource streaming (periodic and event-driven)
- Server API: properties, buckets, endpoints, device-to-device calls
- TLS support via Zephyr's mbedTLS integration
- Fully configurable via Kconfig

## Quick start

### 1. Add to your `west.yml`

```yaml
manifest:
  projects:
    - name: iotmp-zephyr
      url: https://github.com/thinger-io/IOTMP-Zephyr
      revision: main
      path: modules/lib/iotmp-zephyr

    - name: iotmp-embedded
      url: https://github.com/thinger-io/IOTMP-Embedded
      revision: main
      path: modules/lib/iotmp-embedded
```

### 2. Enable in `prj.conf`

```
CONFIG_THINGER_IOTMP=y
```

### 3. Use in your application

```cpp
#include <zephyr/kernel.h>
#include <thinger/iotmp/client.hpp>

static thinger::iotmp::client thing;

int main() {
    thing.set_credentials("username", "device_id", "device_credential");
    thing.set_host("iot.thinger.io");

    // Output resource (sensor)
    thing["temperature"] = [](thinger::iotmp::output& out) {
        out["celsius"] = 23.5;
    };

    // Input resource (actuator)
    thing["led"] = [](thinger::iotmp::input& in) {
        bool state = in["state"].get<bool>();
    };

    thing.start();

    while(true) {
        k_sleep(K_SECONDS(5));
    }
}
```

## Configuration (Kconfig)

| Option | Default | Description |
|--------|---------|-------------|
| `CONFIG_THINGER_IOTMP_STACK_SIZE` | 4096 | Client thread stack size |
| `CONFIG_THINGER_IOTMP_PRIORITY` | 7 | Client thread priority |
| `CONFIG_THINGER_IOTMP_KEEPALIVE_SECONDS` | 60 | Keep-alive interval |
| `CONFIG_THINGER_IOTMP_MAX_MESSAGE_SIZE` | 4096 | Maximum message body size |
| `CONFIG_THINGER_IOTMP_TX_QUEUE_DEPTH` | 8 | Outbound message queue depth |
| `CONFIG_THINGER_IOTMP_MAX_RESOURCES` | 16 | Maximum registered resources |
| `CONFIG_THINGER_IOTMP_TLS` | y | Enable TLS (requires mbedTLS) |
| `CONFIG_THINGER_IOTMP_RECONNECT_BASE_MS` | 1000 | Reconnection base delay |
| `CONFIG_THINGER_IOTMP_RECONNECT_MAX_MS` | 60000 | Reconnection max delay |

## Server API

The client provides blocking, thread-safe methods callable from any thread:

```cpp
// Properties
thing.set_property("config", data);
thing.get_property("config", result);

// Data buckets
thing.write_bucket("readings", data);

// Endpoints
thing.call_endpoint("email_alert");
thing.call_endpoint("webhook", payload);
```

## Architecture

```
┌──────────────────┐  ┌──────────────────┐
│   Application    │  │   Sensor thread  │
└────────┬─────────┘  └────────┬─────────┘
         │ thing["res"] = ...  │ thing.stream("res")
         ▼                     ▼
┌─────────────────────────────────────────┐
│           IOTMP Client Thread           │
│  poll() + eventfd + keepalive + streams │
├─────────────────────────────────────────┤
│        IOTMP-Embedded (core)            │
│   PSON encoder/decoder + messages       │
├─────────────────────────────────────────┤
│       Zephyr BSD Sockets + mbedTLS      │
└─────────────────────────────────────────┘
```

## Requirements

- Zephyr RTOS v4.0+
- `CONFIG_NET_SOCKETS=y`
- `CONFIG_CPP=y` and `CONFIG_STD_CPP17=y`
- `CONFIG_GLIBCXX_LIBCPP=y` (for STL support)
- `CONFIG_EVENTFD=y`

## License

MIT License — see [LICENSE](LICENSE) for details.
