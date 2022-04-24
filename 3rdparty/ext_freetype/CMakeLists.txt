SET(PREFIX_ext_freetype "${EXTPREFIX}" )
if(APPLE OR UNIX)
ExternalProject_Add( ext_freetype
    DOWNLOAD_DIR ${EXTERNALS_DOWNLOAD_DIR}
    URL https://download.savannah.gnu.org/releases/freetype/freetype-2.11.1.tar.gz
    URL_HASH SHA256=f8db94d307e9c54961b39a1cc799a67d46681480696ed72ecf78d4473770f09b

    CONFIGURE_COMMAND <SOURCE_DIR>/configure --prefix=${PREFIX_ext_freetype} ${GLOBAL_AUTOMAKE_PROFILE}
    BUILD_COMMAND make
    INSTALL_COMMAND make install

    UPDATE_COMMAND ""
)
else()
ExternalProject_Add( ext_freetype
    DOWNLOAD_DIR ${EXTERNALS_DOWNLOAD_DIR}
    URL https://download.savannah.gnu.org/releases/freetype/freetype-2.11.1.tar.gz
    URL_HASH SHA256=f8db94d307e9c54961b39a1cc799a67d46681480696ed72ecf78d4473770f09b

    INSTALL_DIR ${PREFIX_ext_freetype}
    CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${PREFIX_ext_freetype} -DCMAKE_DISABLE_FIND_PACKAGE_BZip2=ON -DCMAKE_BUILD_TYPE=${GLOBAL_BUILD_TYPE} -DFT_WITH_PNG=TRUE ${GLOBAL_PROFILE}

    UPDATE_COMMAND ""
)
endif()
