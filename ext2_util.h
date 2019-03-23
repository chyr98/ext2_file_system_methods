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

/*The struct contains the name of each entries on the path*/
struct linked_dir
{
    char *name;
    struct linked_dir *next;
};

struct dir_entry_to_add{
    struct ext2_dir_entry *contained_in;
    struct ext2_dir_entry *new_file;
};

/*
** Determines if the string <path> only contains slashes.
*/
int only_contains_slashes(char *path)
{
    for (int i = 0; i < strlen(path); i++)
    {
        if (path[i] != '/')
        {
            return 0;
        }
    }

    return 1;
}

//return the needed length of a directory entry with give name length <name_len>
int dir_entry_len(int name_len){
    int ret = ((name_len + 8) / 4) * 4;
    if (name_len % 4 != 0){
        ret = ret + 4;
    }
    return ret;
}

/*
** Get a linked_dir struct based on <path>, 
** which is convenient for parsing an input path from user
*/
struct linked_dir *get_linked_dirs(char *path)
{

    struct linked_dir *dirs = NULL;

    while (!only_contains_slashes(path) && strlen(path) > 0)
    {
        struct linked_dir *dir = (struct linked_dir *)malloc(sizeof(struct linked_dir));
        dir->name = strdup(basename(path));
        // printf("basename at this level: %s\n", dir->name);
        dir->next = dirs;
        dirs = dir;
        path = dirname(path);
        // printf("Now path is: %s\n", path);
    }

    struct linked_dir *root_dir = (struct linked_dir *)malloc(sizeof(struct linked_dir));
    root_dir->name = strdup(".");
    root_dir->next = dirs;
    dirs = root_dir;

    return dirs;
}

/*
** Check the type of file with i_mode <mode>
*/
char i_mode_type_checker(unsigned short mode){
    mode = (mode >> 13) << 13;

    if (mode == EXT2_S_IFLNK){
        return 'l';
    }
    else if (mode == EXT2_S_IFREG){
        return 'f';
    }

    else if (mode == EXT2_S_IFDIR){
        return 'd';
    }

    else{
        // error
        return 'E';
    }
}

/*
** Returns the value of <position>th bit on the bitmap <bitmap>
*/
int bitmap_find(unsigned char *bitmap, int position){
    int ret;
    while (position > 8){
        position = position - 8;
        bitmap = bitmap + 1;
    }

    unsigned char info = *bitmap;
    ret = (info >> (position - 1)) & 1;

    int mask = 1 << (position - 1);
    info = info | mask;
    memcpy(bitmap, &info, 1);

    return ret;
}


/* 
** Finds and returns the position of first zero in a bitmap with size <bitmap_size>
** Returns -1 if no more free inodes/blocks available
*/
int first_zero(unsigned char *bitmap, int bitmap_size){
    int position = 1;
    while (position <= bitmap_size){
        if (!bitmap_find(bitmap, position)){
            return position;
        }
        position++;
    }
    return -1;
}



/* 
** Sets up the common info for a new inode.
*/
void setup_inode(struct ext2_inode *inode)
{

    inode->i_uid = 0;
    inode->i_dtime = 0;

    inode->i_gid = 0;

    inode->osd1 = 0;

    for (int i = 1; i < 15; i++)
    {
        inode->i_block[i] = 0;
    }
    inode->i_generation = 0;
    inode->i_file_acl = 0;
    inode->i_dir_acl = 0;
    inode->i_faddr = 0;
    for (int i = 0; i < 3; i++)
    {
        inode->extra[i] = 0;
    }
}

//format the path into a string like '/root/level1/file'
char *modify_path(char *path){
    char *formatted = malloc(strlen(path) + 2);
    int has_back_slash = 0;
    int j = 0;

    if (path[0] != '/'){
	formatted[j] = '/';
	j++;
    }
    for(int i = 0; i < strlen(path); i++){
	if(path[i] == '/'){
	    if(!has_back_slash){
	    	formatted[j] = path[i];
		has_back_slash = 1;
		j++;
	    }
	}
	else{
	    formatted[j] = path[i];
	    has_back_slash = 0;
	    j++;
	}
    }

    if(formatted[j-1] == '/')
	formatted[j-1] = '\0';
    else
    	formatted[j] = '\0';

    return formatted;
}

struct dir_entry_to_add *find_possible_file_in_dir(unsigned char *disk, struct ext2_inode *inode, char *name){
    struct dir_entry_to_add *ret = malloc(sizeof(struct dir_entry_to_add));

