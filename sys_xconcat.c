#include <linux/linkage.h>
#include <linux/moduleloader.h>
#include <linux/buffer_head.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>

//#define EXTRA_CREDIT

struct myargs{
__user const char *outfile;
__user const char **infiles;
unsigned int infile_count;
int oflags;
mode_t mode;
unsigned int flags;
}; 

int checkFile(const char *filename){
// Function checks a file for various errors like: cannot open, cannot read
	struct file *filp_read = NULL;

	filp_read = filp_open(filename,O_RDONLY,0);
	if(!filp_read || IS_ERR(filp_read)){
		printk(KERN_ALERT "No such file! %s\n",filename);
		return -ENOENT;
	}
	if(!filp_read->f_op->read){
		printk(KERN_ALERT "%s donot have read permission !\n",filename);
		return -EACCES;
	}
	if(filp_read != NULL){
		filp_close(filp_read,NULL);
	}

	return 0;
}

asmlinkage extern long (*sysptr)(void *arg,unsigned int length);

int wrapfs_read_file(const char *filename, void *buf, int len, int pos){
//This function reads file data to a buffer and returns number of bytes read 
	struct file *filp;
	mm_segment_t oldfs;
	int bytes;

// Check for partial read in case files gets corrupted before reading start
	filp=filp_open(filename,O_RDONLY,0);

	if(!filp || IS_ERR(filp)){
		printk(KERN_ALERT "No such file! %s\n",filename);
		bytes = -ENOENT;
		goto end_read;
	}

	if(!filp->f_op->read){
		printk(KERN_ALERT "%s donot have read permission !\n",filename);
		bytes = - EACCES;
		goto end_read;
	}
	// partial read check end 
	filp->f_pos=pos;
	oldfs=get_fs();
	set_fs(KERNEL_DS);
	bytes = filp->f_op->read(filp,buf,len,&filp->f_pos);
	set_fs(oldfs);

	if(bytes==0){
		printk(KERN_ALERT "No data available in file: %s \n",(char *)
								filename);
		printk(KERN_ERR "errno : %d \n",-ENODATA);
	}	

	end_read:
	if(filp && !IS_ERR(filp)){
		filp_close(filp,NULL);
	}	
	return bytes;
}

int wrapfs_write_file(struct file *filp,void *buf,int len){
//This function writes data to a file from buffer of PAGE_SIZE length
	mm_segment_t oldfs;
	int ret;

	if(!filp->f_op->write){
		printk(KERN_ALERT "Syscall don't allow writing file");
		ret = -EACCES;
		goto end;
	}	
	
	oldfs=get_fs();
	set_fs(KERNEL_DS);
	ret=vfs_write(filp,buf,len,&filp->f_pos);	
	set_fs(oldfs);
	
	if(ret < 0){
		printk("Outfile partially written \n");
		ret = -EIO;
	}

	end:
	return ret;
}

int checkName(struct file *op, const char *infile){
// This function returns error if input and output files are same
	struct file *ip = NULL;
	ip = filp_open(infile,O_RDONLY,0);

	if(op->f_dentry->d_inode == ip->f_dentry->d_inode){
		printk("Input and Output files are same: %s \n",infile);
		return -EINVAL;
	}

	return 0;
}

int unlink_file(struct file *filp){
//This function deletes/unlinks a specified file
	int ret = 0;
	struct dentry *den = NULL;
	den = filp->f_dentry;
	filp_close(filp,NULL);
	
	mutex_lock(&den->d_parent->d_inode->i_mutex);
	ret = vfs_unlink(den->d_parent->d_inode,den);
	mutex_unlock(&den->d_parent->d_inode->i_mutex);

	if(ret < 0){
		printk(KERN_ALERT "vfs_unlink error in unlink_file");
	}

	return ret;
}

int rename_file(struct file *old, struct file *new){
//This function renames file, used for atomic mode
	int ret = 0;
	struct dentry *den = NULL;
	den = old->f_dentry;
	filp_close(old,NULL);

	mutex_lock(&den->d_parent->d_inode->i_mutex);
	ret = vfs_unlink(den->d_parent->d_inode,den);
	mutex_unlock(&den->d_parent->d_inode->i_mutex);

	if(ret < 0){
		printk(KERN_ALERT "vfs_unlink error in rename_file");
		goto end_rename;
	}

	ret =vfs_rename(new->f_dentry->d_parent->d_inode,new->f_dentry,den->
					      d_parent->d_inode,den);
	if(ret < 0){
		printk(KERN_ALERT "vfs_rename error in rename_file");
		goto end_rename;
	}
	
	end_rename:
	return ret;
}

