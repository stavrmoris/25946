#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <unistd.h>
#include <ulimit.h>

extern char **environ;

typedef struct {
    int option;
    char *argument;
} option_event;

static void usage(FILE *stream, const char *program)
{
    fprintf(stream,
            "Usage: %s [-i] [-s] [-p] [-u] [-Ulimit] [-c] [-Climit] "
            "[-d] [-v] [-Vname=value] ...\n"
            "Options are applied from right to left; options may be repeated.\n"
            "  -i              print real/effective user and group IDs\n"
            "  -s              make the process a process-group leader\n"
            "  -p              print process, parent and process-group IDs\n"
            "  -u              print the open-file limit\n"
            "  -Ulimit         set the open-file limit\n"
            "  -c              print the core-file limit in bytes\n"
            "  -Climit         set the core-file limit in bytes\n"
            "  -d              print the current working directory\n"
            "  -v              print the environment\n"
            "  -Vname=value    set or replace an environment variable\n",
            program);
}

static int parse_unsigned(const char *text, const char *option_name,
                          unsigned long long *value)
{
    char *end = NULL;
    unsigned long long parsed;

    if (text == NULL || *text == '\0' || *text == '-') {
        fprintf(stderr, "%s expects a non-negative decimal value: %s\n",
                option_name, text == NULL ? "(missing)" : text);
        return -1;
    }

    errno = 0;
    parsed = strtoull(text, &end, 10);
    if (errno == ERANGE || end == text || *end != '\0') {
        fprintf(stderr, "%s expects a non-negative decimal value: %s\n",
                option_name, text);
        return -1;
    }

    *value = parsed;
    return 0;
}

static int append_event(option_event **events, size_t *count, size_t *capacity,
                        int option, const char *argument)
{
    option_event *grown;
    char *copy = NULL;

    if (argument != NULL) {
        copy = strdup(argument);
        if (copy == NULL) {
            perror("strdup");
            return -1;
        }
    }

    if (*count == *capacity) {
        size_t new_capacity = *capacity == 0 ? 8 : *capacity * 2;
        grown = realloc(*events, new_capacity * sizeof(**events));
        if (grown == NULL) {
            perror("realloc");
            free(copy);
            return -1;
        }
        *events = grown;
        *capacity = new_capacity;
    }

    (*events)[*count].option = option;
    (*events)[*count].argument = copy;
    ++*count;
    return 0;
}

static int print_ids(void)
{
    printf("real_uid=%lu\n", (unsigned long)getuid());
    printf("effective_uid=%lu\n", (unsigned long)geteuid());
    printf("real_gid=%lu\n", (unsigned long)getgid());
    printf("effective_gid=%lu\n", (unsigned long)getegid());
    return 0;
}

static int make_group_leader(void)
{
    if (setpgid(0, 0) == -1) {
        perror("setpgid");
        return -1;
    }
    printf("setpgid_success=1\n");
    return 0;
}

static int print_process_ids(void)
{
    printf("pid=%lu\n", (unsigned long)getpid());
    printf("ppid=%lu\n", (unsigned long)getppid());
    printf("pgrp=%lu\n", (unsigned long)getpgrp());
    return 0;
}

static void print_rlim_value(const char *name, rlim_t value);

static int print_ulimit(void)
{
    long value = ulimit(UL_GETFSIZE);

    if (value == -1) {
        perror("ulimit(UL_GETFSIZE)");
        return -1;
    }

    printf("ulimit=%ld\n", value);
    return 0;
}

static int set_ulimit(const char *argument)
{
    unsigned long long value;

    if (parse_unsigned(argument, "-U", &value) == -1) {
        return -1;
    }

    if (value > LONG_MAX) {
        fprintf(stderr, "-U value is too large: %s\n", argument);
        return -1;
    }

    if (ulimit(UL_SETFSIZE, (long)value) == -1) {
        perror("ulimit(UL_SETFSIZE)");
        return -1;
    }

    printf("ulimit_set=%llu\n", value);
    return 0;
}

static void print_rlim_value(const char *name, rlim_t value)
{
    if (value == RLIM_INFINITY) {
        printf("%s=unlimited\n", name);
    } else {
        printf("%s=%llu\n", name, (unsigned long long)value);
    }
}

static int print_core_limit(void)
{
    struct rlimit limit;

    if (getrlimit(RLIMIT_CORE, &limit) == -1) {
        perror("getrlimit(RLIMIT_CORE)");
        return -1;
    }
    print_rlim_value("core_size_limit_bytes", limit.rlim_cur);
    return 0;
}

