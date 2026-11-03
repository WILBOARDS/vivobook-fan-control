cmake_minimum_required(VERSION 3.22)
project(vivobook-fan-control VERSION 1.3.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui DBus Widgets)
qt_standard_project_setup()

add_library(fancore STATIC
    src/core/sensors.cpp
    src/core/controller.cpp
    src/core/policyconfig.cpp
)
target_include_directories(fancore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(fancore PUBLIC Qt6::Core)

# Command line, helper and policy daemon: Qt Core only, no GUI stack.
qt_add_executable(vivobook-fanctl src/main_cli.cpp src/cli.cpp)
target_link_libraries(vivobook-fanctl PRIVATE fancore Qt6::Core)
target_compile_definitions(vivobook-fanctl PRIVATE APP_VERSION="${PROJECT_VERSION}")

qt_add_executable(fancontrol-helper helper/main.cpp)
target_link_libraries(fancontrol-helper PRIVATE fancore Qt6::Core)

qt_add_executable(fancontrol-policyd helper/policyd.cpp)
target_link_libraries(fancontrol-policyd PRIVATE fancore Qt6::Core)

# Graphical window. Also answers to the command line verbs for convenience.
qt_add_executable(vivobook-fan-control
    src/main.cpp
    src/cli.cpp
    src/ui/fancard.cpp
    src/ui/historychart.cpp
    src/ui/policypanel.cpp
    src/ui/settingspage.cpp
    src/ui/theme.cpp
    src/ui/mainwindow.cpp
)
target_link_libraries(vivobook-fan-control PRIVATE fancore Qt6::Core Qt6::Gui Qt6::DBus Qt6::Widgets)
target_compile_definitions(vivobook-fan-control PRIVATE APP_VERSION="${PROJECT_VERSION}")

# Installed into /usr, so resolve Qt from the system loader path instead of a
# deployed-app $ORIGIN runpath.
set_target_properties(vivobook-fanctl fancontrol-helper fancontrol-policyd vivobook-fan-control
                      PROPERTIES INSTALL_RPATH "")

option(BUILD_UI_SNAPSHOT "Build the offscreen UI snapshot tool (development only)" OFF)
if(BUILD_UI_SNAPSHOT)
    qt_add_executable(ui-snapshot tests/ui_snapshot.cpp)
    target_link_libraries(ui-snapshot PRIVATE fancore Qt6::Core Qt6::Gui Qt6::DBus Qt6::Widgets)
    target_sources(ui-snapshot PRIVATE src/ui/fancard.cpp src/ui/historychart.cpp
                                       src/ui/policypanel.cpp src/ui/settingspage.cpp
                                       src/ui/theme.cpp src/ui/mainwindow.cpp)
endif()

include(GNUInstallDirs)

# The "cli" component is a self-contained headless install; "gui" adds only the
# window, so the two ship as separate .deb packages.
set(CLI_LIBEXECDIR ${CMAKE_INSTALL_LIBEXECDIR}/${PROJECT_NAME})

install(TARGETS vivobook-fanctl RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT cli)
install(TARGETS fancontrol-helper fancontrol-policyd
        RUNTIME DESTINATION ${CLI_LIBEXECDIR} COMPONENT cli)
install(FILES data/org.vivobookfancontrol.policy
        DESTINATION ${CMAKE_INSTALL_DATADIR}/polkit-1/actions COMPONENT cli)
install(FILES data/vivobook-fan-restore.service data/vivobook-fan-policy.service
        DESTINATION lib/systemd/system COMPONENT cli)
install(FILES README.md LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/${PROJECT_NAME}
        COMPONENT cli)

install(TARGETS vivobook-fan-control RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT gui)
install(FILES data/${PROJECT_NAME}.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
        COMPONENT gui)
install(FILES data/icons/${PROJECT_NAME}.svg
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps COMPONENT gui)
install(FILES data/${PROJECT_NAME}.service DESTINATION lib/systemd/user COMPONENT gui)
