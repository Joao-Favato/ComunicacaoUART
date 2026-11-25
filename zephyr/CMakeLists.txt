cmake_minimum_required(VERSION 3.13.1)
include($ENV{ZEPHYR_BASE}/cmake/app/boilerplate.cmake NO_POLICY_SCOPE)
project(ComunicacaoUART)

FILE(GLOB app_sources ../src/*.c*)
target_sources(app PRIVATE ${app_sources} ../lib/pwm_z42/src/pwm_z42.c)
target_include_directories(app PRIVATE ../lib/pwm_z42/src)
