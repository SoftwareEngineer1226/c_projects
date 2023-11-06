#include "minixfs.h"
#include "minixfs_utils.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/**
 * Virtual paths:
 *  Add your new virtual endpoint to minixfs_virtual_path_names
 */
char *minixfs_virtual_path_names[] = {"info", /* add your paths here*/};

/**
 * Forward declaring block_info_string so that we can attach unused on it
 * This prevents a compiler warning if you haven't used it yet.
 *
 * This function generates the info string that the virtual endpoint info should
 * emit when read
 */
static char *block_info_string(ssize_t num_used_blocks) __attribute__((unused));
static char *block_info_string(ssize_t num_used_blocks) {
    char *block_string = NULL;
    ssize_t curr_free_blocks = DATA_NUMBER - num_used_blocks;
    asprintf(&block_string,
             "Free blocks: %zd\n"
             "Used blocks: %zd\n",
             curr_free_blocks, num_used_blocks);
    return block_string;
}

// Don't modify this line unless you know what you're doing
int minixfs_virtual_path_count =
    sizeof(minixfs_virtual_path_names) / sizeof(minixfs_virtual_path_names[0]);

int minixfs_chmod(file_system *fs, char *path, int new_permissions) {
    inode* i_node = get_inode(fs, path);
    if(i_node == NULL){
        errno = ENOENT;
        return -1;
    }
    clock_gettime(CLOCK_REALTIME, &(i_node->ctim));

    int mask = (1 << 2) - 1;
    i_node->mode = (new_permissions << 2) | (i_node->mode & mask);
    return 0;
}

int minixfs_chown(file_system *fs, char *path, uid_t owner, gid_t group) {
    inode* i_node = get_inode(fs, path);
    if(i_node == NULL){
        errno = ENOENT;
        return -1;
    }
    clock_gettime(CLOCK_REALTIME, &(i_node->ctim));

    if(owner != (u_int)-1){
        i_node->uid = owner;
    }
    if(group != (u_int)-1){
        i_node->gid = group;
    }

    return 0;
}
char* get_offset_char(file_system*fs, inode* _inode, off_t offset){
    int index_of_db = offset/(16 * KILOBYTE);
    int db_offset = offset%(16 * KILOBYTE);
    data_block *db = &(fs->data_root[_inode->direct[index_of_db]]);
    return &(db->data[db_offset]);
}

inode *minixfs_create_inode_for_path(file_system *fs, const char *path) {
    inode* i_node = get_inode(fs, path);
    if(i_node != NULL){
        return NULL;
    }
    
    
    int len = (int)strlen(path);
    const char *endptr = path + len;
    while (*endptr != '/') {
        endptr--;
    }



    char* filename = (char*)(endptr + 1);
    if(!valid_filename(filename)){
        return NULL;
    }
    char *parent_path = malloc(endptr - path + strlen("/") + 1);


    strncpy(parent_path, path, endptr - path + 1);
    parent_path[endptr - path + 1] = '\0';


    inode *parent_node = get_inode(fs, parent_path);
    if(parent_node == NULL){
        free(parent_path);

        return NULL;
    }

    inode_number ret_node_num = first_unused_inode(fs);
    inode* ret_node = &(fs->inode_root[ret_node_num]);
    init_inode(parent_node, ret_node);
    char* block = get_offset_char(fs, parent_node, parent_node->size);

    minixfs_dirent dirent;
    dirent.name = (char*)filename;
    dirent.inode_num = ret_node_num;
    
    make_string_from_dirent(block, dirent);
    parent_node->size+= FILE_NAME_ENTRY;
    free(parent_path);
    return ret_node;
}

size_t get_used_datablock_count(file_system* fs){

    size_t used_datablocks = 0;

    for(int i=0;i<(int)fs->meta->inode_count;i++){
        if(get_data_used(fs, i) == 1){
            used_datablocks++;
        }
    }
    return used_datablocks;
}

ssize_t minixfs_virtual_read(file_system *fs, const char *path, void *buf,
                             size_t count, off_t *off) {
    if (!strcmp(path, "info")) {
        size_t used = get_used_datablock_count(fs);
        char* string_to_print = block_info_string(used);
        ssize_t size = 0;
        int line = 0;
        while(1){
            if(*(string_to_print+size) == '\n') line++;
            size++;
            if(line == 2) break;
        }
        size -= *off;
        memcpy(buf, string_to_print + *off, size);
        *off += size;
        return size;
    }

    errno = ENOENT;
    return -1;
}
data_block* get_nth_indirect_block(file_system* fs, inode* _inode, int n){

    data_block_number indirect_block_number = _inode->indirect;
    data_block indirect_block = fs->data_root[indirect_block_number];
    data_block_number* ind_block_nums = (data_block_number*) indirect_block.data;
    if(ind_block_nums[n] == UNASSIGNED_NODE) return NULL;
    return &(fs->data_root[ind_block_nums[n]]);
    
}

