set(mpi_sources)
set(mpi_headers)
set(mpi_libraries)

if(${ENABLE_MPI})
  set(mpi_sources
      # model/point-to-point-remote-channel.cc
  )
  set(mpi_headers
      # model/point-to-point-remote-channel.h
  )
  set(mpi_libraries
      # ${libmpi}
      # ${MPI_CXX_LIBRARIES}
  )
endif()

build_lib(
  LIBNAME point-to-point
  SOURCE_FILES
    ${mpi_sources}
    helper/point-to-point-helper.cc
    helper/qbb-helper.cc
    model/point-to-point-channel.cc
    model/point-to-point-net-device.cc
    model/ppp-header.cc
    model/qbb-net-device.cc
    model/pause-header.cc
    model/cn-header.cc
    model/pint.cc
    model/qbb-header.cc
    model/qbb-channel.cc
    model/qbb-remote-channel.cc
    model/rdma-driver.cc
    model/rdma-queue-pair.cc
    model/rdma-hw.cc
    model/switch-node.cc
    model/switch-mmu.cc
    model/nvswitch-node.cc
    # 新增：真实光学物理建模节点（独立于 optical-module-node）。
    model/optical-physical-node.cc
    # 新增：方案B物理层增强链路（光层额外时延/重配/可选丢包）。
    model/optical-qbb-channel.cc
    # 新增：编译最小可用光模块实体（小固定时延 + 更友好的拥塞记账）。
    model/optical-module-node.cc
    # 新增：OCS 抽象节点（动态端口匹配 fabric）。
    model/optical-ocs.cc
    model/point-to-point-remote-channel.cc
  HEADER_FILES
    ${mpi_headers}
    helper/point-to-point-helper.h
    helper/qbb-helper.h
    helper/sim-setting.h
    model/point-to-point-channel.h
    model/point-to-point-net-device.h
    model/ppp-header.h
    model/qbb-net-device.h
    model/pause-header.h
    model/cn-header.h
    model/pint.h
    model/qbb-header.h
    model/qbb-channel.h
    model/qbb-remote-channel.h
    model/rdma-driver.h
    model/rdma-queue-pair.h
    model/rdma-hw.h
    model/switch-node.h
    model/switch-mmu.h
    model/trace-format.h
    model/nvswitch-node.h
    # 新增：真实光学物理建模节点头文件。
    model/optical-physical-node.h
    # 新增：方案B物理层增强链路头文件。
    model/optical-qbb-channel.h
    # 新增：导出最小可用光模块实体头文件。
    model/optical-module-node.h
    # 新增：导出 OCS 抽象节点头文件。
    model/optical-ocs.h
    model/point-to-point-remote-channel.h
  LIBRARIES_TO_LINK ${libnetwork}
                    ${mpi_libraries}
  TEST_SOURCES test/point-to-point-test.cc
)
