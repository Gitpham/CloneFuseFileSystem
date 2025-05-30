#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "wfs.h"
#include <unistd.h> 
#include <time.h>
//This C program initializes a file to an empty filesystem. I.e. to the state, where the filesystem can be mounted and other files and directories can be created under the root inode. The program receives three arguments: the raid mode, disk image file (multiple times), the number of inodes in the filesystem, and the number of data blocks in the system. The number of blocks should always be rounded up to the nearest multiple of 32 to prevent the data structures on disk from being misaligned. For example:

//./mkfs -r 1 -d disk.img -d disk1.img -i 32 -b 200
//initializes all disks (disk1 and disk2) to an empty filesystem with 32 inodes and 224 data blocks. The size of the inode and data bitmaps are determined by the number of blocks specified by mkfs. If mkfs finds that the disk image file is too small to accommodate the number of blocks, it should exit with return code -1. mkfs should write the superblock and root inode to the disk image./


static int disk_order = 1;


int init_disks(int * disks, int num_disks, int num_inodes, int num_datablocks, int raid_mode){

	time_t t_result;
	for(int i = 0; i < num_disks; i++){
		// INIT THE SUPER BLOCK 
		struct wfs_sb * superblock = malloc(sizeof(struct wfs_sb)); 	
		superblock->num_inodes = num_inodes;
		superblock->num_data_blocks = num_datablocks;
		superblock->i_bitmap_ptr = sizeof(struct wfs_sb);

        int i_bitmap_size = num_inodes /8;
        int d_bitmap_size = num_datablocks /8;
		superblock->d_bitmap_ptr = superblock->i_bitmap_ptr + (i_bitmap_size);

		//inode offset is a multiple of 512
		off_t inode_offset = sizeof(struct wfs_sb) + (i_bitmap_size) + (d_bitmap_size);
		int remainder = inode_offset % 512;
		if(remainder != 0) inode_offset = inode_offset + (512 - remainder);
		superblock->i_blocks_ptr = inode_offset;

		off_t datablocks_offset = inode_offset + (512 * num_inodes);
		superblock->d_blocks_ptr = datablocks_offset;	

		superblock->raid_mode = raid_mode;
		superblock->total_disks = num_disks;
		// Set disk number in order and increment for next disk
		superblock->disk_order = disk_order;
		disk_order++;

		
		
        //INIT THE ROOT DIR.
		struct wfs_inode * root_inode = malloc(sizeof(struct wfs_inode));
		root_inode->num = 0;
		root_inode->mode = S_IRWXU | S_IFDIR;
		root_inode->uid = getuid();
		root_inode->gid = getgid();
		root_inode->size = 0;
		root_inode->nlinks = 1;
		for(int i =0;i < N_BLOCKS;i++) {
			root_inode->blocks[i] = -1;
		}

		
		t_result = time(NULL);
		root_inode->atim = t_result; 
		root_inode->mtim = t_result;
		root_inode->ctim = t_result; 

		if(write(disks[i], superblock, sizeof(struct wfs_sb)) == -1){
			printf("failed to write superblock to disk[%d]: %d\n", i, disks[i]);
			free(superblock);
			free(root_inode);
			exit(-1);
		};
		
		if(lseek(disks[i], superblock->i_blocks_ptr, SEEK_SET) == -1){
			printf("failed to lseek()\n");
			free(superblock);
			free(root_inode);
			exit(-1);
		}	
        
        if(write(disks[i], root_inode, sizeof(struct wfs_inode)) == -1){ printf("failed to write root_inode to disk[%d]: %d\n", i, disks[i]);
			free(superblock);
			free(root_inode);
			exit(-1);
		};
		

		if(lseek(disks[i], superblock->i_bitmap_ptr, SEEK_SET) == -1){
			printf("failed to lseek()\n");
			free(superblock);
			free(root_inode);
			exit(-1);
		}	

        unsigned char i_bitmap[i_bitmap_size];
        for(int i = 0; i < i_bitmap_size; i++){
            i_bitmap[i]= 0x00;
        }
        i_bitmap[0] = 0x01;

        if(write(disks[i], &i_bitmap, sizeof(char) * i_bitmap_size)  == -1){ 
            printf("failed to write bitmap to disk[%d]: %d\n", i, disks[i]);
			free(superblock);
			free(root_inode);
			exit(-1);
		};

		int file_size = lseek(disks[i], 0, SEEK_END);
		if(file_size <= ((512 * num_datablocks) + (512 * num_inodes))){
			printf("too many blocks");
			free(superblock);
			free(root_inode);
			exit(-1);
		}	

		close(disks[i]);	
        free(superblock);
        superblock = NULL;
        free(root_inode);
        root_inode = NULL;			
	}

	return 0;	

}

int main(int argc, char *argv[])
{
 
	if (argc < 3){
		exit(-1);
	}
	
	int raid_mode = -1;
	int num_disks = 0;
	int * disks = NULL;
	int num_inodes = -1;
	int num_datablocks = -1;
	for(int i = 0; i < argc; i++){

		if(argv[i][0] == '-'){

			if(argv[i][1] == 'r'){
				if(raid_mode != -1){
					printf("multiple arguments for raid\n");
					exit(-1);
				}		
				raid_mode = atoi(argv[i + 1]);
				i++;
				continue;
			}
			
			if(argv[i][1] == 'd'){
				num_disks++;
				int fd = open(argv[++i], O_RDWR);
				if(fd == -1){
					printf("failed to open file %s\n", argv[i]);
					free(disks);
					exit(-1);
				}	

				if(disks == NULL) disks = malloc(sizeof(int));
				int * temp = realloc(disks, sizeof(int) * num_disks);
				if(temp == NULL){
					printf("failed to realloc\n");
                    free(temp);
					exit(-1);
				}
				disks = temp;
				disks[num_disks-1] = fd;
				continue;
			}
		
			if(argv[i][1] == 'i'){
				
				if(num_inodes != -1){
					printf("multiple arguments for num_inodes\n");
					exit(-1);
				}		
				num_inodes = atoi(argv[i + 1]);
				int remainder = num_inodes % 32;
				if(remainder !=0) num_inodes = (32 -remainder) + num_inodes;
				i++;
				continue;

			}
	
			if(argv[i][1] == 'b'){
				
				if(num_datablocks != -1){
					printf("multiple arguments for num_datablocks\n");
					free(disks);
					exit(-1);
				}		
				num_datablocks = atoi(argv[i + 1]);
				int remainder = num_datablocks % 32;
				if(remainder !=0) num_datablocks = (32 -remainder) + num_datablocks;
				i++;
				continue;

			}
		}
	}	

	if(num_disks < 2){
		printf("need at least 2 disks");
		free(disks);
		exit(1);
	}

	if((raid_mode < 0) | (raid_mode > 1)){
		printf("invalid raid mode");
		free(disks);
		exit(1);
	}

	init_disks(disks, num_disks, num_inodes, num_datablocks, raid_mode);
    free(disks);
	exit(0);
}
