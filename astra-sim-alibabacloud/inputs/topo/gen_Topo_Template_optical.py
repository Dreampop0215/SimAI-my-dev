"""
This file can generate topology of AlibabaHPN, Spectrum-X, DCN+.
Users can freely customize the topology according to their needs。
"""

import argparse
import warnings


def build_topo_header(nodes, gpu_per_server, nvswitch_num, switch_num,
                      optical_module_num, links, gpu_type):
    """
    中文注释：输出新格式 topo 头。
    新格式: node_num gpus_per_server nvswitch_num switch_num optical_module_num link_num gpu_type
    """
    return (
        f"{nodes} {gpu_per_server} {nvswitch_num} {switch_num} "
        f"{optical_module_num} {int(links)} {gpu_type}"
    )


def _build_output_name(base_name, parameters, with_optical):
    """中文注释：开启光模块时追加后缀，避免覆盖原始拓扑文件。"""
    name = (
        f"{base_name}_{parameters['gpu']}g_{parameters['gpu_per_server']}gps_"
        f"{parameters['bandwidth']}_{parameters['gpu_type']}"
    )
    if with_optical:
        name += "_optical_2hop"
    return name


def _write_link(f, src, dst, bw, lat, err):
    f.write(f"{src} {dst} {bw} {lat} {err}\n")


def _write_asw_psw_links(f, asw_nodes, psw_nodes, optical_nodes, parameters):
    """
    中文注释：光模块开启时使用 2-hop 方式写链路:
    asw -> optical -> psw。
    未开启时保持传统 asw -> psw 直连。
    """
    use_optical = len(optical_nodes) > 0
    for asw in asw_nodes:
        for idx, psw in enumerate(psw_nodes):
            if use_optical:
                optical = optical_nodes[idx % len(optical_nodes)]
                _write_link(
                    f, asw, optical,
                    parameters['ap_bandwidth'], parameters['latency'], parameters['error_rate']
                )
                _write_link(
                    f, optical, psw,
                    parameters['ap_bandwidth'], parameters['latency'], parameters['error_rate']
                )
            else:
                _write_link(
                    f, asw, psw,
                    parameters['ap_bandwidth'], parameters['latency'], parameters['error_rate']
                )


