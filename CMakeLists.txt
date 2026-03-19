if(CONFIG_THINGER_IOTMP)

zephyr_library_named(thinger_iotmp)

zephyr_library_sources(
    src/iotmp_client.cpp
)

zephyr_include_directories(include)

endif()
