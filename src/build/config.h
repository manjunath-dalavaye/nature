#ifndef NATURE_BUILD_CONFIG_H
#define NATURE_BUILD_CONFIG_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <limits.h>
#include "utils/helper.h"

typedef enum {
    OS_LINUX = 1,
    OS_DARWIN,
    ARCH_AMD64,
    ARCH_RISCV64,
    ARCH_WASM,
} build_param_t;

extern build_param_t BUILD_OS;
extern build_param_t BUILD_ARCH;

extern char *NATURE_ROOT; // linux/darwin/freebsd default root

extern char BUILD_OUTPUT_NAME[PATH_MAX]; // main
extern char BUILD_OUTPUT_DIR[PATH_MAX]; // default is work_dir test 使用，指定编译路径输出文件
extern char BUILD_OUTPUT[PATH_MAX]; // default = BUILD_OUTPUT_DIR/BUILD_OUTPUT_NAME

extern char *WORKDIR; // 执行 shell 命令所在的目录(import 搜索将会基于该目录进行文件搜索)
extern char *BASE_NS; // 最后一级目录的名称，也可以自定义
extern char *TEMP_DIR; // 链接临时目录

extern char *BUILD_ENTRY; // nature build {test/main.n} 花括号包起来的这部分
extern char SOURCE_PATH[PATH_MAX]; // /opt/test/main.n 的绝对路径

#define LINUX_BUILD_TMP_DIR  "/tmp/nature-build.XXXXXX"
//#define DARWIN_BUILD_TMP_DIR  ""
#define LIB_START_FILE "crt1.o"
#define LIB_RUNTIME_FILE "libruntime.a"
#define LIBC_FILE "libc.a"
#define LIBUCONTEXT_FILE "libucontext.a"
#define LINKER_OUTPUT "a.out"


static inline char *temp_dir() {
    char *tmp_dir;
    if (BUILD_OS == OS_LINUX) {
        char temp[] = LINUX_BUILD_TMP_DIR;
        VOID mkdtemp(temp);

        size_t size = strlen(LINUX_BUILD_TMP_DIR) + 1;
        tmp_dir = malloc(size);
        memset(tmp_dir, '\0', size);
        strcpy(tmp_dir, temp);
    } else {
        goto ERROR;
    }

    return tmp_dir;
    ERROR:
    assertf(false, "[cross_tmp_dir] unsupported BUILD_OS/BUILD_ARCH pair %s/%s", BUILD_OS, BUILD_ARCH);
    exit(1);
}

static inline char *parser_base_ns(char *dir) {
    char *result = dir;
    // 取最后一节
    char *trim_path = strrchr(dir, '/');
    if (trim_path != NULL) {
        result = trim_path + 1;
    }

    return result;
}

static inline char *os_to_string(uint8_t os) {
    if (os == OS_LINUX) {
        return "linux";
    }
    return NULL;
}

static inline char *arch_to_string(uint8_t arch) {
    if (arch == ARCH_AMD64) {
        return "amd64";
    }
    return NULL;
}

static inline uint8_t os_to_uint8(char *os) {
    if (str_equal(os, "linux")) {
        return OS_LINUX;
    }
    return 0;
}

static inline uint8_t arch_to_uint8(char *arch) {
    if (str_equal(arch, "amd64")) {
        return ARCH_AMD64;
    }
    return 0;
}

static inline void config_init() {
    WORKDIR = get_workdir();
    // 当前所在目录的最后一级目录(当 import 已 base_ns 开头时，表示从 root_ns 进行文件搜索)
    BASE_NS = parser_base_ns(WORKDIR);
    TEMP_DIR = temp_dir();

    // BUILD_OUTPUT_DIR 优先从 main 入口解析，如果没有值才是 work_dir
    if (strlen(BUILD_OUTPUT_DIR) == 0) {
        strcpy(BUILD_OUTPUT_DIR, WORKDIR);
    }

    strcpy(BUILD_OUTPUT, path_join(BUILD_OUTPUT_DIR, BUILD_OUTPUT_NAME));
    assertf(!dir_exists(BUILD_OUTPUT), "build output='%s' cannnot be a directory", BUILD_OUTPUT);
}

static inline void env_init() {
    char *os = getenv("BUILD_OS");
    if (os != NULL) {
        BUILD_OS = os_to_uint8(os);
    }

    char *arch = getenv("BUILD_ARCH");
    if (arch != NULL) {
        BUILD_ARCH = arch_to_uint8(arch);
    }

    if (BUILD_OS != OS_LINUX) {
        assertf(false,
                "only support compiles to os=linux, please with BUILD_OS build, example: BUILD_OS=linux nature build main.n");
    }
    if (BUILD_ARCH != ARCH_AMD64) {
        assertf(false,
                "only support compiles to arch=amd64, please with BUILD_ARCH build, example BUILD_ARCH=mad64 nature build main.n");
    }

    char *root = getenv("NATURE_ROOT");
    if (root != NULL) {
        NATURE_ROOT = root;
    }
}


#endif //NATURE_ENV_H
