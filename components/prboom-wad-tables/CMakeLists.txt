idf_component_register(SRCS GAMMATBL.c SINETABL.c TANGTABL.c TANTOANG.c
                       INCLUDE_DIRS include)

# These headers are part of the component's source set for CMake-aware IDEs.
file(GLOB component_headers CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_LIST_DIR}/include/*.h")
target_sources(${COMPONENT_LIB} PRIVATE ${component_headers})