    struct ext2_dir_entry *curr_dir_entry;

    int index = 0;
    int block_visited = 0;
    while (block_visited < inode->i_blocks / 2){
        if (inode->i_block[index] != 0){
	        block_visited++;

            char *block_beginning = (char *) (disk + inode->i_block[index]*EXT2_BLOCK_SIZE);
            curr_dir_entry = (struct ext2_dir_entry *)block_beginning;

            while ((char *)curr_dir_entry - block_beginning < EXT2_BLOCK_SIZE){
                struct ext2_dir_entry *entry_in_gap = curr_dir_entry;

                while ((char *)entry_in_gap - (char *)curr_dir_entry < curr_dir_entry->rec_len){
		            if (entry_in_gap->name_len == 0)
			            break;

                    //create a string of file name of curr dir entry with null termination
                    char buff[entry_in_gap->name_len + 1];
                    memcpy(buff, entry_in_gap->name, entry_in_gap->name_len);
                    buff[entry_in_gap->name_len] = '\0';

		            //check if the file has the name we are looking for and a reasonable reclen
                    if ((!strcmp(buff, name)) && (((char *)entry_in_gap - block_beginning + entry_in_gap->rec_len) <= EXT2_BLOCK_SIZE)){
                        ret->contained_in = curr_dir_entry;
			            ret->new_file = entry_in_gap;
                        return ret;
                    }

		    

                    entry_in_gap = (struct ext2_dir_entry *)((char *)entry_in_gap + dir_entry_len(entry_in_gap->name_len));
                }

                curr_dir_entry = (struct ext2_dir_entry *)((char *)curr_dir_entry + curr_dir_entry->rec_len);
            }

        }
	    index++;

    }
    return NULL;
}

/*
** Find the dir entry in front of file with name <name> in the directory with inode <inode>
** Return the previous file's directory entry, or NULL if the file is not found.
*/
struct ext2_dir_entry *find_previous_file_in_dir(unsigned char *disk, struct ext2_inode *inode, char *name){
    struct ext2_dir_entry *curr_dir_entry;
    struct ext2_dir_entry *pre_dir_entry = NULL;

    for (int i = 0; i < inode->i_blocks / 2; i++){
        if (inode->i_block[i] != 0){
            char *block_beginning = (char *) (disk + inode->i_block[i]*EXT2_BLOCK_SIZE);
            curr_dir_entry = (struct ext2_dir_entry *)block_beginning;
            while ((char *)curr_dir_entry - block_beginning < EXT2_BLOCK_SIZE){
                //create a string of file name of curr dir entry with null termination
                char buff[curr_dir_entry->name_len + 1];
                memcpy(buff, curr_dir_entry->name, curr_dir_entry->name_len);
                buff[curr_dir_entry->name_len] = '\0';

                if (!strcmp(buff, name)){
                    return pre_dir_entry;
                }
                pre_dir_entry = curr_dir_entry;
                curr_dir_entry = (struct ext2_dir_entry *)((char *)curr_dir_entry + curr_dir_entry->rec_len);
            }

        }

    }
    return NULL;
}
/*
** Find the last dir entry in the directory with inode <inode>
*/
struct ext2_dir_entry *find_last_file_in_dir(unsigned char *disk, struct ext2_inode *inode){
    struct ext2_dir_entry *previous_dir_entry;
    struct ext2_dir_entry *curr_dir_entry;

    for (int i = 0; i < inode->i_blocks / 2; i++){
        if (inode->i_block[i] != 0){
            char *block_beginning = (char *) (disk + inode->i_block[i]*EXT2_BLOCK_SIZE);
            curr_dir_entry = (struct ext2_dir_entry *)block_beginning;
            while ((char *)curr_dir_entry - block_beginning < EXT2_BLOCK_SIZE){
                previous_dir_entry = curr_dir_entry;
                curr_dir_entry = (struct ext2_dir_entry *)((char *)curr_dir_entry + curr_dir_entry->rec_len);
            }

        }

    }
    return previous_dir_entry;
}

/*
** Finds the dir entry of file with name <name> in the directory with inode <inode>
** and return the file's directory entry, or NULL if the file is not found.
*/
struct ext2_dir_entry *find_file_in_dir(unsigned char *disk, struct ext2_inode *inode, char *name)
{
    struct ext2_dir_entry *curr_dir_entry;

