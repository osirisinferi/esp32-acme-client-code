file(GLOB SOURCES *.cpp *.c
	../libraries/ftpclient/src/*.c)

idf_component_register(SRCS "${SOURCES}"
  INCLUDE_DIRS "." ../libraries ../components
  ../libraries/ftpclient ../libraries/ftpclient/include
  ${BUILD_DIR}
  REQUIRES acmeclient esp_littlefs arduinojson esp_http_client nvs_flash)

add_custom_command(OUTPUT ${BUILD_DIR}/build.h
	COMMAND ${COMPONENT_DIR}/generate_build_date ${BUILD_DIR}/build.h
	DEPENDS ${SOURCES}
	VERBATIM)

add_custom_target(buildh DEPENDS ${BUILD_DIR}/build.h)
add_dependencies(${COMPONENT_LIB} buildh)
