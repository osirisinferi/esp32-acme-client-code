#
# This is a project Makefile. It is assumed the directory this Makefile resides in is a
# project subdirectory.
#

PROJECT_NAME := acmeclient

COMPONENT_SRCDIRS = libraries/arduinojson libraries/ftpclient

include $(IDF_PATH)/make/project.mk
