#!/usr/bin/env bash
# 上面这一行叫 shebang，意思是：用系统 PATH 里找到的 bash 执行这个脚本。

# 文件名：stage2_sanitizer_regression.sh
# 用途：自动编译 ASAN / TSAN 版本，启动 WebServer，运行阶段二基础与压力回归。
# 运行方式：./sh_lib/stage2_sanitizer_regression.sh [host]
# 示例：./sh_lib/stage2_sanitizer_regression.sh 127.0.0.1
# 注意：这个脚本会使用 /tmp 下的独立构建目录，不会清理你的普通 build 目录。

# set -u 表示使用未定义变量时立刻报错，避免变量名写错后继续执行。
set -u

# HOST 表示服务器监听 IP；如果没有传入第一个参数，就默认使用 127.0.0.1。
HOST="${1:-127.0.0.1}"

# ASAN_PORT 表示 ASAN 版本 WebServer 使用的端口；可以通过环境变量覆盖。
ASAN_PORT="${ASAN_PORT:-8896}"

# TSAN_PORT 表示 TSAN 版本 WebServer 使用的端口；可以通过环境变量覆盖。
TSAN_PORT="${TSAN_PORT:-8897}"

# ROOT_DIR 表示项目根目录；脚本在 sh_lib 下，所以脚本目录再往上一级就是项目根。
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# ASAN_BUILD_DIR 表示 ASAN 构建目录。
ASAN_BUILD_DIR="${ASAN_BUILD_DIR:-/tmp/cppserver-stage2-asan}"

# TSAN_BUILD_DIR 表示 TSAN 构建目录。
TSAN_BUILD_DIR="${TSAN_BUILD_DIR:-/tmp/cppserver-stage2-tsan}"

# OUT_DIR 表示测试输出目录。
OUT_DIR="${OUT_DIR:-/tmp/cppserver_stage2_sanitizer_$(date +%Y%m%d%H%M%S)}"

# SANITIZER_FILE_PATH 表示 sanitizer 回归使用的静态文件；默认用 a1.png 控制耗时。
SANITIZER_FILE_PATH="${SANITIZER_FILE_PATH:-/pic/a1.png}"

# ASAN_LOG_PREFIX 表示 ASAN 报告文件前缀；ASAN 有错误时会生成 prefix.pid 文件。
ASAN_LOG_PREFIX="${OUT_DIR}/asan_report"

# TSAN_LOG_PREFIX 表示 TSAN 报告文件前缀；TSAN 有错误时会生成 prefix.pid 文件。
TSAN_LOG_PREFIX="${OUT_DIR}/tsan_report"

# current_pid 保存当前正在运行的 WebServer 进程号，方便退出时清理。
current_pid=""

# pass_count 记录通过的大步骤数量。
pass_count=0

# fail_count 记录失败的大步骤数量。
fail_count=0

# mkdir -p 创建输出目录；目录已存在时不报错。
mkdir -p "${OUT_DIR}"

# 打印测试标题。
echo "CPPServer stage2 sanitizer regression"

# 打印项目根目录。
echo "root=${ROOT_DIR}"

# 打印测试输出目录。
echo "out_dir=${OUT_DIR}"

# 打印 ASAN 端口。
echo "asan_port=${ASAN_PORT}"

# 打印 TSAN 端口。
echo "tsan_port=${TSAN_PORT}"

# 打印 sanitizer 回归使用的静态文件。
echo "sanitizer_file=${SANITIZER_FILE_PATH}"

# 打印空行。
echo

# for 循环检查脚本依赖的外部命令。
for cmd in cmake setarch uname curl grep awk pgrep seq tail sleep; do
  # command -v 用来检查命令是否存在。
  if ! command -v "${cmd}" >/dev/null 2>&1; then
    # >&2 表示把错误输出到标准错误。
    echo "[FATAL] missing command: ${cmd}" >&2
    # exit 2 表示测试环境不满足要求。
    exit 2
  # fi 表示 if 判断结束。
  fi
# done 表示 for 循环结束。
done

# 如果基础 HTTP 测试脚本不存在，就无法继续。
if [[ ! -x "${ROOT_DIR}/sh_lib/stage2_basic_http_test.sh" ]]; then
  # 打印明确错误。
  echo "[FATAL] missing executable script: sh_lib/stage2_basic_http_test.sh" >&2
  # exit 2 表示测试环境不满足要求。
  exit 2
# fi 表示 if 判断结束。
fi