    for (int i = 0; i < inode->i_blocks / 2; i++)
    {
        if (inode->i_block[i] != 0)
        {
            char *block_beginning = (char *)(disk + inode->i_block[i] * EXT2_BLOCK_SIZE);
            curr_dir_entry = (struct ext2_dir_entry *)block_beginning;
            while ((char *)curr_dir_entry - block_beginning < EXT2_BLOCK_SIZE)
            {
                //create a string of file name of curr dir entry with null termination
                char buff[curr_dir_entry->name_len + 1];
                memcpy(buff, curr_dir_entry->name, curr_dir_entry->name_len);
                buff[curr_dir_entry->name_len] = '\0';

                if (!strcmp(buff, name))
                {
                    return curr_dir_entry;
                }
                curr_dir_entry = (struct ext2_dir_entry *)((char *)curr_dir_entry + curr_dir_entry->rec_len);
            }
        }
    }
    return NULL;
}


/*
** Finds the inode number of the file given its path,
** prints out error messages if path is not available.
*/
unsigned int findpath(struct linked_dir *dirs, unsigned char *disk)
{
    //char *path_dup, *file_name;
    unsigned int inode;

    struct ext2_group_desc *gd = (struct ext2_group_desc *)(disk + 2 * EXT2_BLOCK_SIZE);
    struct ext2_inode *inodes = (struct ext2_inode *)(disk + gd->bg_inode_table * EXT2_BLOCK_SIZE);
    struct ext2_inode *curr_inode;
    struct ext2_dir_entry *curr_dir_entry;
    inode = EXT2_ROOT_INO;

    while (dirs != NULL)
    {
        curr_inode = (struct ext2_inode *)((unsigned char *)inodes + (inode - 1) * 128);
        if (i_mode_type_checker(curr_inode->i_mode) != 'd'){
            fprintf(stderr, "The file with inode %d is not a directory\n", inode);
            exit(1);
        }

        if ((curr_dir_entry = find_file_in_dir(disk, curr_inode, dirs->name)) == NULL)
        {
            return -1;
        }
        inode = curr_dir_entry->inode;
        dirs = dirs->next;
    }

    return inode;
}

/*
** Sets the <position>th bit on the bitmap <bitmap> to zero.
*/
void bitmap_zero(unsigned char *bitmap, int position)
{
    while (position > 8)
    {
        position = position - 8;
        bitmap = bitmap + 1;
    }

    unsigned char info = *bitmap;
    int mask = 1 << (position - 1);
    info = info & ~mask;
    memcpy(bitmap, &info, 1);
}

/*
** Sets the <position>th bit on the bitmap <bitmap> to one.
*/
void bitmap_one(unsigned char *bitmap, int position)
{
    while (position > 8)
    {
        position = position - 8;
        bitmap = bitmap + 1;
    }

    unsigned char info = *bitmap;
    int mask = 1 << (position - 1);
    info = info | mask;
    memcpy(bitmap, &info, 1);
}

/* 
** Determines whether or not the given <de> has an entry named the same as <filename> and is not a directoty
*/
int has_same_non_dir(struct ext2_dir_entry *de, char *filename)
{
    int byte_count = 0;

    while (byte_count < EXT2_BLOCK_SIZE)
    {
        if ((de->file_type != EXT2_FT_DIR) && (strcmp(de->name, filename) == 0))
        {
            return 1;
        }

        byte_count += de->rec_len;
        de = (struct ext2_dir_entry *)((char *)de + de->rec_len);
    }

    return 0;
}


/* 
** Counts and returns the number of free inodes/blocks in the inode/block bitmap of <gd>
*/
int free_count(unsigned char *disk, int bitmap_size, char type){
    int count = 0;
    int mask = 1;
    struct ext2_group_desc *gd = (struct ext2_group_desc *)(disk + 2 * EXT2_BLOCK_SIZE);
    for (int i = 0; i < bitmap_size; i++){
        int in_use;
        if (type == 'i'){
            in_use = (*(disk + gd->bg_inode_bitmap * EXT2_BLOCK_SIZE + (i / 8)) >> (i % 8)) & mask;
        }

        else{ // type == 'b'
            in_use = (*(disk + gd->bg_block_bitmap * EXT2_BLOCK_SIZE + (i / 8)) >> (i % 8)) & mask;
        }

        if (!in_use){
            count += 1;
        }
    }

    return count;
}

/*
** Fixes and counts the mismatch between the free inodes/blocks counts in sb and bg with their corresponding bitmaps
** Trust the bitmaps.
*/

