file(GLOB_RECURSE Headers **.h)
file(GLOB_RECURSE Sources **.cc)

add_executable(p4-fusion ${Headers} ${Sources})

target_include_directories(p4-fusion PRIVATE
    ../${HELIX_API}/include/
    ../vendor/libgit2/include/
    ../vendor/minitrace/
    ${CMAKE_CURRENT_LIST_DIR}
)

target_link_directories(p4-fusion PRIVATE
    ../${HELIX_API}/lib/
)

target_link_libraries(p4-fusion
    client
    rpc
    supp
    p4script_cstub
    ${OPENSSL_SSL_LIBRARIES}
    ${OPENSSL_CRYPTO_LIBRARIES}
    git2
)

if(MTR_ENABLED)
    target_link_libraries(p4-fusion minitrace)
endif()

if (APPLE)
    find_library(COREFOUNDATION_LIB CoreFoundation REQUIRED)
    find_library(CFNETWORK_LIB CFNetwork REQUIRED)
    find_library(COCOA_LIB Cocoa REQUIRED)
    find_library(SECURITY_LIB Security REQUIRED)
    target_link_libraries(p4-fusion
        ${CFNETWORK_LIB}
        ${COREFOUNDATION_LIB}
        ${COCOA_LIB}
        ${SECURITY_LIB}
    )
endif()