def Rail_Opti_SingleToR(parameters):
    nodes_per_asw = parameters['nics_per_aswitch']
    asw_switch_num_per_segment = parameters['gpu_per_server']
    capacity_per_segment = nodes_per_asw * asw_switch_num_per_segment
    if parameters['gpu'] % capacity_per_segment == 0:
        segment_num = int(parameters['gpu'] / capacity_per_segment)
    else:
        segment_num = int(parameters['gpu'] / capacity_per_segment) + 1

    if segment_num != parameters['asw_switch_num'] / asw_switch_num_per_segment:
        warnings.warn(
            "Error relations between total GPU Nums and total aws_switch_num.\n"
            "The correct asw_switch_num is set to "
            + str(segment_num * asw_switch_num_per_segment)
        )
        parameters['asw_switch_num'] = segment_num * asw_switch_num_per_segment

    print("asw_switch_num: " + str(parameters['asw_switch_num']))
    if segment_num > int(parameters['asw_per_psw'] / asw_switch_num_per_segment):
        raise ValueError("Number of GPU exceeds the capacity of Rail_Optimized_SingleToR(One Pod)")

    pod_num = 1
    print("psw_switch_num: " + str(parameters['psw_switch_num']))
    print("Creating Topology of totally " + str(segment_num) + " segment(s), totally " + str(pod_num) + " pod(s).")

    nv_switch_num = int(parameters['gpu'] / parameters['gpu_per_server']) * parameters['nv_switch_per_server']
    optical_module_num = parameters['optical_module_num']
    use_optical = optical_module_num > 0

    nodes = int(parameters['gpu'] + parameters['asw_switch_num'] + parameters['psw_switch_num'] + nv_switch_num + optical_module_num)
    servers = int(parameters['gpu'] / parameters['gpu_per_server'])
    switch_num = int(parameters['psw_switch_num'] + parameters['asw_switch_num'])
    switch_nodes = int(switch_num + nv_switch_num + optical_module_num)

    asw_psw_edges = int(parameters['psw_switch_num'] / pod_num * parameters['asw_switch_num'])
    links = int(
        asw_psw_edges
        + servers * asw_switch_num_per_segment
        + servers * parameters['nv_switch_per_server'] * parameters['gpu_per_server']
    )
    if use_optical:
        # 中文注释：2-hop 拆边后，asw-psw 每一条逻辑链路变成两条物理链路。
        links += asw_psw_edges

    if parameters['topology'] == 'Spectrum-X':
        file_name = _build_output_name('Spectrum-X', parameters, use_optical)
    else:
        file_name = _build_output_name('Rail_Opti_SingleToR', parameters, use_optical)

    with open(file_name, 'w') as f:
        print(file_name)
        first_line = build_topo_header(
            nodes, parameters['gpu_per_server'], nv_switch_num,
            switch_num, optical_module_num, links, parameters['gpu_type']
        )
        f.write(first_line + '\n')

        nv_switch = []
        asw_switch = []
        psw_switch = []
        optical_switch = []
        dsw_switch = []
        sec_line = ""
        nnodes = nodes - switch_nodes

        for i in range(nnodes, nodes):
            sec_line = sec_line + str(i) + " "
            if len(nv_switch) < nv_switch_num:
                nv_switch.append(i)
            elif len(asw_switch) < parameters['asw_switch_num']:
                asw_switch.append(i)
            elif len(psw_switch) < parameters['psw_switch_num']:
                psw_switch.append(i)
            elif len(optical_switch) < optical_module_num:
                optical_switch.append(i)
            else:
                dsw_switch.append(i)

        f.write(sec_line + '\n')

        ind_asw = 0
        curr_node = 0
        group_num = 0
        group_account = 0
        ind_nv = 0

        for i in range(parameters['gpu']):
            curr_node += 1
            if curr_node > parameters['gpu_per_server']:
                curr_node = 1
                ind_nv += parameters['nv_switch_per_server']

            for j in range(0, parameters['nv_switch_per_server']):
                _write_link(
                    f, i, nv_switch[ind_nv + j],
                    parameters['nvlink_bw'], parameters['nv_latency'], parameters['error_rate']
                )

            _write_link(
                f,
                i,
                asw_switch[group_num * asw_switch_num_per_segment + ind_asw],
                parameters['bandwidth'], parameters['latency'], parameters['error_rate']
            )

            ind_asw += 1
            group_account += 1

            if ind_asw == asw_switch_num_per_segment:
                ind_asw = 0
            if group_account == (parameters['gpu_per_server'] * parameters['nics_per_aswitch']):
                group_num += 1
                group_account = 0

        _write_asw_psw_links(f, asw_switch, psw_switch, optical_switch, parameters)


