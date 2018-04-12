cmake_minimum_required(VERSION 3.10)
project(Leader_election C)

set(CMAKE_C_STANDARD 99)

add_executable(Leader_election main.c API.c API.h Node.h Node.c)