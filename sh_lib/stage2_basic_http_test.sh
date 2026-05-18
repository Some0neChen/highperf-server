#!/usr/bin/env bash
# 上面这一行叫 shebang，意思是：用系统 PATH 里找到的 bash 来执行这个脚本。

# 文件名：stage2_basic_http_test.sh
# 用途：阶段二基础 HTTP 行为回归测试。
# 运行方式：./sh_lib/stage2_basic_http_test.sh [host] [port]
# 示例：./sh_lib/stage2_basic_http_test.sh 127.0.0.1 8888
# 注意：这个脚本默认认为服务器已经启动，只负责发请求和校验结果。

# set -u 表示：使用未定义变量时立刻报错，避免变量名写错后脚本继续乱跑。
set -u

# HOST 表示目标服务器 IP；如果用户没传第一个参数，就默认使用 127.0.0.1。
HOST="${1:-127.0.0.1}"

# PORT 表示目标服务器端口；如果用户没传第二个参数，就默认使用 8888。
PORT="${2:-8888}"

# BASE_URL 是 curl 使用的 HTTP 地址前缀，例如 http://127.0.0.1:8888。
BASE_URL="http://${HOST}:${PORT}"

# BASH_SOURCE[0] 表示当前脚本路径；dirname 取出脚本所在目录；/.. 回到项目根目录。
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# date 生成当前时间戳；测试输出默认放到 /tmp，避免污染项目目录。
OUT_DIR="${OUT_DIR:-/tmp/cppserver_stage2_basic_$(date +%Y%m%d%H%M%S)}"

# FILE_PATH 表示要测试的静态资源路径；可以通过环境变量覆盖，例如 FILE_PATH=/pic/g1.png。
FILE_PATH="${FILE_PATH:-/pic/a1.png}"

# SOURCE_FILE 表示本地源文件路径，用来和服务器下载结果做 sha256 哈希比对。
SOURCE_FILE="${ROOT_DIR}/src${FILE_PATH}"

# pass_count 记录通过的用例数量。
pass_count=0

# fail_count 记录失败的用例数量。
fail_count=0

# mkdir -p 表示创建目录；如果目录已经存在也不报错。
mkdir -p "${OUT_DIR}"

# echo 打印测试标题，方便在终端里看清楚这次测的是谁。
echo "CPPServer stage2 basic HTTP test"

# echo 打印目标服务地址。
echo "target=${BASE_URL}"

# echo 打印测试产物目录，失败时可以进去看 body/err 文件。
echo "out_dir=${OUT_DIR}"

# echo 打印静态文件测试路径。
echo "file_path=${FILE_PATH}"

# echo 打印空行，让输出更好读。
echo

# for 循环逐个检查脚本依赖的命令是否存在。
for cmd in curl sha256sum awk grep timeout cat; do
  # command -v 会查找命令是否在 PATH 里；>/dev/null 表示丢掉正常输出。
  if ! command -v "${cmd}" >/dev/null 2>&1; then
    # >&2 表示把错误信息输出到标准错误。
    echo "[FATAL] missing command: ${cmd}" >&2
    # exit 2 表示脚本因为环境问题退出，不是服务器功能测试失败。
    exit 2
  # fi 表示 if 判断结束。
  fi
# done 表示 for 循环结束。
done

# 如果默认静态文件不存在，就尝试切换到另一个历史测试文件。
if [[ ! -f "${SOURCE_FILE}" ]]; then
  # 把静态资源路径切换为 /pic/g1.png。
  FILE_PATH="/pic/g1.png"
  # 根据新的 FILE_PATH 重新计算本地源文件路径。
  SOURCE_FILE="${ROOT_DIR}/src${FILE_PATH}"
# fi 表示 if 判断结束。
fi

# 如果两个候选静态文件都不存在，说明当前项目目录不适合跑静态文件测试。
if [[ ! -f "${SOURCE_FILE}" ]]; then
  # 打印明确的错误信息。
  echo "[FATAL] source file not found: ${SOURCE_FILE}" >&2
  # exit 2 表示环境不满足测试要求。
  exit 2
# fi 表示 if 判断结束。
fi

