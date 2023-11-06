#include "minixfs.h"
#include "minixfs_utils.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main() {
    file_system* fs = open_fs("test.fs");


    //off_t off = 0;

    inode* inod = minixfs_create_inode_for_path(fs, "/dog2.png");
    if(inod == NULL){
        printf("Inode creation is failed.");
        //return -1;
    }

    

    off_t off = 0;

    char *str = "Hello World!";

    minixfs_write(fs, "/newfile", str, 12, &off);

    FILE *file = fopen("goodies/dog.png", "rb");



    fseek(file, 0, SEEK_END); // Seek to the end of the file
    long file_size = ftell(file); // Get the current file position
    rewind(file); // Reset the file position to the beginning

    size_t total_size = file_size;


    char buffer[total_size];
    

    size_t elements_read = fread(buffer, 1, total_size, file);

    if (elements_read != (size_t)total_size) {
        if (feof(file)) {
            printf("End of file reached or not enough data.\n");
        } else if (ferror(file)) {
            perror("Error reading from file");
        }
    }
    off = 0;

    minixfs_write(fs, "/dog2.png", buffer, total_size, &off);


    fclose(file);

    close_fs(&fs);



   


}
