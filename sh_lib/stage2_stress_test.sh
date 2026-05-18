#!/usr/bin/env bash
# 上面这一行叫 shebang，意思是：用系统 PATH 里找到的 bash 执行这个脚本。

# 文件名：stage2_stress_test.sh
# 用途：阶段二慢客户端与并发压力回归测试。
# 运行方式：./sh_lib/stage2_stress_test.sh [host] [port]
# 示例：./sh_lib/stage2_stress_test.sh 127.0.0.1 8888
# 注意：这个脚本默认服务器已经启动，只负责发请求和校验结果。

# set -u 表示使用未定义变量时立刻报错，避免变量名写错后继续执行。
set -u

# HOST 表示目标服务器 IP；如果没有传入第一个参数，就默认使用 127.0.0.1。
HOST="${1:-127.0.0.1}"

# PORT 表示目标服务器端口；如果没有传入第二个参数，就默认使用 8888。
PORT="${2:-8888}"

# BASE_URL 是 curl 访问服务器时使用的 URL 前缀。
BASE_URL="http://${HOST}:${PORT}"

# ROOT_DIR 表示项目根目录；脚本在 sh_lib 下，所以脚本目录再往上一级就是项目根。
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# OUT_DIR 表示测试输出目录；默认放到 /tmp，避免污染项目目录。
OUT_DIR="${OUT_DIR:-/tmp/cppserver_stage2_stress_$(date +%Y%m%d%H%M%S)}"

# FILE_PATH 表示用于静态文件测试的 URL 路径；默认优先使用较大的 g1.png。
FILE_PATH="${FILE_PATH:-/pic/g1.png}"

# SOURCE_FILE 表示 FILE_PATH 对应的本地源文件，用来做 SHA256 完整性比对。
SOURCE_FILE="${ROOT_DIR}/src${FILE_PATH}"

# 如果 g1.png 不存在，就降级使用 a1.png，保证不同机器上更容易跑通。
if [[ ! -f "${SOURCE_FILE}" ]]; then
  # 切换静态文件测试路径。
  FILE_PATH="/pic/a1.png"
  # 重新计算本地源文件路径。
  SOURCE_FILE="${ROOT_DIR}/src${FILE_PATH}"
# fi 表示 if 判断结束。
fi

# PING_N 表示并发 /ping 请求数量；可以通过环境变量覆盖。
PING_N="${PING_N:-120}"

# ECHO_N 表示并发 /echo 请求数量；可以通过环境变量覆盖。
ECHO_N="${ECHO_N:-60}"

# NOTFOUND_N 表示并发 404 请求数量；可以通过环境变量覆盖。
NOTFOUND_N="${NOTFOUND_N:-30}"

# FILE_N 表示并发静态文件下载数量；可以通过环境变量覆盖。
FILE_N="${FILE_N:-8}"

# PATH_N 表示并发路径越界请求数量；可以通过环境变量覆盖。
PATH_N="${PATH_N:-20}"

# BAD_N 表示并发非法请求数量；可以通过环境变量覆盖。
BAD_N="${BAD_N:-20}"

# KEEPALIVE_N 表示同连接多请求测试数量；可以通过环境变量覆盖。
KEEPALIVE_N="${KEEPALIVE_N:-10}"

# CLOSE_N 表示 Connection: close 测试数量；可以通过环境变量覆盖。
CLOSE_N="${CLOSE_N:-10}"

# CHECK_CONNECTION_CLOSE 表示是否执行 Connection: close 语义测试；1 表示执行，0 表示跳过。
CHECK_CONNECTION_CLOSE="${CHECK_CONNECTION_CLOSE:-1}"

# SLOW_N 表示慢客户端数量；可以通过环境变量覆盖。
SLOW_N="${SLOW_N:-2}"

# RATE 表示慢客户端下载限速；100k 约等于每秒 100KB。
RATE="${RATE:-100k}"

# pass_count 记录通过的批次数量。
pass_count=0

# fail_count 记录失败的请求数量。
fail_count=0

# mkdir -p 创建输出目录和各类子目录；目录已存在时不报错。
mkdir -p "${OUT_DIR}"/{ping,echo,notfound,file,path,bad,keepalive,slow}

# 打印测试标题。
echo "CPPServer stage2 stress test"

# 打印目标服务地址。
echo "target=${BASE_URL}"

# 打印输出目录。
echo "out_dir=${OUT_DIR}"

# 打印静态文件路径。
echo "file=${FILE_PATH}"

# 打印慢客户端限速。
echo "slow_rate=${RATE}"

# 打印空行，让输出更好读。
echo

