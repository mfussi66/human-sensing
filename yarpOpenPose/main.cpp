/*
 * Copyright (C) 2019 iCub Facility - Istituto Italiano di Tecnologia
 * Author: Vadim Tikhanoff
 * email:  vadim.tikhanoff@iit.it
 * Permission is granted to copy, distribute, and/or modify this program
 * under the terms of the GNU General Public License, version 2 or any
 * later version published by the Free Software Foundation.
 *
 * A copy of the license can be found at
 * http://www.robotcub.org/icub/license/gpl.txt
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
 * Public License for more details
 */

// C++ std library dependencies
#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>
#include <array>

// OpenCV dependencies
#include <opencv2/core/core.hpp>
#include <opencv2/opencv.hpp>

// Other 3rdpary depencencies
//#include <gflags/gflags.h> // DEFINE_bool, DEFINE_int32, DEFINE_int64, DEFINE_uint64, DEFINE_double, DEFINE_string
//#include <glog/logging.h> // google::InitGoogleLogging, CHECK, CHECK_EQ, LOG, VLOG, ...

// OpenPose dependencies
 #include <openpose/core/headers.hpp>
 #include <openpose/filestream/headers.hpp>
 #include <openpose/gui/headers.hpp>
 #include <openpose/pose/headers.hpp>
 #include <openpose/producer/headers.hpp>
 #include <openpose/thread/headers.hpp>
 #include <openpose/utilities/headers.hpp>
 #include <openpose/wrapper/headers.hpp>
//#include <openpose/flags.hpp>

// yarp dependencies
#include <yarp/os/BufferedPort.h>
#include <yarp/os/ResourceFinder.h>
#include <yarp/os/RFModule.h>
#include <yarp/os/Network.h>
#include <yarp/os/Log.h>
#include <yarp/os/Time.h>
#include <yarp/os/LogStream.h>
#include <yarp/os/Semaphore.h>
#include <yarp/sig/Image.h>
#include <yarp/os/RpcClient.h>
#include <yarp/os/Stamp.h>
#include <yarp/cv/Cv.h>

/********************************************************/
class ImageInput : public op::WorkerProducer<std::shared_ptr<std::vector<std::shared_ptr<op::Datum>>>>
{
private:

    yarp::sig::ImageOf<yarp::sig::PixelRgb> *inImage;
    bool mClosed;
public:
    /********************************************************/
    ImageInput() : mClosed{false}
    {
    }

    /********************************************************/
    ~ImageInput()
    {
    }

    /********************************************************/
    void initializationOnThread() {

    }

    /********************************************************/
    void setImage(yarp::sig::ImageOf<yarp::sig::PixelRgb> &inImage) {
        this->inImage = &inImage;
    }

    /********************************************************/
    std::shared_ptr<std::vector<std::shared_ptr<op::Datum>>> workProducer()
    {
        if (mClosed)
        {
            mClosed = true;
            return nullptr;
        }
        else
        {
            // Create new datum
            auto datumsPtr = std::make_shared<std::vector<std::shared_ptr<op::Datum>>>();
            datumsPtr->emplace_back();
            auto& datum = datumsPtr->at(0);
            datum = std::make_shared<op::Datum>();

            if (inImage->width() * inImage->height() > 0)
            {
                const cv::Mat in_cv = yarp::cv::toCvMat(*inImage);
                // Fill datum
                datum->cvInputData = OP_CV2OPCONSTMAT(in_cv);
                // If empty frame -> return nullptr
                if (datum->cvInputData.empty())
                {
                    mClosed = true;
                    op::opLog("Empty frame detected. Closing program.", op::Priority::Max);
                    datumsPtr = nullptr;
                }
            }
            else
            {
                datumsPtr = nullptr;
            }
            return datumsPtr;
        }
    }

    /********************************************************/
    bool isFinished() const
    {
        return mClosed;
    }
};

