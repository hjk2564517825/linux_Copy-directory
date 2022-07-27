#include "thread_pool.h"

//线程取消例程函数
void handler(void *arg)  //arg = pool->lock
{
	printf("[%u] is ended.\n",
		(unsigned)pthread_self());

	//无论线程在什么状态下被取消，一定要先解锁，再响应取消。
	pthread_mutex_unlock((pthread_mutex_t *)arg);
}

//线程的例程函数
void *routine(void *arg)
{
	//1. 先将传递过来的线程池的地址接住。
	thread_pool *pool = (thread_pool *)arg;
	struct task *p;

	while(1)
	{
		//2. 设置线程的取消例程函数。
		//   将来线程收到取消时，先执行handler,再响应取消。
		pthread_cleanup_push(handler, (void *)&pool->lock);
		
		//3. 上锁
		pthread_mutex_lock(&pool->lock);
		
		//4. 当任务队列中没有任务 并且 线程池为开启状态
		while(pool->waiting_tasks == 0 && !pool->shutdown)
		{
			//那么就会进入条件变量中睡眠。
			pthread_cond_wait(&pool->cond, &pool->lock);  //自动解锁
		}
			
		//5. 当执行到这个地方时：
		//要不就是线程池中有任务。
		//要不就是线程池为关闭的状态。
		//或者是两个都不成立。

		//6. 如果当前线程池关闭了，并且没有任务做，那么线程就会解锁退出。
		//   意味着如果线程池关闭了，但是还是任务，线程是不会退出，直到所有任务都做完为止。
		if(pool->waiting_tasks == 0 && pool->shutdown == true)
		{	
			//解锁
			pthread_mutex_unlock(&pool->lock);	
			
			//线程退出
			pthread_exit(NULL); 
		}
		
		//7. 让p指向头节点的下一个节点。
		p = pool->task_list->next;
		
		//8. 让头节点的指针域指向p的下一个节点。
		pool->task_list->next = p->next;

		//第7第8步就是为了将p节点取出来。
		
		//9. 当前等待的任务个数-1
		pool->waiting_tasks--;

		//10. 解锁
		pthread_mutex_unlock(&pool->lock);
		
		//11. 删除线程取消例程函数
		pthread_cleanup_pop(0); //0->不会执行
		
		//12. 设置线程当前为不能响应取消。
		pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL); 
		
		//13. 执行p指向节点的函数与参数，执行任务的过程中不能响应取消请求。
		(p->do_task)(p->arg);   
		
		//14. 设置线程当前能响应取消请求
		pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

		//15. p指向的任务已经做完了，所以可以释放掉p的空间。
		free(p);
	}
}

//函数功能： 初始化线程池
bool init_pool(thread_pool *pool, unsigned int threads_number)
{
	//1. 初始化互斥锁。
	pthread_mutex_init(&pool->lock, NULL);
	
	//2. 初始化条件变量。
	pthread_cond_init(&pool->cond, NULL);

	//3. 初始化当前系统为开启状态。
	pool->shutdown = false;
	
	//4. 为任务队列的头节点申请空间。
	pool->task_list = malloc(sizeof(struct task));
	
	//5. 申请线程ID号储存空间
	pool->tids = malloc(sizeof(pthread_t) * MAX_ACTIVE_THREADS);  //20

	//6. 如果以上两个步骤执行失败，那么线程池就会初始化失败。
	if(pool->task_list == NULL || pool->tids == NULL)
	{
		perror("allocate memory error");
		return false;
	}

	//6. 当前头节点的指针域指向NULL，说明当前一个任务都没有。
	pool->task_list->next = NULL;
	
	//7. 设置当前系统最大的等待任务个数为1000个。
	pool->max_waiting_tasks = MAX_WAITING_TASKS;
	
	//8. 当等待需要处理的任务为0个。
	pool->waiting_tasks = 0;
	
	//9. 设置当前线程池中线程的个数
	pool->active_threads = threads_number;

	//10. 通过循环去创建线程
	int i;
	for(i=0; i<pool->active_threads; i++)
	{
		if(pthread_create(&((pool->tids)[i]), NULL,routine, (void *)pool) != 0)
		{
			perror("create threads error");
			return false;
		}
	}

	//11. 初始化线程池成功。
	return true;
}