# for 循环检查脚本依赖的外部命令。
for cmd in curl sha256sum timeout grep awk wc cat basename seq; do
  # command -v 用来检查命令是否存在；>/dev/null 表示丢掉正常输出。
  if ! command -v "${cmd}" >/dev/null 2>&1; then
    # >&2 表示把错误输出到标准错误。
    echo "[FATAL] missing command: ${cmd}" >&2
    # exit 2 表示测试环境不满足要求。
    exit 2
  # fi 表示 if 判断结束。
  fi
# done 表示 for 循环结束。
done

# 如果本地源文件不存在，就没法校验静态文件完整性。
if [[ ! -f "${SOURCE_FILE}" ]]; then
  # 打印明确错误。
  echo "[FATAL] source file not found: ${SOURCE_FILE}" >&2
  # exit 2 表示测试环境不满足要求。
  exit 2
# fi 表示 if 判断结束。
fi

# SOURCE_HASH 保存本地源文件 SHA256 第一列。
SOURCE_HASH="$(sha256sum "${SOURCE_FILE}" | awk '{print $1}')"

# 打印源文件哈希，方便失败时对照。
echo "file_sha256=${SOURCE_HASH}"

# 打印空行。
echo

# record_result 用于记录一个批次的测试结果。
record_result() {
  # name 是批次名称。
  local name="$1"
  # failed 是这个批次中失败的请求数量。
  local failed="$2"
  # 如果 failed 等于 0，说明这一批全通过。
  if [[ "${failed}" -eq 0 ]]; then
    # 打印 PASS。
    echo "[PASS] ${name}"
    # 通过批次数量加一。
    pass_count=$((pass_count + 1))
  # else 表示 failed 不等于 0。
  else
    # 打印 FAIL 和失败数量。
    echo "[FAIL] ${name}: ${failed} failed"
    # 总失败请求数累加。
    fail_count=$((fail_count + failed))
  # fi 表示 if 判断结束。
  fi
# } 表示函数定义结束。
}

# run_batch 用来并发运行同一种测试函数。
run_batch() {
  # name 是批次名称。
  local name="$1"
  # count 是要并发运行多少个请求。
  local count="$2"
  # func 是真正执行单个请求的函数名。
  local func="$3"
  # pids 数组保存后台任务进程号。
  local pids=()
  # failed 保存当前批次失败数量。
  local failed=0

  # 打印当前批次信息。
  echo "[RUN ] ${name}, count=${count}"

  # seq 生成从 1 到 count 的序号。
  for i in $(seq 1 "${count}"); do
    # 在后台运行测试函数；& 表示后台执行。
    "${func}" "${i}" &
    # $! 表示上一个后台进程的 PID。
    pids+=("$!")
  # done 表示 for 循环结束。
  done

  # 遍历所有后台任务 PID。
  for pid in "${pids[@]}"; do
    # wait 等待某个后台任务结束；如果它失败，wait 也返回失败。
    if ! wait "${pid}"; then
      # 当前批次失败数量加一。
      failed=$((failed + 1))
    # fi 表示 if 判断结束。
    fi
  # done 表示 for 循环结束。
  done

  # 记录当前批次结果。
  record_result "${name}" "${failed}"
# } 表示函数定义结束。
}

# test_ping 测试单个 GET /ping 请求。
test_ping() {
  # i 是当前请求序号。
  local i="$1"
  # body 保存响应体。
  local body="${OUT_DIR}/ping/${i}.body"
  # err 保存 curl 错误输出。
  local err="${OUT_DIR}/ping/${i}.err"
  # code 保存 HTTP 状态码。
  local code=""
  # curl 发送请求，-o 保存响应体，-w 输出状态码。
  code="$(curl --noproxy '*' -sS -o "${body}" -w '%{http_code}' --max-time 5 "${BASE_URL}/ping" 2>"${err}")" || return 1
  # 校验状态码。
  [[ "${code}" == "200" ]] || return 1
  # 校验响应体。
  [[ "$(cat "${body}")" == "pong" ]] || return 1
# } 表示函数定义结束。
}

# test_echo 测试单个 POST /echo 请求。
test_echo() {
  # i 是当前请求序号。
  local i="$1"
  # body 保存响应体。
  local body="${OUT_DIR}/echo/${i}.body"
  # err 保存 curl 错误输出。
  local err="${OUT_DIR}/echo/${i}.err"
  # payload 是当前请求体，每个请求带不同编号，避免误判固定响应。
  local payload="stage2-echo-${i}"
  # code 保存 HTTP 状态码。
  local code=""
  # curl 发送 POST 请求。
  code="$(curl --noproxy '*' -sS -o "${body}" -w '%{http_code}' --max-time 5 -X POST --data "${payload}" "${BASE_URL}/echo" 2>"${err}")" || return 1
  # 校验状态码。
  [[ "${code}" == "200" ]] || return 1
  # 校验响应体必须等于请求体。
  [[ "$(cat "${body}")" == "${payload}" ]] || return 1
# } 表示函数定义结束。
}

