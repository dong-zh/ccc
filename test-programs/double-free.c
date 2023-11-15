#include <stdlib.h>

int main(void)
{
	int *ptr = malloc(sizeof(int));
	free(ptr);
	free(ptr);
}
