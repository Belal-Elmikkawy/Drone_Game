// pro_T.c - Target Generator (Fixed Visibility)
#include "common.h"

int main(int argc, char *argv[]) {
    srand(time(NULL) ^ getpid() ^ 999);

    int max_w = DEFAULT_WIDTH;
    int max_h = DEFAULT_HEIGHT;
    
    // Initial batch: Generate 3 targets immediately
    for(int i=0; i<3; i++) { 
        printf("%d,%d\n", rand()%(max_w-2)+1, rand()%(max_h-2)+1); 
        fflush(stdout); 
    }

    while(1) {
        // Generate a new target every 4-7 seconds
        sleep((rand()%4)+4);
        
        printf("%d,%d\n", rand()%(max_w-2)+1, rand()%(max_h-2)+1); 
        fflush(stdout);
    }
    return 0;
}
