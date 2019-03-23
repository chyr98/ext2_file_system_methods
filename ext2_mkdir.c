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
#include "ext2_util.h"
#include <errno.h>
// #define _GNU_SOURCE

unsigned char *disk;

int main(int argc, char **argv)
{
    if (argc != 3)
    {
        fprintf(stderr, "Usage: %s <image file name> <path>\n", argv[0]);
        exit(1);
    }

    int fd = open(argv[1], O_RDWR);
    char *path = argv[2];
    char *back_up_path = strdup(argv[2]);

    disk = mmap(NULL, 128 * EXT2_BLOCK_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (disk == MAP_FAILED)
    {
        perror("mmap");
        exit(1);
    }

    struct ext2_super_block *sb = (struct ext2_super_block *)(disk + EXT2_BLOCK_SIZE);
    struct ext2_group_desc *gd = (struct ext2_group_desc *)(disk + 2 * EXT2_BLOCK_SIZE);
    struct ext2_inode *inodes = (struct ext2_inode *)(disk + gd->bg_inode_table * EXT2_BLOCK_SIZE);


    char *parent_path = dirname(path);

    char *dir_name = basename(back_up_path);

    struct linked_dir *dirs = get_linked_dirs(parent_path);

    int parent_inode_number;
    if ((parent_inode_number = findpath(dirs, disk)) == -1)
    {
        return ENOENT;
    }

    // Gets the inode of the parent directory
    struct ext2_inode *parent_inode = (struct ext2_inode *)((unsigned char *)inodes + (parent_inode_number - 1) * 128);

    // Gets the parent directory's directory entry
    struct ext2_dir_entry *parent_de = (struct ext2_dir_entry *)(disk + (parent_inode->i_block[0]) * EXT2_BLOCK_SIZE);

    // Find the first available block and the first available inode
    int inode_num;

    if((inode_num = first_zero(disk + gd->bg_inode_bitmap*EXT2_BLOCK_SIZE, sb->s_inodes_count)) == -1){
        fprintf(stderr, "Error: no more free inodes.\n");
        exit(1);
    }
     

    int block_num;
    if((block_num = first_zero(disk + gd->bg_block_bitmap*EXT2_BLOCK_SIZE, sb->s_blocks_count)) == -1){
        fprintf(stderr, "Error: no more free blocks.\n");
        exit(1);
    }
    struct ext2_inode *inode = (struct ext2_inode *)((unsigned char *)inodes + (inode_num - 1) * 128);


    setup_inode(inode);
    inode->i_size = EXT2_BLOCK_SIZE;
    inode->i_links_count = 2;
    inode->i_blocks = 2;           // one block (2 block sectors) for storing a dir entry
    inode->i_block[0] = block_num; // Since it's just a newly created directory, it doesn't need more space than 1 block
    inode->i_mode = EXT2_S_IFDIR;

    struct ext2_dir_entry *new_de = (struct ext2_dir_entry *)malloc(sizeof(struct ext2_dir_entry));
    int new_de_rec_len;

    // Traverse until the last dir entry in <dest_parent_de> to add <new_de>
    int byte_count = 0;

    struct ext2_dir_entry *de = parent_de;
    int has_same_name = 0;
    while (byte_count < EXT2_BLOCK_SIZE){

        int predict = byte_count + de->rec_len;
        if (predict < EXT2_BLOCK_SIZE && ~has_same_name){
            //printf("DE name: %s\n", de->name);
            if (strcmp(de->name, dir_name) == 0){
                has_same_name = 1;
                break;
            }

            byte_count += de->rec_len;
            de = (struct ext2_dir_entry *)((char *)de + de->rec_len);
        }

        else if (!has_same_name){
            //printf("DE name: %s\n", de->name);
            //printf("DE rec_len: %d\n", de->rec_len);
            //printf("DE inode: %d\n", de->inode);

            // If so, it means we've reached the last file in this de, and we need to get an actual rec_len of it
            int len = de->name_len;
            int name_length = (len % 4 == 0) ? len : (len + (4 - len % 4)); // align to 4B
            int act_rec_len = (sizeof(int) + sizeof(short) + sizeof(char) * 2 + name_length);
            de->rec_len = act_rec_len;
            byte_count += act_rec_len;

            // Now, we can add the new de
            new_de_rec_len = EXT2_BLOCK_SIZE - byte_count;

            new_de->inode = inode_num;
            new_de->rec_len = new_de_rec_len;
            new_de->name_len = strlen(dir_name);
            new_de->file_type = EXT2_FT_DIR;
            strcpy(new_de->name, dir_name);
            // Make sure size of <name> field aligns to 4B
            int new_len = new_de->name_len;
            int new_de_name_length = (new_len % 4 == 0) ? new_len : (new_len + (4 - new_len % 4)); // align to 4B
            int new_de_act_len = 8 + new_de_name_length;
            for (int k = 0; k < strlen(dir_name) % 4; k++){
                strcat(new_de->name, "\0");
            }

            memcpy((disk + (parent_inode->i_block[0]) * EXT2_BLOCK_SIZE + byte_count), (char *)new_de, new_de_act_len);

            break;
        }

    
    }

    if(has_same_name){
        return EEXIST;
    }


    // Sets up the new block
    
    struct ext2_dir_entry *dir_entry = (struct ext2_dir_entry *)malloc(sizeof(struct ext2_dir_entry)+4);
    dir_entry->file_type = EXT2_FT_DIR;
    dir_entry->inode = inode_num;
    dir_entry->name_len = 1;
    dir_entry->rec_len = 12;
    strcpy(dir_entry->name, strdup("."));
    // Make sure size of <name> field aligns to 4B
    for (int k = 0; k < 3; k++)
    {
        strcat(dir_entry->name, "\0");
    }

    struct ext2_dir_entry *dir_parent_entry = (struct ext2_dir_entry *)malloc(sizeof(struct ext2_dir_entry)+4);
    dir_parent_entry->file_type = EXT2_FT_DIR;
    dir_parent_entry->inode = parent_inode_number;
    dir_parent_entry->name_len = 2;
    dir_parent_entry->rec_len = 1012;
    strcpy(dir_parent_entry->name, strdup(".."));
    // Make sure size of <name> field aligns to 4B
    for (int r = 0; r < 2; r++)
    {
        strcat(dir_parent_entry->name, "\0");
    }

    memcpy(disk + block_num * EXT2_BLOCK_SIZE, (char *) dir_entry, 12);
    memcpy(disk + block_num * EXT2_BLOCK_SIZE + 12, (char *) dir_parent_entry, 12);

   
    bitmap_one(disk + gd->bg_block_bitmap * EXT2_BLOCK_SIZE, block_num);
    bitmap_one(disk + gd->bg_inode_bitmap * EXT2_BLOCK_SIZE, inode_num);


    // Update the counts
    ((struct ext2_group_desc *)(disk + 2 * EXT2_BLOCK_SIZE))->bg_free_blocks_count -= 1;
    ((struct ext2_group_desc *)(disk + 2 * EXT2_BLOCK_SIZE))->bg_free_inodes_count -= 1;
    ((struct ext2_group_desc *)(disk + 2 * EXT2_BLOCK_SIZE))->bg_used_dirs_count += 1;
    ((struct ext2_super_block *)(disk + EXT2_BLOCK_SIZE))->s_free_blocks_count -=1;
    ((struct ext2_super_block *)(disk + EXT2_BLOCK_SIZE))->s_free_inodes_count -=1;
    parent_inode->i_links_count+=1;


    free(new_de);
    free(dir_entry);
    free(dir_parent_entry);


    return 0;
}