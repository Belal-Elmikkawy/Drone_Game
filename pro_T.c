#include "common.h"
int main(int argc, char *argv[]) {
    srand(time(NULL) ^ getpid() ^ 1234);
    for(int i=0; i<3; i++) { 
        printf("%d,%d\n", rand()%(DEFAULT_WIDTH-2)+1, rand()%(DEFAULT_HEIGHT-2)+1); 
        fflush(stdout); 
    }
    while(1) {
        sleep((rand()%4)+5);
        printf("%d,%d\n", rand()%(DEFAULT_WIDTH-2)+1, rand()%(DEFAULT_HEIGHT-2)+1); 
        fflush(stdout);
    }
    return 0;
}
