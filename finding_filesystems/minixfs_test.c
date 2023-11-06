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
    
    long off = 0;


    char *str = "Hello World!";

    minixfs_write(fs, "/newfile", str, 12, &off);

    FILE *file = fopen("goodies/dog.png", "rb");



    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    rewind(file);
    off = 0;
    size_t buf_size = 512;
    char buffer[buf_size];

    while (off < file_size) {
        size_t elements_to_read = (size_t)((file_size - off < (long)buf_size) ? file_size - off : buf_size);
        size_t elements_read = fread(buffer, 1, elements_to_read, file);
        if (elements_read != elements_to_read) {
            perror("Error reading from the input file");
            fclose(file);
            return 1;
        }

        if (minixfs_write(fs, "/dog16.png", buffer, elements_read, &off) < 0) {
            perror("Error writing to minixfs");
            fclose(file);
            return 1;
        }
        printf("off : %zu\n", off);
    }
    fclose(file);

    close_fs(&fs);

}
