cmake_minimum_required(VERSION 3.1)

get_filename_component(PROJECT_NAME ${CMAKE_CURRENT_SOURCE_DIR} NAME)
project(${PROJECT_NAME})

set (CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC")

set(${PROJECT_NAME}_HEADERS
  block.h
  graph.h
  energy.h
  LinkedBlockList.h
  GCoptimization.h
)

set(${PROJECT_NAME}_SOURCES
  graph.cpp
  maxflow.cpp
  LinkedBlockList.cpp
  GCoptimization.cpp
)

add_library(${PROJECT_NAME} STATIC ${${PROJECT_NAME}_HEADERS} ${${PROJECT_NAME}_SOURCES})

set_target_properties(${PROJECT_NAME} PROPERTIES
                      FOLDER "3rd_party")


target_include_directories(${PROJECT_NAME} PRIVATE ${SegmentGeneration_gco_INCLUDE_DIR})

