#!/usr/bin/env bash
# File: stage1_concurrency_test.sh
# Purpose: run concurrent HTTP regression tests against CPPServer.
# Usage:
#   ./stage1_concurrency_test.sh [host] [port]
#
# Tunables:
#   PING_N=80 ECHO_N=40 NOTFOUND_N=20 FILE_N=6 SLOW_N=2 BAD_N=10 ./stage1_concurrency_test.sh 127.0.0.1 8888

set -u

HOST="${1:-127.0.0.1}"
PORT="${2:-8888}"
BASE_URL="http://${HOST}:${PORT}"

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT_DIR="${OUT_DIR:-/tmp/cppserver_stage1_concurrency_$(date +%Y%m%d%H%M%S)}"

PING_N="${PING_N:-80}"
ECHO_N="${ECHO_N:-40}"
NOTFOUND_N="${NOTFOUND_N:-20}"
FILE_N="${FILE_N:-6}"
SLOW_N="${SLOW_N:-2}"
BAD_N="${BAD_N:-10}"
RATE="${RATE:-100k}"
FILE_PATH="${FILE_PATH:-/pic/g1.png}"
SOURCE_FILE="${ROOT_DIR}/src${FILE_PATH}"

if [[ ! -f "${SOURCE_FILE}" ]]; then
  FILE_PATH="/pic/a1.png"
  SOURCE_FILE="${ROOT_DIR}/src${FILE_PATH}"
fi

mkdir -p "${OUT_DIR}"/{ping,echo,notfound,file,slow,bad}

for cmd in curl sha256sum timeout grep awk; do
  if ! command -v "${cmd}" >/dev/null 2>&1; then
    echo "[FATAL] missing command: ${cmd}" >&2
    exit 2
  fi
done

if [[ ! -f "${SOURCE_FILE}" ]]; then
  echo "[FATAL] source file not found: ${SOURCE_FILE}" >&2
  exit 2
fi

source_hash="$(sha256sum "${SOURCE_FILE}" | awk '{print $1}')"
pass_count=0
fail_count=0

record_result() {
  local name="$1"
  local failed="$2"
  if [[ "${failed}" -eq 0 ]]; then
    echo "[PASS] ${name}"
    pass_count=$((pass_count + 1))
  else
    echo "[FAIL] ${name}: ${failed} failed"
    fail_count=$((fail_count + failed))
  fi
}

run_batch() {
  local name="$1"
  local count="$2"
  local func="$3"
  local pids=()
  local failed=0

  echo "[RUN ] ${name}, count=${count}"
  for i in $(seq 1 "${count}"); do
    "${func}" "${i}" &
    pids+=("$!")
  done

  for pid in "${pids[@]}"; do
    if ! wait "${pid}"; then
      failed=$((failed + 1))
    fi
  done

  record_result "${name}" "${failed}"
}

test_ping() {
  local i="$1"
  local body="${OUT_DIR}/ping/${i}.body"
  local err="${OUT_DIR}/ping/${i}.err"
  local code
  code="$(curl --noproxy '*' -sS -o "${body}" -w '%{http_code}' --max-time 5 "${BASE_URL}/ping" 2>"${err}")" || return 1
  [[ "${code}" == "200" ]] || return 1
  [[ "$(cat "${body}")" == "pong" ]] || return 1
}

test_echo() {
  local i="$1"
  local body="${OUT_DIR}/echo/${i}.body"
  local err="${OUT_DIR}/echo/${i}.err"
  local code
  code="$(curl --noproxy '*' -sS -o "${body}" -w '%{http_code}' --max-time 5 \
    -X POST --data '{"msg":"hello"}' "${BASE_URL}/echo" 2>"${err}")" || return 1
  [[ "${code}" == "200" ]] || return 1
  [[ "$(cat "${body}")" == '{"msg":"hello"}' ]] || return 1
}

test_notfound() {
  local i="$1"
  local body="${OUT_DIR}/notfound/${i}.body"
  local err="${OUT_DIR}/notfound/${i}.err"
  local code
  code="$(curl --noproxy '*' -sS -o "${body}" -w '%{http_code}' --max-time 5 \
    "${BASE_URL}/not-found-${i}" 2>"${err}")" || return 1
  [[ "${code}" == "404" ]] || return 1
  [[ "$(cat "${body}")" == "Not Found" ]] || return 1
}

test_file() {
  local i="$1"
  local out="${OUT_DIR}/file/${i}.bin"
  local err="${OUT_DIR}/file/${i}.err"
  local code
  code="$(curl --noproxy '*' -sS -o "${out}" -w '%{http_code}' --max-time 30 \
    "${BASE_URL}${FILE_PATH}" 2>"${err}")" || return 1
  [[ "${code}" == "200" ]] || return 1
  [[ "$(sha256sum "${out}" | awk '{print $1}')" == "${source_hash}" ]] || return 1
}

test_slow_file() {
  local i="$1"
  local out="${OUT_DIR}/slow/${i}.bin"
  local err="${OUT_DIR}/slow/${i}.err"
  local code
  code="$(curl --noproxy '*' -sS --limit-rate "${RATE}" -o "${out}" -w '%{http_code}' --max-time 120 \
    "${BASE_URL}${FILE_PATH}" 2>"${err}")" || return 1
  [[ "${code}" == "200" ]] || return 1
  [[ "$(sha256sum "${out}" | awk '{print $1}')" == "${source_hash}" ]] || return 1
}

test_bad_request() {
  local i="$1"
  local resp="${OUT_DIR}/bad/${i}.resp"
  local err="${OUT_DIR}/bad/${i}.err"
  timeout 5 bash -c "exec 3<>/dev/tcp/${HOST}/${PORT}; printf 'GET /ping HTTP/1.1\r\nHost: localhost\r\nContent-Length: abc\r\n\r\n' >&3; cat <&3" \
    >"${resp}" 2>"${err}" || return 1
  grep -q '400 Bad Request' "${resp}" || return 1
  grep -qi 'connection: close' "${resp}" || return 1
}

echo "CPPServer stage1 concurrency test"
echo "target=${BASE_URL}"
echo "out_dir=${OUT_DIR}"
echo "file=${FILE_PATH}"
echo "file_sha256=${source_hash}"
echo

run_batch "ping" "${PING_N}" test_ping
run_batch "echo" "${ECHO_N}" test_echo
run_batch "notfound" "${NOTFOUND_N}" test_notfound
run_batch "file" "${FILE_N}" test_file
run_batch "slow_file" "${SLOW_N}" test_slow_file
run_batch "bad_request" "${BAD_N}" test_bad_request

echo
echo "batch_pass=${pass_count}"
echo "request_fail=${fail_count}"
echo "out_dir=${OUT_DIR}"

if [[ "${fail_count}" -ne 0 ]]; then
  exit 1
fi

exit 0
