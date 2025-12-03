#include "common.h"
int main(int argc, char *argv[]) {
    srand(time(NULL) ^ getpid());
    // Start with 5 random ones
    for(int i=0; i<5; i++) { 
        printf("%d,%d\n", rand()%(DEFAULT_WIDTH-2)+1, rand()%(DEFAULT_HEIGHT-2)+1); 
        fflush(stdout); 
    }
    while(1) {
        // New obstacle every 2-4 seconds
        sleep((rand()%3)+2);
        printf("%d,%d\n", rand()%(DEFAULT_WIDTH-2)+1, rand()%(DEFAULT_HEIGHT-2)+1); 
        fflush(stdout);
    }
    return 0;
}