# sha256sum 计算文件哈希；awk '{print $1}' 只取第一列哈希值。
SOURCE_HASH="$(sha256sum "${SOURCE_FILE}" | awk '{print $1}')"

# record_pass 是一个函数，用来统一打印通过结果并累加通过数量。
record_pass() {
  # local 表示函数内局部变量；$1 表示调用函数时传入的第一个参数。
  local name="$1"
  # 打印通过的用例名。
  echo "[PASS] ${name}"
  # $((...)) 是 Bash 的整数计算语法。
  pass_count=$((pass_count + 1))
# } 表示函数定义结束。
}

# record_fail 是一个函数，用来统一打印失败结果并累加失败数量。
record_fail() {
  # name 表示失败用例名。
  local name="$1"
  # reason 表示失败原因。
  local reason="$2"
  # 打印失败用例名和失败原因。
  echo "[FAIL] ${name}: ${reason}"
  # 失败数量加一。
  fail_count=$((fail_count + 1))
# } 表示函数定义结束。
}

# assert_text_equal 用来比较两个字符串是否相等。
assert_text_equal() {
  # name 表示当前断言名称。
  local name="$1"
  # expected 表示期望值。
  local expected="$2"
  # actual 表示实际值。
  local actual="$3"
  # [[ ... ]] 是 Bash 的条件判断语法；!= 表示不相等。
  if [[ "${actual}" != "${expected}" ]]; then
    # 打印期望值和实际值，方便定位问题。
    record_fail "${name}" "expected '${expected}', got '${actual}'"
    # return 1 表示这个断言失败。
    return 1
  # fi 表示 if 判断结束。
  fi
  # return 0 表示这个断言通过。
  return 0
# } 表示函数定义结束。
}

# test_ping 测试 GET /ping 是否返回 200 和 pong。
test_ping() {
  # name 表示用例名称。
  local name="GET /ping"
  # body 文件保存响应体。
  local body="${OUT_DIR}/ping.body"
  # err 文件保存 curl 的错误输出。
  local err="${OUT_DIR}/ping.err"
  # code 保存 HTTP 状态码。
  local code=""
  # curl 发起请求；-sS 安静模式但保留错误；-o 保存 body；-w 输出状态码。
  if ! code="$(curl --noproxy '*' -sS -o "${body}" -w '%{http_code}' --max-time 5 "${BASE_URL}/ping" 2>"${err}")"; then
    # curl 命令本身失败时记录失败。
    record_fail "${name}" "curl failed, see ${err}"
    # return 结束当前测试函数。
    return
  # fi 表示 if 判断结束。
  fi
  # 校验 HTTP 状态码必须是 200。
  assert_text_equal "${name} status" "200" "${code}" || return
  # 校验响应体必须是 pong。
  assert_text_equal "${name} body" "pong" "$(cat "${body}")" || return
  # 所有断言通过后记录 PASS。
  record_pass "${name}"
# } 表示函数定义结束。
}

# test_echo 测试 POST /echo 是否原样返回请求 body。
test_echo() {
  # name 表示用例名称。
  local name="POST /echo"
  # body 文件保存响应体。
  local body="${OUT_DIR}/echo.body"
  # err 文件保存 curl 的错误输出。
  local err="${OUT_DIR}/echo.err"
  # payload 是本次 POST 发送的请求体。
  local payload="hello stage2"
  # code 保存 HTTP 状态码。
  local code=""
  # curl -X POST 指定方法；--data 发送请求体。
  if ! code="$(curl --noproxy '*' -sS -o "${body}" -w '%{http_code}' --max-time 5 -X POST --data "${payload}" "${BASE_URL}/echo" 2>"${err}")"; then
    # curl 命令本身失败时记录失败。
    record_fail "${name}" "curl failed, see ${err}"
    # return 结束当前测试函数。
    return
  # fi 表示 if 判断结束。
  fi
  # 校验 HTTP 状态码必须是 200。
  assert_text_equal "${name} status" "200" "${code}" || return
  # 校验响应体必须和请求体一致。
  assert_text_equal "${name} body" "${payload}" "$(cat "${body}")" || return
  # 所有断言通过后记录 PASS。
  record_pass "${name}"
# } 表示函数定义结束。
}

