#!/usr/bin/env bash
# ============================================================
#  lint.sh — статический анализ (cppcheck).
#
#  Одна и та же команда локально и в CI: .github/workflows/ci.yml
#  вызывает этот скрипт. Раньше флаги жили только в workflow, и
#  повторить проверку у себя было нечем — про замечание узнавали
#  уже из упавшей сборки.
#
#  Установка: sudo apt install cppcheck
# ============================================================
set -euo pipefail

cd "$(dirname "$0")/.."

if ! command -v cppcheck >/dev/null 2>&1; then
    echo "cppcheck не найден. Установить: sudo apt install cppcheck" >&2
    exit 127
fi

# Проверяем весь src/, а не пару файлов: раньше main.cpp, sensor.cpp,
# battery.cpp и mqtt.cpp не смотрел никто.
#
# cstyleCast — по всему Arduino- и IDF-API (payload кадра WebSocket в
# esp_http_server — uint8_t*, приведения к char* и обратно неизбежны),
# поэтому подавлен осознанно.
#
# knownConditionTrueFalse на файле тестов — шум по устройству дела: тест
# сверяет заранее известный ответ чистой функции, и анализатор каждый раз
# сообщает, что условие всегда истинно. На src/ проверка остаётся включённой.
exec cppcheck \
    --enable=warning,style,performance \
    --suppress=missingIncludeSystem \
    --suppress=unusedFunction \
    --suppress=invalidPrintfArgType_sint \
    --suppress=cstyleCast \
    --suppress=knownConditionTrueFalse:test/test_native/test_main.cpp \
    --error-exitcode=1 \
    --inline-suppr \
    "$@" \
    src/ \
    test/test_native/test_main.cpp
