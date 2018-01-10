#include "opencv2/imgproc/imgproc.hpp"
#include "opencv2/highgui/highgui.hpp"

#include "cv.h"
#include "highgui.h"
#include <stdio.h>
#include <ctype.h>
#include <iostream>
#include <XnCppWrapper.h>
#include <time.h>

using namespace std;
using namespace cv;

int backproject_mode = 0;
int select_object = 0;
int track_object = 0;
int show_hist = 1;
int print_num=0;   //��Ƭ��������ʼ��

CvPoint origin;
CvRect selection;
CvRect track_window;
CvBox2D track_box;
CvConnectedComp track_comp;
CvPoint2D32f center,center_temp,center_temperater;

int hdims=16;
float hranges_arr[]={0,180};
float* hranges=hranges_arr;
int vmin=10,vmax=256,smin=30;

IplImage *image = 0, *hsv = 0, *hue = 0, *mask = 0, *backproject = 0, *histimg = 0;
CvHistogram *hist = 0;

//��Ӧ����¼�
void on_mouse(int event,int x,int y,int flags,void* param)
{
	if(!image)
		return;

	if(image->origin)
		y = image->height - y;

	if(select_object)
	{
		selection.x = MIN(x,origin.x);
		selection.y = MIN(y,origin.y);
		selection.width = selection.x + CV_IABS(x - origin.x);
		selection.height = selection.y + CV_IABS(y - origin.y);

		selection.x = MAX(selection.x,0);
		selection.y = MAX(selection.y,0);
		selection.width = MIN(selection.width,image->width);
		selection.height = MIN(selection.height,image->height);
		selection.width -= selection.x;
		selection.height -= selection.y;
	}

	switch(event)
	{
	case CV_EVENT_LBUTTONDOWN:
		origin = cvPoint(x,y);
		selection = cvRect(x,y,0,0);
		select_object = 1;
		break;
	case CV_EVENT_LBUTTONUP:
		select_object = 0;
		if(selection.width>0&&selection.height>0)
		{
			track_object = -1;
			center=cvPoint2D32f(0.0,0.0);
		}
		break;
	}
}

//OpenNI������
void CheckOpenNIError(XnStatus eResult,string sStatus)
{
	if(eResult!=XN_STATUS_OK)
		cout<<sStatus<<"Error: "<<xnGetStatusString(eResult)<<endl;
}