int check_sb_gd_bitmap(unsigned char *disk){
    int diff;

    struct ext2_super_block *sb = (struct ext2_super_block *)(disk + EXT2_BLOCK_SIZE);
    struct ext2_group_desc *gd = (struct ext2_group_desc *)(disk + 2 * EXT2_BLOCK_SIZE);
    int free_inodes_count = free_count(disk, sb->s_inodes_count, 'i');
    int free_blocks_count = free_count(disk, sb->s_blocks_count, 'b');
    
    int errors_count = 0;

    if (sb->s_free_inodes_count != free_inodes_count)
    {   
        diff = abs(free_inodes_count - sb->s_free_inodes_count);
        errors_count += diff;
        sb->s_free_inodes_count = free_inodes_count;
        printf("Fixed: superblock's free inodes counter was off by %d compared to the bitmap\n", diff);
    }

    if (sb->s_free_blocks_count != free_blocks_count)
    {
        diff = abs(free_blocks_count - sb->s_free_blocks_count);
        errors_count += diff;
        sb->s_free_blocks_count = free_blocks_count;
        printf("Fixed: superblock's free blocks counter was off by %d compared to the bitmap\n", diff);
    }

    if (gd->bg_free_inodes_count != free_inodes_count)
    {
        diff = abs(free_inodes_count - gd->bg_free_inodes_count);
        errors_count += diff;
        gd->bg_free_inodes_count = free_inodes_count;
        printf("Fixed: block group's free inodes counter was off by %d compared to the bitmap\n", diff);
    }

    if (gd->bg_free_blocks_count != free_blocks_count)
    {
        diff = abs(free_blocks_count - gd->bg_free_blocks_count);
        errors_count += diff;
        gd->bg_free_blocks_count = free_blocks_count;
        printf("Fixed: block group's free blocks counter was off by %d compared to the bitmap\n", diff);
    }

    return errors_count;
}



/* 
** Determines if the given de's file_type matches its inode's i_mode, and fix it if it was prevuiously mismatched.
** Returns 0 if they are matched, 1 otherwise.
*/
int mode_ft_match(struct ext2_dir_entry *de, unsigned char *disk, struct ext2_inode *inodes, int inode_number){
    struct ext2_inode *inode = (struct ext2_inode *)((unsigned char *)inodes + (de->inode - 1) * 128);
    int shift_amount = 12;
    int i_mode = inode->i_mode >> shift_amount;

    if ((i_mode == (EXT2_S_IFLNK >> shift_amount)) && de->file_type != EXT2_FT_SYMLINK)
    {
        printf("Fixed: Entry type vs inode mismatch: inode [%d]\n", inode_number);

        de->file_type = EXT2_FT_SYMLINK;
        return 1;
    }

    if ((i_mode == (EXT2_S_IFREG >> shift_amount)) && de->file_type != EXT2_FT_REG_FILE)
    {
        printf("Fixed: Entry type vs inode mismatch: inode [%d]\n", inode_number);

        de->file_type = EXT2_FT_REG_FILE;
        return 1;
    }

    if ((i_mode == (EXT2_S_IFDIR >> shift_amount)) && de->file_type != EXT2_FT_DIR)
    {
        printf("Fixed: Entry type vs inode mismatch: inode [%d]\n", inode_number);

        de->file_type = EXT2_FT_DIR;
        return 1;
    }

    return 0;
}


/* 
** Determines if the given inode is marked as allocated in the inode bitmap
** Returns 0 if it's marked, 1 otherwise (and have it fixed before the return).
*/
int inode_marked_allocated(struct ext2_dir_entry *de, unsigned char *disk, struct ext2_inode *inodes, int inode_number)
{   
    struct ext2_group_desc *gd = (struct ext2_group_desc *)(disk + 2 * EXT2_BLOCK_SIZE);

    
    int mask = 1;

    int marked = (*(disk + gd->bg_inode_bitmap * EXT2_BLOCK_SIZE + ((inode_number-1) / 8)) >> ((inode_number-1) % 8)) & mask;

    if (!marked){
        bitmap_one((unsigned char *)(disk + gd->bg_inode_bitmap * EXT2_BLOCK_SIZE), inode_number);
        printf("Fixed: inode [%d] not marked as in-use\n", inode_number);
        return 1;
    }

    return 0;
}



