/*****************************************************************************************
Copyright 2011 Rafael Muñoz Salinas. All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are
permitted provided that the following conditions are met:

   1. Redistributions of source code must retain the above copyright notice, this list of
      conditions and the following disclaimer.

   2. Redistributions in binary form must reproduce the above copyright notice, this list
      of conditions and the following disclaimer in the documentation and/or other materials
      provided with the distribution.

THIS SOFTWARE IS PROVIDED BY Rafael Muñoz Salinas ''AS IS'' AND ANY EXPRESS OR IMPLIED
WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL Rafael Muñoz Salinas OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

The views and conclusions contained in the software and documentation are those of the
authors and should not be interpreted as representing official policies, either expressed
or implied, of Rafael Muñoz Salinas.
********************************************************************************************/
#include <iostream>
#include <fstream>
#include <sstream>
#include <aruco/aruco.h>
#include <aruco/cvdrawingutils.h>


//for server
#include <algorithm>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <list>
#include <set>
#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/asio.hpp>
#include <boost/thread.hpp>
#include <boost/bind.hpp>
#include <boost/smart_ptr.hpp>
#include <boost/asio.hpp>
#include <boost/thread.hpp>

#include "Controller.hpp"
//#include "server.hpp"


using namespace cv;
using namespace aruco;

string TheInputVideo;
string TheIntrinsicFile;
float TheMarkerSize=-1;
int ThePyrDownLevel;
bool gui = 0;
MarkerDetector MDetector;
VideoCapture TheVideoCapturer;
vector<Marker> TheMarkers;
Mat TheInputImage,TheInputImageCopy;
CameraParameters TheCameraParameters;
Controller *controller;
void cvTackBarEvents(int pos,void*);
bool readCameraParameters(string TheIntrinsicFile,CameraParameters &CP,Size size);

pair<double,double> AvrgTime(0,0) ;//determines the average time required for detection
double ThresParam1,ThresParam2;
int iThresParam1,iThresParam2;
int waitTime=0;

bool readArguments ( int argc,char **argv )
{
    if (argc<2) {
        cerr<<"Invalid number of arguments"<<endl;
        cerr<<"Usage: (in.avi|live) [intrinsics.yml] [marker size in m] [gui 0 or 1]"<<endl;
        return false;
    }
    TheInputVideo=argv[1];
    if (argc>=3)
        TheIntrinsicFile=argv[2];
    if (argc>=4)
        TheMarkerSize=atof(argv[3]);
    if (argc>=5)
        gui = atoi(argv[4]);
    if (argc==3)
        cerr<<"NOTE: You need makersize to see 3d info!!!!"<<endl;
    return true;
}

using boost::asio::ip::tcp;
const int max_length = 1024;
typedef boost::shared_ptr<tcp::socket> socket_ptr;

void session(socket_ptr sock, Controller *controller) {
    try {
        //listen for incoming data
        for (;;) {
          char data[max_length];
          printf("got data %s\n", data );

          //Parse the command type
          if (data[0] == 'C') {
            printf("command found %s", &data[1]);
            controller->command(data);
          }
          else if (data[0]=='s' || data[0]=='S')
            printf("Stop command found");
            controller->command("C1000");//set throttle to 0
          }

          //Read some data
        boost::system::error_code error;
        size_t length = sock->read_some(boost::asio::buffer(data), error);
        if (error == boost::asio::error::eof) {
                printf("Connection closed \n");
                controller->sock.reset();
                break; // Connection closed cleanly by peer.
            }
            else if (error)
            throw boost::system::system_error(error); // Some other error.
        //Two ways so send back data
        //boost::asio::write(*sock, boost::asio::buffer(data, length));
        //sock->send(boost::asio::buffer("data back \n", 10));
        }
    } catch (std::exception& e) {
        std::cerr << "Exception in thread: " << e.what() << "\n";
    }
}

//The main tcp server function.
void server_loop(Controller *controller)
{
    boost::asio::io_service io_service;
    tcp::acceptor a(io_service, tcp::endpoint(tcp::v4(), 8080));
    //Keep connections open and then shove a socket in when a connection has been found.
    for (;;) {
        socket_ptr sock(new tcp::socket(io_service));
        a.accept(*sock);
        controller->sock = sock;
        //Launch the session function
        boost::thread t(boost::bind(session, sock, controller));
    }
}