int main(int argc, char** argv)
{
	XnStatus eResult=XN_STATUS_OK;
	xn::ImageMetaData imageMD;
	xn::DepthMetaData depthMD;

	//opencv
	IplImage* imgRGB8u=cvCreateImage(cvSize(640,480),IPL_DEPTH_8U,3);  
	IplImage* imageShow=cvCreateImage(cvSize(640,480),IPL_DEPTH_8U,3);  
	IplImage* imgDepth16u=cvCreateImage(cvSize(640,480),IPL_DEPTH_16U,1);  
	IplImage* depthShow=cvCreateImage(cvSize(640,480),IPL_DEPTH_8U,1);  
	//cvNamedWindow("depth",1);
	char key=0;
    CvFont font;
	cvInitFont(&font,CV_FONT_HERSHEY_DUPLEX ,1.0f,1.0f,0,1,CV_AA);

	//initial context
	xn::Context mContext;
	eResult=mContext.Init();
	CheckOpenNIError(eResult,"initial context");

	//create image generator
	xn::ImageGenerator mImageGenerator;
	eResult=mImageGenerator.Create(mContext);
	CheckOpenNIError(eResult,"Create image generator");

	//create depth generator
	xn::DepthGenerator mDepthGenerator;
	eResult=mDepthGenerator.Create(mContext);
	CheckOpenNIError(eResult,"Create depth generator");

	//set map mode
	XnMapOutputMode mapMode;
	mapMode.nXRes=640;
	mapMode.nYRes=480;
	mapMode.nFPS=15;
	eResult=mImageGenerator.SetMapOutputMode(mapMode);
	eResult=mDepthGenerator.SetMapOutputMode(mapMode);

	//correct view port    
	mDepthGenerator.GetAlternativeViewPointCap().SetViewPoint(mImageGenerator);   

	//start generate data
	eResult=mContext.StartGeneratingAll();
	eResult=mContext.WaitNoneUpdateAll();

	cout<< "Hot keys: \n"
		"\tESC or s - quit tracking\n"
		"To initialize tracking, select the object with mouse\n";

	cvNamedWindow("CamShift",1);
	cvMoveWindow("CamShift",400,200);
	cvSetMouseCallback("CamShift",on_mouse,0);//��ȡROI����

	while((key!=27)&&!(eResult= mContext.WaitNoneUpdateAll( )))
	{
		mImageGenerator.GetMetaData(imageMD);
		//mDepthGenerator.GetMetaData(depthMD);
		memcpy(imgRGB8u->imageData,imageMD.Data(),640*480*3);
		cvCvtColor(imgRGB8u,imageShow,CV_RGB2BGR);
		//memcpy(imgDepth16u->imageData,depthMD.Data(),640*480*2);  
		//cvConvertScale(imgDepth16u,depthShow,255/4096.0,0);  
		/*cvShowImage("depth",depthShow);
		cvMoveWindow("depth",700,200);*/
		int c;
		if( !imageShow )
			break;

		//����ռ�
		if( !image )
		{
			image = cvCreateImage(cvGetSize(imageShow),8,3);
			image->origin = imageShow->origin;
			hsv = cvCreateImage(cvGetSize(imageShow),8,3);
			hue = cvCreateImage(cvGetSize(imageShow),8,1);
			mask = cvCreateImage(cvGetSize(imageShow),8,1);
			backproject = cvCreateImage(cvGetSize(imageShow),8,1);
			hist = cvCreateHist(1,&hdims,CV_HIST_ARRAY, &hranges,1);//����ֱ��ͼ			
		}

		cvCopy(imageShow,image,0);
		cvCvtColor(image,hsv,CV_BGR2HSV);//ת����hsv��ɫ�ռ�

		if(track_object!=0)
		{
			int _vmin = vmin, _vmax = vmax;

			//�������Ԫ���Ƿ���������֮��
			cvInRangeS(hsv, cvScalar(0,smin,MIN(_vmin,_vmax),0),cvScalar(180,256,MAX(_vmin,_vmax),0), mask);
			cvSplit(hsv, hue, 0, 0, 0); //����hsvͼ�����ͨ��,���H����

			if( track_object < 0 )
			{
				float max_val = 0.f;
				cvSetImageROI(hue,selection);
				cvSetImageROI(mask,selection);
				cvCalcHist(&hue,hist,0, mask);
				cvGetMinMaxHistValue(hist,0,&max_val,0,0);//��ȡֱ��ͼ�������ֵ
				cvConvertScale(hist->bins,hist->bins,max_val?255./max_val:0.,0);//ʹ�����Ա任ת������ 
				cvResetImageROI(hue);
				cvResetImageROI(mask);
				track_window = selection;
				track_object = 1;
			}

			cvCalcBackProject(&hue, backproject, hist);//����hue�ķ���ͶӰͼ
			cvAnd(backproject, mask, backproject, 0);//�õ���Ĥ�ڵķ���ͶӰ
			//camshift�㷨Ӧ��
			cvCamShift( backproject, track_window,cvTermCriteria(CV_TERMCRIT_EPS|CV_TERMCRIT_ITER,10,1),&track_comp, &track_box );
			track_window = track_comp.rect;//�õ����ٽ���ľ��ο�

			if( image->origin )
				track_box.angle = -track_box.angle;

			//�������ٽ����λ��
			cvEllipseBox(image,track_box,CV_RGB(255,0,0),1,CV_AA,0);
			//cvRectangle(image,cvPoint(track_window.x,track_window.y),cvPoint(track_window.x+track_window.width,track_window.y+track_window.height),CV_RGB(255,0,0),1,CV_AA,0);
			//������ֵ��ֵ
			center=track_box.center;
			//cout<<"��������Ϊ��"<<"("<<center.x<<","<<center.y<<")"<<endl;
			 
			/*�����ɫͼ��ͶӰ�����ͼ�������꣨x,y�����Լ��ڸõ��zֵ������
			����������������ҵ��õ������ͼ���е�����ֵ��Ҳ�����*/
			//ʵʱ���㲢������ٽ�����ĵ���ά����
	        XnPoint3D proP,realP;
			unsigned int idx;
			idx=center.y*640+center.x;//���������ͼ���ϵ�����
			proP.X=center.x;
			proP.Y=center.y;
			proP.Z=mDepthGenerator.GetDepthMap()[idx];//����λ��mmת��Ϊm
			mDepthGenerator.ConvertProjectiveToRealWorld(1,&proP,&realP);

			if(realP.X==0||realP.Y==0||realP.Z==0)
				continue;
			cout<<"֡_"<<print_num<<"����ά����Ϊ��"<<"("<<realP.X<<","<<realP.Y<<","<<realP.Z<<")"<<endl;
			Sleep(1000);//����ÿһ֡��ͣ�ٵ�ʱ��
		}  

		if(select_object&&selection.width>0&&selection.height>0)//�������������ѡ�񣬻���ѡ���
		{
			cvSetImageROI(image,selection);
			cvXorS(image,cvScalarAll(255),image,0);
			cvResetImageROI(image);
		}

		cvShowImage("CamShift",image);

		//����ÿһ֡��Ƶ����
	    char frame_name[13];
		sprintf(frame_name, "%s%d%s", "֡_", print_num, ".jpg");//�����ͼƬ��
        cvSaveImage(frame_name,image);   //����һ֡ͼƬ
		print_num++;
		c = cvWaitKey(1000);//����֡��ͣ�ٵ�ʱ���������ά����ÿ֡��ļ��ʱ����ͬ

		if((char) c == 'p')
			c=cvWaitKey(0);
		if((char) c == 27)
			break;
		if(((char) c) == 's')
		{
			track_object=0;
			break;
		}
	}
	//cvDestroyWindow("depth");
	cvDestroyWindow("CamShift");
	cvReleaseImage(&imgDepth16u); 
	cvReleaseImage(&imgRGB8u);  
	cvReleaseImage(&depthShow); 
	cvReleaseImage(&imageShow);  
	mContext.StopGeneratingAll();  
	mContext.Shutdown();  
	return 0;
}

