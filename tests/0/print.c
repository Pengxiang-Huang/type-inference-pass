#include <stdlib.h>
#include <stdio.h>

int main(){
	int b = 1;
	int * a = &b;
	*a = 1;
	printf("hello test 0: %d\n", *a);
	return 0;
}

