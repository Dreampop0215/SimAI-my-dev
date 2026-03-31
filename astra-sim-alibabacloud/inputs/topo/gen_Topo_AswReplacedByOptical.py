#!/usr/bin/env python3
"""
Generate a topology where ASW is replaced by optical switches.

Topology shape (SingleToR style):
GPU -> NVSwitch
GPU -> Optical
Optical -> PSW

Output topo header format:
node_num gpus_per_server nvswitch_num switch_num optical_module_num link_num gpu_type
"""

import argparse
import math
import warnings


def build_topo_header(
    nodes,
    gpu_per_server,
    nvswitch_num,
    switch_num,
    optical_module_num,
    links,
    gpu_type,
):
    return (
        f"{nodes} {gpu_per_server} {nvswitch_num} {switch_num} "
        f"{optical_module_num} {int(links)} {gpu_type}"
    )


def write_link(f, src, dst, bw, lat, err):
    f.write(f"{src} {dst} {bw} {lat} {err}\n")


def generate_single_tor_asw_replaced(parameters):
    nodes_per_optical = parameters["nics_per_optical"]
    optical_num_per_segment = parameters["gpu_per_server"]
    capacity_per_segment = nodes_per_optical * optical_num_per_segment
    segment_num = int(math.ceil(parameters["gpu"] / capacity_per_segment))

    expected_optical = segment_num * optical_num_per_segment
    if parameters["optical_module_num"] != expected_optical:
        warnings.warn(
            "Mismatch between GPU count and optical_module_num. "
            f"Use {expected_optical} optical nodes for SingleToR mapping."
        )
        parameters["optical_module_num"] = expected_optical

    if segment_num > int(parameters["optical_per_psw"] / optical_num_per_segment):
        raise ValueError("GPU count exceeds one-pod capacity for this template.")

    nv_switch_num = (
        int(parameters["gpu"] / parameters["gpu_per_server"])
        * parameters["nv_switch_per_server"]
    )
    optical_module_num = parameters["optical_module_num"]

    # Only PSW stays as type-1 switch; ASW is replaced by optical (type-3).
    switch_num = parameters["psw_switch_num"]
    nodes = parameters["gpu"] + nv_switch_num + switch_num + optical_module_num
    switch_nodes = nv_switch_num + switch_num + optical_module_num

    links = (
        parameters["gpu"] * parameters["nv_switch_per_server"]  # GPU-NV
        + parameters["gpu"]  # GPU-Optical
        + optical_module_num * parameters["psw_switch_num"]  # Optical-PSW
    )

    file_name = (
        f"{parameters['topology']}_{parameters['gpu']}g_{parameters['gpu_per_server']}gps_"
        f"{parameters['bandwidth']}_{parameters['gpu_type']}_asw_replaced_by_optical"
    )

    with open(file_name, "w", encoding="utf-8") as f:
        first_line = build_topo_header(
            nodes=nodes,
            gpu_per_server=parameters["gpu_per_server"],
            nvswitch_num=nv_switch_num,
            switch_num=switch_num,
            optical_module_num=optical_module_num,
            links=links,
            gpu_type=parameters["gpu_type"],
        )
        f.write(first_line + "\n")

        nv_switch_nodes = []
        psw_nodes = []
        optical_nodes = []
        sec_line = ""
        nnodes = nodes - switch_nodes
        for i in range(nnodes, nodes):
            sec_line = sec_line + str(i) + " "
            if len(nv_switch_nodes) < nv_switch_num:
                nv_switch_nodes.append(i)
            elif len(psw_nodes) < parameters["psw_switch_num"]:
                psw_nodes.append(i)
            elif len(optical_nodes) < optical_module_num:
                optical_nodes.append(i)
        f.write(sec_line + "\n")

        ind_opt = 0
        curr_node = 0
        group_num = 0
        group_account = 0
        ind_nv = 0
        for i in range(parameters["gpu"]):
            curr_node += 1
            if curr_node > parameters["gpu_per_server"]:
                curr_node = 1
                ind_nv += parameters["nv_switch_per_server"]

            # GPU -> NVSwitch links
            for j in range(parameters["nv_switch_per_server"]):
                write_link(
                    f,
                    i,
                    nv_switch_nodes[ind_nv + j],
                    parameters["nvlink_bw"],
                    parameters["nv_latency"],
                    parameters["error_rate"],
                )

            # GPU -> Optical (replacing GPU -> ASW)
            write_link(
                f,
                i,
                optical_nodes[group_num * optical_num_per_segment + ind_opt],
                parameters["bandwidth"],
                parameters["latency"],
                parameters["error_rate"],
            )

            ind_opt += 1
            group_account += 1
            if ind_opt == optical_num_per_segment:
                ind_opt = 0
            if group_account == (
                parameters["gpu_per_server"] * parameters["nics_per_optical"]
            ):
                group_num += 1
                group_account = 0

        # Optical -> PSW all-to-all
        for opt in optical_nodes:
            for psw in psw_nodes:
                write_link(
                    f,
                    opt,
                    psw,
                    parameters["ap_bandwidth"],
                    parameters["latency"],
                    parameters["error_rate"],
                )

    print(file_name)


