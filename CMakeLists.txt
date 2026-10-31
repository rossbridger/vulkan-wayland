cmake_minimum_required(VERSION 3.16)

project(vulkan-wayland LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 1. Find PkgConfig (standard tool for discovering Wayland)
find_package(PkgConfig REQUIRED)
find_package(Vulkan REQUIRED)

pkg_check_modules(WAYLAND_CLIENT REQUIRED wayland-client)

# Find wayland-scanner executable via pkg-config
pkg_get_variable(WAYLAND_SCANNER wayland-scanner wayland_scanner)

# Find a protocol path (e.g., from wayland-protocols package)
pkg_get_variable(WAYLAND_PROTOCOLS wayland-protocols pkgdatadir)

set(WAYLAND_PROTOCOL_SRCS "")

# Macro to generate protocol source and header files
macro(wayland_generate_protocols _protocol_xml _output_name)
    set(_output_c ${CMAKE_CURRENT_BINARY_DIR}/${_output_name}.c)
    set(_output_h ${CMAKE_CURRENT_BINARY_DIR}/${_output_name}.h)
    add_custom_command(
        OUTPUT "${_output_c}"
        COMMAND ${WAYLAND_SCANNER} private-code "${_protocol_xml}" "${_output_c}"
        DEPENDS "${_protocol_xml}"
        VERBATIM
    )
    add_custom_command(
        OUTPUT "${_output_h}"
        COMMAND ${WAYLAND_SCANNER} client-header "${_protocol_xml}" "${_output_h}"
        DEPENDS "${_protocol_xml}"
        VERBATIM
    )
    list(APPEND WAYLAND_PROTOCOL_SRCS "${_output_c}" "${_output_h}")
endmacro()

set(XDG_SHELL_XML "${WAYLAND_PROTOCOLS}/stable/xdg-shell/xdg-shell.xml")
set(XDG_DECORATION_XML "${WAYLAND_PROTOCOLS}/unstable/xdg-decoration/xdg-decoration-unstable-v1.xml")

# Generate the files
wayland_generate_protocols(${XDG_SHELL_XML} xdg-shell-protocol)
wayland_generate_protocols(${XDG_DECORATION_XML} xdg_decoration_protocol)

add_executable(vulkan-wayland
    main.cpp
    ${WAYLAND_PROTOCOL_SRCS}
    window.h window.cpp
    shader.vert
    shader.frag
)

# 4. Include directories and link libraries
target_include_directories(vulkan-wayland PRIVATE ${WAYLAND_CLIENT_INCLUDE_DIRS} ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(vulkan-wayland PRIVATE ${WAYLAND_CLIENT_LIBRARIES} Vulkan::Vulkan)
target_compile_options(vulkan-wayland PRIVATE -fpermissive) # have to use this to suppress errors on pointer casting

# compiler vulkan shaders
function(add_shaders TARGET_NAME)
  set(SHADER_SOURCE_FILES ${ARGN}) # the rest of arguments to this function will be assigned as shader source files

  # Validate that source files have been passed
  list(LENGTH SHADER_SOURCE_FILES FILE_COUNT)
  if(FILE_COUNT EQUAL 0)
    message(FATAL_ERROR "Cannot create a shaders target without any source files")
  endif()

  set(SHADER_TARGETS)

  foreach(SHADER_SOURCE IN LISTS SHADER_SOURCE_FILES)
    cmake_path(GET SHADER_SOURCE FILENAME SHADER_NAME)

    # Build command
    set(SHADER_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${SHADER_NAME}.spv")

    # Add product
    set(SHADER_TARGET)
    string(APPEND SHADER_TARGET ${SHADER_NAME})
    list(APPEND SHADER_TARGETS ${SHADER_TARGET})

    add_custom_target(${SHADER_TARGET}
      COMMAND Vulkan::glslc "${CMAKE_CURRENT_SOURCE_DIR}/${SHADER_SOURCE}" "-o" "${SHADER_OUTPUT}"
      DEPENDS ${SHADER_SOURCE}
      COMMENT "Compiling Shaders ${SHADER_SOURCE}"
      SOURCES ${SHADER_SOURCE}
    )

  endforeach()
  add_dependencies(${TARGET_NAME} ${SHADER_TARGETS})
endfunction()

add_shaders(vulkan-wayland shader.vert shader.frag)

include(GNUInstallDirs)
install(TARGETS vulkan-wayland
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
