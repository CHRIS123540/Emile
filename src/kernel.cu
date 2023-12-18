#include <vector>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cuda_runtime.h>
#include <device_launch_parameters.h>
#include <iostream>
#include <chrono>


__device__ void myfun(int* a,int n)
{
    int tid = threadIdx.x;
    int t = 0;
    for(int i = 1;i < n; i = i << 1)//从序列长度 1 到 序列长度 n/2，我们的目的是对 一个双调函数排序，所以不用对n
    {
        //判断升序还是降序 0为升 1为降 我们的目的是获得 序列长度为2，所以要除2*i
        bool order = (tid / (2*i))%2;
        //printf("%d %d \n",tid, order);       
        for(int j = i; j >= 1; j = j >> 1)
        {      
            if(((tid / j)%2) == 0) //除跳跃的步长 再取模 这是对自身的序列做排序，所以不用*2
            {            
                // 升序 & 出现 前 > 后   || 降序 & 前 < 后 并且在最后一次的时候，没用降序，所以一定要判断边界
                if ((tid + j < n) && (   ((!order) == (a[tid] > a[tid + j]))    ||   (order == (a[tid] < a[tid + j]))    ))
                {
                    t = a[tid];
                    a[tid] = a[tid + j];
                    a[tid + j] = t;
                }               
            }
            __syncthreads();
        }
    }
}



__global__ void myfun_shared(int* a,int n)
{
    int tid = threadIdx.x;
    
    __shared__ int a_share[1024];
    int t = 0,flag1 = 0, flag2 = 0;
    a_share[tid] = a[tid];
    __syncthreads();
    for(int i = 1;i < n; i = i << 1)//从序列长度 1 到 序列长度 n/2，我们的目的是对 一个双调函数排序，所以不用对n
    {
        //判断升序还是降序 0为升 1为降 我们的目的是获得 序列长度为2，所以要除2*i
//        bool order = (tid / (2*i))%2;
        flag1++;
        bool order = (tid >> flag1)%2;
        //printf("%d %d \n",tid, order);
        flag2 = flag1 - 1;       
        for(int j = i; j >= 1; j = j >> 1)
        {
            if(((tid >> flag2)%2) == 0) //除跳跃的步长 再取模 这是对自身的序列做排序，所以不用*2
            {            
                // 升序 & 出现 前 > 后   || 降序 & 前 < 后 并且在最后一次的时候，没用降序，所以一定要判断边界
                if ((tid + j < n) && (   ((!order) == (a_share[tid] > a_share[tid + j]))    ||   (order == (a_share[tid] < a_share[tid + j]))    ))
                {
                    t = a_share[tid];
                    a_share[tid] = a_share[tid + j];
                    a_share[tid + j] = t;
                }               
            }
            flag2--;
            __syncthreads();
        }
    }
    a[tid] = a_share[tid];
}





