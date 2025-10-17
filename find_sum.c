#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

int
main(int argc, char *argv[])
{
  int fd;
  char *input_string;
  int i, sum = 0, num = 0, is_in_num = 0;
  
  if(argc < 2)
  {
    printf(1, "Usage: find_sum string\n");
    exit();
  }
    
  input_string = argv[1];

  for(i = 0; input_string[i] != '\0'; i++)
  {  
    char current_char = input_string[i];
    
    if(current_char >= '0' && current_char <= '9')
    {
      num = num * 10 + (current_char - '0');
      is_in_num = 1;
    } 
    else 
    {
      if(is_in_num)
      {
        sum += num;
        num = 0;
        is_in_num = 0;
      }
    }
  }
  if(is_in_num)
    sum += num;

  fd = open("result.txt", O_CREATE | O_WRONLY);
  
  if(fd < 0)
  {
    printf(1, "open file result.txt failed\n");
    exit();
  }
  printf(fd, "%d\n", sum);
  close(fd);

  exit();
}