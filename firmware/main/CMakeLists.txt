file(GLOB_RECURSE SRC_FILES "*.c" "*.h")
idf_component_register(SRCS ${SRC_FILES} INCLUDE_DIRS ".")
