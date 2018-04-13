cmake_minimum_required(VERSION 3.5)
project(Leader_election C)

set(CMAKE_C_STANDARD 99)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)


add_executable(Leader_election main.c Node.h Node.c)

target_link_libraries(Leader_election Threads::Threads)

