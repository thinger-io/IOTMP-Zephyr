cmake_minimum_required(VERSION 3.20.0)

# Point to both modules for local development
list(APPEND ZEPHYR_EXTRA_MODULES
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../iotmp-embedded
)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(thinger_iotmp_sample)

target_sources(app PRIVATE src/main.cpp)
