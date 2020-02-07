cmake_minimum_required(VERSION 2.8.9 FATAL_ERROR)
project(CANnibal)

set_property(GLOBAL PROPERTY FIND_LIBRARY_USE_LIB64_PATHS ON)
add_definitions(-ldl)

find_package(Boost REQUIRED system)
find_package(websocketpp REQUIRED)

find_package(PkgConfig REQUIRED)
pkg_check_modules(JSONCPP jsoncpp)

include_directories(
                    ${WEBSOCKETPP_INCLUDE_DIR}
                    ${Boost_INCLUDE_DIR}
                    ${JSONCPP_INCLUDE_DIRS})
link_directories(
                 ${Boost_LIBRARY_DIRS}
                 ${JSONCPP_LIBRARIES})

add_executable (${PROJECT_NAME}
                ${PROJECT_NAME}.cpp)

target_link_libraries(${PROJECT_NAME}
                      ${CMAKE_DL_LIBS}
                      ${Boost_LIBRARIES}
                      ${JSONCPP_LIBRARIES}
                      pthread)

target_link_libraries(${PROJECT_NAME}
                      ${CMAKE_DL_LIBS}
                      pthread)

install(TARGETS ${PROJECT_NAME}
        RUNTIME DESTINATION /usr/bin)
