#define _POSIX_C_SOURCE 200809L
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>

static char *editor_command(const char *editor, const char *path)
{
    size_t n = strlen(editor), m = strlen(path);
    char *command, *p;
    if (n > SIZE_MAX - 6 || m > (SIZE_MAX - n - 6) / 4) return NULL;
    command = malloc(n + 4 * m + 6);
    if (command == NULL) return NULL;
    memcpy(command, editor, n);
    p = command + n;
    *p++ = ' ';
    *p++ = '\'';
    if (*path == '-') {
        *p++ = '.';
        *p++ = '/';
    }
    /* Заключаем путь в кавычки, чтобы оболочка не исполняла его содержимое */
    for (; *path; ++path) {
        if (*path == '\'') {
            memcpy(p, "'\\''", 4);
            p += 4;
        } else {
            *p++ = *path;
        }
    }
    *p++ = '\'';
    *p = '\0';
    return command;
}

int main(int argc, char **argv)
{
    int mandatory = argc == 3 && strcmp(argv[1], "--mandatory") == 0;
    int fd, result, status = EXIT_FAILURE;
    const char *path, *editor;
    char *command;
    struct stat info;
    struct flock lock = {0};

    if (argc != 2 && !mandatory) {
        fprintf(stderr, "Usage: %s [--mandatory] FILE\n", argv[0]);
        return EXIT_FAILURE;
    }
    path = argv[mandatory ? 2 : 1];
    fd = open(path, O_RDWR | O_CLOEXEC | O_NONBLOCK);
    if (fd == -1) { perror("open"); return EXIT_FAILURE; }
    if (fstat(fd, &info) == -1) { perror("fstat"); goto cleanup; }
    if (!S_ISREG(info.st_mode)) {
        fputs("Expected a regular file\n", stderr);
        goto cleanup;
    }
    if (!!((info.st_mode & S_ISGID) && !(info.st_mode & S_IXGRP)) != mandatory) {
        fputs("File mode does not match the requested locking experiment\n", stderr);
        goto cleanup;
    }

    /* Нулевая длина блокирует файл от начала до конца, включая будущие записи */
    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_SET;
    if (fcntl(fd, F_SETLK, &lock) == -1) {
        if (errno == EACCES || errno == EAGAIN) {
            fputs("File is already locked\n", stderr);
        } else {
            perror("fcntl");
        }
        goto cleanup;
    }

    editor = getenv("EDITOR");
    if (editor == NULL || *editor == '\0') editor = "vi";
    command = editor_command(editor, path);
    if (command == NULL) {
        fputs("Cannot allocate editor command\n", stderr);
        goto cleanup;
    }
    puts("File locked, starting editor");
    fflush(stdout);
    /* Родитель сохраняет дескриптор открытым, пока редактор работает */
    result = system(command);
    free(command);
    if (result == -1) {
        perror("system");
    } else if (WIFEXITED(result)) {
        status = WEXITSTATUS(result);
    }

cleanup:
    if (close(fd) == -1) { perror("close"); return EXIT_FAILURE; }
    return status;
}
