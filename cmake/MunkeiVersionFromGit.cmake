# The MIT License (MIT)
#
# Copyright (c) 2016-2017 Theo Willows
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

# Modified by Archie Atkinson

cmake_minimum_required( VERSION 3.5 )

include( CMakeParseArguments )

function( version_from_git )
  # Parse arguments
  set( options OPTIONAL FAST )
  set( oneValueArgs
    GIT_EXECUTABLE
    INCLUDE_HASH
    LOG
    TIMESTAMP
    )
  set( multiValueArgs )
  cmake_parse_arguments( ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN} )

  # Defaults
  if( NOT DEFINED ARG_INCLUDE_HASH )
    set( ARG_INCLUDE_HASH ON )
  endif()

  if( DEFINED ARG_GIT_EXECUTABLE )
    set( GIT_EXECUTABLE "${ARG_GIT_EXECUTABLE}" )
  else ()
    # Find Git or bail out
    find_package( Git )
    if( NOT GIT_FOUND )
      message( FATAL_ERROR "[MunkeiVersionFromGit] Git not found" )
    endif()
  endif()

  # Git describe
  execute_process(
    COMMAND           "${GIT_EXECUTABLE}" describe --tags --always
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    RESULT_VARIABLE   git_result_describe
    OUTPUT_VARIABLE   git_describe
    ERROR_VARIABLE    git_error_describe
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE
    )
  if( NOT git_result_describe EQUAL 0 )
    message( WARNING
      "[MunkeiVersionFromGit] Failed to execute 'git describe': ${git_error_describe}"
      )
    set( git_describe "" )
  endif()

  # Git dirty
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env bash -c "${GIT_EXECUTABLE} diff --quiet && echo 'clean' || echo 'dirty'"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    RESULT_VARIABLE   git_result_dirty
    OUTPUT_VARIABLE   git_dirty
    ERROR_VARIABLE    git_error_dirty
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE
    )
  if( NOT git_result_dirty EQUAL 0 )
    message( WARNING
      "[MunkeiVersionFromGit] Failed to execute git diff: ${git_error_dirty}"
      )
    set( git_dirty "" )
  endif()

  # Get Git tag (most recent reachable tag)
  execute_process(
    COMMAND           "${GIT_EXECUTABLE}" describe --tags --abbrev=0
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    RESULT_VARIABLE   git_result_tag
    OUTPUT_VARIABLE   git_tag
    ERROR_VARIABLE    git_error_tag
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE
    )
  if( NOT git_result_tag EQUAL 0 )
    # No tag found (or other error). Don't fail — handle gracefully later.
    message( VERBOSE
      "[MunkeiVersionFromGit] No tag found or failed to get tag: ${git_error_tag}"
      )
    set( git_tag "0.0.0" )
  endif()

  # Get Git branch
  execute_process(
    COMMAND           "${GIT_EXECUTABLE}" rev-parse --abbrev-ref HEAD
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    RESULT_VARIABLE   git_result_branch
    OUTPUT_VARIABLE   git_branch
    ERROR_VARIABLE    git_error_branch
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE
    )
  if( NOT git_result_branch EQUAL 0 )
    message( WARNING
      "[MunkeiVersionFromGit] Failed to get Git branch: ${git_error_branch}"
      )
    set( git_branch "" )
  endif()

  # Get short hash
  execute_process(
    COMMAND           "${GIT_EXECUTABLE}" rev-parse --short HEAD
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    RESULT_VARIABLE   git_result_hash
    OUTPUT_VARIABLE   git_hash
    ERROR_VARIABLE    git_error_hash
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE
    )
  if( NOT git_result_hash EQUAL 0 )
    message( WARNING
      "[MunkeiVersionFromGit] Failed to get Git hash: ${git_error_hash}"
      )
    set( git_hash "" )
  endif()

  # Initialize version parts to safe defaults
  set( version_major "0" )
  set( version_minor "0" )
  set( version_patch "0" )
  set( identifiers "" )
  set( metadata "" )

  # If we have a tag, try to parse it as semver vMAJOR.MINOR.PATCH...
  if( git_tag MATCHES "^v(0|[1-9][0-9]*)[.](0|[1-9][0-9]*)[.](0|[1-9][0-9]*)(-[.0-9A-Za-z-]+)?([+][.0-9A-Za-z-]+)?$" )
    set( version_major "${CMAKE_MATCH_1}" )
    set( version_minor "${CMAKE_MATCH_2}" )
    set( version_patch "${CMAKE_MATCH_3}" )
    set( identifiers   "${CMAKE_MATCH_4}" )
    set( metadata      "${CMAKE_MATCH_5}" )
    # strip leading characters from identifiers/metadata (regex captures include leading '-'/'+' chars)
    if( identifiers MATCHES "^-.+" )
      string( SUBSTRING "${identifiers}" 1 -1 identifiers )
    endif()
    if( metadata MATCHES "^\\+.+" )
      string( SUBSTRING "${metadata}" 1 -1 metadata )
    endif()
  else()
    if( git_tag STREQUAL "" )
      # No tag found. We'll fall back to 0.0.0 and use describe/hash for identifiers/metadata.
      if( git_describe MATCHES ".+" )
        # Use describe as prerelease identifier (safe because it often contains commit counts / hash)
        set( identifiers "${git_describe}" )
      endif()
      if( ARG_INCLUDE_HASH AND git_hash MATCHES ".+" )
        set( metadata "${git_hash}" )
      endif()
      message( VERBOSE
        "[MunkeiVersionFromGit] No semver tag found. Falling back to 0.0.0 with describe/hash info."
        )
    else()
      # Tag present but not valid semantic version — warn and fall back
      # message( WARNING
      #   "[MunkeiVersionFromGit] Git tag isn't valid semantic version: [${git_tag}] — falling back to 0.0.0"
      #   )
      if( ARG_INCLUDE_HASH AND git_hash MATCHES ".+" )
        set( metadata "${git_hash}" )
      elseif( git_describe MATCHES ".+" )
        set( identifiers "${git_describe}" )
      endif()
    endif()
  endif()

  # If include-hash requested and metadata already has something, ensure git_hash is included as well
  if( ARG_INCLUDE_HASH AND git_hash MATCHES ".+" )
    if( metadata MATCHES ".+" )
      # append shorter hash (use dot as separator)
      string( REPLACE "." ";" _tmp_metadata_list "${metadata}" )
      list( APPEND _tmp_metadata_list "${git_hash}" )
      string( REPLACE ";" "." metadata "${_tmp_metadata_list}" )
    elseif( NOT metadata MATCHES ".+" )
      set( metadata "${git_hash}" )
    endif()
  endif()

  # Construct the version variables
  set( version "${version_major}.${version_minor}.${version_patch}" )
  set( semver  "${version}" )

  # Identifiers (prerelease)
  if( identifiers MATCHES ".+" )
    # ensure identifiers have no leading '-' (already stripped above)
    set( semver "${semver}-${identifiers}" )
  endif()

  # Metadata (build metadata)
  if( metadata MATCHES ".+" )
    # ensure metadata has no leading '+'
    set( semver "${semver}+${metadata}" )
  endif()

  # Log the results
  if( ARG_LOG )
    message( STATUS
      "[MunkeiVersionFromGit] Version: ${version}
     Git tag:     [${git_tag}]
     Git hash:    [${git_hash}]
     Git branch:  [${git_branch}]
     Git dirty:   [${git_dirty}]
     Decorated:   [${git_describe}]
     Identifiers: [${identifiers}]
     Metadata:    [${metadata}]
     SemVer:      [${semver}]"
      )
  endif()

  # Set parent scope variables
  set( GIT_TAG       ${git_tag}       PARENT_SCOPE )
  set( GIT_DESCRIBE  ${git_describe}  PARENT_SCOPE )
  set( GIT_BRANCH    ${git_branch}    PARENT_SCOPE )
  set( GIT_HASH      ${git_hash}      PARENT_SCOPE )
  set( GIT_DIRTY     ${git_dirty}     PARENT_SCOPE )
  set( SEMVER        ${semver}        PARENT_SCOPE )
  set( VERSION       ${version}       PARENT_SCOPE )
  set( VERSION_MAJOR ${version_major} PARENT_SCOPE )
  set( VERSION_MINOR ${version_minor} PARENT_SCOPE )
  set( VERSION_PATCH ${version_patch} PARENT_SCOPE )

endfunction( version_from_git )
