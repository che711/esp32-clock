# ============================================================
#  gen_version.py — версия прошивки и время сборки.
#
#  Запускается перед сборкой (extra_scripts в platformio.ini).
#  Результат — $BUILD_DIR/generated/fw_version.h с FW_VERSION и
#  FW_BUILT; дашборд показывает их в шапке, чтобы после прошивки было
#  видно, что плата действительно работает на новом коде.
#
#  Руками заголовок не трогать: перезаписывается на сборке.
# ============================================================
import datetime
import os
import re
import subprocess

Import("env")  # noqa: F821  (SCons подставляет Import в область скрипта)

PROJECT_DIR = env["PROJECT_DIR"]                                   # noqa: F821
OUT_DIR = os.path.join(env.subst("$BUILD_DIR"), "generated")      # noqa: F821
OUT_HEADER = os.path.join(OUT_DIR, "fw_version.h")

# Английские месяцы списком, а не strftime("%b"): тот зависит от локали
# машины, и на русской сборка писала бы «сен».
_MONTHS = ["Jan", "Feb", "Mar", "Apr", "May", "Jun",
           "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"]


def git_version() -> str:
    # --tags — чтобы подхватывался и лёгкий тег, а не только аннотированный;
    # --dirty — прошивку с незакоммиченными правками видно сразу;
    # --always — пока тегов нет, выходит просто короткий хеш.
    try:
        out = subprocess.check_output(
            ["git", "describe", "--tags", "--always", "--dirty"],
            cwd=PROJECT_DIR, stderr=subprocess.DEVNULL, text=True).strip()
    except (OSError, subprocess.CalledProcessError):
        return "unknown"   # сборка из архива без .git
    # Строка уходит в JSON без экранирования: имя тега задаёт человек, и
    # кавычка в нём порвала бы снимок. Всё, кроме безопасных знаков, — в «_».
    return re.sub(r"[^A-Za-z0-9._+-]", "_", out) or "unknown"


def build_time() -> str:
    now = datetime.datetime.now()
    return "%02d %s %d %02d:%02d" % (now.day, _MONTHS[now.month - 1],
                                     now.year, now.hour, now.minute)


def main():
    header = (
        "// СГЕНЕРИРОВАНО автоматически (scripts/gen_version.py).\n"
        "// Править здесь бессмысленно.\n"
        "#pragma once\n"
        "#define FW_VERSION \"%s\"\n"
        "#define FW_BUILT   \"%s\"\n" % (git_version(), build_time())
    )

    os.makedirs(OUT_DIR, exist_ok=True)

    # Время с точностью до минуты, поэтому заголовок меняется раз в минуту,
    # и тогда пересобирается web_api.cpp — единственный, кто его включает.
    # Цена — пара секунд на сборку; зато «built» честный, а не время
    # последней правки web_api.cpp, как вышло бы с __DATE__/__TIME__.
    if os.path.isfile(OUT_HEADER):
        with open(OUT_HEADER, "r", encoding="utf-8") as old:
            if old.read() == header:
                return

    with open(OUT_HEADER, "w", encoding="utf-8") as out:
        out.write(header)


main()

# Тот же каталог добавляет и gen_web_ui.py; AppendUnique не дублирует -I.
env.AppendUnique(CPPPATH=[OUT_DIR])  # noqa: F821
