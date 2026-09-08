#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

int main() {
    setuid(0);
    
    FILE *file = fopen("flag.txt", "r");
    if (file == NULL) {
        perror("Error opening flag.txt");
        return 1;
    }
    
    char flag[256];
    if (fgets(flag, sizeof(flag), file) == NULL) {
        perror("Error reading flag.txt");
        fclose(file);
        return 1;
    }
    
    fclose(file);
    printf("%s", flag);
    return 0;
}