cmake_minimum_required(VERSION 3.0.0)
project(mqtt_client VERSION 0.1.0)
set(CMAKE_CXX_STANDARD 17)

add_executable(${PROJECT_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp)

find_package (vsomeip3 REQUIRED)
find_package(Boost 1.58 COMPONENTS system thread log REQUIRED)
# Find Paho MQTT C++ library
find_package(PahoMqttCpp REQUIRED)

target_link_libraries(${PROJECT_NAME} PRIVATE 
    vsomeip3 
    ${Boost_LIBRARIES} 
    PahoMqttCpp::paho-mqttpp3)

# Specify include directories
target_include_directories(${PROJECT_NAME} PRIVATE
    ${PahoMqttCpp_INCLUDE_DIRS} 
    ${Boost_INCLUDE_DIR} 
    ${VSOMEIP_INCLUDE_DIRS})

target_include_directories(${PROJECT_NAME} PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}/dependencies
)

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})