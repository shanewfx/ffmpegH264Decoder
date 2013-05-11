// ffmpegDemo.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

#ifdef __cplusplus
extern "C" {
#endif

#include "libavcodec/avcodec.h"
#include "libavutil/avutil.h"
#include "libswscale/swscale.h"
//#include "snprintf.h"

#ifdef __cplusplus
}
#endif

#define INBUF_SIZE 1024*10/*4096*/

static uint8_t* pFrameBuffer = NULL;

static bool FindStartCode (unsigned char* pBuf, int nZerosInStartcode)  
{  
    bool bFind = false;   

#if 0
    bFind = true;
    for (int i = 0; i < nZerosInStartcode; i++) {  
        if (pBuf[i] != 0) {
            bFind = false;  
        }
    }  

    if (pBuf[i] != 1) {
        bFind = false; 
    }
#else
    //h264 NALU startcode: 00 00 00 01
    if ((*(unsigned long*)pBuf & 0x00FFFFFF) == 0 && pBuf[3] == 1) {
        bFind = true;
    }
#endif
    return bFind;  
}  

static bool Check_StartCode(unsigned char* pBuf, int nPos)  
{ 
    return FindStartCode(&pBuf[nPos-4], 3); 
}  

static int GetNextNal(FILE* pInFile, unsigned char* pBuf)  
{  
    int nPos   = 0;  
    int nCount = 0;  

    while (!feof(pInFile) && ++nCount <= 4) {  
        pBuf[nPos++] = fgetc(pInFile);  
    }  

    if (!Check_StartCode(pBuf, nPos)) {  
        return 0;  
    }  

    while (!feof(pInFile) && (pBuf[nPos++] = fgetc(pInFile)) == 0);

    bool bCheckNextByte  = false;
    bool bStartCodeFound = false;   

    while (!bStartCodeFound) {  
        if (feof(pInFile)) {  
            return nPos-1;  
        }

        pBuf[nPos++] = fgetc(pInFile);  
        if (bCheckNextByte) {
            if (pBuf[nPos-1] == 0x41 || pBuf[nPos-1] == 0x67) {
                bStartCodeFound = true;
                break;
            }
            else {
                bCheckNextByte = false;
            }
        }

        if (Check_StartCode(pBuf, nPos)) {
            bCheckNextByte = true;
        }
    }  

    fseek(pInFile, -5, SEEK_CUR);  
    return nPos - 5;  
}

static void SavePPM(AVFrame* pFrame, int nWidth, int nHeight, int nFrame)
{
    char szFilename[32];

    // Open file
    sprintf(szFilename, "debuglog\\frame%d.ppm", nFrame);

    FILE* pFile = fopen(szFilename, "wb");
    if (pFile == NULL) {
        return;
    }

    // Write header
    fprintf(pFile, "P6\n%d %d\n255\n", nWidth, nHeight);

    // Write pixel data
    for(int i = 0; i < nHeight; i++) {
        fwrite(pFrame->data[0] + i * pFrame->linesize[0], 1, nWidth * 3, pFile);
    }

    // Close file
    fclose(pFile);
}

static void SaveYUV420P(AVFrame* pFrame, int nWidth, int nHeight, const char* lpszFileName)
{
    int nPicSize = nHeight * nWidth;  
    int nTotalSize = nPicSize * 1.5;  
 
    unsigned char* pBuf = new unsigned char[nTotalSize];
    if (pBuf == NULL) {
        return;
    }

    //copy yuv420p data to buffer
    int nOffset = 0, i = 0;   
    for (i= 0; i < nHeight; i++) {   
        memcpy(pBuf + nOffset, pFrame->data[0] + i * pFrame->linesize[0], nWidth);   
        nOffset += nWidth;   
    }   
    for (i = 0; i < nHeight/2; i++) {   
        memcpy(pBuf + nOffset, pFrame->data[1] + i * pFrame->linesize[1], nWidth/2);   
        nOffset += nWidth/2;   
    }   
    for (i = 0; i < nHeight/2; i++) {   
        memcpy(pBuf + nOffset, pFrame->data[2] + i * pFrame->linesize[2], nWidth/2);   
        nOffset += nWidth/2;   
    }   

    //save as yuv file 
    FILE *f = fopen(lpszFileName, "ab+"); 
    fwrite(pBuf, 1, nTotalSize, f); 
    fclose(f); 

    delete [] pBuf;  
}


static void SavePGM(unsigned char* pBuf, int nWrap, int nXSize, int nYSize, char* lpszFileName) 
{ 
    FILE* f = fopen(lpszFileName, "ab+"); 
    fprintf(f, "P5\n%d %d\n%d\n", nXSize, nYSize, 255); 
    for (int i=0; i < nYSize; i++) {
        fwrite(pBuf + i * nWrap, 1, nXSize, f); 
    }
    fclose(f); 
} 