/********************************************************/
class ImageProcessing : public op::Worker<std::shared_ptr<std::vector<std::shared_ptr<op::Datum>>>>
{
private:
    std::string moduleName;
    yarp::os::BufferedPort<yarp::os::Bottle>  targetPort;
    yarp::os::Stamp stamp;
public:
    std::map<unsigned int, std::string> mapParts {
        {0,  "Nose"},
        {1,  "Neck"},
        {2,  "RShoulder"},
        {3,  "RElbow"},
        {4,  "RWrist"},
        {5,  "LShoulder"},
        {6,  "LElbow"},
        {7,  "LWrist"},
        {8,  "MidHip"},
        {9,  "RHip"},
        {10, "RKnee"},
        {11, "RAnkle"},
        {12, "LHip"},
        {13, "LKnee"},
        {14, "LAnkle"},
        {15, "REye"},
        {16, "LEye"},
        {17, "REar"},
        {18, "LEar"},
        {19, "LBigToe"},
        {20, "LSmallToe"},
        {21, "LHeel"},
        {22, "RBigToe"},
        {23, "RSmallToe"},
        {24, "RHeel"},
        {25, "Background"}
    };
    
    std::map<unsigned int, std::string> mapPartsCoco {
        {0,  "Nose"},
        {1,  "Neck"},
        {2,  "RShoulder"},
        {3,  "RElbow"},
        {4,  "RWrist"},
        {5,  "LShoulder"},
        {6,  "LElbow"},
        {7,  "LWrist"},
        {8,  "RHip"},
        {9,  "RKnee"},
        {10, "RAnkle"},
        {11, "LHip"},
        {12, "LKnee"},
        {13, "LAnkle"},
        {14, "REye"},
        {15, "LEye"},
        {16, "REar"},
        {17, "LEar"},
        {18, "Background"}
    };

    /********************************************************/
    ImageProcessing(const std::string& moduleName)
    {
        this->moduleName = moduleName;
    }

    /********************************************************/
    ~ImageProcessing()
    {
        targetPort.close();
    }

    /********************************************************/
    void initializationOnThread()
    {
        targetPort.open("/"+ moduleName + "/target:o");
    }

    /********************************************************/
    void setStamp(const yarp::os::Stamp &stamp)
    {
        this->stamp = stamp;
    }

    /********************************************************/
    void work(std::shared_ptr<std::vector<std::shared_ptr<op::Datum>>>& datumsPtr)
    {
        if (datumsPtr != nullptr && !datumsPtr->empty())
        {
            yarp::os::Bottle &peopleBottle = targetPort.prepare();
            peopleBottle.clear();
            yarp::os::Bottle &mainList = peopleBottle.addList();
            auto& tDatumsNoPtr = *datumsPtr;
            //Record people pose data
            op::Array<float> pose(tDatumsNoPtr.size());
            op::Array<float> face(tDatumsNoPtr.size());
            for (auto i = 0; i < tDatumsNoPtr.size(); i++)
            {
                pose = tDatumsNoPtr[i]->poseKeypoints;
                face = tDatumsNoPtr[i]->faceKeypoints;

                if (!pose.empty() && pose.getNumberDimensions() != 3)
                    op::error("pose.getNumberDimensions() != 3.", __LINE__, __FUNCTION__, __FILE__);

                const auto numberPeople = pose.getSize(0);
                const auto numberBodyParts = pose.getSize(1);
                const auto numberKeypoints = pose.getSize(2);

                const auto numberFaceParts = face.getSize(1);
                const auto numberFaceKeypoints = face.getSize(2);

                for (auto person = 0 ; person < numberPeople ; person++)
                {
                    yarp::os::Bottle &peopleList = mainList.addList();
                    for (auto bodyPart = 0 ; bodyPart < numberBodyParts ; bodyPart++)
                    {
                        yarp::os::Bottle &partList = peopleList.addList();
                        const auto finalIndex = pose.getSize(2)*(person*numberBodyParts + bodyPart);
                        
                        if (numberBodyParts < 19 )
                            partList.addString(mapPartsCoco[bodyPart].c_str());
                        else
                            partList.addString(mapParts[bodyPart].c_str());
                        
                        partList.addFloat64(pose[finalIndex]);
                        partList.addFloat64(pose[finalIndex+1]);
                        partList.addFloat64(pose[finalIndex+2]);

                    }
                    
                    if (!face.empty())
                    {
                        yarp::os::Bottle &partList = peopleList.addList();
                        partList.addString("Face");
                        for (auto facePart = 0 ; facePart < numberFaceParts ; facePart++)
                        {   

                            const auto faceIndex = face.getSize(2)*(person*numberFaceKeypoints + facePart);
                            yarp::os::Bottle &faceList = partList.addList();
                            faceList.addFloat64(face[faceIndex]);
                            faceList.addFloat64(face[faceIndex + 1]);
                            faceList.addFloat64(face[faceIndex + 2]);                                           
                        }
                    }
                }
            }
            if (peopleBottle.size())
            {
                targetPort.setEnvelope(stamp);
                targetPort.write();
            }
        }
    }
};