int main(int argc,char **argv)
{
    controller = new Controller();

    //Start up a new thread to run the server
    boost::thread server_thread(boost::bind(server_loop, controller));

    try {
        if (readArguments (argc,argv)==false) {
            return 0;
        }

        if (TheInputVideo=="live") {
            TheVideoCapturer.open(0);
            waitTime=10;
        }
        else  TheVideoCapturer.open(TheInputVideo);
        if (!gui) {
            TheVideoCapturer.set(CV_CAP_PROP_FRAME_WIDTH, 320);
            TheVideoCapturer.set(CV_CAP_PROP_FRAME_HEIGHT, 240);
        }

        //TheVideoCapturer.set(CV_CAP_PROP_FPS, 5);

        //check video is open
        if (!TheVideoCapturer.isOpened()) {
            cerr<<"Could not open video"<<endl;
            return -1;
        }

        //read first image to get the dimensions
        TheVideoCapturer>>TheInputImage;

        //read camera parameters if passed
        if (TheIntrinsicFile!="") {
            TheCameraParameters.readFromXMLFile(TheIntrinsicFile);
            TheCameraParameters.resize(TheInputImage.size());
        }
        //Configure other parameters
        if (ThePyrDownLevel>0)
            MDetector.pyrDown(ThePyrDownLevel);

        //Create gui
        if (gui) {
            cv::namedWindow("thres",1);
            cv::namedWindow("in",1);
            MDetector.getThresholdParams( ThresParam1,ThresParam2);
            MDetector.setCornerRefinementMethod(MarkerDetector::LINES);
            iThresParam1=ThresParam1;
            iThresParam2=ThresParam2;
            cv::createTrackbar("ThresParam1", "in",&iThresParam1, 13, cvTackBarEvents);
            cv::createTrackbar("ThresParam2", "in",&iThresParam2, 13, cvTackBarEvents);
        }
        char key=0;
        int index=0;

        //capture until press ESC or until the end of the video
        while ( key!=27 && TheVideoCapturer.grab())
        {


            TheVideoCapturer.retrieve( TheInputImage);

            vector<uchar> buf;
            //prepend length of buffer to beginning



            imencode(".jpg", TheInputImage, buf);

            unsigned long int len = buf.size();
            uchar * bufLen = (uchar *) &len;
            std::vector<uchar> len_vec(bufLen, bufLen + sizeof(unsigned long));
            buf.insert(buf.begin(), len_vec.begin(), len_vec.end());

            if (controller->sock) {
                printf("writing over socket!!!\n");
                //controller->sock->send(boost::asio::buffer("BREAKBREAKBREAK",15));
                controller->sock->send(boost::asio::buffer(buf));
                //controller->sock->send(boost::asio::buffer("BREAKBREAKBREAK",15));
            }

            //copy image

            index++; //number of images captured
            double tick = (double)getTickCount();//for checking the speed
            //Detection of markers in the image passed
            MDetector.detect(TheInputImage,TheMarkers,TheCameraParameters,TheMarkerSize);
            //chekc the speed by calculating the mean speed of all iterations
            AvrgTime.first+=((double)getTickCount()-tick)/getTickFrequency();
            AvrgTime.second++;
            //cout<<"Time detection="<<1000*AvrgTime.first/AvrgTime.second<<" milliseconds"<<endl;
            //print marker info and draw the markers in image
            TheInputImage.copyTo(TheInputImageCopy);
            if (gui) {
                for (unsigned int i=0;i<TheMarkers.size();i++) {
                    //cout<<TheMarkers[i]<<endl;
                    TheMarkers[i].draw(TheInputImageCopy,Scalar(0,0,255),1);
                }

                //draw a 3d cube in each marker if there is 3d info
                if (  TheCameraParameters.isValid()) {
                    for (unsigned int i=0;i<TheMarkers.size();i++) {
                        CvDrawingUtils::draw3dCube(TheInputImageCopy,TheMarkers[i],TheCameraParameters);
                        CvDrawingUtils::draw3dAxis(TheInputImageCopy,TheMarkers[i],TheCameraParameters);
                    }
                }
            }

            for (unsigned int i=0;i<TheMarkers.size();i++) {
                //TheMarkers.size = .0508;
                double pos[3];
                double rot[4];

                //cout << TheMarkers[i].Rvec.at<float>(0,0)/3.1415*180 << " " << TheMarkers[i].Rvec.at<float>(1,0)/3.1415*180 << " " << TheMarkers[i].Rvec.at<float>(2,0)/3.1415*180 << " " << endl;// << "     " << TheMarkers[i].Tvec << endl;
                controller->controlMarker(TheMarkers[i]);
                //Aligns with the xyz cross hatch.
                //x,y origin are center of screen
                //z is

                //cout << TheMarkers[i].getPerimeter() << endl;
            }

            if (gui) {
                //show input with augmented information and  the thresholded image
                cv::imshow("in",TheInputImageCopy);
                cv::imshow("thres",MDetector.getThresholdedImage());
            }

            key=cv::waitKey(waitTime);//wait for key to be pressed
        }

    } catch (std::exception &ex) {
        cout<<"Exception :"<<ex.what()<<endl;
    }

}


void cvTackBarEvents(int pos,void*)
{
    if (iThresParam1<3) iThresParam1=3;
    if (iThresParam1%2!=1) iThresParam1++;
    if (ThresParam2<1) ThresParam2=1;
    ThresParam1=iThresParam1;
    ThresParam2=iThresParam2;
    MDetector.setThresholdParams(ThresParam1,ThresParam2);
    //recompute
    MDetector.detect(TheInputImage,TheMarkers,TheCameraParameters);
    TheInputImage.copyTo(TheInputImageCopy);
    for (unsigned int i=0;i<TheMarkers.size();i++)	TheMarkers[i].draw(TheInputImageCopy,Scalar(0,0,255),1);

    //draw a 3d cube in each marker if there is 3d info
    if (TheCameraParameters.isValid())
        for (unsigned int i=0;i<TheMarkers.size();i++)
            CvDrawingUtils::draw3dCube(TheInputImageCopy,TheMarkers[i],TheCameraParameters);

    cv::imshow("in",TheInputImageCopy);
    cv::imshow("thres",MDetector.getThresholdedImage());
}


