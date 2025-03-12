cmake_minimum_required(VERSION 3.30)
project(Design C)

set(CMAKE_C_STANDARD 11)

add_executable(Design main.c
        server.c
        server.h)