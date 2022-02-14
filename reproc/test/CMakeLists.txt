cmake_minimum_required(VERSION 3.0.0)

add_doctest(argv argv.cpp)
add_doctest(environment environment.cpp)
add_doctest(io io.cpp)
add_doctest(stop stop.cpp)
add_doctest(working-directory working-directory.cpp)