# test_not_found 测试不存在的路由是否返回 404。
test_not_found() {
  # name 表示用例名称。
  local name="GET 404"
  # body 文件保存响应体。
  local body="${OUT_DIR}/not_found.body"
  # err 文件保存 curl 的错误输出。
  local err="${OUT_DIR}/not_found.err"
  # code 保存 HTTP 状态码。
  local code=""
  # curl 请求一个大概率不存在的路径。
  if ! code="$(curl --noproxy '*' -sS -o "${body}" -w '%{http_code}' --max-time 5 "${BASE_URL}/not-found-stage2-test" 2>"${err}")"; then
    # curl 命令本身失败时记录失败。
    record_fail "${name}" "curl failed, see ${err}"
    # return 结束当前测试函数。
    return
  # fi 表示 if 判断结束。
  fi
  # 校验 HTTP 状态码必须是 404。
  assert_text_equal "${name} status" "404" "${code}" || return
  # 校验响应体必须是 Not Found。
  assert_text_equal "${name} body" "Not Found" "$(cat "${body}")" || return
  # 所有断言通过后记录 PASS。
  record_pass "${name}"
# } 表示函数定义结束。
}

# test_bad_content_length 测试非法 Content-Length 是否返回 400 并关闭连接。
test_bad_content_length() {
  # name 表示用例名称。
  local name="bad Content-Length"
  # resp 文件保存完整 HTTP 响应报文。
  local resp="${OUT_DIR}/bad_content_length.resp"
  # err 文件保存 bash/raw tcp 的错误输出。
  local err="${OUT_DIR}/bad_content_length.err"
  # timeout 限制最多等待 5 秒，避免服务器不回包时脚本卡住。
  if ! timeout 5 bash -c "exec 3<>/dev/tcp/${HOST}/${PORT}; printf 'GET /ping HTTP/1.1\r\nHost: localhost\r\nContent-Length: abc\r\n\r\n' >&3; cat <&3" >"${resp}" 2>"${err}"; then
    # raw TCP 请求失败时记录失败。
    record_fail "${name}" "raw request failed, see ${err}"
    # return 结束当前测试函数。
    return
  # fi 表示 if 判断结束。
  fi
  # grep -q 表示只判断文本是否存在，不打印匹配内容。
  if ! grep -q "400 Bad Request" "${resp}"; then
    # 如果响应里没有 400 状态行，就记录失败。
    record_fail "${name}" "missing 400 Bad Request"
    # return 结束当前测试函数。
    return
  # fi 表示 if 判断结束。
  fi
  # grep -qi 表示忽略大小写查找 connection: close。
  if ! grep -qi "connection: close" "${resp}"; then
    # 如果 400 响应没有要求关闭连接，就记录失败。
    record_fail "${name}" "missing connection: close"
    # return 结束当前测试函数。
    return
  # fi 表示 if 判断结束。
  fi
  # 所有断言通过后记录 PASS。
  record_pass "${name}"
# } 表示函数定义结束。
}

# test_path_escape 测试试图越过静态资源根目录的路径是否返回 400。
test_path_escape() {
  # name 表示用例名称。
  local name="path escape"
  # body 文件保存响应体。
  local body="${OUT_DIR}/path_escape.body"
  # err 文件保存 curl 的错误输出。
  local err="${OUT_DIR}/path_escape.err"
  # code 保存 HTTP 状态码。
  local code=""
  # --path-as-is 表示 curl 不要替我们清理 URL 里的 ..，要把原始路径发给服务器。
  if ! code="$(curl --path-as-is --noproxy '*' -sS -o "${body}" -w '%{http_code}' --max-time 5 "${BASE_URL}/../temp/a23.png" 2>"${err}")"; then
    # curl 命令本身失败时记录失败。
    record_fail "${name}" "curl failed, see ${err}"
    # return 结束当前测试函数。
    return
  # fi 表示 if 判断结束。
  fi
  # 校验 HTTP 状态码必须是 400。
  assert_text_equal "${name} status" "400" "${code}" || return
  # 校验响应体必须是 Bad Request。
  assert_text_equal "${name} body" "Bad Request" "$(cat "${body}")" || return
  # 所有断言通过后记录 PASS。
  record_pass "${name}"
# } 表示函数定义结束。
}

