#include "thread_pool.h"
struct file
{
	char srcfile[1024];//源文件描述符
	char dstfile[1024];//目标文件描述符
};


//拷贝普通文件
void *copyregfile(void *arg) 
{
	//转换类型
	struct file * dofile = (struct file *)arg;
	printf("copyregfile is running...\n");

	/*struct stat结构体用来描述文件属性*/
	struct stat file_stat;
	//获取文件的属性
	stat(dofile->srcfile, &file_stat);
 
	//以源文件的类型和权限创建文件
	int srcfd,dstfd;
	srcfd = open(dofile->srcfile,O_RDONLY);
	dstfd = open(dofile->dstfile,O_CREAT | O_TRUNC | O_RDWR,file_stat.st_mode);
 
	if(srcfd == -1 || dstfd == -1)
	{
		perror("open file:");
		
		
	}
 
	int nread;
	char buf[100];
	while((nread = read(srcfd,buf,100)) > 0)
	{
		if( write(dstfd,buf,nread) == -1)
		{
			break;
		}
	}
 
	close(dstfd);
	return NULL;

}


 
//拷贝目录
int copydir(struct file* dofile)
{
	printf("copydir is running... \n");
	struct stat file_stat;

	//获取文件的属性
	stat(dofile->srcfile,&file_stat); 
	//以源目录的类型和目录来创建一个目录
	mkdir(dofile->dstfile,file_stat.st_mode); 

    //打开源目录
	DIR *srcdir = opendir(dofile->srcfile); 
	struct dirent *dp;

	//对本目录下的所有文件进行拷贝操作
	struct file *tmpfile = malloc(sizeof(struct file)); 
 
	while( (dp = readdir(srcdir))!=NULL )
	{
		memset(tmpfile,0,sizeof(struct file));
 
		if(dp->d_name[0] == '.') //如果文件为. 或者 .. 则跳过
		{
			continue;
		}
 
		sprintf(tmpfile->srcfile,"%s/%s",dofile->srcfile,dp->d_name);
		sprintf(tmpfile->dstfile,"%s/%s",dofile->dstfile,dp->d_name);
		
 
		struct stat tmpstat;
		stat(tmpfile->srcfile,&tmpstat);
 
		if(S_ISREG(tmpstat.st_mode))//如果为普通文件,则拷贝
		{
			copyregfile(tmpfile);  
			printf("%s\n",tmpfile->srcfile);
		}
		else if(S_ISDIR(tmpstat.st_mode))//如果为目录，则递归
		{
			copydir(tmpfile);
			printf("%s\n",tmpfile->srcfile);
		}
 
	}
	
	closedir(srcdir);
	free(tmpfile); 
	return 0;
}
 
 
 
int main(int argc,char *argv[])
{
	//设置环境umask值
	umask(0000);
	printf("enter\n");

	//初始化线程池
	thread_pool *pool = malloc(sizeof(thread_pool));
	init_pool(pool, 20);
	//struct file *dofile = (struct file*)arg;

	//判断参数是否足够
	if(argc != 3)
	{
		printf("Please run : ./%s xxx yyy\n",argv[0]);
		return -1;
	}
	
	//新建结构体用于接住被复制文件和要复制到的文件
	struct file dofile;
	strcpy(dofile.srcfile,argv[1]);
	strcpy(dofile.dstfile,argv[2]);
	
	//获取文件权限
	struct stat srcstat;
	stat(dofile.srcfile,&srcstat);
	
	if(S_ISREG(srcstat.st_mode))//如果为普通文件,则拷贝
	{
		//投送任务开始copy
		add_task(pool,copyregfile,(void *)&dofile);
	}
	else if(S_ISDIR(srcstat.st_mode))//如果为目录，则递归
	{
		copydir(&dofile);
	}

	
	printf("current thread number: %d\n",
	            remove_thread(pool, 0));	
	
			
	destroy_pool(pool);
	return 0;
}