def Rail_Opti_DualToR_SinglePlane(parameters):
    nodes_per_asw = parameters['nics_per_aswitch']
    asw_switch_num_per_segment = parameters['gpu_per_server'] * 2
    half_asw_switch_num_per_segment = int(asw_switch_num_per_segment / 2)
    capacity_per_segment = nodes_per_asw * half_asw_switch_num_per_segment

    if parameters['gpu'] % capacity_per_segment == 0:
        segment_num = int(parameters['gpu'] / capacity_per_segment)
    else:
        segment_num = int(parameters['gpu'] / capacity_per_segment) + 1

    if segment_num != parameters['asw_switch_num'] / asw_switch_num_per_segment:
        warnings.warn(
            "Error relations between total GPU Nums and total aws_switch_num.\n"
            "The correct asw_switch_num is set to "
            + str(segment_num * asw_switch_num_per_segment)
        )
        parameters['asw_switch_num'] = segment_num * asw_switch_num_per_segment

    print("asw_switch_num: " + str(parameters['asw_switch_num']))
    if segment_num > int(parameters['asw_per_psw'] / half_asw_switch_num_per_segment):
        raise ValueError("Number of GPU exceeds the capacity of Rail_Optimized_SingleToR(One Pod)")

    pod_num = 1
    print("psw_switch_num: " + str(parameters['psw_switch_num']))
    print("Creating Topology of totally " + str(segment_num) + " segment(s), totally " + str(pod_num) + " pod(s).")

    nv_switch_num = int(parameters['gpu'] / parameters['gpu_per_server']) * parameters['nv_switch_per_server']
    optical_module_num = parameters['optical_module_num']
    use_optical = optical_module_num > 0

    nodes = int(parameters['gpu'] + parameters['asw_switch_num'] + parameters['psw_switch_num'] + nv_switch_num + optical_module_num)
    servers = int(parameters['gpu'] / parameters['gpu_per_server'])
    switch_num = int(parameters['psw_switch_num'] + parameters['asw_switch_num'])
    switch_nodes = int(switch_num + nv_switch_num + optical_module_num)

    asw_psw_edges = int(parameters['psw_switch_num'] / pod_num * parameters['asw_switch_num'])
    links = int(
        asw_psw_edges
        + servers * asw_switch_num_per_segment
        + servers * parameters['nv_switch_per_server'] * parameters['gpu_per_server']
    )
    if use_optical:
        # 中文注释：2-hop 拆边增加同数量的中间光链路。
        links += asw_psw_edges

    if parameters['topology'] == 'AlibabaHPN':
        base_name = 'AlibabaHPN'
        file_name = f"{base_name}_{parameters['gpu']}g_{parameters['gpu_per_server']}gps_DualToR_SinglePlane_{parameters['bandwidth']}_{parameters['gpu_type']}"
    else:
        base_name = 'Rail_Opti'
        file_name = f"{base_name}_{parameters['gpu']}g_{parameters['gpu_per_server']}gps_DualToR_SinglePlane_{parameters['bandwidth']}_{parameters['gpu_type']}"
    if use_optical:
        file_name += '_optical_2hop'

    with open(file_name, 'w') as f:
        print(file_name)
        first_line = build_topo_header(
            nodes, parameters['gpu_per_server'], nv_switch_num,
            switch_num, optical_module_num, links, parameters['gpu_type']
        )
        f.write(first_line + '\n')

        nv_switch = []
        asw_switch_1 = []
        asw_switch_2 = []
        psw_switch = []
        optical_switch = []
        dsw_switch = []
        sec_line = ""
        nnodes = nodes - switch_nodes

        for i in range(nnodes, nodes):
            sec_line = sec_line + str(i) + " "
            if len(nv_switch) < nv_switch_num:
                nv_switch.append(i)
            elif len(asw_switch_1) < parameters['asw_switch_num'] / 2:
                asw_switch_1.append(i)
            elif len(asw_switch_2) < parameters['asw_switch_num'] / 2:
                asw_switch_2.append(i)
            elif len(psw_switch) < parameters['psw_switch_num']:
                psw_switch.append(i)
            elif len(optical_switch) < optical_module_num:
                optical_switch.append(i)
            else:
                dsw_switch.append(i)

        f.write(sec_line + '\n')

        ind_asw = 0
        curr_node = 0
        group_num = 0
        group_account = 0
        ind_nv = 0

        for i in range(parameters['gpu']):
            curr_node += 1
            if curr_node > parameters['gpu_per_server']:
                curr_node = 1
                ind_nv += parameters['nv_switch_per_server']

            for j in range(0, parameters['nv_switch_per_server']):
                _write_link(
                    f, i, nv_switch[ind_nv + j],
                    parameters['nvlink_bw'], parameters['nv_latency'], parameters['error_rate']
                )

            asw_idx = group_num * half_asw_switch_num_per_segment + ind_asw
            _write_link(
                f, i, asw_switch_1[asw_idx],
                parameters['bandwidth'], parameters['latency'], parameters['error_rate']
            )
            _write_link(
                f, i, asw_switch_2[asw_idx],
                parameters['bandwidth'], parameters['latency'], parameters['error_rate']
            )

            ind_asw += 1
            group_account += 1

            if ind_asw == half_asw_switch_num_per_segment:
                ind_asw = 0
            if group_account == (parameters['gpu_per_server'] * parameters['nics_per_aswitch']):
                group_num += 1
                group_account = 0

        _write_asw_psw_links(f, asw_switch_1, psw_switch, optical_switch, parameters)
        _write_asw_psw_links(f, asw_switch_2, psw_switch, optical_switch, parameters)