# test_static_file 测试普通静态文件 GET 是否完整返回。
test_static_file() {
  # name 表示用例名称。
  local name="static file"
  # out 文件保存服务器下载下来的静态文件。
  local out="${OUT_DIR}/static_file.bin"
  # err 文件保存 curl 的错误输出。
  local err="${OUT_DIR}/static_file.err"
  # code 保存 HTTP 状态码。
  local code=""
  # curl 下载静态文件到 out。
  if ! code="$(curl --noproxy '*' -sS -o "${out}" -w '%{http_code}' --max-time 30 "${BASE_URL}${FILE_PATH}" 2>"${err}")"; then
    # curl 命令本身失败时记录失败。
    record_fail "${name}" "curl failed, see ${err}"
    # return 结束当前测试函数。
    return
  # fi 表示 if 判断结束。
  fi
  # 校验 HTTP 状态码必须是 200。
  assert_text_equal "${name} status" "200" "${code}" || return
  # 计算下载文件的 sha256 哈希。
  local got_hash="$(sha256sum "${out}" | awk '{print $1}')"
  # 校验下载文件哈希必须和源文件一致。
  assert_text_equal "${name} sha256" "${SOURCE_HASH}" "${got_hash}" || return
  # 所有断言通过后记录 PASS。
  record_pass "${name}"
# } 表示函数定义结束。
}

# test_normalized_static_file 测试合法的 .. 折叠路径是否能归一化到真实静态文件。
test_normalized_static_file() {
  # name 表示用例名称。
  local name="normalized static file"
  # out 文件保存服务器下载下来的静态文件。
  local out="${OUT_DIR}/normalized_static_file.bin"
  # err 文件保存 curl 的错误输出。
  local err="${OUT_DIR}/normalized_static_file.err"
  # code 保存 HTTP 状态码。
  local code=""
  # NORMALIZED_PATH 是一个不会越界、但包含 .. 的路径。
  local NORMALIZED_PATH="/pic/../pic/$(basename "${FILE_PATH}")"
  # --path-as-is 表示保留 URL 里的 ..，让服务器自己做路径规范化。
  if ! code="$(curl --path-as-is --noproxy '*' -sS -o "${out}" -w '%{http_code}' --max-time 30 "${BASE_URL}${NORMALIZED_PATH}" 2>"${err}")"; then
    # curl 命令本身失败时记录失败。
    record_fail "${name}" "curl failed, see ${err}"
    # return 结束当前测试函数。
    return
  # fi 表示 if 判断结束。
  fi
  # 校验 HTTP 状态码必须是 200。
  assert_text_equal "${name} status" "200" "${code}" || return
  # 计算下载文件的 sha256 哈希。
  local got_hash="$(sha256sum "${out}" | awk '{print $1}')"
  # 校验下载文件哈希必须和源文件一致。
  assert_text_equal "${name} sha256" "${SOURCE_HASH}" "${got_hash}" || return
  # 所有断言通过后记录 PASS。
  record_pass "${name}"
# } 表示函数定义结束。
}

# 下面开始依次执行每个测试函数。
test_ping

# 执行 POST /echo 测试。
test_echo

# 执行 404 测试。
test_not_found

# 执行非法 Content-Length 测试。
test_bad_content_length

# 执行路径越界测试。
test_path_escape

# 执行普通静态文件测试。
test_static_file

# 执行可归一化静态文件测试。
test_normalized_static_file

# 打印空行，让总结和用例输出分开。
echo

# 打印通过数量。
echo "pass_count=${pass_count}"

# 打印失败数量。
echo "fail_count=${fail_count}"

# 打印测试产物目录。
echo "out_dir=${OUT_DIR}"

# 如果 fail_count 不等于 0，说明至少一个用例失败。
if [[ "${fail_count}" -ne 0 ]]; then
  # exit 1 是测试失败的常见退出码。
  exit 1
# fi 表示 if 判断结束。
fi

# exit 0 表示所有测试通过。
exit 0
