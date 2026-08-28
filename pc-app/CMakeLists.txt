cmake_minimum_required(VERSION 3.16)

include(GNUInstallDirs)

find_package(Qt6 6.4 REQUIRED COMPONENTS
    Core
    Gui
    Quick
    Quick3D
    Quick3DAssetUtils
    SerialPort
    Charts)

qt_standard_project_setup(REQUIRES 6.4)
qt_policy(SET QTP0001 NEW)

set(APP_TARGET appWashingMachineHIL)

# ---------------------------------------------------------------------------
# 源文件收集：src 下按子目录(core/communication/models/ui)自动收集，
# 便于后续任务增删文件而无需改动构建脚本。
# ---------------------------------------------------------------------------
file(GLOB_RECURSE APP_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
file(GLOB_RECURSE APP_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h")

# main.cpp 单独作为可执行入口，其余源文件构成静态库供测试复用
list(REMOVE_ITEM APP_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")

file(GLOB_RECURSE APP_QML_FILES CONFIGURE_DEPENDS
    RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/qml/*.qml")

# QML 文件位于 src/ui/qml 下，通过资源别名去掉该前缀，
# 使其在 QML 模块根目录可见（loadFromModule("WashingMachineHIL", "Main")）
foreach(qml_file IN LISTS APP_QML_FILES)
    file(RELATIVE_PATH qml_alias
        "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/qml"
        "${CMAKE_CURRENT_SOURCE_DIR}/${qml_file}")
    set_source_files_properties("${qml_file}" PROPERTIES QT_RESOURCE_ALIAS "${qml_alias}")
endforeach()

# ---------------------------------------------------------------------------
# 外部 3D 模型：RuntimeLoader 内部用 QUrl::toLocalFile() 把路径交给 assimp 读取，
# 无法从 qrc 资源加载（qrc URL 转本地路径为空，assimp 报 "file not found"），
# 因此 GLB 必须以真实文件形式随可执行程序发布，运行时由 QML 侧拼出文件 URL。
# 原文件名含空格，复制时统一重命名为无空格路径以免 URL 解析出错。
# 更换模型时只需改动下面的变量并重新构建。
# ---------------------------------------------------------------------------
set(APP_MACHINE_MODEL "LG Washing Machine.glb")
set(APP_MACHINE_MODEL_DEST "models/washing-machine.glb")

# ---------------------------------------------------------------------------
# 仿真核心库（物理模型 / 仿真引擎 / 通信层）
# ---------------------------------------------------------------------------
add_library(hil_core STATIC ${APP_SOURCES} ${APP_HEADERS})

target_include_directories(hil_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")

target_link_libraries(hil_core PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::SerialPort)

set_target_properties(hil_core PROPERTIES
    AUTOMOC ON
    POSITION_INDEPENDENT_CODE ON)

# ---------------------------------------------------------------------------
# 可执行程序 + QML 模块
# ---------------------------------------------------------------------------
qt_add_executable(${APP_TARGET}
    src/main.cpp)

qt_add_qml_module(${APP_TARGET}
    URI WashingMachineHIL
    VERSION 1.0
    QML_FILES ${APP_QML_FILES})

target_link_libraries(${APP_TARGET} PRIVATE
    hil_core
    Qt6::Quick3D
    Qt6::Quick3DAssetUtils
    Qt6::Charts)

target_compile_definitions(${APP_TARGET} PRIVATE
    WMHIL_VERSION="${PROJECT_VERSION}")

set_target_properties(${APP_TARGET} PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER cn.hil.washingmachine
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}"
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE)

# 参数与场景配置以外部文件形式随可执行程序发布，便于现场调整
add_custom_command(TARGET ${APP_TARGET} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/config"
        "$<TARGET_FILE_DIR:${APP_TARGET}>/config"
    COMMENT "Copying simulation config files")

# GLB 模型需以真实文件供 RuntimeLoader 读取，拷到可执行文件旁
add_custom_command(TARGET ${APP_TARGET} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:${APP_TARGET}>/models"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/${APP_MACHINE_MODEL}"
        "$<TARGET_FILE_DIR:${APP_TARGET}>/${APP_MACHINE_MODEL_DEST}"
    COMMENT "Copying washing machine GLB model")

# ---------------------------------------------------------------------------
# Qt 依赖部署：使用 windeployqt 将 Qt DLL、QML 插件等直接复制到可执行文件旁，
# 使构建产物可直接双击运行，无需手动配置 PATH。
# ---------------------------------------------------------------------------
find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${QT_INSTALL_PREFIX}/bin")

if(WINDEPLOYQT_EXECUTABLE)
    add_custom_command(TARGET ${APP_TARGET} POST_BUILD
        COMMAND "${WINDEPLOYQT_EXECUTABLE}"
            --no-translations
            --no-system-d3d-compiler
            --no-opengl-sw
            "$<TARGET_FILE:${APP_TARGET}>"
        COMMENT "Deploying Qt dependencies via windeployqt"
    )
else()
    message(WARNING "windeployqt not found - exe will need Qt DLLs in PATH")
endif()

install(TARGETS ${APP_TARGET}
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})

install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/config"
    DESTINATION ${CMAKE_INSTALL_BINDIR})

install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/${APP_MACHINE_MODEL}"
    DESTINATION "${CMAKE_INSTALL_BINDIR}/models"
    RENAME "washing-machine.glb")

# ---------------------------------------------------------------------------
# 单元测试（Qt Test 可用时启用）
# ---------------------------------------------------------------------------
option(HIL_BUILD_TESTS "Build unit tests for the HIL simulation core" ON)

if(HIL_BUILD_TESTS)
    find_package(Qt6 QUIET COMPONENTS Test)
    if(Qt6Test_FOUND)
        enable_testing()
        add_subdirectory(tests)
    else()
        message(STATUS "Qt6::Test not found - skipping hil_core unit tests")
    endif()
endif()