def Rail_Opti_DualToR_DualPlane(parameters):
    nodes_per_asw = parameters['nics_per_aswitch']
    asw_switch_num_per_segment = parameters['gpu_per_server'] * 2
    half_asw_switch_num_per_segment = int(asw_switch_num_per_segment / 2)
    capacity_per_segment = nodes_per_asw * half_asw_switch_num_per_segment

    if parameters['gpu'] % capacity_per_segment == 0:
        segment_num = int(parameters['gpu'] / capacity_per_segment)
    else:
        segment_num = int(parameters['gpu'] / capacity_per_segment) + 1

    if segment_num != parameters['asw_switch_num'] / asw_switch_num_per_segment:
        warnings.warn(
            "Error relations between total GPU Nums and total aws_switch_num.\n"
            "The correct asw_switch_num is set to "
            + str(segment_num * asw_switch_num_per_segment)
        )
        parameters['asw_switch_num'] = segment_num * asw_switch_num_per_segment

    print("asw_switch_num: " + str(parameters['asw_switch_num']))
    if segment_num > int(parameters['asw_per_psw'] / half_asw_switch_num_per_segment):
        raise ValueError("Number of GPU exceeds the capacity of Rail_Optimized_SingleToR(One Pod)")

    pod_num = 1
    print("psw_switch_num: " + str(parameters['psw_switch_num']))
    print("Creating Topology of totally " + str(segment_num) + " segment(s), totally " + str(pod_num) + " pod(s).")

    nv_switch_num = int(parameters['gpu'] / parameters['gpu_per_server']) * parameters['nv_switch_per_server']
    optical_module_num = parameters['optical_module_num']
    use_optical = optical_module_num > 0

    nodes = int(parameters['gpu'] + parameters['asw_switch_num'] + parameters['psw_switch_num'] + nv_switch_num + optical_module_num)
    servers = int(parameters['gpu'] / parameters['gpu_per_server'])
    switch_num = int(parameters['psw_switch_num'] + parameters['asw_switch_num'])
    switch_nodes = int(switch_num + nv_switch_num + optical_module_num)

    asw_psw_edges = int(parameters['psw_switch_num'] / pod_num / 2 * parameters['asw_switch_num'])
    links = int(
        asw_psw_edges
        + servers * asw_switch_num_per_segment
        + servers * parameters['nv_switch_per_server'] * parameters['gpu_per_server']
    )
    if use_optical:
        links += asw_psw_edges

    if parameters['topology'] == 'AlibabaHPN':
        file_name = f"AlibabaHPN_{parameters['gpu']}g_{parameters['gpu_per_server']}gps_DualToR_DualPlane_{parameters['bandwidth']}_{parameters['gpu_type']}"
    else:
        file_name = f"Rail_Opti_{parameters['gpu']}g_{parameters['gpu_per_server']}gps_DualToR_DualPlane_{parameters['bandwidth']}_{parameters['gpu_type']}"
    if use_optical:
        file_name += '_optical_2hop'

    with open(file_name, 'w') as f:
        print(file_name)
        first_line = build_topo_header(
            nodes, parameters['gpu_per_server'], nv_switch_num,
            switch_num, optical_module_num, links, parameters['gpu_type']
        )
        f.write(first_line + '\n')

        nv_switch = []
        asw_switch_1 = []
        asw_switch_2 = []
        psw_switch_1 = []
        psw_switch_2 = []
        optical_switch = []
        dsw_switch = []
        sec_line = ""
        nnodes = nodes - switch_nodes

        for i in range(nnodes, nodes):
            sec_line = sec_line + str(i) + " "
            if len(nv_switch) < nv_switch_num:
                nv_switch.append(i)
            elif len(asw_switch_1) < parameters['asw_switch_num'] / 2:
                asw_switch_1.append(i)
            elif len(asw_switch_2) < parameters['asw_switch_num'] / 2:
                asw_switch_2.append(i)
            elif len(psw_switch_1) < parameters['psw_switch_num'] / 2:
                psw_switch_1.append(i)
            elif len(psw_switch_2) < parameters['psw_switch_num'] / 2:
                psw_switch_2.append(i)
            elif len(optical_switch) < optical_module_num:
                optical_switch.append(i)
            else:
                dsw_switch.append(i)

        f.write(sec_line + '\n')

        ind_asw = 0
        curr_node = 0
        group_num = 0
        group_account = 0
        ind_nv = 0

        for i in range(parameters['gpu']):
            curr_node += 1
            if curr_node > parameters['gpu_per_server']:
                curr_node = 1
                ind_nv += parameters['nv_switch_per_server']

            for j in range(0, parameters['nv_switch_per_server']):
                _write_link(
                    f, i, nv_switch[ind_nv + j],
                    parameters['nvlink_bw'], parameters['nv_latency'], parameters['error_rate']
                )

            asw_idx = group_num * half_asw_switch_num_per_segment + ind_asw
            _write_link(
                f, i, asw_switch_1[asw_idx],
                parameters['bandwidth'], parameters['latency'], parameters['error_rate']
            )
            _write_link(
                f, i, asw_switch_2[asw_idx],
                parameters['bandwidth'], parameters['latency'], parameters['error_rate']
            )

            ind_asw += 1
            group_account += 1

            if ind_asw == half_asw_switch_num_per_segment:
                ind_asw = 0
            if group_account == (parameters['gpu_per_server'] * parameters['nics_per_aswitch']):
                group_num += 1
                group_account = 0

        _write_asw_psw_links(f, asw_switch_1, psw_switch_1, optical_switch, parameters)
        _write_asw_psw_links(f, asw_switch_2, psw_switch_2, optical_switch, parameters)


