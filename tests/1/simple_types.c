#include <stdio.h>
#include <stdlib.h>

int main() {
    // 1. Allocate an int
    int *a = (int*) malloc(sizeof(int));
    *a = 10; // Store i32

    // 2. Cast to float pointer (Aliasing!)
    float *b = (float*)a;
    
    // 3. Store float (Conflict!)
    *b = 3.14f; 

    return 0;
}
