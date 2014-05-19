#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ctype.h>
#include <math.h>
#include <stdbool.h>

#define __NR_xconcat	349	/* our private syscall number */

struct myargs{
const char *outfile;
const char **infiles;
unsigned int infile_count;
int oflags;
mode_t mode;
unsigned int flags;
};

int isChar(char * optarg){	
	int i=0; 
	
	for(i=0 ; i<strlen(optarg) ; i++){
		if(!isdigit(optarg[i])){ //check if a digit in mode not number
			return 0;
		}
	}
	return -1;
}

int main(int argc, char *argv[]){
	int rc;
	int tc=0, ta=0, te=0, tt=0; //Variables store values of oflags like: tc 					    stores O_CREAT, ta stores O_APPEND
	bool tA = false, tP = false, tN = false;
	struct myargs args;
	int i=0, j=0;
	char ch;
	static const char *opString = "acteANPm:h"; // Values of allowed flags	
	
	args.flags = 0x00; //Default flags value
	args.mode = 0550; // Default value of mode	

	if(argc >= 2){	
		while((ch=getopt(argc, argv, opString))!=-1){
			switch(ch){
			case 'a': //O_APPEND
			{
				ta=O_APPEND;
				break;
			}
			case 'c': //O_CREAT
			{
				tc=O_CREAT;
				break;
			}
			case 't': //O_TRUNC
			{
				tt=O_TRUNC;									break;
			}
			case 'e': //O_EXCL
			{											te=O_EXCL;
				break;
			}	
			case 'A': // Atomic mode Extra Credit
			{
				tA = true;
				break;
			}
			case 'N': // Return Bumber of files
			{
				tN = true;
				break;
			}
			case 'P': // Return percentage
			{	
				tP = true;
				break;
			}
			case 'm': // Specify file permissions
			{
				args.mode=strtol(optarg,NULL,0);
				if(strlen(optarg)!=3 || optarg==""|| optarg==
 				     NULL|| args.mode<0 || args.mode>777 ||
 				strchr(optarg,'.')!=NULL || isChar(optarg)==0){

					perror("Invalid parameters for mode\n");					exit(1);
				}
					args.mode=strtol(optarg,NULL,8);
					break;
			}
			case 'h': //Short usage string
			{
				printf("\n\nFollowing program copies the functio					nality of the system call cat and concat					inates multiple files to a single file w					ith some user added variations.\n\n Synt					ax to execute the program :  ./xhw1 [fla					gs] outfile infile1 infile2... infile(n) 					\n\n For flags you can choose the follow					ing options :\n -a: Append: Append mode 					\n -c: Create mode \n -t: Truncate mode 					\n -e: EXCL mode \n -A: Atomic concat mo					de \n -N: return no of files \n -P: retu					rns percentage of data written out \n -m					 ARG, here ARG is the permission \n -h: 					print usage detail string \n\n You can c					hoose zero,one or multiple flags accordi					ng to your requirements.\n\n ");
					exit(1);
					break;
			}
			case '?': //Error on unknown command line arguments
			{
				errno = EINVAL;
				perror("Invalid flag entered !!! \n");
				exit(1);
			}
			default:				
			{
	 			break;
			}	
			}
		}
		
		if((argc - optind) >= 2){
			args.oflags = ta | tc | tt | te; // get multiple values 								     for oflags
			if((ta+tc+tt+te)==0){ // set default value for oflags if 							 no value given by user
				args.oflags = O_CREAT;
			}
		
			args.infile_count = 0;
			args.outfile = argv[optind];		
			args.infiles = malloc((argc-optind-1)*sizeof(char *));
								     optind++;
			//flags value computation start
			if(tA && tN && tP){
				errno = EINVAL;
				perror("A,N,P cannot be used together \n");
				exit(1);
			}
			if(tA && tN){
				args.flags = 0x05;
			}
			else if(tA && tP){
				args.flags = 0x06;
			}
			else if(tN && tP){
				errno = EINVAL;
				perror("N,P cannot be used together \n");
				exit(1);
			}
			else if(tN){
				args.flags = 0x01;
			}
			else if(tP){
				args.flags = 0x02;
			}
			else if(tA){
				args.flags = 0x04;
			}
			//flag value computation end
 
			for(i = optind,j = 0 ; i < argc ; i++,j++){// Put input 							       files into args
				args.infiles[j]=argv[i];							args.infile_count++;
			}

			if(args.infile_count > 20){
				errno = EINVAL;
				perror("Number of input files cannot be greater"								"than 20 \n");
				exit(1);
			}
	
			void *param = (void *)&args;
			rc = syscall(__NR_xconcat, param, sizeof(args)); //call 		    syscall with user specified argw wrapped into param pointer

			if (rc >= 0){
				printf("%d\n", rc);	
			} 
			else{		
				printf("syscall returned%d (errno=%d)\n", rc ,  									errno);
				perror(""); 
			}	
		}
		else{
			errno = EINVAL;
			perror("");
		}
	}
	else{
		errno = EINVAL;
		perror("");			        }
	exit(rc);
}
