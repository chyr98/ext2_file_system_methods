#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <string.h>
#include "ext2.h"
#include "ext2_util.h"
#include <errno.h>

unsigned char *disk;

int main(int argc, char **argv)
{
    if (argc != 4)
    {
        fprintf(stderr, "Usage: %s <image file name> <path to source file> <path to dest>\n", argv[0]);
        exit(1);
    }

    int fd = open(argv[1], O_RDWR);

    disk = mmap(NULL, 128 * EXT2_BLOCK_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (disk == MAP_FAILED)
    {
        perror("mmap");
        exit(1);
    }
    //printf("Get here\n");

    struct ext2_super_block *sb = (struct ext2_super_block *)(disk + EXT2_BLOCK_SIZE);
    struct ext2_group_desc *gd = (struct ext2_group_desc *)(disk + 2 * EXT2_BLOCK_SIZE);
    struct ext2_inode *inodes = (struct ext2_inode *)(disk + gd->bg_inode_table * EXT2_BLOCK_SIZE);

    char *source_path = argv[2];
    char *dest_path = strdup(argv[3]);
    char *back_up_dest_path = strdup(argv[3]);
    FILE *fp;

    // ===== Source side ===================================
    if ((fp = fopen(source_path, "r")) == NULL)
    {
        perror("fopen");
        return ENOENT;
    }

    //printf("Get here\n");
    char *dest_path_file_name = basename(strdup(back_up_dest_path));
    //printf("dest name: %s\n", dest_path_file_name); //works good,,

    char *source_file_name = basename(strdup(source_path)); // the name of the source file, if needed;

    // =====================================================

    // ===== Destination side ==============================

    // Get the path (and a linked list struct) of the parent of the destination
    char *dest_parent_path = dirname(strdup(dest_path));
    struct linked_dir *dest_parent_dirs = get_linked_dirs(dest_parent_path);

    // Get a linked list struct of the destination
    struct linked_dir *dest_dirs = get_linked_dirs(back_up_dest_path);
    //printf("Get here\n");

    // Cases:

    // -2) if <dest_parent_path> cannot be found on the disk, return ENOENT;
    // -3) if the destination is not a folder and is found on the disk which is named the same as <source_file_name>, return EEXIST.

    //ELSE,
    //  0) if the destination is not a folder and is not found on the disk (and its parent_dir is found), then we need to use basename(dest_path)
    //     as the name of the copied file, i.e., we will need to traverse dest_parent_de;
    //  1) if the destination is a folder and is found on the disk, then we need to use <source_file_name>
    //     as the name of the copied file, i.e., we will need to traverse dest_de.

    int case_number;
 
    // Get the inode of the parent directory
    int dest_parent_inode_number = findpath(dest_parent_dirs, disk);


    int dest_inode_number = findpath(dest_dirs, disk);


    if (dest_parent_inode_number < 0)
    {
        // case_number = -2;
        return ENOENT;
    }
    //printf("Get here\n");

    // Get the destination's parent directory's directory entry
    struct ext2_inode *dest_parent_inode = (struct ext2_inode *)((unsigned char *)inodes + (dest_parent_inode_number - 1) * 128);
    struct ext2_dir_entry *dest_parent_de = (struct ext2_dir_entry *)(disk + dest_parent_inode->i_block[0] * EXT2_BLOCK_SIZE);

    if (dest_inode_number > 0 && has_same_non_dir(dest_parent_de, source_file_name))
    {
        // case_number = -3
        return EEXIST;
    }
    //printf("Get here\n");

    if (dest_inode_number < 0)
    {
        case_number = 0;
    }

    // Get the destination's directory's directory entry, if it's a directory
    struct ext2_inode *dest_inode = (struct ext2_inode *)malloc(sizeof(struct ext2_inode));
    struct ext2_dir_entry *dest_de = (struct ext2_dir_entry *)malloc(sizeof(struct ext2_dir_entry));
    if (dest_inode_number > 0)
    {
        case_number = 1;
        dest_inode = (struct ext2_inode *)((unsigned char *)inodes + (dest_inode_number - 1) * 128);
        dest_de = (struct ext2_dir_entry *)(disk + dest_inode->i_block[0] * EXT2_BLOCK_SIZE);
    }

    //======================================================================

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

    // Set up the new inode
    setup_inode(inode);
    inode->i_mode = EXT2_S_IFREG;

    inode->i_links_count = 1;
    inode->i_blocks = 0;           // may need to be updated after memcpy
    inode->i_block[0] = block_num; // may need to be updated after memcpy

    // Give the copied file a new de
    struct ext2_dir_entry *new_de = (struct ext2_dir_entry *)malloc(sizeof(struct ext2_dir_entry));

    int new_de_rec_len;


    // Traverse until the last dir entry in <dest_parent_de> to add <new_de>
    int byte_count = 0;

    struct ext2_dir_entry *parent_de = (case_number == 0) ? dest_parent_de : dest_de;
    char *file_name = (case_number == 0) ? dest_path_file_name : source_file_name;
    struct ext2_inode *parent_inode = (case_number == 0) ? dest_parent_inode : dest_inode;
    //printf("Case number: %d\n", case_number);

    struct ext2_dir_entry *de = parent_de;
    while (byte_count < EXT2_BLOCK_SIZE)
    {

        int predict = byte_count + de->rec_len;
        if (predict < EXT2_BLOCK_SIZE)
        {
            //printf("DE name: %s\n", de->name);

            byte_count += de->rec_len;
            de = (struct ext2_dir_entry *)((char *)de + de->rec_len);
        }

        else
        {

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
            new_de->name_len = strlen(file_name);
            new_de->file_type = EXT2_FT_REG_FILE;
            strcpy(new_de->name, file_name);
            // Make sure size of <name> field aligns to 4B
            int new_len = new_de->name_len;
            int new_de_name_length = (new_len % 4 == 0) ? new_len : (new_len + (4 - new_len % 4)); // align to 4B
            int new_de_act_len = 8 + new_de_name_length;
            for (int k = 0; k < strlen(file_name) % 4; k++)
            {
                strcat(new_de->name, "\0");
            }

            //*(disk + (parent_inode->i_block[0]) * EXT2_BLOCK_SIZE + byte_count) = *((s *)new_de);
            memcpy((disk + (parent_inode->i_block[0]) * EXT2_BLOCK_SIZE + byte_count), (char *)new_de, new_de_act_len);

            break;
        }
    }

    // Sets up the new block
    char *block = (char *)(disk + (block_num)*EXT2_BLOCK_SIZE);
    bitmap_one(disk + gd->bg_block_bitmap * EXT2_BLOCK_SIZE, block_num);

    // Get the size (unit: byte) of the copied file
    fseek(fp, 0, SEEK_END);
    long int total_bytes = ftell(fp);
    inode->i_size = total_bytes;
    int blocks_needed = total_bytes / EXT2_BLOCK_SIZE;
    int blocks_used = 0;
    if (total_bytes % EXT2_BLOCK_SIZE > 0)
    {
        blocks_needed += 1;
    }

    fseek(fp, 0, SEEK_SET);

    int *single_indirect_data_block;
    // if block_completed==EXT2_BLOCK_SIZE, then the current block has been fully filled, need to find another to go on filling


    // <blocks_used> = blocks actually used -1, which can be an index into the i_block array
    int bytes_completed = 0;

    for (long int i = 0; i < total_bytes; i++)
    {
        // <blocks_used> = blocks actually used -1, which can be an index into the i_block array
        if (bytes_completed == EXT2_BLOCK_SIZE)
        {

            if((block_num = first_zero(disk + gd->bg_block_bitmap*EXT2_BLOCK_SIZE, sb->s_blocks_count)) == -1){
                fprintf(stderr, "Error: no more free blocks.\n");
                exit(1);
            }
            bitmap_one(disk + gd->bg_block_bitmap * EXT2_BLOCK_SIZE, block_num);
            blocks_used += 1;
            block = (char *)(disk + (block_num)*EXT2_BLOCK_SIZE);


            if (blocks_used > 12)
            {
                // Need to use single-indirect blocks now
                *(single_indirect_data_block + (blocks_used -13)) = block_num;
            }

            else if (blocks_used == 12)
            {   
                i-=1; // accounts for one char copying which should not be done now
                inode->i_block[12] = block_num;  
                int data_block_number = inode->i_block[12];
                
                single_indirect_data_block = (int *) (disk + (data_block_number)*EXT2_BLOCK_SIZE);
                *single_indirect_data_block = block_num;
                blocks_needed += 1;
            }

            else{
                // have not yet used up the direct blocks            
                inode->i_block[blocks_used] = block_num;
            }

            bytes_completed = 0;
        }

        if (blocks_used != 12)
        {
            char buffer[2];

            if (fread(buffer, 1, 1, fp) != 1)
            {
                perror("fread");
            }

            memcpy(block, buffer, 1);
            block += 1;
            bytes_completed += 1;
        }

        else
        {
            bytes_completed = EXT2_BLOCK_SIZE;
        }
    }

    inode->i_blocks = blocks_needed * 2;

    // Update the bitmap for inodes
    bitmap_one(disk + gd->bg_inode_bitmap * EXT2_BLOCK_SIZE, inode_num);

    // Update the counts
    ((struct ext2_group_desc *)(disk + 2 * EXT2_BLOCK_SIZE))->bg_free_blocks_count -= blocks_needed;
    ((struct ext2_group_desc *)(disk + 2 * EXT2_BLOCK_SIZE))->bg_free_inodes_count -= 1;

    ((struct ext2_super_block *)(disk + EXT2_BLOCK_SIZE))->s_free_blocks_count -= blocks_needed;
    ((struct ext2_super_block *)(disk + EXT2_BLOCK_SIZE))->s_free_inodes_count -= 1;


    fclose(fp);


    return 0;
}