/**********************************************************/
class ImageOutput : public op::WorkerConsumer<std::shared_ptr<std::vector<std::shared_ptr<op::Datum>>>>
{
private:
    std::string moduleName;
    yarp::os::BufferedPort<yarp::sig::ImageOf<yarp::sig::PixelRgb> > outPort;
    yarp::os::BufferedPort<yarp::sig::ImageOf<yarp::sig::PixelRgb> > outPortPropag;
    yarp::os::BufferedPort<yarp::sig::ImageOf<yarp::sig::PixelFloat> > outFloatPort;
    yarp::sig::ImageOf<yarp::sig::PixelFloat> *inFloat;

    std::mutex             mutex;

    bool sendFloat;
    yarp::os::Stamp stamp;
    yarp::os::Stamp stampFloat;

public:

    /********************************************************/
    ImageOutput(const std::string& moduleName)
    {
        this->moduleName = moduleName;

    }

    /********************************************************/
    void initializationOnThread()
    {
        outPort.open("/" + moduleName + "/image:o");
        outFloatPort.open("/" + moduleName + "/float:o");
        outPortPropag.open("/" + moduleName + "/propag:o");
        sendFloat = false;
    }

    void setFlag(const bool flag) {
        sendFloat = flag;

    }
    /********************************************************/
    void setStamp(const yarp::os::Stamp &stamp)
    {
        this->stamp = stamp;
    }

    /********************************************************/
    void setImage(yarp::sig::ImageOf<yarp::sig::PixelFloat> &inFloat, const yarp::os::Stamp &stamp) {
        const std::lock_guard<std::mutex> lock(mutex);
        this->inFloat = &inFloat;
        this->stampFloat = stamp;
    }


    /********************************************************/
    ~ImageOutput()
    {
        outPort.close();
        outPortPropag.close();
	    outFloatPort.close();
    }

    /********************************************************/
    void workConsumer(const std::shared_ptr<std::vector<std::shared_ptr<op::Datum>>>& datumsPtr)
    {
        if (datumsPtr != nullptr && !datumsPtr->empty())
        {
            yarp::sig::ImageOf<yarp::sig::PixelRgb> &outImage  = outPort.prepare();
            yarp::sig::ImageOf<yarp::sig::PixelRgb> &outImagePropag  = outPortPropag.prepare();

            const std::lock_guard<std::mutex> lock(mutex);

            if (sendFloat)
            {
                yarp::sig::ImageOf<yarp::sig::PixelFloat> &outImageFloat  = outFloatPort.prepare();
                outImageFloat = *inFloat;
            }

            outImage.resize(OP_OP2CVCONSTMAT(datumsPtr->at(0)->cvOutputData).cols, OP_OP2CVCONSTMAT(datumsPtr->at(0)->cvOutputData).rows);
            outImagePropag.resize(OP_OP2CVCONSTMAT(datumsPtr->at(0)->cvInputData).cols, OP_OP2CVCONSTMAT(datumsPtr->at(0)->cvInputData).rows);

            cv::Mat colour = OP_OP2CVCONSTMAT(datumsPtr->at(0)->cvOutputData);
            outImage = yarp::cv::fromCvMat<yarp::sig::PixelRgb>(colour);

            cv::Mat colourOrig = OP_OP2CVCONSTMAT(datumsPtr->at(0)->cvInputData);
            outImagePropag = yarp::cv::fromCvMat<yarp::sig::PixelRgb>(colourOrig);

            outPort.setEnvelope(stamp);
            outPort.write();
            outPortPropag.setEnvelope(stamp);
            outPortPropag.write();

            if (sendFloat)
            {
                outFloatPort.setEnvelope(stampFloat);
                outFloatPort.write();
            }

            sendFloat = false;
        }
        else
            op::opLog("Nullptr or empty datumsPtr found.", op::Priority::Max, __LINE__, __FUNCTION__, __FILE__);
    }
};

