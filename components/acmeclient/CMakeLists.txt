idf_component_register(
	SRCS Acme.cpp Dyndns.cpp
	INCLUDE_DIRS .
	REQUIRES arduinojson esp_https_server esp_http_client mbedtls)
