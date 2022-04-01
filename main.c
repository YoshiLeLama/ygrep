#include <errno.h>
#include <fcntl.h>
#include <memory.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <wait.h>

#define BUF_SIZE 512

int check_pattern(char *pattern, char *line)
{
    int first_char_index = -1;
    char first_char;

    int line_len = strlen(line);

    int pattern_len = strlen(pattern);
    for (int i = 0; i < pattern_len; ++i)
    {
        if (pattern[i] != '.')
        {
            first_char = pattern[i];
            first_char_index = i;
            break;
        }
    }

    // Dans le cas où il n'y a que des points
    if (first_char_index == -1)
    {
        return line_len >= pattern_len;
    }

    int max_index = line_len - pattern_len + first_char_index;
    int max_offset = pattern_len - first_char_index;
    int success;
    for (int i = first_char_index; i < max_index; ++i)
    {
        if (line[i] == first_char)
        {
            success = 1;
            for (int off = 0; off < max_offset; ++off)
            {
                if (pattern[first_char_index + off] != '.' && line[i + off] != pattern[first_char_index + off])
                {
                    success = 0;
                    break;
                }
            }

            if (success)
            {
                return 1;
            }
        }
    }

    return 0;
}

void run_grep(char *pattern, char *file, int pipe_fd)
{
    int fd = open(file, O_RDONLY);
    if (fd == -1)
    {
        if (errno == ENOENT) {
            printf("ygrep: %s: Le fichier ou le répertoire n'existe pas\n", file);
        } else {
            perror("open");
        }
        exit(EXIT_FAILURE);
    }

    int buf_size = BUF_SIZE;
    char *buf = malloc(buf_size * sizeof(char));
    if ((buf = memset(buf, '\0', buf_size)) == NULL)
    {
        exit(EXIT_FAILURE);
    }

    dup2(pipe_fd, STDOUT_FILENO);
    close(pipe_fd);

    int cnt;
    int index;
    int line = 1;
    while ((cnt = read(fd, buf, buf_size)) > 0)
    {
        index = -1;
        for (int i = 0; i < buf_size; ++i)
        {
            if (buf[i] == '\n' || buf[i] == '\0')
            {
                index = i;
                buf[index] = '\0';
                break;
            }
        }

        if (index == -1)
        {
            lseek(fd, -buf_size, SEEK_CUR);
            buf_size *= 2;
            if ((buf = realloc(buf, buf_size * sizeof(char))) == NULL)
            {
                perror("realloc");
                exit(EXIT_FAILURE);
            }
        }
        else
        {
            if (check_pattern(pattern, buf))
            {
                printf("\033[31m%s (%i) \t: \033[0m%s\n", file, line, buf);
            }
            lseek(fd, index - cnt + 1, SEEK_CUR);
            ++line;
        }
    }

    free(buf);

    exit(EXIT_SUCCESS);
}

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        printf("Not enough arguments\n");
        exit(EXIT_FAILURE);
    }

    int pipes[2 * (argc - 2)];

    pid_t children[argc - 2];
    for (int i = 0; i < argc - 2; ++i)
    {
        pipe(&pipes[2 * i]);
        children[i] = fork();
        if (children[i] == -1)
        {
            perror("fork");
            exit(EXIT_FAILURE);
        }
        else if (children[i])
        {
            continue;
        }
        else
        {
            close(pipes[2 * i]);
            run_grep(argv[1], argv[2 + i], pipes[2 * i + 1]);
        }
    }

    int cnt;
    int status;
    char *buf = malloc(BUF_SIZE * sizeof(char));
    for (int i = 0; i < argc - 2; ++i)
    {
        waitpid(children[i], &status, 0);

        if ((buf = memset(buf, '\0', BUF_SIZE)) == NULL)
        {
            perror("memset");
            exit(EXIT_FAILURE);
        }

        struct pollfd pfd = {.fd = pipes[2*i], .events=POLL_IN};

        while (poll(&pfd, 1, 0) > 0)
        {
            if (read(pipes[2 * i], buf, BUF_SIZE) == -1) {
                perror("read");
                exit(EXIT_FAILURE);
            } 
            printf("%s", buf);
            if ((buf = memset(buf, '\0', BUF_SIZE)) == NULL)
            {
                perror("memset");
                exit(EXIT_FAILURE);
            }
        }
    }

    free(buf);

    return 0;
}