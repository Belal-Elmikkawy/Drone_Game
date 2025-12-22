// pro_O.c - Obstacle Generator (Fixed Visibility)
#include "common.h"

int main(int argc, char *argv[]) {
    srand(time(NULL) ^ getpid());
    
    // Use DEFAULT dimensions so they always appear on the Blackboard
    int max_w = DEFAULT_WIDTH; 
    int max_h = DEFAULT_HEIGHT;

    // Initial batch: Generate 5 obstacles immediately
    for(int i=0; i<5; i++) { 
        printf("%d,%d\n", rand()%(max_w-2)+1, rand()%(max_h-2)+1); 
        fflush(stdout); 
    }

    while(1) {
        // Generate a new obstacle every 2-4 seconds
        sleep((rand()%3)+2);
        
        printf("%d,%d\n", rand()%(max_w-2)+1, rand()%(max_h-2)+1); 
        fflush(stdout);
    }
    return 0;
}