def No_Rail_Opti_SingleToR(parameters):
    nodes_per_asw = parameters['nics_per_aswitch']
    asw_switch_num_per_segment = 1
    capacity_per_segment = nodes_per_asw * asw_switch_num_per_segment

    if parameters['gpu'] % capacity_per_segment == 0:
        segment_num = int(parameters['gpu'] / capacity_per_segment)
    else:
        segment_num = int(parameters['gpu'] / capacity_per_segment) + 1

    if segment_num != parameters['asw_switch_num'] / asw_switch_num_per_segment:
        warnings.warn(
            "Error relations between total GPU Nums and total aws_switch_num.\n"
            "The correct asw_switch_num is set to "
            + str(segment_num * asw_switch_num_per_segment)
        )
        parameters['asw_switch_num'] = segment_num * asw_switch_num_per_segment

    print("asw_switch_num: " + str(parameters['asw_switch_num']))
    if segment_num > int(parameters['asw_per_psw'] / asw_switch_num_per_segment):
        raise ValueError("Number of GPU exceeds the capacity of Rail_Optimized_SingleToR(One Pod)")

    pod_num = 1
    print("psw_switch_num: " + str(parameters['psw_switch_num']))
    print("Creating Topology of totally " + str(segment_num) + " segment(s), totally " + str(pod_num) + " pod(s).")

    nv_switch_num = int(parameters['gpu'] / parameters['gpu_per_server']) * parameters['nv_switch_per_server']
    optical_module_num = parameters['optical_module_num']
    use_optical = optical_module_num > 0

    nodes = int(parameters['gpu'] + parameters['asw_switch_num'] + parameters['psw_switch_num'] + nv_switch_num + optical_module_num)
    servers = int(parameters['gpu'] / parameters['gpu_per_server'])
    switch_num = int(parameters['psw_switch_num'] + parameters['asw_switch_num'])
    switch_nodes = int(switch_num + nv_switch_num + optical_module_num)

    asw_psw_edges = int(parameters['psw_switch_num'] / pod_num * parameters['asw_switch_num'])
    links = int(
        asw_psw_edges
        + servers * parameters['gpu_per_server']
        + servers * parameters['nv_switch_per_server'] * parameters['gpu_per_server']
    )
    if use_optical:
        links += asw_psw_edges

    if parameters['topology'] == 'DCN+':
        file_name = _build_output_name('DCN+SingleToR', parameters, use_optical)
    else:
        file_name = _build_output_name('No_Rail_Opti', parameters, use_optical)

    with open(file_name, 'w') as f:
        print(file_name)
        first_line = build_topo_header(
            nodes, parameters['gpu_per_server'], nv_switch_num,
            switch_num, optical_module_num, links, parameters['gpu_type']
        )
        f.write(first_line + '\n')

        nv_switch = []
        asw_switch = []
        psw_switch = []
        optical_switch = []
        dsw_switch = []
        sec_line = ""
        nnodes = nodes - switch_nodes

        for i in range(nnodes, nodes):
            sec_line = sec_line + str(i) + " "
            if len(nv_switch) < nv_switch_num:
                nv_switch.append(i)
            elif len(asw_switch) < parameters['asw_switch_num']:
                asw_switch.append(i)
            elif len(psw_switch) < parameters['psw_switch_num']:
                psw_switch.append(i)
            elif len(optical_switch) < optical_module_num:
                optical_switch.append(i)
            else:
                dsw_switch.append(i)

        f.write(sec_line + '\n')

        ind_asw = 0
        curr_node = 0
        group_num = 0
        group_account = 0
        ind_nv = 0

        for i in range(parameters['gpu']):
            curr_node += 1
            if curr_node > parameters['gpu_per_server']:
                curr_node = 1
                ind_nv += parameters['nv_switch_per_server']

            for j in range(0, parameters['nv_switch_per_server']):
                _write_link(
                    f, i, nv_switch[ind_nv + j],
                    parameters['nvlink_bw'], parameters['nv_latency'], parameters['error_rate']
                )

            _write_link(
                f,
                i,
                asw_switch[group_num * asw_switch_num_per_segment + ind_asw],
                parameters['bandwidth'], parameters['latency'], parameters['error_rate']
            )

            group_account += 1
            if group_account == nodes_per_asw:
                group_num += 1
                group_account = 0

        _write_asw_psw_links(f, asw_switch, psw_switch, optical_switch, parameters)


