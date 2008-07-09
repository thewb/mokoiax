

 /* File : example.c */
 
 #include <time.h>
 #include <stdio.h>
 #include <strings.h>

 double My_variable = 3.0;

 int main(int argc, char *argv) {
	printf("zomgz hax\n");	
	return 0;
 }
 
 int fact(int n) {
     if (n <= 1) return 1;
     else return n*fact(n-1);
 }
 
 int my_mod(int x, int y) {
     return (x%y);
 }
 	
 char *get_time()
 {
     time_t ltime;
     time(&ltime);
     return ctime(&ltime);
 }