__global__ void warpfun(int* a,int* b,int n)
{
    int tid = threadIdx.x;
    
    __shared__ unsigned int a_share[1024];
    __shared__ unsigned int hash_share[1024];
    __shared__ unsigned int seq_share[1024];
    __shared__ unsigned int flow_share[1024];
    __shared__ unsigned int temp[2048];
    __shared__ unsigned int flag_share[1024];
    __shared__ unsigned int feature_share[1024];
    
    int t = 0,flag1 = 0, flag2 = 0;
    a_share[tid] = a[tid]*1024+tid;
//    a_share[tid] = a[tid];
    hash_share[tid] = a[tid];
    seq_share[tid] = tid + 1;
    flow_share[tid] = 1;//flow列
    //temp不需要初始化，只是一个双指针算法
    flag_share[tid] = 0;
    feature_share[tid] = tid;
    __syncthreads();
    for(int i = 1;i < n; i = i << 1)//从序列长度 1 到 序列长度 n/2，我们的目的是对 一个双调函数排序，所以不用对n
    {
        //判断升序还是降序 0为升 1为降 我们的目的是获得 序列长度为2，所以要除2*i
//        bool order = (tid / (2*i))%2;
        flag1++;
        bool order = (tid >> flag1)%2;
        //printf("%d %d \n",tid, order);
        flag2 = flag1 - 1;       
        for(int j = i; j >= 1; j = j >> 1)
        {
            if(((tid >> flag2)%2) == 0) //除跳跃的步长 再取模 这是对自身的序列做排序，所以不用*2
            {            
                // 升序 & 出现 前 > 后   || 降序 & 前 < 后 并且在最后一次的时候，没用降序，所以一定要判断边界
                if ((tid + j < n) && (   ((!order) == (a_share[tid] > a_share[tid + j]))    ||   (order == (a_share[tid] < a_share[tid + j]))    ))
                {
                    t = a_share[tid];
                    a_share[tid] = a_share[tid + j];
                    a_share[tid + j] = t;
                
                    t = seq_share[tid];
                    seq_share[tid] = seq_share[tid + j];
                    seq_share[tid + j] = t;

                    t = hash_share[tid];
                    hash_share[tid] = hash_share[tid + j];
                    hash_share[tid + j] = t;
                
                }               
            }
            flag2--;
            __syncthreads();
        }
    }
    
    //这里做flow列 邻居节点的处理
    if(tid > 0)
    {
        flow_share[tid] = (hash_share[tid] != hash_share[tid-1]);
    }
    temp[tid] = flow_share[tid];
    __syncthreads();

    
    
    int in = 1;
    int out = 0;
    //前缀和计算有问题
    if(tid < n)
    {
        for(int i = 1;i < n;i = i<<1)
            {
                in = 1 - in;
                out = 1 - out; 
                int index = i;
                if((tid - index) >= 0)
                {                
                    temp[tid + n * out] = temp[tid + n * in] + temp[tid - index + n * in];                    
//                    temp[tid + out] = temp[tid ] + temp[tid - index ];                
                }
                else
                {
                    temp[tid + n * out] = temp[tid + n * in];
                }
                __syncthreads();
            }        
    }
    flow_share[tid] = temp[tid + n * out];
    __syncthreads();

    

    int j;
//计算不同流有多少个数据包
    if(tid > 0)
    {
        if(flow_share[tid] != flow_share[tid - 1])
        {
            j = flow_share[tid];
            flag_share[j] = tid;

        }
    }
    __syncthreads();
   
   j = flow_share[tid];
   feature_share[tid] = feature_share[tid] - flag_share[j];
    __syncthreads();

    a[tid] = hash_share[tid];
    b[tid] = feature_share[tid];

}

int main(int argc, char** argv)
{
    int n = 1024;
    int array[n];
    int back[n];

    for(int i = 0 ; i < n; i++)
    {
        array[i] = rand()%64;
        back[i] = array[i];
        printf("%d ",array[i]);
    }
    printf("\n\n");

    size_t nbytes = n * sizeof(int);
    int* Garray = NULL;
    cudaMalloc((void**)&Garray,nbytes);
  
    int* Back = NULL;
    cudaMalloc((void**)&Back,nbytes);
    
    cudaMemcpy(Garray, array, nbytes, cudaMemcpyHostToDevice);

    // 执行内核函数
    warpfun<<<1, n>>>(Garray,Back, n);
    cudaDeviceSynchronize(); // 确保内核执行完成
    
    cudaMemcpy(array, Garray, nbytes, cudaMemcpyDeviceToHost);
    cudaMemcpy(back, Back, nbytes, cudaMemcpyDeviceToHost);
    
    
    for(int i = 0 ; i < n; i++)
    {
        printf("%d ",array[i]);
    }
    printf("\n\n");
 
    for(int i = 0 ; i < n; i++)
    {
        //if((back[i]-back[i+1]) > 0)
            printf("%d ",back[i]);
    }    
    printf("\n\n");
    for(int i = 0 ; i < n-1; i++)
    {
        if((back[i]-back[i+1]) > 0)
            printf("%d ",back[i]);
    }
    printf("\n\n");

    cudaFree(Garray);
    cudaFree(Back);

    return EXIT_SUCCESS;
}
