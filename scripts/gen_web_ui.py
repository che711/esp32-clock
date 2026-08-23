# ============================================================
#  gen_web_ui.py — пакует web/index.html в gzip-массив для прошивки.
#
#  Запускается автоматически перед сборкой (extra_scripts в
#  platformio.ini). Результат — $BUILD_DIR/generated/web_ui_gz.h,
#  во флеш попадает ~17 КБ вместо 75 КБ исходного HTML, и столько же
#  уходит по сети: браузер распаковывает gzip сам.
#
#  Единственный источник правды — web/index.html. Руками генерируемый
#  заголовок не трогать: он перезаписывается на каждой сборке.
# ============================================================
import gzip
import hashlib
import os

Import("env")  # noqa: F821  (SCons подставляет Import в область скрипта)

SRC_HTML = os.path.join(env["PROJECT_DIR"], "web", "index.html")  # noqa: F821
OUT_DIR = os.path.join(env.subst("$BUILD_DIR"), "generated")      # noqa: F821
OUT_HEADER = os.path.join(OUT_DIR, "web_ui_gz.h")


def render_header(raw: bytes) -> str:
    # mtime=0 — иначе в gzip попадает время сборки, содержимое заголовка
    # меняется на ровном месте и SCons каждый раз пересобирает main.cpp.
    packed = gzip.compress(raw, compresslevel=9, mtime=0)

    rows = []
    for offset in range(0, len(packed), 16):
        chunk = packed[offset:offset + 16]
        rows.append("    " + " ".join("0x%02x," % byte for byte in chunk))

    saved = 100 - (len(packed) * 100 // len(raw))

    # ETag страницы — хеш её содержимого. Нужен, чтобы браузер не показывал
    # старый дашборд после перепрошивки: сервер отдаёт страницу без единого
    # заголовка кеширования, и браузер в таком случае вправе держать копию
    # эвристически, сколько сочтёт нужным. Именно так и происходило —
    # устройство раздавало новый UI, а вкладка показывала прошлый.
    etag = hashlib.sha256(raw).hexdigest()[:16]

    return (
        "// СГЕНЕРИРОВАНО автоматически из web/index.html\n"
        "// (scripts/gen_web_ui.py). Править здесь бессмысленно.\n"
        "#pragma once\n"
        "#include <stdint.h>\n"
        "#include <pgmspace.h>\n"
        "\n"
        "// %d байт HTML -> %d байт gzip (-%d%%)\n"
        "#define INDEX_HTML_GZ_LEN %d\n"
        "// Хеш исходного HTML: меняется вместе со страницей, ходит в ETag\n"
        "#define INDEX_HTML_ETAG \"\\\"%s\\\"\"\n"
        "\n"
        "const uint8_t INDEX_HTML_GZ[] PROGMEM = {\n"
        "%s\n"
        "};\n" % (len(raw), len(packed), saved, len(packed), etag, "\n".join(rows))
    )


def main():
    if not os.path.isfile(SRC_HTML):
        raise SystemExit("gen_web_ui: не найден %s" % SRC_HTML)

    with open(SRC_HTML, "rb") as src:
        header = render_header(src.read())

    os.makedirs(OUT_DIR, exist_ok=True)

    # Перезаписываем только при реальном изменении: иначе меняется mtime
    # и main.cpp пересобирается на каждый запуск pio run.
    if os.path.isfile(OUT_HEADER):
        with open(OUT_HEADER, "r", encoding="utf-8") as old:
            if old.read() == header:
                return

    with open(OUT_HEADER, "w", encoding="utf-8") as out:
        out.write(header)
    print("gen_web_ui: %s обновлён" % os.path.relpath(OUT_HEADER, env["PROJECT_DIR"]))  # noqa: F821


main()

# Чтобы #include "web_ui_gz.h" из src/ находил сгенерированный заголовок
env.Append(CPPPATH=[OUT_DIR])  # noqa: F821
