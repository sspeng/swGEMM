/*************************************************************************
	> File Name: copy_border.c
	> Author: 
	> Mail: 
	> Created Time: Wed Jan  2 20:07:22 2019
 ************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <string.h>
#include <unistd.h>
#include <slave.h>
#include <dma.h>
#include <math.h>
#include "include/common_slave.h"

__thread_local dma_desc dma_src, dma_dst;


// M is the continus dimesion
//void copy_border_float32(float* src, float* dst, const int M, const int N, 
        //const int Ms, const int Ns, const int Me, const int Ne)
void copy_border_float32(CopyData* params)
{
    float* src = (float*)(params->src);
    float* dst = (float*)(params->dst);
    const int M = params->M;
    const int N = params->N;
    const int Ms = params->Ms;
    const int Ns = params->Ns;
    const int Me = params->Me;
    const int Ne = params->Ne;
    const int trans = params->trans;
    const int id = athread_get_id(-1);
    const int cid = id%8, rid = id/8;

    if(Ms%32 != 0 || Ns%32 != 0)
    {
        printf("copy border error: Ms %d, Ns %d\n", Ms, Ns);
        exit(0);
    }
    volatile int reply_src = 0, reply_dst = 0;
    //dma_desc dma_src, dma_dst;
    dma_set_op(&dma_dst, DMA_PUT);
    dma_set_mode(&dma_dst, PE_MODE);
    dma_set_reply(&dma_dst, &reply_dst);
    dma_set_op(&dma_src, DMA_GET);
    dma_set_mode(&dma_src, PE_MODE);
    dma_set_reply(&dma_src, &reply_src);
    if(trans == 0) // column major
    {
        if( id < 32 ) // copy the right panel
        {
            if(Ns >= Ne)
                return;
            int i = 0, j = 0;
            float* buf = (float*)ldm_malloc(sizeof(float)*Me);
            float* start;
            dma_set_size(&dma_src, sizeof(float)*M);
            dma_set_size(&dma_dst, sizeof(float)*Me);

            for(j = id; j < Ne-Ns; j += 32)
            {
                if(Ns+j < N)
                {
                    //copy Ns+j column
                    start = &src[(Ns+j)*M];
                    dma(dma_src, (long)start, (long)buf);
                    for(i = M; i < Me; i ++)
                        buf[i] = 0.0;
                    dma_wait(&reply_src, 1);
                    reply_src = 0;
                }
                else
                {
                    for(i = 0; i < Me; i ++)
                        buf[i] = 0.0;
                }
                //store
                start = &dst[(Ns+j)*Me];
                dma(dma_dst, (long)start, (long)buf);
                dma_wait(&reply_dst, 1);
                reply_dst = 0;
            }

            ldm_free(buf, sizeof(float)*Me);
        }
        else //copy the bottom panel
        {
            if(Ms >= Me)
                return;
            int cols = Ns / 32;
            float* src_origin = &src[(id-32)*cols*M];
            float* dst_origin = &dst[(id-32)*cols*Me];
            int i, j, k;
            // ldm size
            int max_cols = (56 * 1024) / ((Me-Ms)*sizeof(float));
            int numb = (cols + max_cols - 1)/max_cols;
            int BS = max_cols;
            int remBS = cols - (numb-1)*BS;
            float* buf, *start;
            if(numb > 1)
                buf = (float*)ldm_malloc(sizeof(float) * BS * (Me-Ms));
            else
                buf = (float*)ldm_malloc(sizeof(float) * remBS * (Me-Ms));

            for(i = 0;i < numb; i ++)
            {
                int currBS = (i < numb-1) ? BS : remBS;
                //read currBS * (M - Ms)
                dma_set_size(&dma_src, currBS * (M-Ms) * sizeof(float));
                dma_set_bsize(&dma_src, (M-Ms) * sizeof(float));
                dma_set_stepsize(&dma_src, Ms * sizeof(float));
                start = &src_origin[ i*BS*M + Ms];
                dma(dma_src, (long)start, (long)buf);
                dma_wait(&reply_src, 1);
                reply_src = 0;

                //extend
                for(j = currBS-1; j >= 0; j --)
                {
                    for(k = M-Ms-1; k >= 0; k --)
                        buf[j*(Me-Ms)+k] = buf[j*(M-Ms)+k];
                    for(k = Me-Ms-1; k >= M-Ms; k --)
                        buf[j*(Me-Ms)+k] = 0.0;
                }

                //store
                dma_set_size(&dma_dst, currBS * (Me-Ms) * sizeof(float));
                dma_set_bsize(&dma_dst, (Me-Ms) * sizeof(float));
                dma_set_stepsize(&dma_dst, Ms * sizeof(float));
                start = &dst_origin[ i*BS*Me + Ms];
                dma(dma_dst, (long)start, (long)buf);
                dma_wait(&reply_dst, 1);
                reply_dst = 0;

            }

            if(numb > 1)
                ldm_free(buf, sizeof(float) * BS * (Me-Ms));
            else
                ldm_free(buf, sizeof(float) * remBS * (Me-Ms));
        }
    }
    else // row major
    {
        if(id < 32) // right panel
        {
            if(Ns >= Ne)
                return;
            int i, j, k;
            float* start, *buf;
            int rows = Ms/32;
            float* src_origin = &src[id*rows*N];
            float* dst_origin = &dst[id*rows*Ne];
            int max_rows = (56*1024) / ((Ne-Ns)*sizeof(float));

            int numb = (rows + max_rows - 1) / max_rows;
            int BS = max_rows;
            int remBS = rows - (numb-1)*BS;
            if(numb > 1)
                buf = (float*)ldm_malloc( sizeof(float) * BS * (Ne-Ns));
            else
                buf = (float*)ldm_malloc( sizeof(float) * remBS * (Ne-Ns) );

            for(i = 0;i < numb; i ++)
            {
                int currBS = (i < numb-1) ? BS : remBS;
                //read 
                dma_set_size(&dma_src, currBS * (N-Ns) * sizeof(float));
                dma_set_bsize(&dma_src, (N-Ns) * sizeof(float));
                dma_set_stepsize(&dma_src, Ns * sizeof(float));
                start = &src_origin[i*BS*N + Ns];
                dma(dma_src, (long)start, (long)buf);
                dma_wait(&reply_src, 1);
                reply_src = 0;

                /*if(id == 0)
                {
                    printf("buf : ");
                    for(i = 0;i < (Ne-Ns) * currBS; i ++)
                        printf("%.1f ", buf[i]);
                    printf("\n\n");
                }*/
                //extend 
                for(k = currBS-1; k >= 0; k --)
                {
                    for(j = N-Ns-1; j >= 0; j --)
                        buf[k*(Ne-Ns) + j] = buf[k*(N-Ns) + j];
                    for(j = Ne-Ns-1; j >= N-Ns; j --)
                        buf[k*(Ne-Ns) + j] = 0.0;
                }

                /*if(id == 0)
                {
                    printf("buf : ");
                    for(i = 0;i < (Ne-Ns) * currBS; i ++)
                        printf("%.1f ", buf[i]);
                    printf("\n\n");
                }*/
                // store
                dma_set_size(&dma_dst, currBS * (Ne-Ns) * sizeof(float));
                dma_set_bsize(&dma_dst, (Ne-Ns) * sizeof(float));
                dma_set_stepsize(&dma_dst, Ns * sizeof(float));
                start = &dst_origin[i*BS*Ne + Ns];
                dma(dma_dst, (long)start, (long)buf);
                dma_wait(&reply_dst, 1);
                reply_dst = 0;
            }

            if(numb > 1)
                ldm_free(buf, sizeof(float) * BS * (Ne-Ns));
            else
                ldm_free(buf, sizeof(float) * remBS * (Ne-Ns));
        }
        else // bottom panel
        {
            if(Ms >= Me)
                return;
            int offset_id = id - 32;
            int i, j, k;
            float* buf = (float*)ldm_malloc(sizeof(float) * Ne);
            float* start;
            dma_set_size(&dma_src, sizeof(float)*N);
            dma_set_size(&dma_dst, sizeof(float)*Ne);

            for(i = offset_id; i < Me-Ms; i += 32)
            {
                //read
                if(Ms+i < M)
                {
                    start = &src[(Ms+i)*N];
                    dma(dma_src, (long)start, (long)buf );
                    for(j = N; j < Ne; j ++)
                        buf[j]= 0.0;
                    dma_wait(&reply_src, 1);
                    reply_src = 0;
                }
                else
                {
                    for(j = 0;j < Ne; j ++)
                        buf[j] = 0.0;
                }

                //store
                start = &dst[(Ms+i)*Ne];
                dma(dma_dst, (long)start, (long)buf);
                dma_wait(&reply_dst, 1);
                reply_dst = 0;
            }

            ldm_free(buf, sizeof(float) * Ne);

        }
    }
}
