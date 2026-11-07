# Copies the built overlay into the live D2RLoader plugins folder.
# Run by CMake after each link (see CMakeLists.txt), or by hand:
#   cmake -DSRC_DLL=<dll> -DSRC_DATA=<overlay>/data -DDEST=<plugins folder> -P deploy.cmake
#
# Never fails the build: problems are reported as warnings, so a running game
# or a read-only Program Files folder does not stop compilation.

if(NOT EXISTS "${DEST}")
  message(WARNING "ESR Wiki overlay: live plugins folder not found, nothing deployed: ${DEST}")
  return()
endif()

set(_dest_data "${DEST}/esr-wiki-overlay")
set(_dest_images "${_dest_data}/images")
file(MAKE_DIRECTORY "${_dest_images}")

file(COPY_FILE "${SRC_DLL}" "${DEST}/d2rl-esr-wiki-overlay.dll"
     RESULT _result ONLY_IF_DIFFERENT)
if(NOT _result EQUAL 0)
  message(WARNING "ESR Wiki overlay: could not copy the DLL to ${DEST}. "
                  "Close Diablo II, or run the build from an Administrator shell. (${_result})")
  return()
endif()

file(COPY_FILE "${SRC_DATA}/wiki-data.txt" "${_dest_data}/wiki-data.txt"
     RESULT _result ONLY_IF_DIFFERENT)
if(NOT _result EQUAL 0)
  message(WARNING "ESR Wiki overlay: could not copy wiki-data.txt to ${_dest_data}.")
endif()

file(COPY_FILE "${SRC_DATA}/cube-wiki-data.txt" "${_dest_data}/cube-wiki-data.txt"
     RESULT _result ONLY_IF_DIFFERENT)
if(NOT _result EQUAL 0)
  message(WARNING "ESR Wiki overlay: could not copy cube-wiki-data.txt to ${_dest_data}.")
endif()

file(COPY_FILE "${SRC_DATA}/web-unique-art.txt" "${_dest_data}/web-unique-art.txt"
     RESULT _result ONLY_IF_DIFFERENT)
if(NOT _result EQUAL 0)
  message(WARNING "ESR Wiki overlay: could not copy web-unique-art.txt to ${_dest_data}.")
endif()

file(COPY_FILE "${SRC_DATA}/web-word-art.txt" "${_dest_data}/web-word-art.txt"
     RESULT _result ONLY_IF_DIFFERENT)
if(NOT _result EQUAL 0)
  message(WARNING "ESR Wiki overlay: could not copy web-word-art.txt to ${_dest_data}.")
endif()

file(COPY_FILE "${SRC_DATA}/web-base-art.txt" "${_dest_data}/web-base-art.txt"
     RESULT _result ONLY_IF_DIFFERENT)
if(NOT _result EQUAL 0)
  message(WARNING "ESR Wiki overlay: could not copy web-base-art.txt to ${_dest_data}.")
endif()

foreach(_ias ias-calculator.html ias-calculator-data.js)
  file(COPY_FILE "${SRC_DATA}/website-ias/${_ias}" "${_dest_data}/${_ias}"
       RESULT _result ONLY_IF_DIFFERENT)
  if(NOT _result EQUAL 0)
    message(WARNING "ESR Wiki overlay: could not copy ${_ias} to ${_dest_data}.")
  endif()
endforeach()

file(GLOB _pngs "${SRC_DATA}/images/*.png")
set(_copied 0)
foreach(_png IN LISTS _pngs)
  get_filename_component(_name "${_png}" NAME)
  file(COPY_FILE "${_png}" "${_dest_images}/${_name}" RESULT _result ONLY_IF_DIFFERENT)
  if(_result EQUAL 0)
    math(EXPR _copied "${_copied} + 1")
  endif()
endforeach()

# Language data: data/locales/<code>/{ui-strings,wiki-data,cube-wiki-data}.txt. Without these the plugin
# shows English whatever the game language is.
file(GLOB _locale_dirs LIST_DIRECTORIES true "${SRC_DATA}/locales/*")
set(_locales 0)
foreach(_dir IN LISTS _locale_dirs)
  if(IS_DIRECTORY "${_dir}")
    get_filename_component(_code "${_dir}" NAME)
    file(MAKE_DIRECTORY "${_dest_data}/locales/${_code}")
    file(GLOB _lfiles "${_dir}/*.txt")
    foreach(_lf IN LISTS _lfiles)
      get_filename_component(_lname "${_lf}" NAME)
      file(COPY_FILE "${_lf}" "${_dest_data}/locales/${_code}/${_lname}" RESULT _result ONLY_IF_DIFFERENT)
      if(NOT _result EQUAL 0)
        message(WARNING "ESR Wiki overlay: could not copy locales/${_code}/${_lname} to ${_dest_data}.")
      endif()
    endforeach()
    math(EXPR _locales "${_locales} + 1")
  endif()
endforeach()

message(STATUS "ESR Wiki overlay deployed to ${DEST} (DLL, wiki-data.txt, ${_copied} images, ${_locales} locales)")
