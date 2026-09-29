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

foreach(test_name IN ITEMS drivecodec drivesimulator driveui)
    qt_add_executable(tst_${test_name} tst_${test_name}.cpp)
    target_link_libraries(tst_${test_name} PRIVATE hil_core Qt6::Test)
    set_target_properties(tst_${test_name} PROPERTIES
        AUTOMOC ON WIN32_EXECUTABLE FALSE MACOSX_BUNDLE FALSE)
    add_test(NAME ${test_name} COMMAND tst_${test_name} -o -,txt)
endforeach()

target_compile_definitions(tst_driveui PRIVATE
    DRIVE_QML_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../src/ui/qml")
get_target_property(QT_TEST_QMAKE Qt6::qmake IMPORTED_LOCATION)
execute_process(COMMAND "${QT_TEST_QMAKE}" -query QT_INSTALL_PLUGINS
    OUTPUT_VARIABLE QT_TEST_PLUGINS OUTPUT_STRIP_TRAILING_WHITESPACE)
set(DRIVE_UI_TEST_ENV
    "QT_QPA_PLATFORM=offscreen"
    "QT_QPA_PLATFORM_PLUGIN_PATH=${QT_TEST_PLUGINS}/platforms"
    "QT_QUICK_BACKEND=software"
    "QT_QUICK_CONTROLS_STYLE=Basic"
    "QT_ASSUME_STDERR_HAS_CONSOLE=1")
if(WIN32)
    list(APPEND DRIVE_UI_TEST_ENV "QT_QPA_FONTDIR=$ENV{WINDIR}/Fonts")
endif()
set_tests_properties(driveui PROPERTIES TIMEOUT 30 ENVIRONMENT "${DRIVE_UI_TEST_ENV}")
