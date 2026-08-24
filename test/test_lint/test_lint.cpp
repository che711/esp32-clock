#include <unity.h>
#include <stdlib.h>
#include <sys/wait.h>

// ============================================================
//  test_lint — статический анализ как обычный тест.
//
//  cppcheck крутится в CI и роняет там работу Lint. Чтобы узнавать
//  о замечании до пуша, тот же скрипт (scripts/lint.sh) запускается
//  здесь: `pio test -e native` теперь показывает и его результат.
//
//  Путь к корню проекта приходит из platformio.ini через PROJECT_ROOT:
//  каталог, из которого запущен тестовый бинарь, зависит от PlatformIO,
//  и полагаться на него нельзя.
// ============================================================

#ifndef PROJECT_ROOT
#define PROJECT_ROOT "."
#endif

static bool haveCppcheck() {
    return system("command -v cppcheck >/dev/null 2>&1") == 0;
}

// Без установленного cppcheck тест не падает, а пропускается: иначе у
// всякого, кто только что клонировал проект, тесты сразу красные, и красный
// этот — не про код.
void test_cppcheck_clean() {
    if (!haveCppcheck())
        TEST_IGNORE_MESSAGE("cppcheck не установлен: sudo apt install cppcheck");

    int rc = system(PROJECT_ROOT "/scripts/lint.sh");
    TEST_ASSERT_TRUE_MESSAGE(rc != -1, "не удалось запустить scripts/lint.sh");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, WEXITSTATUS(rc), "cppcheck нашёл замечания");
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_cppcheck_clean);
    return UNITY_END();
}