/********************************************************/
class Module : public yarp::os::RFModule
{
private:
    yarp::os::ResourceFinder    *rf;
    yarp::os::RpcServer         rpcPort;
    yarp::os::BufferedPort<yarp::sig::ImageOf<yarp::sig::PixelRgb> > inPort;
    yarp::os::BufferedPort<yarp::sig::ImageOf<yarp::sig::PixelFloat> > inFloatPort;

    std::string                 model_name;
    std::string                 model_folder;
    std::string                 net_resolution;
    std::string                 img_resolution;
    int                         num_gpu;
    int                         num_gpu_start;
    int                         num_scales;
    float                       scale_gap;
    float                       net_input_size_dynamic_behavior;
    int                         keypoint_scale;
    bool                        heatmaps_add_parts;
    bool                        heatmaps_add_bkg;
    bool                        heatmaps_add_PAFs;
    int                         heatmaps_scale_mode;
    int                         render_pose;
    int                         part_to_show;
    bool                        disable_blending;
    double                      alpha_pose;
    double                      alpha_heatmap;
    double                      render_threshold;
    bool                        part_candidates;
    int                         number_people_max;
    bool                        body_enable;
    bool                        hand_enable;
    std::string                 hand_net_resolution;
    int                         hand_scale_number;
    double                      hand_scale_range;
    bool                        hand_tracking;
    double                      hand_alpha_pose;
    double                      hand_alpha_heatmap;
    double                      hand_render_threshold;
    int                         hand_render;
    bool                        face_enable;
    std::string                 face_net_resolution;
    int                         face_detector;
    double                      face_render_threshold;
    int                         face_render;
    double                      face_alpha_pose;
    double                      face_alpha_heatmap;
    bool                        flags_3d;
    int                         views_3d;
        
    ImageInput                  *inputClass;
    ImageProcessing             *processingClass;
    ImageOutput                 *outputClass;

    op::WrapperT<op::Datum> opWrapper{op::ThreadManagerMode::Asynchronous};