static int set_core_limit(const char *argument)
{
    unsigned long long value;
    struct rlimit limit;

    if (parse_unsigned(argument, "-C", &value) == -1) {
        return -1;
    }
    if (getrlimit(RLIMIT_CORE, &limit) == -1) {
        perror("getrlimit(RLIMIT_CORE)");
        return -1;
    }
    if ((rlim_t)value > limit.rlim_max) {
        fprintf(stderr, "-C value exceeds the hard core-size limit: %s\n",
                argument);
        return -1;
    }
    limit.rlim_cur = (rlim_t)value;
    if (setrlimit(RLIMIT_CORE, &limit) == -1) {
        perror("setrlimit(RLIMIT_CORE)");
        return -1;
    }
    printf("core_size_limit_set_bytes=%llu\n", value);
    return 0;
}

static int print_working_directory(void)
{
    char *directory = getcwd(NULL, 0);

    if (directory == NULL) {
        perror("getcwd");
        return -1;
    }
    printf("current_working_directory=%s\n", directory);
    free(directory);
    return 0;
}

static int print_environment(void)
{
    char **entry;

    for (entry = environ; entry != NULL && *entry != NULL; ++entry) {
        puts(*entry);
    }
    return 0;
}

static int set_environment_variable(const char *argument)
{
    const char *separator;
    size_t name_length;
    char *name;

    if (argument == NULL) {
        fprintf(stderr, "-V expects NAME=VALUE\n");
        return -1;
    }
    separator = strchr(argument, '=');
    if (separator == NULL || separator == argument) {
        fprintf(stderr, "-V expects NAME=VALUE: %s\n", argument);
        return -1;
    }

    name_length = (size_t)(separator - argument);
    name = malloc(name_length + 1);
    if (name == NULL) {
        perror("malloc");
        return -1;
    }
    memcpy(name, argument, name_length);
    name[name_length] = '\0';

    if (setenv(name, separator + 1, 1) == -1) {
        perror("setenv");
        free(name);
        return -1;
    }
    printf("environment_set=%s=%s\n", name, separator + 1);
    free(name);
    return 0;
}

static int apply_event(const option_event *event)
{
    switch (event->option) {
    case 'i':
        return print_ids();
    case 's':
        return make_group_leader();
    case 'p':
        return print_process_ids();
    case 'u':
    return print_ulimit();
    case 'U':
        return set_ulimit(event->argument);
    case 'c':
        return print_core_limit();
    case 'C':
        return set_core_limit(event->argument);
    case 'd':
        return print_working_directory();
    case 'v':
        return print_environment();
    case 'V':
        return set_environment_variable(event->argument);
    default:
        fprintf(stderr, "internal error: unsupported option -%c\n",
                event->option);
        return -1;
    }
}

int main(int argc, char **argv)
{
    option_event *events = NULL;
    size_t count = 0;
    size_t capacity = 0;
    int option;
    int status = 0;

    opterr = 0;
    while ((option = getopt(argc, argv, ":ispuU:cC:dvV:")) != -1) {
        if (option == ':') {
            fprintf(stderr, "option -%c requires an argument\n", optopt);
            usage(stderr, argv[0]);
            status = 2;
            goto cleanup;
        }
        if (option == '?') {
            /* The assignment asks us to test options separated by '-'.
             * For example, treat -i-p like the equivalent -i -p. */
            if (optopt == '-') {
                continue;
            }
            fprintf(stderr, "invalid option: -%c\n", optopt);
            usage(stderr, argv[0]);
            status = 2;
            goto cleanup;
        }
        if (append_event(&events, &count, &capacity, option, optarg) == -1) {
            status = 1;
            goto cleanup;
        }
    }

    if (optind < argc) {
        fprintf(stderr, "unexpected operand: %s\n", argv[optind]);
        usage(stderr, argv[0]);
        status = 2;
        goto cleanup;
    }

    if (count == 0) {
        usage(stdout, argv[0]);
        goto cleanup;
    }

    for (size_t index = count; index > 0; --index) {
        if (apply_event(&events[index - 1]) == -1) {
            status = 1;
            break;
        }
    }

cleanup:
    for (size_t index = 0; index < count; ++index) {
        free(events[index].argument);
    }
    free(events);
    return status;
}

// === === === === === ===

//-i	                Выводит реальные и эффективные UID/GID процесса
//-s	                Делает текущий процесс лидером группы процессов через setpgid(0, 0)
//-p	                Выводит PID, PPID и идентификатор группы процессов
//-u	                Выводит текущий лимит ulimit
//-Uчисло	            Устанавливает лимит ulimit, например -U64
//-c	                Выводит текущий лимит размера core-файла в байтах
//-Cчисло	            Устанавливает лимит core-файла, например -C0
//-d	                Выводит текущую рабочую директорию
//-v	                Печатает всё окружение процесса
//-VИМЯ=ЗНАЧЕНИЕ	    Устанавливает или заменяет переменную окружения

// === === === === === ===

//./options -s -p
//./options -VTEST=hello -v
//./options -C0 -c

// === === === === === ===