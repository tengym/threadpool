#ifdef __cplusplus
extern "C"{
#endif /* __cplusplus */

#include "threadpool.h"

#define MAX_THREADPOOL_NUM      (30)

struct job
{
    EPOLL_EV_CB callback;
    void *arg;                            
    struct job *next;
};

typedef struct threadpool
{
    int thread_num;                  
    int queue_max_num;                
    struct job *head;                
    struct job *tail;          
    pthread_t *pthreads;            
    pthread_mutex_t mutex;           
    pthread_cond_t queue_empty;      
    pthread_cond_t queue_not_empty;   
    pthread_cond_t queue_not_full;    
    int queue_cur_num;                
    int queue_close;                  
    int pool_close;                   
}THREAD_POOL_S;

typedef struct
{
    int used_flag;
    THREAD_POOL_S *pthreadpool;
}Thread_Param_t;

static int init_flag  = 0;
static Thread_Param_t gst_threadpool[MAX_THREADPOOL_NUM];

THREAD_POOL_S* threadpool_init(int thread_num, int queue_max_num);
int threadpool_add_job(struct threadpool *pool, EPOLL_EV_CB callback, void *arg);
int threadpool_destroy(struct threadpool *pool);
void* threadpool_function(void* arg);

struct threadpool* threadpool_init(int thread_num, int queue_max_num)
{
    struct threadpool *pool = NULL;
    do 
    {
        pool = (struct threadpool *)malloc(sizeof(struct threadpool));
        if (NULL == pool)
        {
            printf("failed to malloc threadpool!\n");
            break;
        }
		memset(pool, 0, sizeof(struct threadpool));
        pool->thread_num = thread_num;
        pool->queue_max_num = queue_max_num;
        pool->queue_cur_num = 0;
        pool->head = NULL;
        pool->tail = NULL;
        if (pthread_mutex_init(&(pool->mutex), NULL))
        {
            printf("failed to init mutex!\n");
            break;
        }
        if (pthread_cond_init(&(pool->queue_empty), NULL))
        {
            printf("failed to init queue_empty!\n");
            break;
        }
        if (pthread_cond_init(&(pool->queue_not_empty), NULL))
        {
            printf("failed to init queue_not_empty!\n");
            break;
        }
        if (pthread_cond_init(&(pool->queue_not_full), NULL))
        {
            printf("failed to init queue_not_full!\n");
            break;
        }
        pool->pthreads = (pthread_t *)malloc(sizeof(pthread_t) * thread_num);
        if (NULL == pool->pthreads)
        {
            printf("failed to malloc pthreads!\n");
            break;
        }
		memset(pool->pthreads, 0, sizeof(pthread_t) * thread_num);
		
        pool->queue_close = 0;
        pool->pool_close = 0;
        int i;
        for (i = 0; i < pool->thread_num; ++i)
        {
            pthread_create(&(pool->pthreads[i]), NULL, threadpool_function, (void *)pool);
        }

        return pool;    
    } while (0);
    
    return NULL;
}

int threadpool_add_job(struct threadpool* pool, EPOLL_EV_CB callback, void *arg)
{
    assert(pool != NULL);
    assert(callback != NULL);
    assert(arg != NULL);

    pthread_mutex_lock(&(pool->mutex));
    while ((pool->queue_cur_num >= pool->queue_max_num) && !(pool->queue_close || pool->pool_close))
    {
        pthread_cond_wait(&(pool->queue_not_full), &(pool->mutex));   //��������ʱ��͵ȴ�
    }

    if (pool->queue_close || pool->pool_close)    //���йرջ����̳߳عرվ��˳�
    {
        pthread_mutex_unlock(&(pool->mutex));
        return -1;
    }

    struct job *pjob =(struct job*) malloc(sizeof(struct job));
    if (NULL == pjob)
    {
        pthread_mutex_unlock(&(pool->mutex));
        return -1;
    } 
	memset(pjob, 0, sizeof(struct job));
    pjob->callback = callback;    
    pjob->arg = arg;
    pjob->next = NULL;
    if (pool->head == NULL)   
    {
        pool->head = pool->tail = pjob;
        pthread_cond_broadcast(&(pool->queue_not_empty));  //���пյ�ʱ����������ʱ��֪ͨ�̳߳��е��̣߳����зǿ�
    }
    else
    {
        pool->tail->next = pjob;
        pool->tail = pjob;    
    }
    pool->queue_cur_num++;
    pthread_mutex_unlock(&(pool->mutex));
	
    return 0;
}

void* threadpool_function(void* arg)
{
    struct threadpool *pool = (struct threadpool*)arg;
    struct job *pjob = NULL;
	
    while (1)  //��ѭ��
    {
        pthread_mutex_lock(&(pool->mutex));
        while ((pool->queue_cur_num == 0) && !pool->pool_close)   //����Ϊ��ʱ���͵ȴ����зǿ�
        {
            pthread_cond_wait(&(pool->queue_not_empty), &(pool->mutex));
        }

        if (pool->pool_close)   //�̳߳عرգ��߳̾��˳�
        {
            pthread_mutex_unlock(&(pool->mutex));
            pthread_exit(NULL);
        }

        pool->queue_cur_num--;
        pjob = pool->head;
        if (pool->queue_cur_num == 0)
        {
            pool->head = pool->tail = NULL;
        }
        else 
        {
            pool->head = pjob->next;
        }
		
        if (pool->queue_cur_num == 0)
        {
            pthread_cond_signal(&(pool->queue_empty));        //����Ϊ�գ��Ϳ���֪ͨthreadpool_destroy�����������̺߳���
        }

        if (pool->queue_cur_num < pool->queue_max_num)
        {
            pthread_cond_broadcast(&(pool->queue_not_full));  //���з������Ϳ���֪ͨthreadpool_add_job���������������
        }

        pthread_mutex_unlock(&(pool->mutex));
        
        pjob->callback(pjob->arg);   //�߳�����Ҫ���Ĺ������ص������ĵ���
        free(pjob);
        pjob = NULL;    
    }
}

int threadpool_destroy(struct threadpool *pool)
{
    assert(pool != NULL);
	
    pthread_mutex_lock(&(pool->mutex));
    if (pool->queue_close || pool->pool_close)   //�̳߳��Ѿ��˳��ˣ���ֱ�ӷ���
    {
        pthread_mutex_unlock(&(pool->mutex));
        return -1;
    }
    
    pool->queue_close = 1;        //�ö��йرձ�־
    while (pool->queue_cur_num != 0)
    {
        pthread_cond_wait(&(pool->queue_empty), &(pool->mutex));  //�ȴ�����Ϊ��
    }    
    
    pool->pool_close = 1;      //���̳߳عرձ�־
    pthread_mutex_unlock(&(pool->mutex));
    pthread_cond_broadcast(&(pool->queue_not_empty));  //�����̳߳��������������߳�
    pthread_cond_broadcast(&(pool->queue_not_full));   //������������threadpool_add_job����
    int i;
    for (i = 0; i < pool->thread_num; ++i)
    {
        pthread_join(pool->pthreads[i], NULL);    //�ȴ��̳߳ص������߳�ִ�����
    }
    
    pthread_mutex_destroy(&(pool->mutex));          //������Դ
    pthread_cond_destroy(&(pool->queue_empty));
    pthread_cond_destroy(&(pool->queue_not_empty));   
    pthread_cond_destroy(&(pool->queue_not_full));    
    free(pool->pthreads);
	pool->pthreads = NULL;
    struct job *p;
    while (pool->head != NULL)
    {
        p = pool->head;
        pool->head = p->next;
        free(p);
		p = NULL;
    }
    free(pool);
	pool = NULL;

    return 0;
}

int threadpool_get_handle(void)
{
    int i = 0;
    int ret = 0;

    for(i = 0; i < MAX_THREADPOOL_NUM; i++)
    {
        if(0 == gst_threadpool[i].used_flag)
        {
            ret = i;
            break;
        }
    }
    if(i == MAX_THREADPOOL_NUM)
    {
        ret = -1;
    }
    
    return ret;
}

int threadpool_api_create(int thread_num, int *handle)
{
    int ret = -1;

    if(NULL == handle)
    {
        return -1;
    }

    if(init_flag == 0)
    {
        init_flag = 1;
        memset(&gst_threadpool, 0, sizeof(gst_threadpool));
    }

    ret = threadpool_get_handle();
    if(ret < 0)
    {
        return -1;
    }

    int queue_max_num = thread_num; /*10*/
	gst_threadpool[ret].pthreadpool = threadpool_init(thread_num, queue_max_num);
	if(NULL == gst_threadpool[ret].pthreadpool)
	{
		return -1;
	}

	gst_threadpool[ret].used_flag = 1;

    *handle = ret;
	
	return 0;
}

int threadpool_api_addtask(int handle, EPOLL_EV_CB callback, void *arg)
{
	return threadpool_add_job(gst_threadpool[handle].pthreadpool, callback, arg);
}

int threadpool_api_destory(int handle)
{
	threadpool_destroy(gst_threadpool[handle].pthreadpool);

	gst_threadpool[handle].used_flag = 0;
	
	return 0;
} 

#ifdef __cplusplus
}
#endif /* __cplusplus */

