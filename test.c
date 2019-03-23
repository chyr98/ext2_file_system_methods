#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <string.h>
#include <libgen.h>
#include "ext2.h"
// #include "ext2_util.h"

int only_contains_slashes(char *path)
{
    for (int i = 0; i < strlen(path); i++)
    {
        if (path[i] != '/')
        {
            return 0;
        }
    }

    //printf("Only contains slashes!\n");
    return 1;
}


struct linked_dir
{
    char *name;
    struct linked_dir *next;
};

struct linked_dir *get_linked_dirs(char *path)
{

    struct linked_dir *dirs = NULL;

    while (!only_contains_slashes(path))
    {
        struct linked_dir *dir = (struct linked_dir *)malloc(sizeof(struct linked_dir));
        dir->name = strdup(basename(path));
        printf("basename at this level: %s\n", dir->name);
        dir->next = dirs;
        dirs = dir;
        path = dirname(path);
        printf("Now path is: %s\n", path);
    }

    struct linked_dir *root_dir = (struct linked_dir *)malloc(sizeof(struct linked_dir));
    root_dir->name = ".";
    root_dir->next = dirs;
    dirs = root_dir;

    return dirs;
}
int main(int argc, char **argv)
{
    if (argc != 2)
    {
        fprintf(stderr, "Usage: ./test path_name");
        exit(1);
    }

    char *path_name = argv[1];
    char *parent_path_name = dirname(path_name);


    printf("Dirname: %s\n", parent_path_name);

    printf("Basename: %s\n", basename(parent_path_name));

    // struct linked_dir *dirs = {NULL, NULL};

    // while (strcmp(path_name, "/") != 0)
    // {
    //     struct linked_dir *dir = (struct linked_dir *)malloc(sizeof(struct linked_dir));
    //     dir->name = basename(path_name);
    //     dir->next = dirs;
    //     //printf("current dir: %s\n", dir.name);
    //     dirs = dir;
    //     //printf("%s\n",basename(path_name));
    //     path_name = dirname(path_name);
    // }

    int level = 0;
    struct linked_dir *directories = get_linked_dirs(parent_path_name);
    // print the dirs names

    while (directories != NULL)
    {
        printf("level: %d, dir: %s\n", level, directories->name);
        directories = directories->next;
        level += 1;
    }

    // char name[6] = "Hello\0";
    // printf("length: %d\n", (int) strlen(name));

    return 0;
}