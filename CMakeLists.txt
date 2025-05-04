cmake_minimum_required(VERSION 3.31)
project(splendid_net C)

set(CMAKE_C_STANDARD 99)

add_executable(splendid_net app.c
        xnet_tiny.h
        xnet_tiny.c)
