#include "common.h"

int main(int argc, char *argv[]) {
    register_process("Targets");
    setup_watchdog_monitor("Targets");

    srand(time(NULL) ^ getpid() ^ 999);
    int max_w = DEFAULT_WIDTH;
    int max_h = DEFAULT_HEIGHT;
    
    for(int i=0; i<3; i++) { 
        printf("%d,%d\n", rand()%(max_w-2)+1, rand()%(max_h-2)+1); 
        fflush(stdout); 
    }

    while(1) {
        set_status("Sleeping");
        sleep((rand()%4)+4);
        
        set_status("Generating Target");
        printf("%d,%d\n", rand()%(max_w-2)+1, rand()%(max_h-2)+1); 
        fflush(stdout);
    }
    return 0;
}
