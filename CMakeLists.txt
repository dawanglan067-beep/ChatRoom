cmake_minimum_required(VERSION 3.19)
project(ChatRoom LANGUAGES CXX)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets Network Sql)
find_package(Qt6 6.5 QUIET COMPONENTS WebSockets)

qt_standard_project_setup()

qt_add_executable(ChatRoom
    WIN32 MACOSX_BUNDLE
    authdialog.cpp
    authdialog.h
    authservice.cpp
    authservice.h
    chatclient.cpp
    chatclient.h
    chatstore.cpp
    chatstore.h
    chatutils.cpp
    chatutils.h
    conversation.h
    conversationitemdelegate.cpp
    conversationitemdelegate.h
    conversationlistmodel.cpp
    conversationlistmodel.h
    conversationmanager.cpp
    conversationmanager.h
    databasemanager.cpp
    databasemanager.h
    emojipicker.cpp
    emojipicker.h
    main.cpp
    mediautils.cpp
    mediautils.h
    messagehandler.cpp
    messagehandler.h
    mainwindow.cpp
    profilemanager.cpp
    profilemanager.h
    mainwindow.h
    message.h
    networkservice.cpp
    networkservice.h
    securestorage.cpp
    timeformatutils.cpp
    timeformatutils.h
    messagebubbledelegate.cpp
    messagebubbledelegate.h
    messagelistmodel.cpp
    messagelistmodel.h
    smtpmailer.cpp
    smtpmailer.h
)

target_link_libraries(ChatRoom
    PRIVATE
        Qt::Core
        Qt::Widgets
        Qt::Network
        Qt::Sql
)

if(WIN32)
    target_link_libraries(ChatRoom PRIVATE crypt32)
endif()

if (TARGET Qt6::WebSockets)
    target_link_libraries(ChatRoom PRIVATE Qt::WebSockets)
    target_compile_definitions(ChatRoom PRIVATE CHATROOM_HAS_WEBSOCKETS)
endif()

include(GNUInstallDirs)

install(TARGETS ChatRoom
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

qt_generate_deploy_app_script(
    TARGET ChatRoom
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})

include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Test)

    qt_add_executable(ChatRoomUnitTests
        tests/chatstoremodel_test.cpp
        chatstore.cpp
        chatstore.h
        conversationlistmodel.cpp
        conversationlistmodel.h
        messagelistmodel.cpp
        messagelistmodel.h
        timeformatutils.cpp
        timeformatutils.h
        conversation.h
        message.h
    )

    target_link_libraries(ChatRoomUnitTests
        PRIVATE
            Qt::Core
            Qt::Test
    )
    target_include_directories(ChatRoomUnitTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

    add_test(NAME ChatRoomUnitTests COMMAND ChatRoomUnitTests)
endif()