static int DecodeFrame(const char* lpszOutFileName, AVCodecContext* pDecCtx, 
                       AVFrame* pFrame,  AVFrame* pFrameRGB, int* pnFrameCount, AVPacket* pAVPacket, int bLastFrame) 
{ 
    int nGotFrame = 0; 
    int nLen = avcodec_decode_video2(pDecCtx, pFrame, &nGotFrame, pAVPacket); 
    if (nLen < 0) { 
        fprintf(stderr, "Error while decoding frame %d\n", *pnFrameCount); 
        return nLen; 
    } 

    if (nGotFrame) { 
        printf("Saving %sframe %3d\n", bLastFrame ? "last " : "", *pnFrameCount); 
        fflush(stdout); 

#if 0
        char buf[1024];
        /* the picture is allocated by the decoder, no need to free it */ 
        snprintf(buf, sizeof(buf), outfilename, *pnFrameCount); 
        SavePGM(pFrame->data[0], pFrame->linesize[0], pDecCtx->width, pDecCtx->height, buf); 
#else
        //yuv420p to rgb
        if (pFrameBuffer == NULL) {
            int numBytes = avpicture_get_size(PIX_FMT_RGB24, pDecCtx->width, pDecCtx->height);

            pFrameBuffer = (uint8_t *)av_malloc(numBytes * sizeof(uint8_t));

            avpicture_fill((AVPicture *)pFrameRGB, pFrameBuffer, PIX_FMT_RGB24, pDecCtx->width, pDecCtx->height);
        }

        SwsContext* pImgConvertCtx = NULL;
        pImgConvertCtx = sws_getCachedContext(pImgConvertCtx, pDecCtx->width, pDecCtx->height, pDecCtx->pix_fmt,
            pDecCtx->width, pDecCtx->height, PIX_FMT_RGB24, SWS_BICUBIC, NULL, NULL, NULL);

        sws_scale(pImgConvertCtx, (const uint8_t* const*)pFrame->data, pFrame->linesize, 0, 
            pDecCtx->height, pFrameRGB->data, pFrameRGB->linesize);

        //save to file
        SavePPM(pFrameRGB, pDecCtx->width, pDecCtx->height, *pnFrameCount);
        SaveYUV420P(pFrame, pDecCtx->width, pDecCtx->height, lpszOutFileName);
#endif

        (*pnFrameCount)++; 
    }

    if (pAVPacket->data) { 
        pAVPacket->size -= nLen; 
        pAVPacket->data += nLen; 
    } 
    return 0; 
} 

static void DecodeH264(const char* lpszInFileName, const char* lpszOutFileName) 
{ 
    printf("Decode video file %s to %s\n", lpszInFileName, lpszOutFileName); 

    uint8_t InBuf[INBUF_SIZE + FF_INPUT_BUFFER_PADDING_SIZE]; 
    memset(InBuf + INBUF_SIZE, 0, FF_INPUT_BUFFER_PADDING_SIZE); 

    AVCodec* pCodec = avcodec_find_decoder(CODEC_ID_H264); 
    if (NULL == pCodec) { 
        fprintf(stderr, "!! find h264 decoder failed\n"); 
        exit(1); 
    } 

    AVCodecContext* pCodecContext = avcodec_alloc_context3(pCodec); 
    if (NULL == pCodecContext) { 
        fprintf(stderr, "!! Could not allocate video codec context\n"); 
        exit(1); 
    } 

    if (pCodec->capabilities & CODEC_CAP_TRUNCATED) {
        pCodecContext->flags |= CODEC_FLAG_TRUNCATED;
    }

    if (avcodec_open2(pCodecContext, pCodec, NULL) < 0) { 
        fprintf(stderr, "!! Could not open codec\n"); 
        exit(1); 
    } 

    FILE* pInFile = fopen(lpszInFileName, "rb"); 
    if (NULL == pInFile) { 
        fprintf(stderr, "Could not open %s\n", lpszInFileName); 
        exit(1); 
    } 

    AVFrame* pFrame = avcodec_alloc_frame(); 
    if (NULL == pFrame) { 
        fprintf(stderr, "Could not allocate video frame\n"); 
        exit(1); 
    } 

    AVFrame* pFrameRGB = avcodec_alloc_frame();
    if (NULL == pFrameRGB) { 
        fprintf(stderr, "Could not allocate RGB frame\n"); 
        exit(1); 
    }

    AVPacket Packet; 
    av_init_packet(&Packet); 


    int nFrameCount = 0; 
	//int idx = 0;
	//int sizes[256] = {3247, 233, 186, 313, 254, 325, 307, 278, 380, 190, 263};
    for(;;) { 
		//int len = sizes[idx];
        //avpkt.size = fread(inbuf, 1, len, f); 
		//idx++;
        Packet.size = GetNextNal(pInFile, InBuf);
        if (Packet.size == 0) {
            printf("end of file, exit");
            break; 
        }

        Packet.data = InBuf; 
        while (Packet.size > 0) {
            if (DecodeFrame(lpszOutFileName, pCodecContext, pFrame, pFrameRGB, &nFrameCount, &Packet, false) < 0) {
                printf("decode_write_frame failed, exit\n");
                exit(1); 
            }
        }
    } 

    Packet.data = NULL; 
    Packet.size = 0; 
    DecodeFrame(lpszOutFileName, pCodecContext, pFrame, pFrameRGB, &nFrameCount, &Packet, true); 

    fclose(pInFile); 

    avcodec_close(pCodecContext); 
    av_free(pCodecContext); 
    avcodec_free_frame(&pFrame);
    av_free(pFrameBuffer);
    avcodec_free_frame(&pFrameRGB); 
}

int _tmain(int argc, _TCHAR* argv[])
{
    //register all the codecs
    avcodec_register_all();

    DecodeH264("test.h264", "test_out.yuv");
    return 0;
}