# test_notfound 测试单个 404 请求。
test_notfound() {
  # i 是当前请求序号。
  local i="$1"
  # body 保存响应体。
  local body="${OUT_DIR}/notfound/${i}.body"
  # err 保存 curl 错误输出。
  local err="${OUT_DIR}/notfound/${i}.err"
  # code 保存 HTTP 状态码。
  local code=""
  # 请求一个不存在的路径。
  code="$(curl --noproxy '*' -sS -o "${body}" -w '%{http_code}' --max-time 5 "${BASE_URL}/not-found-stage2-${i}" 2>"${err}")" || return 1
  # 校验状态码。
  [[ "${code}" == "404" ]] || return 1
  # 校验响应体。
  [[ "$(cat "${body}")" == "Not Found" ]] || return 1
# } 表示函数定义结束。
}

# test_file 测试单个静态文件下载。
test_file() {
  # i 是当前请求序号。
  local i="$1"
  # out 保存下载文件。
  local out="${OUT_DIR}/file/${i}.bin"
  # err 保存 curl 错误输出。
  local err="${OUT_DIR}/file/${i}.err"
  # code 保存 HTTP 状态码。
  local code=""
  # curl 下载静态文件。
  code="$(curl --noproxy '*' -sS -o "${out}" -w '%{http_code}' --max-time 30 "${BASE_URL}${FILE_PATH}" 2>"${err}")" || return 1
  # 校验状态码。
  [[ "${code}" == "200" ]] || return 1
  # 校验下载文件哈希。
  [[ "$(sha256sum "${out}" | awk '{print $1}')" == "${SOURCE_HASH}" ]] || return 1
# } 表示函数定义结束。
}

# test_path_escape 测试路径越界请求。
test_path_escape() {
  # i 是当前请求序号。
  local i="$1"
  # body 保存响应体。
  local body="${OUT_DIR}/path/${i}.body"
  # err 保存 curl 错误输出。
  local err="${OUT_DIR}/path/${i}.err"
  # code 保存 HTTP 状态码。
  local code=""
  # --path-as-is 要求 curl 不要替客户端清理 ../。
  code="$(curl --path-as-is --noproxy '*' -sS -o "${body}" -w '%{http_code}' --max-time 5 "${BASE_URL}/../temp/a23.png" 2>"${err}")" || return 1
  # 校验状态码。
  [[ "${code}" == "400" ]] || return 1
  # 校验响应体。
  [[ "$(cat "${body}")" == "Bad Request" ]] || return 1
# } 表示函数定义结束。
}

# test_bad_request 测试非法 Content-Length。
test_bad_request() {
  # i 是当前请求序号。
  local i="$1"
  # resp 保存完整响应报文。
  local resp="${OUT_DIR}/bad/${i}.resp"
  # err 保存错误输出。
  local err="${OUT_DIR}/bad/${i}.err"
  # timeout 防止服务器没有关闭连接时脚本卡住。
  timeout 5 bash -c "exec 3<>/dev/tcp/${HOST}/${PORT}; printf 'GET /ping HTTP/1.1\r\nHost: localhost\r\nContent-Length: abc\r\n\r\n' >&3; cat <&3" >"${resp}" 2>"${err}" || return 1
  # 校验 400 状态行。
  grep -q "400 Bad Request" "${resp}" || return 1
  # 校验连接关闭语义。
  grep -qi "connection: close" "${resp}" || return 1
# } 表示函数定义结束。
}

# test_keep_alive_pipeline 测试同一 TCP 连接里连续发送两个 keep-alive 请求。
test_keep_alive_pipeline() {
  # i 是当前请求序号。
  local i="$1"
  # resp 保存完整响应报文。
  local resp="${OUT_DIR}/keepalive/${i}.resp"
  # err 保存错误输出。
  local err="${OUT_DIR}/keepalive/${i}.err"
  # status_count 保存 200 状态行出现次数。
  local status_count=""
  # body_count 保存 pong 出现次数。
  local body_count=""
  # status 保存 timeout/raw tcp 命令的退出码。
  local status=""
  # 两个请求都不带 Connection: close，所以服务端保持连接是允许的。
  timeout 5 bash -c "exec 3<>/dev/tcp/${HOST}/${PORT}; printf 'GET /ping HTTP/1.1\r\nHost: localhost\r\n\r\nGET /ping HTTP/1.1\r\nHost: localhost\r\n\r\n' >&3; cat <&3" >"${resp}" 2>"${err}"
  # $? 表示上一条命令退出码；timeout 触发时通常是 124。
  status="$?"
  # 退出码 0 表示服务端主动关了连接；退出码 124 表示连接保持但 timeout 截断读取，两者都可以继续校验响应内容。
  if [[ "${status}" -ne 0 && "${status}" -ne 124 ]]; then
    # 其它退出码代表 raw tcp 请求异常。
    return 1
  # fi 表示 if 判断结束。
  fi
  # grep -o 每匹配一次就输出一行；wc -l 统计匹配次数。
  status_count="$(grep -a -o "HTTP/1.1 200 OK" "${resp}" | wc -l | awk '{print $1}')"
  # 统计 pong 出现次数。
  body_count="$(grep -a -o "pong" "${resp}" | wc -l | awk '{print $1}')"
  # 同一连接两个请求应该得到两个 200。
  [[ "${status_count}" == "2" ]] || return 1
  # 同一连接两个请求应该得到两个 pong。
  [[ "${body_count}" == "2" ]] || return 1
# } 表示函数定义结束。
}