# 如果压力测试脚本不存在，就无法继续。
if [[ ! -x "${ROOT_DIR}/sh_lib/stage2_stress_test.sh" ]]; then
  # 打印明确错误。
  echo "[FATAL] missing executable script: sh_lib/stage2_stress_test.sh" >&2
  # exit 2 表示测试环境不满足要求。
  exit 2
# fi 表示 if 判断结束。
fi

# record_pass 用来记录一个大步骤通过。
record_pass() {
  # name 是步骤名称。
  local name="$1"
  # 打印 PASS。
  echo "[PASS] ${name}"
  # 通过数量加一。
  pass_count=$((pass_count + 1))
# } 表示函数定义结束。
}

# record_fail 用来记录一个大步骤失败。
record_fail() {
  # name 是步骤名称。
  local name="$1"
  # reason 是失败原因。
  local reason="$2"
  # 打印 FAIL。
  echo "[FAIL] ${name}: ${reason}"
  # 失败数量加一。
  fail_count=$((fail_count + 1))
# } 表示函数定义结束。
}

# stop_server 用来停止当前脚本启动的 WebServer。
stop_server() {
  # 如果 current_pid 为空，说明当前没有需要停止的服务。
  if [[ -z "${current_pid}" ]]; then
    # 直接返回成功。
    return 0
  # fi 表示 if 判断结束。
  fi
  # kill -0 只检查进程是否存在，不真正发送终止信号。
  if kill -0 "${current_pid}" >/dev/null 2>&1; then
    # kill 发送默认 TERM 信号，让服务正常退出。
    kill "${current_pid}" >/dev/null 2>&1
    # wait 等待进程退出；服务被 kill 后 wait 返回非 0 也不影响清理。
    wait "${current_pid}" >/dev/null 2>&1 || true
  # fi 表示 if 判断结束。
  fi
  # 清空 current_pid。
  current_pid=""
# } 表示函数定义结束。
}

# trap 表示脚本退出时自动执行 stop_server，避免残留 WebServer 进程。
trap stop_server EXIT

# configure_and_build 用来配置并编译某个 sanitizer 版本。
configure_and_build() {
  # name 是模式名称，例如 ASAN 或 TSAN。
  local name="$1"
  # build_dir 是构建目录。
  local build_dir="$2"
  # cmake_flag 是 CMake sanitizer 开关。
  local cmake_flag="$3"
  # log 文件保存构建输出。
  local log="${OUT_DIR}/${name,,}_build.log"

  # 打印当前构建步骤。
  echo "[RUN ] build ${name}: ${build_dir}"
  # cmake -S 指定源码目录，-B 指定构建目录，cmake_flag 开启对应 sanitizer。
  if ! cmake -S "${ROOT_DIR}" -B "${build_dir}" "${cmake_flag}" >"${log}" 2>&1; then
    # 配置失败时记录失败。
    record_fail "build ${name}" "cmake configure failed, see ${log}"
    # return 1 表示失败。
    return 1
  # fi 表示 if 判断结束。
  fi
  # cmake --build 执行真正编译。
  if ! cmake --build "${build_dir}" -j >>"${log}" 2>&1; then
    # 编译失败时记录失败。
    record_fail "build ${name}" "cmake build failed, see ${log}"
    # return 1 表示失败。
    return 1
  # fi 表示 if 判断结束。
  fi
  # 构建成功时记录通过。
  record_pass "build ${name}"
  # return 0 表示成功。
  return 0
# } 表示函数定义结束。
}