    bool                        closing;

public:
    /********************************************************/
    bool configure(yarp::os::ResourceFinder &rf)
    {
        this->rf=&rf;
        std::string moduleName = rf.check("name", yarp::os::Value("yarpOpenPose"), "module name (string)").asString();

        model_name = rf.check("model_name", yarp::os::Value("BODY_25"), "Model to be used e.g. COCO, MPI, MPI_4_layers. (string)").asString();
        model_folder = rf.check("model_folder", yarp::os::Value("/models"), "Folder where the pose models (COCO and MPI) are located. (string)").asString();
        net_resolution = rf.check("net_resolution", yarp::os::Value("656x368"), "The resolution of the net, multiples of 16. (string)").asString();
        net_input_size_dynamic_behavior = rf.check("net_input_dynamic_behavior", yarp::os::Value(1), "Indicates how to handle dynamic behaviour.(float)").asFloat64();
        img_resolution = rf.check("img_resolution", yarp::os::Value("320x240"), "The resolution of the image (display and output). (string)").asString();
        num_gpu = rf.check("num_gpu", yarp::os::Value(1), "The number of GPU devices to use.(int)").asInt32();
        num_gpu_start = rf.check("num_gpu_start", yarp::os::Value(0), "The GPU device start number.(int)").asInt32();
        num_scales = rf.check("num_scales", yarp::os::Value(1), "Number of scales to average.(int)").asInt32();
        scale_gap = rf.check("scale_gap", yarp::os::Value(0.3), "Scale gap between scales. No effect unless num_scales>1. Initial scale is always 1. If you want to change the initial scale,"
                                                                " you actually want to multiply the `net_resolution` by your desired initial scale.(float)").asFloat64();

        keypoint_scale = rf.check("keypoint_scale", yarp::os::Value(0), "Scaling of the (x,y) coordinates of the final pose data array (op::Datum::pose), i.e. the scale of the (x,y) coordinates that"
                                                                " will be saved with the `write_pose` & `write_pose_json` flags. Select `0` to scale it to the original source resolution, `1`"
                                                                " to scale it to the net output size (set with `net_resolution`), `2` to scale it to the final output size (set with "
                                                                " `resolution`), `3` to scale it in the range [0,1], and 4 for range [-1,1]. Non related with `num_scales` and `scale_gap`.(int)").asInt32();

        heatmaps_add_parts = rf.check("heatmaps_add_parts", yarp::os::Value(false), "If true, it will add the body part heatmaps to the final op::Datum::poseHeatMaps array (program speed will decrease). Not"
                                                                " required for our library, enable it only if you intend to process this information later. If more than one `add_heatmaps_X`"
                                                                " flag is enabled, it will place then in sequential memory order: body parts + bkg + PAFs. It will follow the order on"
                                                                " POSE_BODY_PART_MAPPING in `include/openpose/pose/poseParameters.hpp`.(bool)").asBool();
        heatmaps_add_bkg = rf.check("heatmaps_add_bkg", yarp::os::Value(false), "Same functionality as `add_heatmaps_parts`, but adding the heatmap corresponding to background. (bool)").asBool();

        heatmaps_add_PAFs = rf.check("heatmaps_add_PAFs", yarp::os::Value(false),"Same functionality as `add_heatmaps_parts`, but adding the PAFs.(bool)").asBool();
        heatmaps_scale_mode = rf.check("heatmaps_scale_mode", yarp::os::Value(2), "Set 0 to scale op::Datum::poseHeatMaps in the range [0,1], 1 for [-1,1]; and 2 for integer rounded [0,255].(int)").asInt32();
        //no_render_output = rf.check("no_render_output", yarp::os::Value("false"), "If false, it will fill image with the original image + desired part to be shown. If true, it will leave them empty.(bool)").asBool();
        render_pose = rf.check("render_pose", yarp::os::Value(2), "Set to 0 for no rendering, 1 for CPU rendering (slightly faster), and 2 for GPU rendering(int)").asInt32();
        part_to_show = rf.check("part_to_show", yarp::os::Value(0),"Part to show from the start.(int)").asInt32();
        disable_blending = rf.check("disable_blending", yarp::os::Value(false), "If false, it will blend the results with the original frame. If true, it will only display the results.").asBool();
        alpha_pose = rf.check("alpha_pose", yarp::os::Value(0.6), "Blending factor (range 0-1) for the body part rendering. 1 will show it completely, 0 will hide it.(double)").asFloat64();
        alpha_heatmap = rf.check("alpha_heatmap", yarp::os::Value(0.7), "Blending factor (range 0-1) between heatmap and original frame. 1 will only show the heatmap, 0 will only show the frame.(double)").asFloat64();
        render_threshold = rf.check("render_threshold", yarp::os::Value(0.05), "Only estimated keypoints whose score confidences are higher than this threshold will be rendered. Generally, a high threshold (> 0.5) will only render very clear body parts.(double)").asFloat64();
        number_people_max = rf.check("number_people_max", yarp::os::Value(-1), "This parameter will limit the maximum number of people detected, by keeping the people with top scores. -1 will keep them all.(int)").asInt32();
        part_candidates = rf.check("part_candidates", yarp::os::Value(false), "If true it will fill the op::Datum::poseCandidates array with the body part candidates.(bool)").asBool();
        body_enable = rf.check("body_enable", yarp::os::Value(true), "Disable body keypoint detection. Option only possible for faster (but less accurate) face. (bool)").asBool();
        hand_enable = rf.check("hand_enable", yarp::os::Value(false), "Enables hand keypoint detection. It will share some parameters from the body pose, e.g."
                                                                " `model_folder`. Analogously to `--face`, it will also slow down the performance, increase"
                                                                " the required GPU memory and its speed depends on the number of people.(int)").asBool();
        hand_net_resolution = rf.check("hand_net_resolution", yarp::os::Value("368x368"), "Multiples of 16 and squared. Analogous to `net_resolution` but applied to the hand keypoint (string)").asString();
        hand_scale_number = rf.check("hand_scale_number", yarp::os::Value(1), "Analogous to `scale_number` but applied to the hand keypoint detector.(int)").asInt32();
        hand_scale_range = rf.check("hand_scale_range", yarp::os::Value(0.4), "Analogous purpose than `scale_gap` but applied to the hand keypoint detector. Total range"
                                                                " between smallest and biggest scale. The scales will be centered in ratio 1. E.g. if"
                                                                " scaleRange = 0.4 and scalesNumber = 2, then there will be 2 scales, 0.8 and 1.2.(double)").asFloat64();
        hand_tracking = rf.check("hand_tracking", yarp::os::Value(false), "Adding hand tracking might improve hand keypoints detection for webcam (if the frame rate"
                                                                " is high enough, i.e. >7 FPS per GPU) and video. This is not person ID tracking, it"
                                                                " simply looks for hands in positions at which hands were located in previous frames, but"
                                                                " it does not guarantee the same person ID among frames (bool)").asBool();
        hand_alpha_pose = rf.check("hand_alpha_pose", yarp::os::Value(0.6), "Analogous to `alpha_pose` but applied to hand.(double)").asFloat64();
        hand_alpha_heatmap = rf.check("hand_alpha_heatmap", yarp::os::Value(0.7), "Analogous to `alpha_heatmap` but applied to hand.(double)").asFloat64();
        hand_render_threshold = rf.check("hand_render_threshold", yarp::os::Value(0.2), "Analogous to `render_threshold`, but applied to the hand keypoints.(double)").asFloat64();
        hand_render = rf.check("hand_render", yarp::os::Value(-1), "Analogous to `render_pose` but applied to the hand. Extra option: -1 to use the same(int)").asInt32();
        face_enable = rf.check("face_enable", yarp::os::Value(false), "enables face keypoint detection. It will share some parameters from the body pose, e.g."
                                                        " `model_folder`. Note that this will considerable slow down the performance and increse"
                                                        " the required GPU memory. In addition, the greater number of people on the image, the"
                                                        " slower OpenPose will be.").asBool();

        face_net_resolution = rf.check("face_net_resolution", yarp::os::Value("368x368"), "Multiples of 16 and squared. Analogous to `net_resolution` but applied to the face keypoint"
                                                        " detector. 320x320 usually works fine while giving a substantial speed up when multiple"
                                                        " faces on the image.(string)").asString();
        face_detector = rf.check("face_detector", yarp::os::Value(0), "Kind of face rectangle detector. Select 0 (default) to select OpenPose body detector (most"
                                                        " accurate one and fastest one if body is enabled), 1 to select OpenCV face detector (not"
                                                        " implemented for hands), 2 to indicate that it will be provided by the user, or 3 to"
                                                        " also apply hand tracking (only for hand). Hand tracking might improve hand keypoint"
                                                        " detection for webcam (if the frame rate is high enough, i.e., >7 FPS per GPU) and video."
                                                        " This is not person ID tracking, it simply looks for hands in positions at which hands were"
                                                        " located in previous frames, but it does not guarantee the same person ID among frames.(int)").asInt32();
        face_render_threshold = rf.check("face_render_threshold", yarp::os::Value(0.4), "Analogous to `render_threshold`, but applied to the face keypoints.(double)").asFloat64();
        face_render = rf.check("face_render", yarp::os::Value(-1), "Analogous to `render_pose` but applied to the face. Extra option: -1 to use the same.(int)").asInt32();
        face_alpha_pose = rf.check("face_alpha_pose", yarp::os::Value(0.6), "Analogous to `alpha_pose` but applied to face..(double)").asFloat64();
        face_alpha_heatmap = rf.check("face_alpha_heatmap", yarp::os::Value(0.7), "Analogous to `alpha_heatmap` but applied to face.(double)").asFloat64();
        flags_3d = rf.check("flags_3d", yarp::os::Value(false), "Running OpenPose 3-D reconstruction demo: 1) Reading from a stereo camera system."
                                                        " 2) Performing 3-D reconstruction from the multiple views. 3) Displaying 3-D reconstruction"
                                                        " results. Note that it will only display 1 person. If multiple people is present, it will"
                                                        " fail.(bool)").asBool();
        views_3d = rf.check("views_3d", yarp::os::Value(-1), "Complementary option for `--image_dir` or `--video`. OpenPose will read as many images per"
                                                        " iteration, allowing tasks such as stereo camera processing (`--3d`). Note that"
                                                        " `--camera_parameter_path` must be set. OpenPose must find as many `xml` files in the"
                                                        " parameter folder as this number indicates.(int)").asInt32();

        setName(moduleName.c_str());
        rpcPort.open(("/"+getName("/rpc")).c_str());
        closing = false;

        yDebug() << "Starting yarpOpenPose";

        // Applying user defined configuration
        auto outputSize = op::flagsToPoint(op::String(img_resolution), op::String(img_resolution));
        // netInputSize
        auto netInputSize = op::flagsToPoint(op::String(net_resolution), op::String(net_resolution));
        
        auto netInputSizeDynamicBehavior = net_input_size_dynamic_behavior;
        //pose model
        op::PoseModel poseModel = op::flagsToPoseModel(op::String(model_name));
        // scaleMode
        op::ScaleMode keypointScale = op::flagsToScaleMode(keypoint_scale);
        
        // handNetInputSize
        auto handNetInputSize = op::flagsToPoint(op::String(hand_net_resolution), op::String(hand_net_resolution));

        // heatmaps to add
        std::vector<op::HeatMapType> heatMapTypes = op::flagsToHeatMaps(heatmaps_add_parts, heatmaps_add_bkg, heatmaps_add_PAFs);
        op::checkBool(heatmaps_scale_mode >= 0 && heatmaps_scale_mode <= 2, "Non valid `heatmaps_scale_mode`.", __LINE__, __FUNCTION__, __FILE__);
        op::ScaleMode heatMapsScaleMode = (heatmaps_scale_mode == 0 ? op::ScaleMode::PlusMinusOne : (heatmaps_scale_mode == 1 ? op::ScaleMode::ZeroToOne : op::ScaleMode::UnsignedChar ));

        // Pose configuration
        op::PoseMode pose_mode_body = op::PoseMode::Enabled;
        if (!body_enable)
            pose_mode_body = op::PoseMode::Disabled;

        const auto multipleView = (flags_3d || views_3d > 1);

        const op::WrapperStructPose wrapperStructPose{pose_mode_body, netInputSize, netInputSizeDynamicBehavior, outputSize, keypointScale, num_gpu, num_gpu_start, num_scales, scale_gap, op::flagsToRenderMode(render_pose, multipleView), poseModel, !disable_blending, (float)alpha_pose, (float)alpha_heatmap, part_to_show, op::String(model_folder), heatMapTypes, heatMapsScaleMode, part_candidates, (float)render_threshold, number_people_max, false, -1., op::String(""), op::String(""), 0., false } ;

        // Hand configuration
        const auto handDetector = op::flagsToDetector(0);
        
        const op::WrapperStructHand wrapperStructHand{hand_enable, handDetector/*op::Detector::Provided*/, handNetInputSize, hand_scale_number, (float)hand_scale_range, op::flagsToRenderMode(hand_render, render_pose)};

        // Face configuration
        const auto faceNetInputSize = op::flagsToPoint(op::String(face_net_resolution), "368x368 (multiples of 16)");
        const auto faceDetector = op::flagsToDetector(face_detector);
        
        const op::WrapperStructFace wrapperStructFace{face_enable, faceDetector, faceNetInputSize,
            op::flagsToRenderMode(face_render, multipleView, render_pose),
            (float)face_alpha_pose, (float)face_alpha_heatmap, (float)face_render_threshold};
        
        //opWrapper.disableMultiThreading();

        opWrapper.configure(wrapperStructPose);
        opWrapper.configure(wrapperStructHand);
        opWrapper.configure(wrapperStructFace);
        opWrapper.configure(op::WrapperStructInput{});
        opWrapper.configure(op::WrapperStructOutput{});


        attach(rpcPort);
        inPort.open("/" + moduleName + "/image:i");
        inFloatPort.open("/" + moduleName + "/float:i");

        opWrapper.start();

        // User processing
        inputClass = new ImageInput();
        outputClass = new ImageOutput(moduleName);
        processingClass = new ImageProcessing(moduleName);

        inputClass->initializationOnThread();
        outputClass->initializationOnThread();
        processingClass->initializationOnThread();

        yDebug() << "Running processses";

        yarp::os::Network::connect("/icub/camcalib/left/out", "/yarpOpenPose/image:i");
        yarp::os::Network::connect("/yarpOpenPose/image:o", "/faces");


        return true;
    }