def No_Rail_Opti_DualToR(parameters):
    nodes_per_asw = parameters['nics_per_aswitch']
    asw_switch_num_per_segment = 2
    half_asw_switch_num_per_segment = int(asw_switch_num_per_segment / 2)
    capacity_per_segment = nodes_per_asw * half_asw_switch_num_per_segment

    if parameters['gpu'] % capacity_per_segment == 0:
        segment_num = int(parameters['gpu'] / capacity_per_segment)
    else:
        segment_num = int(parameters['gpu'] / capacity_per_segment) + 1

    if segment_num != parameters['asw_switch_num'] / asw_switch_num_per_segment:
        warnings.warn(
            "Error relations between total GPU Nums and total aws_switch_num.\n"
            "The correct asw_switch_num is set to "
            + str(segment_num * asw_switch_num_per_segment)
        )
        parameters['asw_switch_num'] = segment_num * asw_switch_num_per_segment

    print("asw_switch_num: " + str(parameters['asw_switch_num']))
    if segment_num > int(parameters['asw_per_psw'] / asw_switch_num_per_segment):
        raise ValueError("Number of GPU exceeds the capacity of Rail_Optimized_SingleToR(One Pod)")

    pod_num = 1
    print("psw_switch_num: " + str(parameters['psw_switch_num']))
    print("Creating Topology of totally " + str(segment_num) + " segment(s), totally " + str(pod_num) + " pod(s).")

    nv_switch_num = int(parameters['gpu'] / parameters['gpu_per_server']) * parameters['nv_switch_per_server']
    optical_module_num = parameters['optical_module_num']
    use_optical = optical_module_num > 0

    nodes = int(parameters['gpu'] + parameters['asw_switch_num'] + parameters['psw_switch_num'] + nv_switch_num + optical_module_num)
    servers = int(parameters['gpu'] / parameters['gpu_per_server'])
    switch_num = int(parameters['psw_switch_num'] + parameters['asw_switch_num'])
    switch_nodes = int(switch_num + nv_switch_num + optical_module_num)

    asw_psw_edges = int(parameters['psw_switch_num'] / pod_num * parameters['asw_switch_num'])
    links = int(
        asw_psw_edges
        + servers * parameters['gpu_per_server'] * 2
        + servers * parameters['nv_switch_per_server'] * parameters['gpu_per_server']
    )
    if use_optical:
        links += asw_psw_edges

    if parameters['topology'] == 'DCN+':
        file_name = f"DCN+DualToR_{parameters['gpu']}g_{parameters['gpu_per_server']}gps_{parameters['bandwidth']}_{parameters['gpu_type']}"
    else:
        file_name = f"No_Rail_Opti_{parameters['gpu']}g_{parameters['gpu_per_server']}gps_DualToR_{parameters['bandwidth']}_{parameters['gpu_type']}"
    if use_optical:
        file_name += '_optical_2hop'

    with open(file_name, 'w') as f:
        print(file_name)
        first_line = build_topo_header(
            nodes, parameters['gpu_per_server'], nv_switch_num,
            switch_num, optical_module_num, links, parameters['gpu_type']
        )
        f.write(first_line + '\n')

        nv_switch = []
        asw_switch_1 = []
        asw_switch_2 = []
        psw_switch = []
        optical_switch = []
        dsw_switch = []
        sec_line = ""
        nnodes = nodes - switch_nodes

        for i in range(nnodes, nodes):
            sec_line = sec_line + str(i) + " "
            if len(nv_switch) < nv_switch_num:
                nv_switch.append(i)
            elif len(asw_switch_1) < parameters['asw_switch_num'] / 2:
                asw_switch_1.append(i)
            elif len(asw_switch_2) < parameters['asw_switch_num'] / 2:
                asw_switch_2.append(i)
            elif len(psw_switch) < parameters['psw_switch_num']:
                psw_switch.append(i)
            elif len(optical_switch) < optical_module_num:
                optical_switch.append(i)
            else:
                dsw_switch.append(i)

        f.write(sec_line + '\n')

        ind_asw = 0
        curr_node = 0
        group_num = 0
        group_account = 0
        ind_nv = 0

        for i in range(parameters['gpu']):
            curr_node += 1
            if curr_node > parameters['gpu_per_server']:
                curr_node = 1
                ind_nv += parameters['nv_switch_per_server']

            for j in range(0, parameters['nv_switch_per_server']):
                _write_link(
                    f, i, nv_switch[ind_nv + j],
                    parameters['nvlink_bw'], parameters['nv_latency'], parameters['error_rate']
                )

            asw_idx = group_num * half_asw_switch_num_per_segment + ind_asw
            _write_link(
                f, i, asw_switch_1[asw_idx],
                parameters['bandwidth'], parameters['latency'], parameters['error_rate']
            )
            _write_link(
                f, i, asw_switch_2[asw_idx],
                parameters['bandwidth'], parameters['latency'], parameters['error_rate']
            )

            group_account += 1
            if group_account == nodes_per_asw:
                group_num += 1
                group_account = 0

        _write_asw_psw_links(f, asw_switch_1, psw_switch, optical_switch, parameters)
        _write_asw_psw_links(f, asw_switch_2, psw_switch, optical_switch, parameters)