//函数功能： 投放任务到任务队列中
bool add_task(thread_pool *pool,void *(*do_task)(void *arg), void *arg)
{
	//1. 为新任务申请空间。
	struct task *new_task = malloc(sizeof(struct task));
	if(new_task == NULL)
	{
		perror("allocate memory error");
		return false;
	}
	
	//2. 为新节点的数据域与指针域赋值。
	new_task->do_task = do_task;  
	new_task->arg = arg; 
	new_task->next = NULL;  
	
	//3. 上锁
	pthread_mutex_lock(&pool->lock);
	
	//4. 如果当前需要处理的任务个数 >= 1000
	if(pool->waiting_tasks >= MAX_WAITING_TASKS)
	{
		//解锁
		pthread_mutex_unlock(&pool->lock);
		
		//输出一句话报错: 太多任务了
		fprintf(stderr, "too many tasks.\n");
		
		//释放新节点的空间。
		free(new_task);

		//添加任务失败
		return false;
	}
	
	//5. 寻找最后一个节点，从循环中出来时，tmp就是指向最后一个节点。
	struct task *tmp = pool->task_list;
	while(tmp->next != NULL)
		tmp = tmp->next;
	
	//6. 让最后一个节点的指针域指向新节点。
	tmp->next = new_task;
	
	//7. 当前任务个数+1
	pool->waiting_tasks++;

	//8. 解锁
	pthread_mutex_unlock(&pool->lock);

	//9. 唤醒条件变量中一个线程起来做任务。
	pthread_cond_signal(&pool->cond);
	
	//10. 任务添加成功
	return true;
}

//函数功能： 在线程池中添加线程。
int add_thread(thread_pool *pool, unsigned additional_threads)
{
	//1. 如果想添加0条线程，那么请你滚蛋。
	if(additional_threads == 0)
		return 0; 

	//2. 总的线程数 = 当前线程个数 + 新增条数。
	//		35		=	  5        +   30
	unsigned total_threads = pool->active_threads + additional_threads;
						
	int i, actual_increment = 0;
	for(i = pool->active_threads;  i < total_threads && i < MAX_ACTIVE_THREADS;    i++) 
	{
		if(pthread_create(&((pool->tids)[i]),NULL, routine, (void *)pool) != 0)
		{
			perror("add threads error");
			if(actual_increment == 0) 
				return -1;

			break;
		}

		actual_increment++;   //每创建成功一条线程，那么这个actual_increment就会+1
	}

	//3. 最终线程总条数 = 当前线程总条数 + 实际创建成功的线程的个数
	pool->active_threads += actual_increment; 

	//4. 返回实际创建线程的个数
	return actual_increment; 
}

//函数功能： 删除当前线程池中的线程。  
int remove_thread(thread_pool *pool, unsigned int removing_threads)
{
	//1. 如果想删除0条线程，那么就返回当前线程池中真实的条数。
	if(removing_threads == 0)
		return pool->active_threads; 

	//2.  线程池线程剩余条数 = 当前线程池中线程的个数 - 想删除的线程的个数
	//            7                   10              -     3
	//            0                   10              -     10
	//            -5                  10              -     15
	int remaining_threads = pool->active_threads - removing_threads;
	
	//3. 判断计算的结果，线程池中至少要存活一条线程。
	remaining_threads = remaining_threads > 0 ? remaining_threads : 1;

	//4. 发送取消请求给线程。
	int i;  
	for(i=pool->active_threads-1; i>remaining_threads-1; i--)
	{	
		errno = pthread_cancel(pool->tids[i]); 
		if(errno != 0)
			break;
	}

	//5. 如果一个都取消不了
	if(i == pool->active_threads-1) 
		//就会返回-1
		return -1;
	else
	{
		//计算出实际的条数
		pool->active_threads = i+1; 
		return i+1; 
	}
}

//函数功能： 销毁线程池
bool destroy_pool(thread_pool *pool)
{
	//1. 设置线程池的标志位为关闭状态
	pool->shutdown = true; 
	
	//2. 唤醒所有在条件变量中等待的线程
	pthread_cond_broadcast(&pool->cond);  
	
	//3. 接合所有的线程
	int i;
	for(i=0; i<pool->active_threads; i++)
	{
		errno = pthread_join(pool->tids[i], NULL);

		if(errno != 0)
		{
			printf("join tids[%d] error: %s\n",
					i, strerror(errno));
		}
	
		else
			printf("[%u] is joined\n", (unsigned)pool->tids[i]);
		
	}

	//4. 释放所有申请过的空间。
	free(pool->task_list);
	free(pool->tids);
	free(pool);

	return true;
}
