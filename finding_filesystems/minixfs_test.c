#include "minixfs.h"
#include "minixfs_utils.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main() {
    file_system* fs = open_fs("test.fs");


    //off_t off = 0;
/*
    inode* inod = minixfs_create_inode_for_path(fs, "/abcdef");
    if(inod == NULL){
        printf("Inode creation is failed.");
        return -1;
    }
*/
    

    off_t off = 0;

    char *str = "Hello World!";

    minixfs_write(fs, "/newfile", str, 12, &off);

    close_fs(&fs);

}