/* 
** Determines if the given inode's dtime is 0
** Returns 0 if it is, 1 otherwise (and have it fixed before the return)
*/
int inode_dtime_zero(struct ext2_dir_entry *de, unsigned char *disk, struct ext2_inode *inodes, int inode_number)
{   
    // struct ext2_group_desc *gd = (struct ext2_group_desc *)(disk + 2 * EXT2_BLOCK_SIZE);
    struct ext2_inode *inode = (struct ext2_inode *) ((char *) inodes + (inode_number - 1)*128);

    int marked = inode->i_dtime == 0 ? 1 : 0;

    if (!marked){
        inode->i_dtime = 0;
        printf("Fixed: valid inode marked for deletion: [%d]\n", inode_number);
        return 1;
    }

    return 0;
}



/* 
** Checks if all blocks in used of the given inode is marked as allocated in the block bitmap, and fixes the block bitmap as required.
** Returns the number of times that data block bitmap is fixed.
*/
int block_bitmap_marked(struct ext2_dir_entry *de, unsigned char *disk, struct ext2_inode *inodes, int inode_number){   
    struct ext2_inode *inode = (struct ext2_inode *) ((char *) inodes + (inode_number - 1)*128);

    struct ext2_group_desc *gd = (struct ext2_group_desc *)(disk + 2 * EXT2_BLOCK_SIZE);

    int fixed_count = 0;
    int mask = 1;

    for(int i = 0; i < 12; i++){
        int marked;
        if (inode->i_block[i] > 0){    
            marked = bitmap_find(disk + gd->bg_block_bitmap * EXT2_BLOCK_SIZE, inode->i_block[i]);
            if (!marked){
                bitmap_one(disk + gd->bg_block_bitmap * EXT2_BLOCK_SIZE, inode->i_block[i]);
                fixed_count += 1;
        
            }
        }
        
    }
  
    // For the indirect blocks, if used
    int data_block_number = inode->i_block[12];
    int *single_indirect_data_block = (int *) (disk + (data_block_number)*EXT2_BLOCK_SIZE);
    int marked;
    for (int j = 0; j < 15; j++){

        if(single_indirect_data_block[j] > 0){
            
            marked = (*(disk + gd->bg_block_bitmap * EXT2_BLOCK_SIZE + (single_indirect_data_block[j] / 8)) >> (single_indirect_data_block[j] % 8)) & mask;
            if (!marked){
                bitmap_one(disk + gd->bg_block_bitmap * EXT2_BLOCK_SIZE, single_indirect_data_block[j]);
                fixed_count += 1;
    
            }
        }
        
    }

    if (fixed_count > 0){
        printf("Fixed: %d in-use data blocks not marked in data bitmap for inode: [%d]\n", fixed_count, inode_number);
    }
    

    
    return fixed_count;
}


/*
** General function for walking through each entries and counting, fixing cocrrupts.
*/


int traverse_and_fix(struct ext2_dir_entry *de, struct ext2_inode *inodes, unsigned char *disk, int inode_number, int (*fix_funtion_ptr)(struct ext2_dir_entry *, unsigned char *, struct ext2_inode *, int))
{
    int errors_count = 0;
    int byte_count = 0;
    struct ext2_dir_entry *curr_de = de;
    struct ext2_dir_entry *sub = (struct ext2_dir_entry *) malloc(sizeof(struct ext2_dir_entry));
    struct ext2_inode *sub_inode = (struct ext2_inode *) malloc (sizeof(struct ext2_inode));

    while (byte_count < EXT2_BLOCK_SIZE)
    {

        char curr_name[curr_de->name_len+1];
        memcpy(curr_name, curr_de->name, curr_de->name_len);
        curr_name[curr_de->name_len] = '\0';

        
        errors_count += (*fix_funtion_ptr)(curr_de, disk, inodes, curr_de->inode);
        
        // Make sure that we don't recurse on the entries visited before or currently visied
        if (curr_de->file_type == EXT2_FT_DIR && curr_de != de && strcmp(curr_name, "..")!=0 && strcmp(curr_name, ".")!=0)
        {   
            // The inode which the current entty's inode number refer to
            sub_inode = (struct ext2_inode *)((unsigned char *)inodes + (curr_de->inode - 1)*128);

            for (int i = 0; i < 12; i++){
                if (sub_inode->i_block[i] > 0){
                    sub = (struct ext2_dir_entry *) (disk + sub_inode->i_block[i]*EXT2_BLOCK_SIZE);
                    if (sub->file_type == EXT2_FT_DIR)
                        errors_count+=traverse_and_fix(sub, inodes, disk, sub->inode, fix_funtion_ptr);
                }

            }
            
        }
        
        byte_count += curr_de->rec_len;
        curr_de = (struct ext2_dir_entry *)((char *)curr_de + curr_de->rec_len);
        
    }

    return errors_count;

}