def parse_args():
    parser = argparse.ArgumentParser(
        description="Generate topology with ASW replaced by optical switches."
    )
    parser.add_argument("-topo", "--topology", type=str, default="Spectrum-X")
    parser.add_argument("-g", "--gpu", type=int, default=16)
    parser.add_argument("-gps", "--gpu_per_server", type=int, default=8)
    parser.add_argument("-gt", "--gpu_type", type=str, default="H100")
    parser.add_argument("-er", "--error_rate", type=str, default="0")

    parser.add_argument("-nsps", "--nv_switch_per_server", type=int, default=1)
    parser.add_argument("-nvbw", "--nvlink_bw", type=str, default="2880Gbps")
    parser.add_argument("-nl", "--nv_latency", type=str, default="0.000025ms")

    parser.add_argument("-bw", "--bandwidth", type=str, default="400Gbps")
    parser.add_argument("-l", "--latency", type=str, default="0.0005ms")

    parser.add_argument("-psn", "--psw_switch_num", type=int, default=64)
    parser.add_argument("-apbw", "--ap_bandwidth", type=str, default="400Gbps")

    # Reuse ASW-related capacity meaning: how many GPUs per optical switch.
    parser.add_argument("-npo", "--nics_per_optical", type=int, default=64)
    parser.add_argument("-opp", "--optical_per_psw", type=int, default=64)
    parser.add_argument("-omsn", "--optical_module_num", type=int, default=None)

    return parser.parse_args()


def main():
    args = parse_args()
    parameters = {
        "topology": args.topology,
        "gpu": args.gpu,
        "gpu_per_server": args.gpu_per_server,
        "gpu_type": args.gpu_type,
        "error_rate": args.error_rate,
        "nv_switch_per_server": args.nv_switch_per_server,
        "nvlink_bw": args.nvlink_bw,
        "nv_latency": args.nv_latency,
        "bandwidth": args.bandwidth,
        "latency": args.latency,
        "psw_switch_num": args.psw_switch_num,
        "ap_bandwidth": args.ap_bandwidth,
        "nics_per_optical": args.nics_per_optical,
        "optical_per_psw": args.optical_per_psw,
    }

    if args.optical_module_num is None:
        # Default replacement count mirrors ASW count for SingleToR mapping.
        parameters["optical_module_num"] = args.gpu_per_server * int(
            math.ceil(args.gpu / (args.gpu_per_server * args.nics_per_optical))
        )
    else:
        parameters["optical_module_num"] = args.optical_module_num

    generate_single_tor_asw_replaced(parameters)


if __name__ == "__main__":
    main()
