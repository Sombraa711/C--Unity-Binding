#include "GustoUnityWrapper.h"

#include <visp3/gui/vpDisplayGDI.h>
#include <visp3/gui/vpDisplayOpenCV.h>
#include <visp3/gui/vpDisplayX.h>

#include <nlohmann/json.hpp>

extern "C" {

//-------------------------------------------------------------------
  void  Debug::Log(const char *message, Color color)
  {
    if (callbackInstance != nullptr)
      callbackInstance(message, (int)color, (int)strlen(message));
  }

  void  Debug::Log(const std::string message, Color color)
  {
    const char *tmsg = message.c_str();
    if (callbackInstance != nullptr)
      callbackInstance(tmsg, (int)color, (int)strlen(tmsg));
  }

  void  Debug::Log(const int message, Color color)
  {
    std::stringstream ss;
    ss << message;
    send_log(ss, color);
  }

  void  Debug::Log(const char message, Color color)
  {
    std::stringstream ss;
    ss << message;
    send_log(ss, color);
  }

  void  Debug::Log(const float message, Color color)
  {
    std::stringstream ss;
    ss << message;
    send_log(ss, color);
  }

  void  Debug::Log(const double message, Color color)
  {
    std::stringstream ss;
    ss << message;
    send_log(ss, color);
  }

  void Debug::Log(const bool message, Color color)
  {
    std::stringstream ss;
    if (message)
      ss << "true";
    else
      ss << "false";

    send_log(ss, color);
  }

  void Debug::send_log(const std::stringstream &ss, const Color &color)
  {
    const std::string tmp = ss.str();
    const char *tmsg = tmp.c_str();
    if (callbackInstance != nullptr)
      callbackInstance(tmsg, (int)color, (int)strlen(tmsg));
  }
  //-------------------------------------------------------------------

  //Create a callback delegate
  void RegisterDebugCallback(FuncCallBack cb)
  {
    callbackInstance = cb;
  }



  static bool m_debug_enable_display = true; //!< Flag used to enable/disable display associated to internal image m_I.
  static bool m_debug_display_is_initialized = false; //!< Flag used to know if display associated to internal image m_I is initialized.
  static vpDisplay *displayer = nullptr;









/*!
 * Global variables that are common.
 */
  cv::Mat frame;
  vpImage<vpRGBa> m_I; //!< Internal image updated using Visp_ImageUchar_SetFromColor32Array().
  static vpCameraParameters m_cam; //!< Internal camera parameters updated using Visp_CameraParameters_Init().

  unsigned width = 640, height = 480;
  vpCameraParameters cam;
  std::string videoDevice = "0";
  std::string megaposeAddress = "127.0.0.1";
  unsigned megaposePort = 5555;
  int refinerIterations = 1, coarseNumSamples = 576;
  double reinitThreshold = 0.2;

  std::string detectorModelPath = "/media/sombrali/HDD1/3d_object_detection/visp/script/dataset_generator/yolov7/runs/train/yolo7x_640_480_20240414_001_3objects_combined/weights/best.onnx", detectorConfig = "none";
  std::string detectorFramework = "onnx", detectorTypeString = "yolov7";
  std::string objectName = "cube";
  std::vector<std::string> labels = { "cube" };
  float detectorMeanR = 0.f, detectorMeanG = 0.f, detectorMeanB = 0.f;
  float detectorConfidenceThreshold = 0.65f, detectorNmsThreshold = 0.5f, detectorFilterThreshold = -0.25f;
  float detectorScaleFactor = 0.0039f;
  bool  detectorSwapRB = false;
  //! [Arguments]

  vpDetectorDNNOpenCV dnn;
  std::optional<vpRect> detection = std::nullopt;


  std::shared_ptr<vpMegaPose> megapose;
  std::future<vpMegaPoseEstimate> trackerFuture;
  bool has_track_future = false;

  std::vector<double> megaposeTimes;
  vpMegaPoseEstimate megaposeEstimate; // last Megapose estimation
  vpMegaPoseTracker *megaposeTracker;
  double megaposeStartTime = 0.0;
  vpRect lastDetection; // Last detection (initialization)
  bool initialized = false;
  bool tracking = false;
  vpImage<vpRGBa> overlayImage(height, width);


  void Gusto_CppWrapper_MemoryFree()
  {
    if (m_debug_display_is_initialized) {
      delete displayer;
      delete megaposeTracker;
    }
    return;
  }

  void Gusto_ImageUchar_SetFromColor32Array(unsigned char *bitmap, int height, int width)
  {
    frame = cv::Mat(height, width, CV_8UC4, bitmap);
    cv::cvtColor(frame, frame, cv::COLOR_BGR2RGB);
    cv::flip(frame, frame, 0);
  }


//! [Detect]
/*
 * Run the detection network on an image in order to find a specific object.
 * The best matching detection is returned:
 * - If a previous Megapose estimation is available, find the closest match in the image (Euclidean distance between centers)
 * - Otherwise, take the detection with highest confidence
 * If no detection corresponding to detectionLabel is found, then std::nullopt is returned
 */
  std::optional<vpRect> detectObjectForInitMegaposeDnn(vpDetectorDNNOpenCV &detector, const cv::Mat &I,
    const std::string &detectionLabel)
  {
    std::vector<vpDetectorDNNOpenCV::DetectedFeatures2D> detections_vec;

    detector.detect(I, detections_vec);
    std::vector<vpDetectorDNNOpenCV::DetectedFeatures2D> matchingDetections;
    for (const auto &detection : detections_vec) {
      std::optional<std::string> classnameOpt = detection.getClassName();
      if (classnameOpt) {

        if (*classnameOpt == detectionLabel) {
          matchingDetections.push_back(detection);
        }
      }
    }

    if (matchingDetections.size() == 0) {
      return std::nullopt;
    }
    else if (matchingDetections.size() == 1) {
      if (m_debug_enable_display) {
        Debug::Log("<Sombra> [detectObjectForInitMegaposeDnn] Single detection found", Color::Green);
        Debug::Log("<Sombra> [detectObjectForInitMegaposeDnn] Bounding box: ", Color::Green);
        Debug::Log("<Sombra> [detectObjectForInitMegaposeDnn] Top: " + std::to_string(matchingDetections[0].getBoundingBox().getTop()), Color::Green);
        Debug::Log("<Sombra> [detectObjectForInitMegaposeDnn] Bottom: " + std::to_string(matchingDetections[0].getBoundingBox().getBottom()), Color::Green);
        Debug::Log("<Sombra> [detectObjectForInitMegaposeDnn] Left: " + std::to_string(matchingDetections[0].getBoundingBox().getLeft()), Color::Green);
        Debug::Log("<Sombra> [detectObjectForInitMegaposeDnn] Right: " + std::to_string(matchingDetections[0].getBoundingBox().getRight()), Color::Green);
      }
      return matchingDetections[0].getBoundingBox();
    }
    else {
      // Get detection that is closest to previous object bounding box estimated by Megapose
      // if (previousEstimate) {
      //   vpRect best;
      //   double bestDist = 10000.f;
      //   const vpImagePoint previousCenter = (*previousEstimate).boundingBox.getCenter();
      //   for (const auto &detection : matchingDetections) {
      //     const vpRect detectionBB = detection.getBoundingBox();
      //     const vpImagePoint center = detectionBB.getCenter();
      //     const double matchDist = vpImagePoint::distance(center, previousCenter);
      //     if (matchDist < bestDist) {
      //       bestDist = matchDist;
      //       best = detectionBB;
      //     }
      //   }
      //   return best;

      // }
      // else { // Get detection with highest confidence
      vpRect best;
      double highestConf = 0.0;
      for (const auto &detection : matchingDetections) {
        const double conf = detection.getConfidenceScore();
        if (conf > highestConf) {
          highestConf = conf;
          best = detection.getBoundingBox();
        }
      }
      return best;
    // }
    }
    return std::nullopt;
  }
  void Gusto_CameraParameters_Init(double cam_px, double cam_py, double cam_u0, double cam_v0)
  {
    m_cam.initPersProjWithoutDistortion(cam_px, cam_py, cam_u0, cam_v0);
  }


  void Gusto_MegaPoseServer_Init()
  {
    try {
      megapose = std::make_shared<vpMegaPose>(megaposeAddress, megaposePort, cam, height, width);
    }
    catch (...) {
      throw vpException(vpException::ioError, "Could not connect to Megapose server at " + megaposeAddress + " on port " + std::to_string(megaposePort));
    }
    // vpMegaPoseTracker megaposeTracker(megapose, objectName, refinerIterations);
    megaposeTracker = new vpMegaPoseTracker(megapose, objectName, refinerIterations);
    megapose->setCoarseNumSamples(coarseNumSamples);
    const std::vector<std::string> allObjects = megapose->getObjectNames();

    if (std::find(allObjects.begin(), allObjects.end(), objectName) == allObjects.end()) {
      throw vpException(vpException::badValue, "Object " + objectName + " is not known by the Megapose server!");
    }

  }


  void Gusto_Init(const char *config_path)
  {


    int argc = 3;
    const char *argv[3] = { "C# to C++ binding config", "--config", config_path };

    vpJsonArgumentParser parser("Single object tracking with Megapose", "--config", "/");
    parser.addArgument("width", width, true, "The image width")
      .addArgument("height", height, true, "The image height")
      .addArgument("camera", cam, true, "The camera intrinsic parameters. Should correspond to a perspective projection model without distortion.")
      .addArgument("video-device", videoDevice, true, "Video device")
      .addArgument("object", objectName, true, "Name of the object to track with megapose.")
      // .addArgument("detectionMethod", detectionMethod, true, "How to perform detection of the object to get the bounding box:"
      //   " \"click\" for user labelling, \"dnn\" for dnn detection.")
      .addArgument("reinitThreshold", reinitThreshold, false, "If the Megapose score falls below this threshold, then a reinitialization is be required."
        " Should be between 0 and 1")
      .addArgument("megapose/address", megaposeAddress, true, "IP address of the Megapose server.")
      .addArgument("megapose/port", megaposePort, true, "Port on which the Megapose server listens for connections.")
      .addArgument("megapose/refinerIterations", refinerIterations, false, "Number of Megapose refiner model iterations."
        "A higher count may lead to better accuracy, at the cost of more processing time")
      .addArgument("megapose/initialisationNumSamples", coarseNumSamples, false, "Number of Megapose renderings used for the initial pose estimation.")

      .addArgument("detector/model-path", detectorModelPath, true, "Path to the model")
      .addArgument("detector/config", detectorConfig, true, "Path to the model configuration. Set to none if config is not required.")
      .addArgument("detector/framework", detectorFramework, true, "Detector framework")
      .addArgument("detector/type", detectorTypeString, true, "Detector type")
      .addArgument("detector/labels", labels, true, "Detection class labels")
      .addArgument("detector/mean/red", detectorMeanR, false, "Detector mean red component. Used to normalize image")
      .addArgument("detector/mean/green", detectorMeanG, false, "Detector mean green component. Used to normalize image")
      .addArgument("detector/mean/blue", detectorMeanB, false, "Detector mean red component. Used to normalize image")
      .addArgument("detector/confidenceThreshold", detectorConfidenceThreshold, false, "Detector confidence threshold. "
        "When a detection with a confidence below this threshold, it is ignored")
      .addArgument("detector/nmsThreshold", detectorNmsThreshold, false, "Detector non maximal suppression threshold.")
      .addArgument("detector/filterThreshold", detectorFilterThreshold, false)
      .addArgument("detector/scaleFactor", detectorScaleFactor, false, "Pixel intensity rescaling factor. If set to 1/255, then pixel values are between 0 and 1.")
      .addArgument("detector/swapRedAndBlue", detectorSwapRB, false, "Whether to swap red and blue channels before feeding the image to the detector.");

    parser.parse(argc, argv);


    vpDetectorDNNOpenCV::DNNResultsParsingType detectorType = vpDetectorDNNOpenCV::dnnResultsParsingTypeFromString(detectorTypeString);
    vpDetectorDNNOpenCV::NetConfig netConfig(detectorConfidenceThreshold, detectorNmsThreshold, labels, cv::Size(width, height), detectorFilterThreshold);

    dnn = vpDetectorDNNOpenCV(netConfig, detectorType);

    Debug::Log("Tring to detect object: " + objectName, Color::Blue);
    // if (detectionMethod == DetectionMethod::DNN) {
    dnn.readNet(detectorModelPath, detectorConfig, detectorFramework);
    dnn.setMean(detectorMeanR, detectorMeanG, detectorMeanB);
    dnn.setScaleFactor(detectorScaleFactor);
    // dnn.setSwapRB(detectorSwapRB);
    dnn.setPreferableBackend(cv::dnn::DNN_BACKEND_CUDA);
    dnn.setPreferableTarget(cv::dnn::DNN_TARGET_CUDA);
  }
  // }
  bool Gusto_Detection2D_Process(double *bbox_xywh, double *detection_time)
  {

    double t_start = vpTime::measureTimeMs();
    bool has_det = false;
    if (m_debug_enable_display && m_debug_display_is_initialized) {
      vpDisplay::display(m_I);
    }

    // Detection




    // vpImageConvert::convert(m_I, frame);
    vpImageConvert::convert(frame, m_I);
    // cv::resize(frame, frame, cv::Size(width, height));
    if (m_debug_enable_display && (!m_debug_display_is_initialized)) {
      displayer = new vpDisplayX(m_I);
      // displayer.init(m_I);
      vpDisplay::setTitle(m_I, "Megapose object pose estimation");
      m_debug_display_is_initialized = true;
    }
    detection = detectObjectForInitMegaposeDnn(dnn, frame, objectName);
    // vpRect(detection.value().getLeft(), detection.value().getTop(), detection.value().getWidth(), detection.value().getHeight()), vpColor::red, false, 2);
    if (detection) {
      has_det = true;
      bbox_xywh[0] = detection.value().getLeft();
      bbox_xywh[1] = detection.value().getTop();
      bbox_xywh[2] = detection.value().getWidth();
      bbox_xywh[3] = detection.value().getHeight();
    }

    *detection_time = vpTime::measureTimeMs() - t_start;

    if (m_debug_enable_display && m_debug_display_is_initialized) {
      std::stringstream ss;
      ss << "Loop time: " << *detection_time << "   ";
      ss << "Width: " << frame.cols << "   " << "Height: " << frame.rows << "   ";

      vpDisplay::displayText(m_I, 20, 20, ss.str(), vpColor::red);
      vpDisplay::flush(m_I);
    }
    return has_det;
  }

  bool Gusto_MegaPose_Tracking_Process()
  {
    bool Need_Reinit = true;
    bool overlayModel = true;

    // if (has_track_future && trackerFuture.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
    //   megaposeEstimate = trackerFuture.get();
    //   if (tracking) {
    //     megaposeTimes.push_back(vpTime::measureTimeMs() - megaposeStartTime);
    //   }
    //   has_track_future = false;
    //   tracking = true;
    //   if (overlayModel) {
    //     overlayImage = megapose->viewObjects({ objectName }, { megaposeEstimate.cTo }, "full");
    //   }

    //   if (megaposeEstimate.score < reinitThreshold) { // If confidence is low, require a reinitialisation with 2D detection
    //     initialized = false;
    //     detection = std::nullopt;
    //   }
    // }


  //   //! [Check megapose]
  //   //! [Call MegaPose]
    if (!initialized) {
      tracking = false;
      if (detection) {
        Debug::Log("Sombra: <Client> Initialising Megapose with 2D detection", Color::Blue);
        vpDisplay::displayRectangle(m_I, vpRect(detection.value().getLeft(), detection.value().getTop(), detection.value().getWidth(), detection.value().getHeight()), vpColor::red, false, 2);
        vpDisplay::flush(m_I);
        // initialized = true;
        lastDetection = *detection;
        Debug::Log("Sombra: megaposeTracker->init(m_I, lastDetection)", Color::Red);
        try {
          trackerFuture = megaposeTracker->init(m_I, lastDetection);

        }
        catch (...) {
          throw vpException(vpException::ioError, "Error with megaposeTracker->init(m_I, lastDetection)");
        }
        has_track_future = true;
      }
    }
    else {
      trackerFuture = megaposeTracker->track(m_I);
      has_track_future = true;

      megaposeStartTime = vpTime::measureTimeMs();
    }
    return !Need_Reinit;
  }
}
