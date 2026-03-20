// Thinger.io IOTMP Zephyr Sample
//
// Demonstrates basic usage of the IOTMP client on Zephyr:
// - Connecting to Thinger.io platform
// - Defining resources (sensors and actuators)
// - Streaming data
// - Using the server API (properties, buckets)

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <thinger/iotmp/client.hpp>

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

// Replace with your Thinger.io credentials
#define THINGER_USERNAME    "your_username"
#define THINGER_DEVICE_ID   "your_device"
#define THINGER_CREDENTIALS "your_credentials"

// Global IOTMP client
static thinger::iotmp::client thing;

// Simulated sensor values
static float temperature = 22.5f;
static float humidity = 65.0f;
static bool led_state = false;

int main(void) {
    LOG_INF("Thinger.io IOTMP Zephyr Sample");

    // Configure connection
    thing.set_credentials(THINGER_USERNAME, THINGER_DEVICE_ID, THINGER_CREDENTIALS);
    thing.set_host("iot.thinger.io");

    // State callback
    thing.set_state_callback([](thinger::iotmp::client_state state) {
        switch(state) {
            case thinger::iotmp::client_state::SOCKET_CONNECTING:
                LOG_INF("Connecting...");
                break;
            case thinger::iotmp::client_state::AUTHENTICATED:
                LOG_INF("Authenticated!");
                break;
            case thinger::iotmp::client_state::READY:
                LOG_INF("Ready - resources available");
                break;
            case thinger::iotmp::client_state::DISCONNECTED:
                LOG_INF("Disconnected");
                break;
            case thinger::iotmp::client_state::SOCKET_CONNECTION_ERROR:
                LOG_ERR("Connection error");
                break;
            default:
                break;
        }
    });

    // ---- Define resources ----

    // Output resource: sensor readings (the server can read these)
    thing["environment"] = [](thinger::iotmp::output& out) {
        out["temperature"] = temperature;
        out["humidity"] = humidity;
    };

    // Input resource: LED control (the server can write to this)
    thing["led"] = [](thinger::iotmp::input& in) {
        led_state = in["state"].get<bool>();
        LOG_INF("LED set to: %s", led_state ? "ON" : "OFF");
    };

    // Input/Output resource: read and modify a threshold
    static float threshold = 30.0f;
    thing["threshold"] = [](thinger::iotmp::input& in, thinger::iotmp::output& out) {
        if(!in.is_empty()) {
            threshold = in["value"].get<float>();
            LOG_INF("Threshold set to: %.1f", (double)threshold);
        }
        out["value"] = threshold;
    };

    // Run resource: simple action (no data)
    thing["reboot"] = []() {
        LOG_INF("Reboot requested!");
        // sys_reboot(SYS_REBOOT_WARM);
    };

    // Start the client (launches background thread)
    int rc = thing.start();
    if(rc < 0) {
        LOG_ERR("Failed to start IOTMP client: %d", rc);
        return rc;
    }

    // Main loop — simulate sensor updates
    while(true) {
        k_sleep(K_SECONDS(5));

        // Simulate sensor drift
        temperature += 0.1f * ((k_uptime_get() % 10) - 5);
        humidity += 0.05f * ((k_uptime_get() % 8) - 4);
    }

    return 0;
}