# start_server 用来启动某个 sanitizer 版本的 WebServer。
start_server() {
  # name 是模式名称，例如 ASAN 或 TSAN。
  local name="$1"
  # build_dir 是构建目录。
  local build_dir="$2"
  # port 是监听端口。
  local port="$3"
  # log_prefix 是 sanitizer 报告文件前缀。
  local log_prefix="$4"
  # server_log 保存 WebServer 标准输出和标准错误。
  local server_log="${OUT_DIR}/${name,,}_server.log"
  # binary 是 WebServer 可执行文件路径。
  local binary="${build_dir}/server/WebServer"

  # 如果可执行文件不存在，说明构建失败或目录不对。
  if [[ ! -x "${binary}" ]]; then
    # 记录失败。
    record_fail "start ${name}" "binary not found: ${binary}"
    # return 1 表示失败。
    return 1
  # fi 表示 if 判断结束。
  fi

  # 打印启动信息。
  echo "[RUN ] start ${name}: ${HOST}:${port}"

  # 如果是 ASAN 模式，就设置 ASAN_OPTIONS 后启动。
  if [[ "${name}" == "ASAN" ]]; then
    # ASAN_OPTIONS 的 log_path 表示错误报告写到哪里；halt_on_error=0 表示发现错误后尽量继续。
    ASAN_OPTIONS="log_path=${log_prefix}:detect_leaks=1:halt_on_error=0" \
      setarch "$(uname -m)" -R "${binary}" "${HOST}" "${port}" >"${server_log}" 2>&1 &
  # else 表示 TSAN 模式。
  else
    # TSAN_OPTIONS 的 report_signal_unsafe=0 用来减少信号相关噪声。
    TSAN_OPTIONS="log_path=${log_prefix}:halt_on_error=0:report_signal_unsafe=0" \
      setarch "$(uname -m)" -R "${binary}" "${HOST}" "${port}" >"${server_log}" 2>&1 &
  # fi 表示 if 判断结束。
  fi

  # $! 是刚启动的后台 WebServer 进程号。
  current_pid="$!"
  # 等待服务启动并能响应 /ping。
  if ! wait_server_ready "${port}" "${server_log}"; then
    # 如果服务没起来，记录失败。
    record_fail "start ${name}" "server not ready, see ${server_log}"
    # 停掉可能残留的服务。
    stop_server
    # return 1 表示失败。
    return 1
  # fi 表示 if 判断结束。
  fi
  # 服务启动成功。
  record_pass "start ${name}"
  # return 0 表示成功。
  return 0
# } 表示函数定义结束。
}

