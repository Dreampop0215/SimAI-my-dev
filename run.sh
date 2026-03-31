#!/usr/bin/env bash
set -euo pipefail

# 中文注释：统一入口脚本，避免每次手动输入长命令。
# 中文注释：日志与本次运行生成的 CSV 统一归档到 ./temp_result/TAG+TS/ 子目录。

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${ROOT_DIR}"

usage() {
  cat <<'EOF'
用法:
  ./run.sh -g <TAG> -w <workload> -n <topo> [可选参数]

必填参数:
  -g TAG            本次运行标签(例如 short200_ceopt)
  -w WORKLOAD       workload 文件路径
  -n TOPO           topo 文件路径

可选参数:
  -c CONF           配置文件路径(默认: ./astra-sim-alibabacloud/inputs/config/SimAI.local.conf)
  -b BIN            仿真二进制路径(默认: ./bin/SimAI_simulator_optical_2)
  -t THREADS        线程数(默认: 16)
  -m TIMEOUT        超时时间(默认: 100m)
  -s SEND_LAT       AS_SEND_LAT(默认: 3)
  -e NVLS_ENABLE    AS_NVLS_ENABLE(默认: 1)
  -h                显示帮助

示例:
  ./run.sh \
    -g short200_ceopt \
    -w ./aicb/results/workload/A100-gpt_7B-world_size16-tp8-pp1-ep1-gbs64-mbs2-seq2048-MOE-False-GEMM-False-flash_attn-False_short200_ceopt.txt \
    -n ./Spectrum-X_16g_8gps_400Gbps_H100_optical_2hop
EOF
}

TAG=""
WORKLOAD=""
TOPO=""
CONF="./astra-sim-alibabacloud/inputs/config/SimAI.local.conf"
BIN="./bin/SimAI_simulator_optical_2"
THREADS="16"
TIMEOUT_LIMIT="100m"
SEND_LAT="${AS_SEND_LAT:-3}"
NVLS_ENABLE="${AS_NVLS_ENABLE:-1}"

while getopts ":g:w:n:c:b:t:m:s:e:h" opt; do
  case "${opt}" in
    g) TAG="${OPTARG}" ;;
    w) WORKLOAD="${OPTARG}" ;;
    n) TOPO="${OPTARG}" ;;
    c) CONF="${OPTARG}" ;;
    b) BIN="${OPTARG}" ;;
    t) THREADS="${OPTARG}" ;;
    m) TIMEOUT_LIMIT="${OPTARG}" ;;
    s) SEND_LAT="${OPTARG}" ;;
    e) NVLS_ENABLE="${OPTARG}" ;;
    h)
      usage
      exit 0
      ;;
    :)
      echo "错误: 选项 -${OPTARG} 缺少参数" >&2
      usage
      exit 2
      ;;
    \?)
      echo "错误: 不支持的选项 -${OPTARG}" >&2
      usage
      exit 2
      ;;
  esac
done

if [[ -z "${TAG}" || -z "${WORKLOAD}" || -z "${TOPO}" ]]; then
  echo "错误: -g/-w/-n 为必填参数" >&2
  usage
  exit 2
fi

if [[ ! -x "${BIN}" ]]; then
  echo "错误: 二进制不存在或不可执行: ${BIN}" >&2
  exit 2
fi
if [[ ! -f "${WORKLOAD}" ]]; then
  echo "错误: workload 文件不存在: ${WORKLOAD}" >&2
  exit 2
fi
if [[ ! -f "${TOPO}" ]]; then
  echo "错误: topo 文件不存在: ${TOPO}" >&2
  exit 2
fi
if [[ ! -f "${CONF}" ]]; then
  echo "错误: 配置文件不存在: ${CONF}" >&2
  exit 2
fi

TS="$(date +%F_%H-%M-%S)"
RUN_ID="${TAG}+${TS}"
OUT_DIR="./temp_result/${RUN_ID}"
mkdir -p "${OUT_DIR}"

# 中文注释：日志命名规则保持不变: run_TAG_TS.log。
LOG_FILE="${OUT_DIR}/run_${TAG}_${TS}.log"

# 中文注释：用时间戳文件标记“本次运行开始时刻”，用于筛选本次生成/更新的 CSV。
STAMP_FILE="$(mktemp)"
cleanup() {
  rm -f "${STAMP_FILE}"
}
trap cleanup EXIT

echo "========== SimAI Run =========="
echo "TAG           : ${TAG}"
echo "WORKLOAD      : ${WORKLOAD}"
echo "TOPO          : ${TOPO}"
echo "CONF          : ${CONF}"
echo "BIN           : ${BIN}"
echo "THREADS       : ${THREADS}"
echo "TIMEOUT       : ${TIMEOUT_LIMIT}"
echo "AS_SEND_LAT   : ${SEND_LAT}"
echo "AS_NVLS_ENABLE: ${NVLS_ENABLE}"
echo "OUT_DIR       : ${OUT_DIR}"
echo "LOG_FILE      : ${LOG_FILE}"
echo "================================"

set +e
AS_SEND_LAT="${SEND_LAT}" AS_NVLS_ENABLE="${NVLS_ENABLE}" \
timeout --signal=INT --kill-after=30s "${TIMEOUT_LIMIT}" \
"${BIN}" \
  -t "${THREADS}" \
  -w "${WORKLOAD}" \
  -n "${TOPO}" \
  -c "${CONF}" \
  2>&1 | tee "${LOG_FILE}"
SIM_EXIT="${PIPESTATUS[0]}"
set -e

# 中文注释：收集本次运行后在项目根目录中新产生或被更新的 CSV 文件。
CSV_COUNT=0
while IFS= read -r csv; do
  cp -f "${csv}" "${OUT_DIR}/"
  CSV_COUNT=$((CSV_COUNT + 1))
done < <(find . -maxdepth 1 -type f -name "*.csv" -newer "${STAMP_FILE}" | sort)

echo "SIM_EXIT=${SIM_EXIT}" | tee -a "${LOG_FILE}"
echo "COPIED_CSV_COUNT=${CSV_COUNT}" | tee -a "${LOG_FILE}"
echo "RESULT_DIR=${OUT_DIR}" | tee -a "${LOG_FILE}"

if [[ "${SIM_EXIT}" -eq 0 ]]; then
  echo "运行完成: ${OUT_DIR}"
else
  echo "运行异常退出(退出码=${SIM_EXIT}): ${OUT_DIR}" >&2
fi

exit "${SIM_EXIT}"