# test_connection_close 测试 Connection: close 语义。
test_connection_close() {
  # i 是当前请求序号。
  local i="$1"
  # resp 保存完整响应报文。
  local resp="${OUT_DIR}/keepalive/close_${i}.resp"
  # err 保存错误输出。
  local err="${OUT_DIR}/keepalive/close_${i}.err"
  # status 保存 timeout/raw tcp 命令退出码。
  local status=""
  # 发送一个带 Connection: close 的请求，期望服务端写完响应后主动关闭连接。
  timeout 5 bash -c "exec 3<>/dev/tcp/${HOST}/${PORT}; printf 'GET /ping HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n' >&3; cat <&3" >"${resp}" 2>"${err}"
  # 保存上一条命令的退出码。
  status="$?"
  # 如果服务端没有主动关闭连接，timeout 会返回 124，这里应当判失败。
  [[ "${status}" == "0" ]] || return 1
  # 校验状态行。
  grep -q "HTTP/1.1 200 OK" "${resp}" || return 1
  # 校验响应体。
  grep -q "pong" "${resp}" || return 1
  # 校验响应头中的关闭语义。
  grep -qi "connection: close" "${resp}" || return 1
# } 表示函数定义结束。
}

# test_slow_file 测试慢客户端下载静态文件。
test_slow_file() {
  # i 是当前请求序号。
  local i="$1"
  # out 保存下载文件。
  local out="${OUT_DIR}/slow/${i}.bin"
  # err 保存 curl 错误输出。
  local err="${OUT_DIR}/slow/${i}.err"
  # code 保存 HTTP 状态码。
  local code=""
  # --limit-rate 模拟慢客户端，验证 EPOLLOUT 续写和 partial write。
  code="$(curl --noproxy '*' -sS --limit-rate "${RATE}" -o "${out}" -w '%{http_code}' --max-time 180 "${BASE_URL}${FILE_PATH}" 2>"${err}")" || return 1
  # 校验状态码。
  [[ "${code}" == "200" ]] || return 1
  # 校验下载文件哈希。
  [[ "$(sha256sum "${out}" | awk '{print $1}')" == "${SOURCE_HASH}" ]] || return 1
# } 表示函数定义结束。
}

# 运行并发 /ping 测试。
run_batch "ping" "${PING_N}" test_ping

# 运行并发 /echo 测试。
run_batch "echo" "${ECHO_N}" test_echo

# 运行并发 404 测试。
run_batch "notfound" "${NOTFOUND_N}" test_notfound

# 运行并发静态文件测试。
run_batch "file" "${FILE_N}" test_file

# 运行并发路径越界测试。
run_batch "path_escape" "${PATH_N}" test_path_escape

# 运行并发非法请求测试。
run_batch "bad_request" "${BAD_N}" test_bad_request

# 运行同连接多请求测试。
run_batch "keep_alive_pipeline" "${KEEPALIVE_N}" test_keep_alive_pipeline

# 如果开启 Connection: close 检查，就运行关闭语义测试。
if [[ "${CHECK_CONNECTION_CLOSE}" != "0" ]]; then
  # 运行 Connection: close 测试。
  run_batch "connection_close" "${CLOSE_N}" test_connection_close
# fi 表示 if 判断结束。
fi

# 运行慢客户端测试。
run_batch "slow_file" "${SLOW_N}" test_slow_file

# 打印空行。
echo

# 打印通过批次数量。
echo "batch_pass=${pass_count}"

# 打印失败请求数量。
echo "request_fail=${fail_count}"

# 打印输出目录。
echo "out_dir=${OUT_DIR}"

# 如果失败请求数量不是 0，脚本整体失败。
if [[ "${fail_count}" -ne 0 ]]; then
  # exit 1 表示测试失败。
  exit 1
# fi 表示 if 判断结束。
fi

# exit 0 表示全部测试通过。
exit 0
