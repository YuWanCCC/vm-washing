cmake_minimum_required(VERSION 3.16)

find_package(Qt6 REQUIRED COMPONENTS Test)

qt_add_executable(tst_physicsmodels tst_physicsmodels.cpp)

target_link_libraries(tst_physicsmodels PRIVATE
    hil_core
    Qt6::Test)

set_target_properties(tst_physicsmodels PROPERTIES
    AUTOMOC ON
    WIN32_EXECUTABLE FALSE
    MACOSX_BUNDLE FALSE)

add_test(NAME PhysicsModels COMMAND tst_physicsmodels)
