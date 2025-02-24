set(kritapsdutils_LIB_SRCS
    psd.cpp
    compression.cpp

    asl/kis_asl_reader.cpp
    asl/kis_asl_xml_parser.cpp
    asl/kis_asl_object_catcher.cpp
    asl/kis_asl_callback_object_catcher.cpp
    asl/kis_asl_xml_writer.cpp
    asl/kis_asl_patterns_writer.cpp
    asl/kis_asl_writer.cpp
)

add_library(kritapsdutils SHARED ${kritapsdutils_LIB_SRCS} )
generate_export_header(kritapsdutils BASE_NAME kritapsdutils)

target_include_directories(kritapsdutils
    PUBLIC
        ${ZLIB_INCLUDE_DIR}
)

if (WIN32)
    target_link_libraries(kritapsdutils kritapigment kritaglobal KF5::I18n  ${QT_QTCORE_LIBRARY} ${QT_QTGUI_LIBRARY} ${WIN32_PLATFORM_NET_LIBS} ${ZLIB_LIBRARIES})
else (WIN32)
    target_link_libraries(kritapsdutils kritapigment kritaglobal KF5::I18n  ${QT_QTCORE_LIBRARY} ${QT_QTGUI_LIBRARY} ${ZLIB_LIBRARIES})
endif (WIN32)

set_target_properties(kritapsdutils PROPERTIES
    VERSION ${GENERIC_KRITA_LIB_VERSION} SOVERSION ${GENERIC_KRITA_LIB_SOVERSION}
)

install(TARGETS kritapsdutils  ${INSTALL_TARGETS_DEFAULT_ARGS})

add_subdirectory(tests)