    /**********************************************************/
    bool interruptModule()
    {
        inPort.interrupt();
        inFloatPort.interrupt();
        return true;
    }

    /**********************************************************/
    bool close()
    {
        inPort.close();
        inFloatPort.close();
        delete inputClass;
        delete outputClass;
        delete processingClass;
        return true;
    }
    /**********************************************************/
    bool quit(){
        closing = true;
        opWrapper.stop();
        return true;
    }
    /********************************************************/
    double getPeriod()
    {
        return 0.1;
    }
    /********************************************************/
    bool updateModule()
    {
        if (yarp::sig::ImageOf<yarp::sig::PixelRgb> *inImage = inPort.read())
        {
            yarp::os::Stamp stamp;
            inPort.getEnvelope(stamp);
            inputClass->setImage(*inImage);
            outputClass->setStamp(stamp);
            processingClass->setStamp(stamp);

            if (inFloatPort.getInputCount() > 0)
                if (yarp::sig::ImageOf<yarp::sig::PixelFloat> *inFloat = inFloatPort.read())
                {
                    yarp::os::Stamp stampFloat;
                    inFloatPort.getEnvelope(stampFloat);
                    outputClass->setFlag(true);

                    outputClass->setImage(*inFloat, stampFloat);
                }

            auto datumToProcess = inputClass->workProducer();
            if (datumToProcess != nullptr)
            {
                auto successfullyEmplaced = opWrapper.waitAndEmplace(datumToProcess->at(0)->cvInputData);
                // Pop frame
                std::shared_ptr<std::vector<std::shared_ptr<op::Datum>>> datumProcessed;
                
                const auto status = opWrapper.waitAndPop(datumProcessed);
                
                if (successfullyEmplaced && status)
                {
                    outputClass->workConsumer(datumProcessed);
                    processingClass->work(datumProcessed);
                }
                else
                    yError() << "Processed datum could not be emplaced.";
            }
        }

        return !closing;
    }
};

/********************************************************/
int main(int argc, char *argv[])
{
    yarp::os::Network::init();

    yarp::os::Network yarp;
    if (!yarp.checkNetwork())
    {
        yError("YARP server not available!");
        return 1;
    }

    Module module;
    yarp::os::ResourceFinder rf;

    rf.setVerbose( true );
    rf.setDefaultContext( "yarpOpenPose" );
    rf.setDefaultConfigFile( "yarpOpenPose.ini" );
    rf.setDefault("name","yarpOpenPose");
    rf.configure(argc,argv);

    return module.runModule(rf);
}
