get_filename_component(PROJ_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(ENV_PATH "${PROJ_ROOT}/.env")

file(READ "${ENV_PATH}" ENV_FILE_CONTENTS)
string(REPLACE "\r\n" "\n" ENV_FILE_CONTENTS "${ENV_FILE_CONTENTS}")
string(REPLACE "\n" ";" ENV_LINES "${ENV_FILE_CONTENTS}")

foreach(line IN LISTS ENV_LINES)
  if(line MATCHES "^[ ]*#")  # comment
    continue()
  endif()

  if(line MATCHES "^([A-Za-z_][A-Za-z0-9_]*)=(.*)$")
    set(key "${CMAKE_MATCH_1}")
    set(val "${CMAKE_MATCH_2}")
    string(STRIP "${val}" val)
    set(${key} "${val}")
  endif()
endforeach()

file(GLOB_RECURSE SRC_FILES "*.c" "*.h")
idf_component_register(SRCS ${SRC_FILES} INCLUDE_DIRS ".")

if(DEFINED WIFI_SSID)
  target_compile_definitions(${COMPONENT_LIB} PRIVATE WIFI_SSID="${WIFI_SSID}")
endif()
if(DEFINED WIFI_PASS)
  target_compile_definitions(${COMPONENT_LIB} PRIVATE WIFI_PASS="${WIFI_PASS}")
endif()