def main():
    parser = argparse.ArgumentParser(description='Python script for generating a topology for SimAI')

    # Whole Structure Parameters:
    parser.add_argument('-topo', '--topology', type=str, default=None, help='Template for AlibabaHPN, Spectrum-X, DCN+')
    parser.add_argument('--ro', action='store_true', help='use rail-optimized structure')
    parser.add_argument('--dt', action='store_true', help='enable dual ToR, only for DCN+')
    parser.add_argument('--dp', action='store_true', help='enable dual_plane, only for AlibabaHPN')
    parser.add_argument('-g', '--gpu', type=int, default=None, help='gpus num, default 32')
    parser.add_argument('-er', '--error_rate', type=str, default=None, help='error_rate, default 0')

    # Intra-Host Parameters:
    parser.add_argument('-gps', '--gpu_per_server', type=int, default=None, help='gpu_per_server, default 8')
    parser.add_argument('-gt', '--gpu_type', type=str, default=None, help='gpu_type, default H100')
    parser.add_argument('-nsps', '--nv_switch_per_server', type=int, default=None, help='nv_switch_per_server, default 1')
    parser.add_argument('-nvbw', '--nvlink_bw', type=str, default=None, help='nvlink_bw, default 2880Gbps')
    parser.add_argument('-nl', '--nv_latency', type=str, default=None, help='nv switch latency, default 0.000025ms')
    parser.add_argument('-l', '--latency', type=str, default=None, help='nic latency, default 0.0005ms')

    # Intra-Segment Parameters:
    parser.add_argument('-bw', '--bandwidth', type=str, default=None, help='nic to asw bandwidth, default 400Gbps')
    parser.add_argument('-asn', '--asw_switch_num', type=int, default=None, help='asw_switch_num, default 8')
    parser.add_argument('-npa', '--nics_per_aswitch', type=int, default=None, help='nnics per asw, default 64')

    # Intra-Pod Parameters:
    parser.add_argument('-psn', '--psw_switch_num', type=int, default=None, help='psw_switch_num, default 64')
    parser.add_argument('-apbw', '--ap_bandwidth', type=str, default=None, help='asw to psw bandwidth,default 400Gbps')
    parser.add_argument('-app', '--asw_per_psw', type=int, default=None, help='asw for psw')

    # 中文注释：新增光模块节点数量参数。
    parser.add_argument('-omsn', '--optical_module_num', type=int, default=None, help='optical module node num, default 0')

    args = parser.parse_args()

    default_parameters = []
    parameters = analysis_template(args, default_parameters)

    if not parameters['rail_optimized']:
        if parameters['dual_plane']:
            raise ValueError("Sorry, None Rail-Optimized Structure doesn't support Dual Plane")
        if parameters['dual_ToR']:
            No_Rail_Opti_DualToR(parameters)
        else:
            No_Rail_Opti_SingleToR(parameters)
    else:
        if parameters['dual_ToR']:
            if parameters['dual_plane']:
                Rail_Opti_DualToR_DualPlane(parameters)
            else:
                Rail_Opti_DualToR_SinglePlane(parameters)
        else:
            if parameters['dual_plane']:
                raise ValueError("Sorry, Rail-optimized Single-ToR Structure doesn't support Dual Plane")
            Rail_Opti_SingleToR(parameters)