ssize_t minixfs_write(file_system *fs, const char *path, const void *buf,
                      size_t count, off_t *off) {
    
    
    inode* _inode = get_inode(fs, path);

    if(_inode == NULL){
        _inode = minixfs_create_inode_for_path(fs, path);
    }
    
    size_t block_size = KILOBYTE * 16;
    size_t max_file_size = (NUM_DIRECT_BLOCKS + NUM_DIRECT_BLOCKS) * block_size;

    if (*off + count > max_file_size) {
        errno = ENOSPC;
        return -1;
    }
    clock_gettime(CLOCK_REALTIME, &(_inode->atim));
    clock_gettime(CLOCK_REALTIME, &(_inode->mtim));

    
    size_t block_index = *off / block_size;
    size_t block_offset = *off % block_size;

    long bytes_left = count;
    size_t bytes_to_write_per_time = bytes_left;
    size_t bytes_written = 0;

    if (block_offset + count > block_size) {
        bytes_to_write_per_time = block_size - block_offset;
    }
    if (*off + count > _inode->size) {
        _inode->size = *off + count;
        if(_inode->size > max_file_size){
            _inode->size = max_file_size;
        }
    }


    while(bytes_left > 0){
        
        block_index = *off / block_size;
        block_offset = *off % block_size;
        if (block_offset + bytes_left > block_size) {
            bytes_to_write_per_time = block_size - block_offset;
        }
        data_block *db = NULL;
        if(block_index >= NUM_DIRECT_BLOCKS){

            block_index -= NUM_DIRECT_BLOCKS;
            if(_inode->indirect == UNASSIGNED_NODE){
                inode_number res = add_single_indirect_block(fs, _inode);
                if(res == UNASSIGNED_NODE){
                    errno = ENOSPC;
                    return -1;
                }
            }

            data_block* tempdb = get_nth_indirect_block(fs, _inode, block_index);

            if(tempdb == NULL){
                data_block_number dbnum_tmp = add_data_block_to_indirect_block(fs, (data_block_number*)fs->data_root[_inode->indirect].data);
                if(dbnum_tmp == UNASSIGNED_NODE){
                    errno = ENOSPC;
                    return -1;
                }
                db = &fs->data_root[dbnum_tmp];
            }
            else{
                db = tempdb;
            }

        }
        else{

            if(_inode->direct[block_index] == UNASSIGNED_NODE){
                data_block_number dbnum_tmp = add_data_block_to_inode(fs, _inode);
                if(dbnum_tmp == -1){
                    errno = ENOSPC;
                    return -1;
                }
                db = &fs->data_root[dbnum_tmp];
            }
            else{
                db = &fs->data_root[_inode->direct[block_index]];
                

            }
        }
        memcpy(db->data + block_offset, buf + bytes_written, bytes_to_write_per_time);


        *off += bytes_to_write_per_time;
        bytes_left -= bytes_to_write_per_time;
        bytes_written += bytes_to_write_per_time;
        bytes_to_write_per_time = bytes_left;

    }


    return bytes_written;
}



ssize_t minixfs_read(file_system *fs, const char *path, void *buf, size_t count,
                     off_t *off) {
    const char *virtual_path = is_virtual_path(path);
    if (virtual_path)
        return minixfs_virtual_read(fs, virtual_path, buf, count, off);
    
    inode* _inode = get_inode(fs, path);
    if(_inode == NULL){
        errno = ENOENT;
        return -1;
    }
    
    
    clock_gettime(CLOCK_REALTIME, &(_inode->atim));

    if (*off >= (long)_inode->size) {
        return 0;
    }

    size_t block_size = KILOBYTE * 16;
    
    size_t bytes_remain = count;
    if (*off + bytes_remain > _inode->size) {
        bytes_remain = _inode->size - *off;
    }

    size_t block_index = *off / block_size;
    size_t block_offset = *off % block_size;
    size_t bytes_read = 0;
    
    while (bytes_remain > 0) {


        block_index = *off / block_size;
        block_offset = *off % block_size;
        data_block *db = NULL;


        if(block_index >= NUM_DIRECT_BLOCKS){
            block_index -= NUM_DIRECT_BLOCKS;
            db = get_nth_indirect_block(fs, _inode, block_index);
        }
        else{
            db = &fs->data_root[_inode->direct[block_index]];
        }        
        size_t data_size = 16 * KILOBYTE - block_offset;
        if(data_size > bytes_remain){
            data_size = bytes_remain;
        }
        memcpy(buf + bytes_read, db->data + block_offset, data_size);
        *off += data_size;
        bytes_remain -= data_size;
        bytes_read += data_size;
                
    }

    return bytes_read;  
}