int read_write_operation(void *arg){	
//The function performs various read/write operations in normal and atomic mode	
	int i=0,j=0;
	int pos=0;
	int exceedFactor=0;
	int filesRead=0;
	int totBytesRead=0;
	int ret=0;
	struct kstat fileStat;
	struct file *filp=NULL;
	int tempBytes=0;
	void *buf = kmalloc(PAGE_SIZE,__GFP_WAIT);
	int byte_read=0,byte_write=0;
	int len=PAGE_SIZE;
	const char *outfile=((struct myargs*)arg)->outfile;
	const char *infiles[20];
	unsigned int infile_count=((struct myargs*)arg)->infile_count;
	int oflags=((struct myargs*)arg)->oflags;
	mode_t mode=((struct myargs*)arg)->mode;
	unsigned int flags=((struct myargs*)arg)->flags;
	bool atomic_flag = false;
	bool file_closed = false;
	#ifdef EXTRA_CREDIT
	bool temp_created = false;
	struct file *temp_file = NULL;
	#endif
	
	if(!buf){
		printk(KERN_ALERT "Buffer allocation failed \n");
		ret = -ENOMEM;
		goto end_rw;
	}
	filp = filp_open(outfile,O_RDONLY,0); 

	if(!filp || IS_ERR(filp)){
		atomic_flag = true;
	}

	for(i=0;i<infile_count;i++){
	//Loop copies infiles into local infiles pointer
		infiles[i]=((struct myargs*)arg)->infiles[i];	
		if(!IS_ERR(filp)){
			ret = checkName(filp,infiles[i]);
			if(ret < 0){
				ret = -EINVAL;	
				goto end_rw;
			}

		}
	}

	#ifdef EXTRA_CREDIT
	if((flags == 0x04 || flags == 0x05 || flags == 0x06) && (atomic_flag ==
								 false)){
	/*This EXTRA_CREDIT block opens temp backup file and writes outfile data 								   into it*/
		set_fs(KERNEL_DS);
		vfs_stat(outfile,&fileStat);	
		temp_file = filp_open("tempfile",O_WRONLY | O_CREAT,fileStat.
								mode);	
		ret = checkName(temp_file,outfile);
		if(ret < 0){
			printk("tempfile is reserved for temporary atomic mode"
								 "file \n");
			goto end_rw;
		}

		exceedFactor=((int)fileStat.size/len);
		temp_created = true;

		if(exceedFactor==0){		
			byte_read=wrapfs_read_file(outfile,buf,len,0);
			tempBytes=wrapfs_write_file(temp_file,buf,byte_read);	
		}
		else{
			for(j=0,pos=0;j<exceedFactor;j++){
				byte_read=wrapfs_read_file(outfile,buf,len,pos);				pos+=len;	
				tempBytes=wrapfs_write_file(temp_file,buf,
							    byte_read);		
			}
		}
	}				
	#else
	if(flags == 0x04 || flags == 0x05 || flags == 0x06){		
		printk("Error EXTRA_CREDIT not defined \n");
		ret = -EPERM;
		goto end_rw;
	}
	#endif
	
	filp=filp_open(outfile,O_WRONLY | oflags,mode); /* Opened output file
							 once*/
	if((!filp || IS_ERR(filp)) && (oflags==(int)O_TRUNC || oflags==
						(int)O_APPEND)){
		ret = -ENOENT;
		goto end_rw;
	}
	else if((!filp || IS_ERR(filp)) && (oflags ==(int)(O_EXCL | O_APPEND))){
		ret = -ENOENT;
		goto end_rw;
	}			

	else if((!filp || IS_ERR(filp)) && (oflags == (int)(O_EXCL | O_TRUNC))){
		ret = -ENOENT;
		goto end_rw;
	}	
	else if((!filp || IS_ERR(filp)) && (oflags == (int)(O_EXCL | O_APPEND | 
							O_TRUNC))){
		ret = -ENOENT;
		goto end_rw;
	}		
	else if((!filp || IS_ERR(filp)) && (oflags == (int)(O_APPEND | 
							O_TRUNC))){
		ret = -ENOENT;
		goto end_rw;
	}
	else if((!filp || IS_ERR(filp)) && (oflags == (int)(O_EXCL))){
		ret = -ENOENT;
		goto end_rw;
	}
	else if((!filp || IS_ERR(filp)) && (oflags == (int)(O_EXCL | O_CREAT))){
		ret = -EEXIST;
		goto end_rw;
	}
	else if((!filp || IS_ERR(filp)) && (oflags == (int)(O_EXCL| O_APPEND |
						 O_TRUNC | O_CREAT))){
		ret = -EEXIST;
		goto end_rw;
	}
	else if((!filp || IS_ERR(filp)) && (oflags == (int)(O_EXCL | O_TRUNC |
							 O_CREAT))){
		ret = -EEXIST;
		goto end_rw;
	}
	else if((!filp || IS_ERR(filp)) && (oflags == (int)(O_EXCL | O_CREAT |
							 O_APPEND))){
		ret = -EEXIST;
		goto end_rw;
	}

	//oflags validation end
	filp->f_pos=0; // Set initial output file position to 0, to file start		//Reinitialize variables for for loop
	exceedFactor = 0;
	byte_read = 0;
	tempBytes = 0;
	pos = 0;
	// Reinitialize end

	for(i=0;i<infile_count;i++){
	/*This code block reads input files and write to outfile*/				set_fs(KERNEL_DS);	
		vfs_stat(infiles[i],&fileStat);		
		exceedFactor=((int)fileStat.size/len);	

		if(exceedFactor==0){		
		//exceedFactor = 0,files sieze < PAGE_SIZE
			byte_read=wrapfs_read_file(infiles[i],buf,len,0);				if(byte_read >= 0){
				filesRead++;
				totBytesRead+=byte_read;
			}
			else{
				//Current file is empty so 
			}	
			tempBytes=wrapfs_write_file(filp,buf,byte_read);
			#ifdef EXTRA_CREDIT
			atomic_err:
			if(flags == 0x04 || flags == 0x05 || flags == 0x06){
				if((byte_read < 0) || (tempBytes < 0)){							if(atomic_flag == true){
					/*New file created in atomic mode but 
					unlinked due to error */		
			        		ret = unlink_file(filp);
						file_closed = true;
						if(ret < 0){
							goto end_rw;
						}
						ret = -EIO;									goto end_rw;
					}	
					else{
					// File Already Exists Revert changes
						ret = rename_file(filp,
								temp_file);	
						temp_created = false; /*tempfile					  deleted in rename_file(...) function*/
						file_closed = true;
						if(ret < 0){
							goto end_rw;
						}
						ret = -EIO;
						goto end_rw;
					}	
				}	
			}
			#endif		

			if(tempBytes>1){
				byte_write += tempBytes;
				tempBytes = 0;
			}			
			
		}	
		else{
		//Filesize > PAGE_SIZE read/write in loop
			for(j=0,pos=0;j <= exceedFactor;j++){
				byte_read = wrapfs_read_file(infiles[i],buf,len,
								  pos);		
				pos += len;

				if(byte_read > 1){				
					totBytesRead += byte_read;
				}
				else{
					printk(KERN_ALERT "Current Page: %d"
				       "of File %s  cannot be read or is empty"
							 "\n",j,infiles[i]);
				}	
				tempBytes=wrapfs_write_file(filp,buf,byte_read);
				#ifdef EXTRA_CREDIT				
				if(flags == 0x04 || flags == 0x05 || flags == 
								0x06){
					if(byte_read < 0 || tempBytes < 0){
						goto atomic_err;
					}	
				}
				#endif
				if(tempBytes > 1){
					byte_write+=tempBytes;
					tempBytes=0;
				}
			}
			filesRead++;
		}
		
	}
		
	if(flags==0x00){
	//Normal mode return no of bytes written
		#ifdef EXTRA_CREDIT
		num_bytes:
		#endif
			if(byte_write>=0){
				printk(KERN_ALERT "No. of Bytes written :\n");
				ret = byte_write;
			}
			else{
				printk(KERN_ALERT "Error in writing file: \n");
				ret = -ENOENT;
				goto end_rw;
			}
	}
	else if(flags==0x01){
	// Return number of files written here
		#ifdef EXTRA_CREDIT
		num_files:	
		#endif
			printk(KERN_ALERT "Num. of files written :\n");
			ret = filesRead; /*fileRead contains count of number of 								file read*/	
			goto end_rw;
	}	
	else if(flags==0x02){
	// Return percentage of bytes written
		#ifdef EXTRA_CREDIT
		per_files:
		#endif
			if(totBytesRead==0){
				printk(KERN_ALERT "No percentage as no bytes"
 						    "were written \n");
				ret = 0;
				goto end_rw;

		}
		else{
			printk(KERN_ALERT "Percentage of bytes written:\n");
			ret = (int)((byte_write*100)/totBytesRead);					goto end_rw;
		}
	}
	else if(flags==0x04 || flags == 0x05 || flags == 0x06){
	// Atomic mode activated
		#ifdef EXTRA_CREDIT
		if(flags == 0x04){
			goto num_bytes;
		}
		else if(flags == 0x05){
			goto num_files;
		}
		else if(flags == 0x06){
			goto per_files;
		}
		#endif			
	}
	end_rw:
	// Relsase all resources start
		if(buf){
			kfree(buf);
		}
		if(filp && !IS_ERR(filp) && !file_closed){
			filp_close(filp,NULL);
		}		
		#ifdef EXTRA_CREDIT
		if(flags == 0x04){
			if(temp_created){
				unlink_file(temp_file);
			}
		}
		#endif
	//Release resources end
		return ret;
}	

