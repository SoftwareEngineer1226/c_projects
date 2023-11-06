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
    inode* inod = minixfs_create_inode_for_path(fs, "/dog2.png");
    if(inod == NULL){
        printf("Inode creation is failed.");
        //return -1;
    }
*/
    

    off_t off = 0;

    char *str = "Hello World!";

    minixfs_write(fs, "/newfile", str, 12, &off);

    FILE *file = fopen("goodies/dog.png", "rb");



    fseek(file, 0, SEEK_END); // Seek to the end of the file
    long file_size = ftell(file); // Get the current file position
    rewind(file); // Reset the file position to the beginning

    size_t buf_size = KILOBYTE;


    char buffer[buf_size];
    off = 0;
    while(off < file_size){
        long elements_read = fread(buffer, 1, buf_size, file);
        if((long)off + elements_read> file_size){
            elements_read = file_size - off;
        }

        minixfs_write(fs, "/dog9.png", buffer, elements_read, &off);
        off += elements_read;
    }
    
    fclose(file);

    close_fs(&fs);

}