def analysis_template(args, default_parameters):
    default_parameters = {
        'rail_optimized': True, 'dual_ToR': False, 'dual_plane': False, 'gpu': 32, 'error_rate': 0,
        'gpu_per_server': 8, 'gpu_type': 'H100', 'nv_switch_per_server': 1,
        'nvlink_bw': '2880Gbps', 'nv_latency': '0.000025ms', 'latency': '0.0005ms',
        'bandwidth': '400Gbps', 'asw_switch_num': 8, 'nics_per_aswitch': 64,
        'psw_switch_num': 64, 'ap_bandwidth': '400Gbps', 'asw_per_psw': 64,
        # 中文注释：默认不启用光模块。
        'optical_module_num': 0,
    }

    parameters = {}
    parameters['topology'] = args.topology
    parameters['rail_optimized'] = bool(args.ro)
    parameters['dual_ToR'] = bool(args.dt)
    parameters['dual_plane'] = bool(args.dp)

    if parameters['topology'] == 'Spectrum-X':
        default_parameters.update({'gpu': 4096})
        parameters.update({'rail_optimized': True, 'dual_ToR': False, 'dual_plane': False})
    elif parameters['topology'] == 'AlibabaHPN':
        default_parameters.update({
            'gpu': 15360,
            'bandwidth': '200Gbps',
            'asw_switch_num': 240,
            'nics_per_aswitch': 128,
            'psw_switch_num': 120,
            'asw_per_psw': 240,
        })
        parameters.update({'rail_optimized': True, 'dual_ToR': True, 'dual_plane': False})
        if args.dp:
            default_parameters.update({'asw_per_psw': 120})
            parameters.update({'rail_optimized': True, 'dual_ToR': True, 'dual_plane': True})
    elif parameters['topology'] == 'DCN+':
        default_parameters.update({'gpu': 512, 'asw_switch_num': 8, 'asw_per_psw': 8, 'psw_switch_num': 8})
        parameters.update({'rail_optimized': False, 'dual_ToR': False, 'dual_plane': False})
        if args.dt:
            default_parameters.update({'bandwidth': '200Gbps', 'nics_per_aswitch': 128})
            parameters.update({'rail_optimized': False, 'dual_ToR': True, 'dual_plane': False})

    parameter_keys = [
        'gpu', 'error_rate', 'gpu_per_server', 'gpu_type', 'nv_switch_per_server',
        'nvlink_bw', 'nv_latency', 'latency', 'bandwidth', 'asw_switch_num',
        'nics_per_aswitch', 'psw_switch_num', 'ap_bandwidth', 'asw_per_psw',
        # 中文注释：将光模块节点数量纳入参数解析。
        'optical_module_num',
    ]

    for key in parameter_keys:
        parameters[key] = getattr(args, key, None) if getattr(args, key, None) is not None else default_parameters[key]

    return parameters


if __name__ == '__main__':
    main()
