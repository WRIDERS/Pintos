/* sort.c 
//10957

   Test program to sort a large number of integers.
 
   Intention is to stress virtual memory system.
 
   Ideally, we could read the unsorted array off of the file
   system, and store the result back to the file system! */
#include <stdio.h>

/* Size of array to sort. */
#define SORT_SIZE 3000

int
main (int argc, char **argv)
{
  /* Array to sort.  Static to reduce stack usage. */
  static int array[SORT_SIZE];


  int i, j, tmp;

  /* First initialize the array in descending order. */
  for (i = 0; i < SORT_SIZE; i++)
    array[i] = SORT_SIZE - i - 1;

  /* Then sort in ascending order. */
  for (i = 0; i < SORT_SIZE - 1; i++)
    for (j = 0; j < SORT_SIZE - 1 - i; j++)
      if (array[j] > array[j + 1])
	{
	  tmp = array[j];
	  array[j] = array[j + 1];
	  array[j + 1] = tmp;
	}
	
	i=0;
//  for(i=0;i<SORT_SIZE;i++)
//  printf ("sort exiting with code %d\n", array[i]);

  return array[0];

}
