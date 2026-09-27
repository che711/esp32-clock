# ============================================================
#  gen_web_ui.py — пакует web/index.html в gzip-массив для прошивки.
#
#  Запускается автоматически перед сборкой (extra_scripts в
#  platformio.ini). Результат — $BUILD_DIR/generated/web_ui_gz.h:
#  страница без комментариев, сжатая gzip, — браузер распаковывает
#  её сам. Сколько байт вышло, пишет шапка самого заголовка.
#
#  Единственный источник правды — web/index.html. Руками генерируемый
#  заголовок не трогать: он перезаписывается на каждой сборке.
# ============================================================
import gzip
import hashlib
import os
import re

Import("env")  # noqa: F821  (SCons подставляет Import в область скрипта)

SRC_HTML = os.path.join(env["PROJECT_DIR"], "web", "index.html")  # noqa: F821
OUT_DIR = os.path.join(env.subst("$BUILD_DIR"), "generated")      # noqa: F821
OUT_HEADER = os.path.join(OUT_DIR, "web_ui_gz.h")


# ── Комментарии ──────────────────────────────────────────────
# Комментарии в странице — это объяснения «почему», и в исходнике им место.
# Но браузеру они не нужны, а весят больше четверти страницы: кириллица в
# UTF-8 — два байта на букву. Без них gzip выходит на треть меньше (35 → 23 КБ
# на момент замера), а это первая загрузка после каждой прошивки, в экономе:
# радио спит между маячками, и передача тянется секундами.
#
# Полного минификатора тут нет намеренно — это лишняя зависимость в CI ради
# остатка: отступов и повторяющихся имён, которые gzip и так ужимает лучше
# всего. Вырезается только то, что можно вырезать, не
# разбирая JavaScript:
#   - в <script> — строки, целиком состоящие из «// …». Без шаблонных
#     строк (`…`) и переноса строки через «\» такая строка может быть только
#     комментарием: обычная строка и регулярное выражение на перенос не
#     растягиваются;
#   - в <style> — блоки /* … */;
#   - в разметке — <!-- … -->.
# Если в странице появится то, на чём эти правила перестают быть верными,
# соответствующий шаг пропускается с предупреждением: лучше лишние
# килобайты, чем сломанный дашборд.
_BLOCK = re.compile(r"<(script|style)\b[^>]*>.*?</\1>", re.S)


def _warn(msg):
    print("gen_web_ui: %s" % msg)


def _strip_script(block: str) -> str:
    if "`" in block:
        _warn("в <script> есть шаблонные строки — комментарии JS не вырезаю")
        return block
    if re.search(r"\\\n", block):
        _warn("в <script> есть перенос строки через \\ — комментарии JS не вырезаю")
        return block
    return re.sub(r"^[ \t]*//[^\n]*\n", "", block, flags=re.M)


def _strip_style(block: str) -> str:
    if re.search(r"[\"'][^\"'\n]*/\*", block):
        _warn("в <style> есть строка с /* — комментарии CSS не вырезаю")
        return block
    return re.sub(r"/\*.*?\*/", "", block, flags=re.S)


def _strip_markup(text: str) -> str:
    return re.sub(r"<!--.*?-->", "", text, flags=re.S)


def strip_comments(html: str) -> str:
    out, pos = [], 0
    for m in _BLOCK.finditer(html):
        out.append(_strip_markup(html[pos:m.start()]))
        block = m.group(0)
        out.append(_strip_script(block) if m.group(1) == "script" else _strip_style(block))
        pos = m.end()
    out.append(_strip_markup(html[pos:]))
    # Строки, от которых остались одни пробелы. Хотя бы один перевод строки
    # между соседними строками остаётся, так что пробел между элементами
    # разметки не пропадает.
    return re.sub(r"\n[ \t]*(?=\n)", "", "".join(out))


def render_header(raw: bytes) -> str:
    page = strip_comments(raw.decode("utf-8")).encode("utf-8")

    # mtime=0 — иначе в gzip попадает время сборки, содержимое заголовка
    # меняется на ровном месте и SCons каждый раз пересобирает main.cpp.
    packed = gzip.compress(page, compresslevel=9, mtime=0)

    rows = []
    for offset in range(0, len(packed), 16):
        chunk = packed[offset:offset + 16]
        rows.append("    " + " ".join("0x%02x," % byte for byte in chunk))

    saved = 100 - (len(packed) * 100 // len(raw))

    # ETag страницы — хеш её содержимого. Нужен, чтобы браузер не показывал
    # старый дашборд после перепрошивки. Пока страница уходила без заголовков
    # кеширования, браузер держал копию эвристически, сколько сочтёт нужным:
    # устройство раздавало новый UI, а вкладка показывала прошлый. Теперь
    # handleRoot() шлёт Cache-Control: no-cache и этот ETag, и браузер перед
    # показом сверяется — совпало, приходит 304.
    #
    # Хешируется то, что уходит в браузер, а не исходник: правка одного
    # комментария страницу не меняет, и перекачивать её незачем, а правка
    # правил вырезания меняет — при том же исходнике.
    etag = hashlib.sha256(page).hexdigest()[:16]

    return (
        "// СГЕНЕРИРОВАНО автоматически из web/index.html\n"
        "// (scripts/gen_web_ui.py). Править здесь бессмысленно.\n"
        "#pragma once\n"
        "#include <stdint.h>\n"
        "#include <pgmspace.h>\n"
        "\n"
        "// %d байт HTML, без комментариев %d -> %d байт gzip (-%d%%)\n"
        "#define INDEX_HTML_GZ_LEN %d\n"
        "// Хеш отдаваемой страницы: меняется вместе с ней, ходит в ETag\n"
        "#define INDEX_HTML_ETAG \"\\\"%s\\\"\"\n"
        "\n"
        "const uint8_t INDEX_HTML_GZ[] PROGMEM = {\n"
        "%s\n"
        "};\n" % (len(raw), len(page), len(packed), saved, len(packed), etag,
                  "\n".join(rows))
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