int kernel_validate(void *ptr){
/* This function validates value of oflags,mode and flags passed to kernel from 								user module */
	int ret = 0;
	int mode = ((struct myargs*)ptr)->mode;
	//int flags = ((struct myargs*)ptr)->flags;

	if((((struct myargs*)ptr)->flags == 0x00) || (((struct myargs*)ptr)->
	flags == 0x01) || (((struct myargs*)ptr)->flags == 0x02) || ((struct 
	myargs*)ptr)->flags == 0x04 || ((struct myargs*)ptr)->flags == 0x05 ||
	((struct myargs*)ptr)->flags == 0x06){ 
	// Valid flags value
	}
	else{
		printk("Invalid value of flags \n ");
		ret = -EINVAL;
		goto end_val;
	}

	if( mode > 777 || mode < 0 ) {		
	// Validate mode
		printk("Invalid mode value \n");
		ret = -EINVAL;
		goto end_val;
	}

	if(((struct myargs*)ptr)->infile_count > 20){
		printk("No of input files can't be  greater than 20 \n");
		ret = -EINVAL;
		goto end_val;
	}	
	if(((struct myargs*)ptr)->infile_count == 0){
		printk("No of files is 0 \n");
		ret = -EINVAL;
		goto end_val;
	}

	end_val:
	return ret;

}

