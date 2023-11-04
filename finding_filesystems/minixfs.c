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
    if(owner != (u_int)-1){
        i_node->uid = owner;
    }
    if(group != (u_int)-1){
        i_node->gid = group;
    }

    return 0;
}

inode *minixfs_create_inode_for_path(file_system *fs, const char *path) {
    inode* i_node = get_inode(fs, path);
    if(i_node != NULL){
        return NULL;
    }
    if(!valid_filename(path)){
        return NULL;
    }
    
    int len = (int)strlen(path);
    const char *endptr = path + len;
    while (*endptr != '/') {
        endptr--;
    }

    clock_gettime(CLOCK_REALTIME, &(i_node->atim));
    clock_gettime(CLOCK_REALTIME, &(i_node->atim));


    char* filename = (char*)(endptr + 1);
    char *parent_path = malloc(endptr - path + strlen("/") + 1);
    strncpy(parent_path, path, endptr - path + 1);
    parent_path[endptr - path + 1] = '\0';
    inode *parent_node = get_inode(fs, parent_path);

    inode* ret_node = (inode*) malloc(sizeof(inode));
    init_inode(parent_node, ret_node);

    minixfs_dirent dirent;
    dirent.name = filename;
    size_t inode_number = ret_node - fs->inode_root;
    dirent.inode_num = inode_number;

    make_string_from_dirent(fs->data_root[ret_node->direct[0]].data, dirent);


    return ret_node;
}

size_t get_used_datablock_count(file_system* fs){

    size_t used_datablocks = 0;

    for(int i=0;i<(int)fs->meta->inode_count;i++){
        inode* _inode = (fs->inode_root + (i*sizeof(inode)));
        for(int j = 0; j<NUM_DIRECT_BLOCKS;j++){
            if(_inode->direct[j] > 0){
                used_datablocks++;
            }
        }
        if(_inode->indirect > 0){
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
        printf("%s", string_to_print);
        return 0;
    }

    errno = ENOENT;
    return -1;
}

ssize_t minixfs_write(file_system *fs, const char *path, const void *buf,
                      size_t count, off_t *off) {
    minixfs_create_inode_for_path(fs, path);
    return 0;
}

void print_buffer(void* buf, size_t size){
    for(size_t i=0;i<size;i++){
        printf("%c", *(char*)(buf+i));
    }
    
}

ssize_t minixfs_read(file_system *fs, const char *path, void *buf, size_t count,
                     off_t *off) {
    const char *virtual_path = is_virtual_path(path);
    if (virtual_path)
        return minixfs_virtual_read(fs, virtual_path, buf, count, off);
    
    inode* i_node = get_inode(fs, path);
    if(i_node == NULL){
        errno = ENOENT;
        return -1;
    }

    clock_gettime(CLOCK_REALTIME, &(i_node->atim));

    if (*off >= (long)i_node->size) {
        return 0;
    }

    size_t bytes_remain = i_node->size;

    buf =  (void *)calloc(i_node->size, sizeof(char));
    for (int i = 0; i < NUM_DIRECT_BLOCKS; i++) {
        data_block_number block_number = i_node->direct[i];
        if (block_number > 0) {
            data_block data_block = fs->data_root[block_number];
            size_t data_size = sizeof(data_block.data);
            if(data_size > bytes_remain){
                data_size = bytes_remain;
            }
            memcpy(buf + *off, data_block.data, sizeof(data_block.data));
            
            *off += data_size;
            bytes_remain -= data_size;
            if(bytes_remain == 0)break;
        }
    }

    data_block_number indirect_block_number = i_node->indirect;
    data_block indirect_block = fs->data_root[indirect_block_number];

    for (u_long i = 0; i < NUM_INDIRECT_BLOCKS; i++) {
        data_block_number block_number = indirect_block.data[i];
        if (block_number > 0) {
            data_block data_block = fs->data_root[block_number];
            size_t data_size = sizeof(data_block.data);
            if(data_size > bytes_remain){
                data_size = bytes_remain;
            }
            memcpy(buf + *off, data_block.data, data_size);
            *off += data_size;
            bytes_remain -= data_size;
            if(bytes_remain == 0)break;

        }
    }
    print_buffer(buf, i_node->size);
    return 0;  
}
