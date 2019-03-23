#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <time.h>
#include <string.h>
#include "ext2.h"
#include "ext2_util.h"

unsigned char *disk;

void restoring_file(struct ext2_dir_entry *file, unsigned char *ib, unsigned char *bb, struct ext2_inode *inode_table){
    struct ext2_super_block *sb = (struct ext2_super_block *)(disk + EXT2_BLOCK_SIZE);
    struct ext2_group_desc *gd = (struct ext2_group_desc *)(disk + 2 * EXT2_BLOCK_SIZE);

    struct ext2_inode *curr_inode = inode_table + (file->inode - 1);

    //check if any data block is occupied
    int block_sector_count = 0;
    for (int p = 0; p < 13; p++){
        if (curr_inode->i_block[p] != 0){
             if (bitmap_find(bb, curr_inode->i_block[p])){
                return;
             }
             block_sector_count += 2;
        }

    }

    if (block_sector_count < curr_inode->i_blocks){
        int *single_indirect = (int *) (disk + (curr_inode->i_block[12]) * EXT2_BLOCK_SIZE);
        int index = 0;
        while(block_sector_count < curr_inode->i_blocks){
            if (single_indirect[index] != 0){
                 if (bitmap_find(bb, single_indirect[index])){
                     return;
                 }
                 block_sector_count += 2;
            }
	    index++;

        }

    }

    //restore the inode and data blocks
    bitmap_one(ib, file->inode);
    curr_inode->i_links_count += 1;
    curr_inode->i_dtime = 0;
    gd->bg_free_inodes_count = gd->bg_free_inodes_count - 1;
    sb->s_free_inodes_count = sb->s_free_inodes_count - 1;

    block_sector_count = 0;
    for (int p = 0; p < 13; p++){
        if (curr_inode->i_block[p] != 0){
            bitmap_one(bb, curr_inode->i_block[p]);
	        gd->bg_free_blocks_count = gd->bg_free_blocks_count - 1;
	        sb->s_free_blocks_count = sb->s_free_blocks_count - 1;
            block_sector_count += 2;
        }

    }

    if (block_sector_count < curr_inode->i_blocks){
        int *single_indirect = (int *) (disk + (curr_inode->i_block[12]) * EXT2_BLOCK_SIZE);
        int index = 0;
        while(block_sector_count < curr_inode->i_blocks){
            if (single_indirect[index] != 0){
                bitmap_find(bb, single_indirect[index]);
		        gd->bg_free_blocks_count = gd->bg_free_blocks_count - 1;
		        sb->s_free_blocks_count = sb->s_free_blocks_count - 1;
                block_sector_count += 2;
            }
	        index++;
        }

    }

}

/*
** Recursive restore all the files in <dir> and in subdirectories of <dir>
*/
void recursive_restore(struct ext2_dir_entry *dir, unsigned char *ib, unsigned char *bb, struct ext2_inode *inode_table)
{
    //skip the file if inode is occupied
    if (bitmap_find(ib, dir->inode)){
        return;
    }
    struct ext2_inode *parent_inode = inode_table + dir->inode - 1;
    for (int i = 0; i < 12; i++){

        if (parent_inode->i_block[i] != 0){

            int byte_count = 0;
            unsigned char *block_beginning = disk + parent_inode->i_block[i] * EXT2_BLOCK_SIZE;

            while(byte_count < EXT2_BLOCK_SIZE){
                struct ext2_dir_entry *curr_file = (struct ext2_dir_entry *)(block_beginning + byte_count);
                //get the name of current file with null terminator
                char file_name[curr_file->name_len + 1];
                memcpy(file_name, curr_file->name, curr_file->name_len);
                file_name[curr_file->name_len] = '\0';
                //If the file is a directory and is not <dir> or <dir>'s parent, then delete all files in this sub directory
                if (strcmp(file_name, ".") && strcmp(file_name, "..")){
                    printf("file properties: name: %s, rec_len: %d, name_len: %d\n", file_name, curr_file->rec_len, curr_file->name_len);
                    if (curr_file->file_type == EXT2_FT_DIR)
                        recursive_restore(curr_file, ib, bb, inode_table);
                    restoring_file(curr_file, ib, bb, inode_table);
                }

                byte_count += curr_file->rec_len;
            }

	    }

    }

}


int main(int argc, char **argv) {
    char *path;
    int rs_dir = 0;
    if(argc != 3) {
        if (argc != 4){
            fprintf(stderr, "Usage: %s <image file name> [-r] <path to remove>\n", argv[0]);
            exit(1);
        }
        else if (strcmp(argv[2],"-r")){
            fprintf(stderr, "Usage: %s <image file name> [-r] <path to remove>\n", argv[0]);
                exit(1);
        }
        else{
            path = argv[3];
            rs_dir = 1;
        }
    }
    else{
        path = argv[2];
    }
    int fd = open(argv[1], O_RDWR);

    disk = mmap(NULL, 128 * EXT2_BLOCK_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if(disk == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }

    struct ext2_group_desc *gd = (struct ext2_group_desc *)(disk + 2 * EXT2_BLOCK_SIZE);
    struct ext2_inode *inodes = (struct ext2_inode *) (disk + gd->bg_inode_table*EXT2_BLOCK_SIZE);

    //bitmaps
    unsigned char *bb = disk + gd->bg_block_bitmap*EXT2_BLOCK_SIZE;
    unsigned char *ib = disk + gd->bg_inode_bitmap*EXT2_BLOCK_SIZE;

    char *buff = modify_path(path);
    char *file_name;

    //separate the file name and parent's path
    char *last_separator = strrchr(buff, (int)'/');
    *last_separator = '\0';
    file_name = last_separator + 1;

    unsigned int parent_inode = 0;
    struct linked_dir *linked_dir = get_linked_dirs(buff);
    parent_inode = findpath(linked_dir, disk);


    struct ext2_inode *parent_inode_pt = inodes + (parent_inode - 1);
    struct dir_entry_to_add *restore_file;

    //try to find directory entries in gaps that has the same name with given name
    if ((restore_file = find_possible_file_in_dir(disk, parent_inode_pt, file_name)) == NULL){
        printf("Cannot restore file: directory entry is reused\n");
        exit(0);
    }

    //check if the inodes block is occupied
    if (bitmap_find(ib, restore_file->new_file->inode)){
        printf("Cannot restore file: inode is reused\n");
        exit(0);
    }

    if (restore_file->new_file->file_type == EXT2_FT_DIR){
        if(rs_dir){
            recursive_restore(restore_file->new_file, ib, bb, inodes);
        }
        else{
            fprintf(stderr, "Error: try to restore a dir file without -r flag\n");
            exit(1);
        }
    }

    restoring_file(restore_file->new_file, ib, bb, inodes);

    //restore the directory entry
    (restore_file->new_file)->rec_len = (restore_file->contained_in)->rec_len - ((char *)(restore_file->new_file) - (char *)(restore_file->contained_in));
    (restore_file->contained_in)->rec_len = (char *)(restore_file->new_file) - (char *)(restore_file->contained_in);


    return 0;
}