asmlinkage long xconcat(void *arg,unsigned int length){
/*This is main function which gets arg from user module and checks them for 
						authenticity with */	
	int ret=0,i=0,j=0;
	void *ptr = kmalloc(length,__GFP_WAIT);
	char *temp = kmalloc(sizeof(char*),__GFP_WAIT);

	if(!ptr){
		printk(KERN_ALERT "ptr allocation failed \n");
		ret = - ENOMEM;
		goto end_xcat;
	}
	if(!temp){
		printk(KERN_ALERT "temp allocation failed \n");
		ret = -ENOMEM;
		goto end_xcat;
	}			
	if(arg==NULL){
		printk("Arg is NULL \n");
		ret = -EINVAL;
		goto end_xcat;
	}
	if(length != 24){
		printk("Invalid length from User_Space \n");
		ret = -EFAULT;
		goto end_xcat;
	}
	if(access_ok(VERIFY_READ,arg,length)>0){ // User address validation

		if(copy_from_user(ptr,arg,length)==0)
		{
		// User arguments validation
			temp = getname(((struct myargs*)ptr)->outfile);
			((struct myargs*)ptr)->outfile = temp;
			for(i=0;i<(((struct myargs*)ptr)->infile_count);i++)
			{
				temp = getname(((struct myargs*)ptr)->
							infiles[i]);
				((struct myargs*)arg)->infiles[i] = temp;
			}
		
		}
	else{
		printk(KERN_ALERT "EFAULT in copy_from_user \n");
		ret = -EFAULT;
		goto end_xcat;
	}	
	ret = kernel_validate(ptr);

	if(ret <0){
		goto end_xcat;
	}

	for(i=0;i<((struct myargs*)ptr)->infile_count;i++){
	//This code block checks if every input file exists	
		j=checkFile(((struct myargs*)ptr)->infiles[i]);
	
		if(j < 0){
			// j < 0 file don't exist
			ret = j;
			goto end_xcat;
		}
	}

	ret=read_write_operation(ptr);	
	return ret;
	
	}
	else{
		printk(KERN_ALERT "EFAULT in access_ok \n");
		ret = -EFAULT;
		goto end_xcat;
	}


	end_xcat:
	//Free all resources start
		if(temp){
			kfree(temp);
		}
		if(ptr){
			kfree(ptr);
		}
	//Free all resources end
	return ret;
}

static int __init init_sys_xconcat(void)
{
	printk("installed new sys_xconcat module\n");

	if (sysptr == NULL)
		sysptr = xconcat;
	return 0;
}
static void  __exit exit_sys_xconcat(void)
{
	if (sysptr != NULL)
		sysptr = NULL;
	printk("removed sys_xconcat module\n");
}
module_init(init_sys_xconcat);
module_exit(exit_sys_xconcat);
MODULE_LICENSE("GPL");
