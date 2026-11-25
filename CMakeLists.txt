cmake_minimum_required(VERSION 3.21)

# Dépannage à distance de Packarch, en Qt 6 :
# - packarch-depannage : l'ordinateur dépanné (dossier depannage) ;
# - packarch-admin : la personne qui dépanne (dossier admin) ;
# - common : ce qu'ils partagent.
# Une seule version pour les deux programmes : celle du tag du dépôt
project(packarch-depannage VERSION 0.4.4 LANGUAGES CXX)

option(BUILD_DEPANNAGE "packarch-depannage (ordinateur dépanné)" ON)
option(BUILD_ADMIN "packarch-admin (personne qui dépanne ; demande qtermwidget)" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

include(GNUInstallDirs)
find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Network Test)
add_compile_options(-Wall -Wextra -Wpedantic)
add_compile_definitions(QT_NO_CAST_FROM_ASCII QT_NO_CAST_TO_ASCII)
enable_testing()

# Sans fenêtre (Qt Core seulement) : les commandes de packarch-depannage, lancées
# en root, par ssh ou dans une console, ne dépendent pas de la partie graphique de Qt
add_library(common-core STATIC
  common/chatmsg.cpp common/chatmsg.hpp
  common/tailscale.cpp common/tailscale.hpp)
target_link_libraries(common-core PUBLIC Qt6::Core)
target_include_directories(common-core PUBLIC common)

add_library(common STATIC
  common/singleinstance.cpp common/singleinstance.hpp
  common/theme.cpp common/theme.hpp)
target_link_libraries(common PUBLIC common-core Qt6::Network Qt6::Widgets)

if(BUILD_DEPANNAGE)
  # Sans fenêtre : la session, le tchat, le bureau de l'utilisateur (root, tests)
  add_library(depannage-core STATIC
    depannage/src/session.cpp depannage/src/session.hpp
    depannage/src/chatfile.cpp depannage/src/chatfile.hpp
    depannage/src/desktop.cpp depannage/src/desktop.hpp)
  target_link_libraries(depannage-core PUBLIC common-core)
  target_include_directories(depannage-core PUBLIC depannage/src)

  # packarch-depannage : les commandes et le mode texte, sans la partie graphique
  # de Qt. La fenêtre est un programme à part, qu'il lance : un problème
  # d'affichage (bureau qui ne démarre pas, Qt graphique cassé) laisse le
  # dépannage possible dans une console.
  set(GUI_DIR ${CMAKE_INSTALL_FULL_LIBDIR}/packarch-depannage)
  add_executable(packarch-depannage
    depannage/src/main.cpp
    depannage/src/text.cpp depannage/src/text.hpp)
  target_compile_definitions(packarch-depannage PRIVATE
    APP_VERSION="${PROJECT_VERSION}" GUI_PATH="${GUI_DIR}/packarch-depannage-gui")
  target_link_libraries(packarch-depannage PRIVATE depannage-core)

  add_executable(packarch-depannage-gui
    depannage/src/gui_main.cpp
    depannage/src/window.cpp depannage/src/window.hpp)
  target_compile_definitions(packarch-depannage-gui PRIVATE APP_VERSION="${PROJECT_VERSION}")
  target_link_libraries(packarch-depannage-gui PRIVATE depannage-core common Qt6::Widgets)

  add_executable(test_depannage depannage/tests/test_depannage.cpp)
  target_link_libraries(test_depannage PRIVATE depannage-core Qt6::Test)
  add_test(NAME depannage COMMAND test_depannage)

  # Composants d'installation : un paquet par programme (cmake --install build --component depannage)
  install(TARGETS packarch-depannage RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT depannage)
  install(TARGETS packarch-depannage-gui RUNTIME DESTINATION ${GUI_DIR} COMPONENT depannage)
  install(FILES depannage/data/org.packarch.Depannage.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications COMPONENT depannage)
  install(FILES depannage/data/org.packarch.depannage.policy DESTINATION ${CMAKE_INSTALL_DATADIR}/polkit-1/actions COMPONENT depannage)
  install(FILES depannage/data/packarch-depannage-cleanup.service DESTINATION lib/systemd/system COMPONENT depannage)
endif()

if(BUILD_ADMIN)
  # Terminal intégré (celui de QTerminal)
  find_package(qtermwidget6 REQUIRED)

  add_library(admin-core STATIC
    admin/src/tailnet.cpp admin/src/tailnet.hpp
    admin/src/chat.cpp admin/src/chat.hpp
    admin/src/keys.cpp admin/src/keys.hpp
    admin/src/info.cpp admin/src/info.hpp
    admin/src/screenproto.cpp admin/src/screenproto.hpp)
  target_link_libraries(admin-core PUBLIC common)
  target_include_directories(admin-core PUBLIC admin/src)

  add_executable(packarch-admin
    admin/src/main.cpp
    admin/src/mainwindow.cpp admin/src/mainwindow.hpp
    admin/src/machinepage.cpp admin/src/machinepage.hpp
    admin/src/screenview.cpp admin/src/screenview.hpp
    admin/src/terminal.cpp admin/src/terminal.hpp)
  target_compile_definitions(packarch-admin PRIVATE APP_VERSION="${PROJECT_VERSION}")
  target_link_libraries(packarch-admin PRIVATE admin-core Qt6::Widgets qtermwidget6)
  # Scripts envoyés à l'ordinateur dépanné (infos, écran, saisie), embarqués dans le programme
  qt_add_resources(packarch-admin admin-data PREFIX / BASE admin/data FILES admin/data/infos.sh admin/data/capture.sh admin/data/saisie.py)

  add_executable(test_admin admin/tests/test_admin.cpp)
  target_link_libraries(test_admin PRIVATE admin-core Qt6::Test)
  add_test(NAME admin COMMAND test_admin)

  install(TARGETS packarch-admin RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT admin)
  install(FILES admin/data/org.packarch.Admin.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications COMPONENT admin)
  # L'icône de la zone de notification, à l'ouverture du bureau
  install(FILES admin/data/packarch-admin-tray.desktop DESTINATION ${CMAKE_INSTALL_FULL_SYSCONFDIR}/xdg/autostart COMPONENT admin)
  # Compétence Claude Code du dépannage, liée dans ~/.claude/skills au lancement
  install(DIRECTORY claude/depannage DESTINATION ${CMAKE_INSTALL_DATADIR}/packarch-admin/claude COMPONENT admin)
  target_compile_definitions(packarch-admin PRIVATE SKILL_DIR="${CMAKE_INSTALL_FULL_DATADIR}/packarch-admin/claude/depannage")
endif()
