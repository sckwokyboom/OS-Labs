cmake_minimum_required(VERSION 3.22)
project(lab3_proxy C)

set(CMAKE_C_STANDARD 17)

add_executable(lab3_proxy
        main.c
        logger.h
        cache.h
)