# wait_server_ready 用 curl 轮询 /ping，直到服务可用或超时。
wait_server_ready() {
  # port 是要检查的端口。
  local port="$1"
  # server_log 是服务日志路径，用于失败提示。
  local server_log="$2"
  # i 是重试序号。
  local i=""
  # 最多尝试 40 次，每次间隔 0.25 秒，总共约 10 秒。
  for i in $(seq 1 40); do
    # kill -0 检查进程是否已经提前退出。
    if ! kill -0 "${current_pid}" >/dev/null 2>&1; then
      # 进程已退出，直接失败。
      return 1
    # fi 表示 if 判断结束。
    fi
    # curl 请求 /ping；成功返回 pong 就说明服务已经可用。
    if [[ "$(curl --noproxy '*' -sS --max-time 1 "http://${HOST}:${port}/ping" 2>/dev/null)" == "pong" ]]; then
      # 服务可用，返回成功。
      return 0
    # fi 表示 if 判断结束。
    fi
    # sleep 0.25 表示等待 0.25 秒后再试。
    sleep 0.25
  # done 表示 for 循环结束。
  done
  # 打印一点日志尾部，方便定位服务为什么没启动。
  tail -n 20 "${server_log}" 2>/dev/null || true
  # 超过重试次数仍不可用，返回失败。
  return 1
# } 表示函数定义结束。
}

# run_http_regression 用来跑基础测试和压力测试。
run_http_regression() {
  # name 是模式名称，例如 ASAN 或 TSAN。
  local name="$1"
  # port 是目标端口。
  local port="$2"
  # basic_out 是基础测试输出目录。
  local basic_out="${OUT_DIR}/${name,,}_basic"
  # stress_out 是压力测试输出目录。
  local stress_out="${OUT_DIR}/${name,,}_stress"
  # basic_log 保存基础测试输出。
  local basic_log="${OUT_DIR}/${name,,}_basic.log"
  # stress_log 保存压力测试输出。
  local stress_log="${OUT_DIR}/${name,,}_stress.log"

  # 打印基础测试步骤。
  echo "[RUN ] ${name} basic HTTP regression"
  # OUT_DIR=... 只对后面的脚本命令生效，用来指定测试输出目录。
  if ! OUT_DIR="${basic_out}" "${ROOT_DIR}/sh_lib/stage2_basic_http_test.sh" "${HOST}" "${port}" >"${basic_log}" 2>&1; then
    # 基础测试失败时记录失败。
    record_fail "${name} basic HTTP regression" "failed, see ${basic_log}"
    # return 1 表示失败。
    return 1
  # fi 表示 if 判断结束。
  fi
  # 基础测试通过。
  record_pass "${name} basic HTTP regression"

  # 打印压力测试步骤。
  echo "[RUN ] ${name} stress regression"
  # sanitizer 模式下压力参数略小，避免 TSAN 太慢。
  if ! FILE_PATH="${SANITIZER_FILE_PATH}" PING_N=40 ECHO_N=20 NOTFOUND_N=10 FILE_N=3 PATH_N=8 BAD_N=8 KEEPALIVE_N=4 CLOSE_N=4 SLOW_N=1 RATE=160k OUT_DIR="${stress_out}" \
    "${ROOT_DIR}/sh_lib/stage2_stress_test.sh" "${HOST}" "${port}" >"${stress_log}" 2>&1; then
    # 压力测试失败时记录失败。
    record_fail "${name} stress regression" "failed, see ${stress_log}"
    # return 1 表示失败。
    return 1
  # fi 表示 if 判断结束。
  fi
  # 压力测试通过。
  record_pass "${name} stress regression"
  # return 0 表示成功。
  return 0
# } 表示函数定义结束。
}

# check_sanitizer_report 用来确认 sanitizer 没有生成错误报告。
check_sanitizer_report() {
  # name 是模式名称。
  local name="$1"
  # log_prefix 是 sanitizer 报告文件前缀。
  local log_prefix="$2"
  # pattern 是 sanitizer 实际报告文件匹配模式。
  local pattern="${log_prefix}.*"
  # compgen -G 用来判断是否存在匹配 pattern 的文件。
  if compgen -G "${pattern}" >/dev/null; then
    # 如果有报告文件，说明 sanitizer 发现了问题。
    record_fail "${name} sanitizer report" "report generated: ${pattern}"
    # return 1 表示失败。
    return 1
  # fi 表示 if 判断结束。
  fi
  # 没有报告文件，记录通过。
  record_pass "${name} sanitizer report"
  # return 0 表示成功。
  return 0
# } 表示函数定义结束。
}

# run_one_sanitizer 串起单个 sanitizer 的构建、启动、测试、检查报告。
run_one_sanitizer() {
  # name 是模式名称。
  local name="$1"
  # build_dir 是构建目录。
  local build_dir="$2"
  # cmake_flag 是 CMake sanitizer 开关。
  local cmake_flag="$3"
  # port 是监听端口。
  local port="$4"
  # log_prefix 是 sanitizer 报告文件前缀。
  local log_prefix="$5"

  # 打印分隔线。
  echo
  # 打印当前 sanitizer 名称。
  echo "===== ${name} ====="

  # 构建失败则直接结束本 sanitizer。
  configure_and_build "${name}" "${build_dir}" "${cmake_flag}" || return 1
  # 启动失败则直接结束本 sanitizer。
  start_server "${name}" "${build_dir}" "${port}" "${log_prefix}" || return 1
  # 跑基础和压力回归。
  run_http_regression "${name}" "${port}"
  # 保存测试返回值。
  local test_status="$?"
  # 停止当前 sanitizer 服务。
  stop_server
  # 检查 sanitizer 报告。
  check_sanitizer_report "${name}" "${log_prefix}"
  # 保存报告检查返回值。
  local report_status="$?"
  # 如果测试或报告任意一个失败，则返回失败。
  if [[ "${test_status}" -ne 0 || "${report_status}" -ne 0 ]]; then
    # return 1 表示失败。
    return 1
  # fi 表示 if 判断结束。
  fi
  # return 0 表示成功。
  return 0
# } 表示函数定义结束。
}

# 运行 ASAN 回归。
run_one_sanitizer "ASAN" "${ASAN_BUILD_DIR}" "-DUSE_ASAN=ON" "${ASAN_PORT}" "${ASAN_LOG_PREFIX}" || true

# 运行 TSAN 回归。
run_one_sanitizer "TSAN" "${TSAN_BUILD_DIR}" "-DUSE_TSAN=ON" "${TSAN_PORT}" "${TSAN_LOG_PREFIX}" || true

# 打印空行。
echo

# 打印通过数量。
echo "pass_count=${pass_count}"

# 打印失败数量。
echo "fail_count=${fail_count}"

# 打印输出目录。
echo "out_dir=${OUT_DIR}"

# 打印当前是否还有 WebServer 残留，方便提交前检查。
pgrep -af WebServer || true

# 如果 fail_count 不等于 0，说明至少一个步骤失败。
if [[ "${fail_count}" -ne 0 ]]; then
  # exit 1 表示测试失败。
  exit 1
# fi 表示 if 判断结束。
fi

# exit 0 表示全部测试通过。
exit 0
