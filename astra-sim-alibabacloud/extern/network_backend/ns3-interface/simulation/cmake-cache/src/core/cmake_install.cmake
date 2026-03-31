# Install script for directory: /home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "/usr/local")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "debug")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Install shared libraries without execute permission?
if(NOT DEFINED CMAKE_INSTALL_SO_NO_EXE)
  set(CMAKE_INSTALL_SO_NO_EXE "1")
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "FALSE")
endif()

# Set default install directory permissions.
if(NOT DEFINED CMAKE_OBJDUMP)
  set(CMAKE_OBJDUMP "/usr/bin/objdump")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libns3.36.1-core-debug.so" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libns3.36.1-core-debug.so")
    file(RPATH_CHECK
         FILE "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libns3.36.1-core-debug.so"
         RPATH "")
  endif()
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE SHARED_LIBRARY FILES "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/build/lib/libns3.36.1-core-debug.so")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libns3.36.1-core-debug.so" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libns3.36.1-core-debug.so")
    if(CMAKE_INSTALL_DO_STRIP)
      execute_process(COMMAND "/usr/bin/strip" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libns3.36.1-core-debug.so")
    endif()
  endif()
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/ns3" TYPE FILE FILES
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/realtime-simulator-impl.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/wall-clock-synchronizer.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/int64x64-128.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/helper/csv-reader.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/helper/event-garbage-collector.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/helper/random-variable-stream-helper.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/abort.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/ascii-file.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/ascii-test.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/assert.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/atomic-counter.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/attribute-accessor-helper.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/attribute-construction-list.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/attribute-container-accessor-helper.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/attribute-container.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/attribute-helper.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/attribute.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/boolean.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/breakpoint.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/build-profile.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/calendar-scheduler.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/callback.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/command-line.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/config.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/default-deleter.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/default-simulator-impl.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/deprecated.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/des-metrics.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/double.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/empty.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/enum.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/event-id.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/event-impl.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/fatal-error.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/fatal-impl.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/global-value.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/hash-fnv.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/hash-function.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/hash-murmur3.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/hash.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/heap-scheduler.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/int-to-type.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/int64x64-double.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/int64x64.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/integer.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/length.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/list-scheduler.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/log-macros-disabled.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/log-macros-enabled.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/log.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/make-event.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/map-scheduler.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/math.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/names.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/node-printer.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/nstime.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/object-base.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/object-factory.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/object-map.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/object-ptr-container.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/object-vector.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/object.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/pair.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/pointer.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/priority-queue-scheduler.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/ptr.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/random-variable-stream.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/ref-count-base.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/rng-seed-manager.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/rng-stream.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/scheduler.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/show-progress.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/simple-ref-count.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/simulation-singleton.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/simulator-impl.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/simulator.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/singleton.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/string.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/synchronizer.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/system-path.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/system-wall-clock-ms.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/system-wall-clock-timestamp.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/test.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/time-printer.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/timer-impl.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/timer.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/trace-source-accessor.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/traced-callback.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/traced-value.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/trickle-timer.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/tuple.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/type-id.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/type-name.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/type-traits.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/uinteger.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/unix-fd-reader.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/unused.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/valgrind.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/vector.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/watchdog.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/src/core/model/random-variable.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/build/include/ns3/config-store-config.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/build/include/ns3/core-config.h"
    "/home/dreampop/simai/SimAI/astra-sim-alibabacloud/extern/network_backend/ns3-interface/simulation/build/include/ns3/core-module.h"
    )
endif()

