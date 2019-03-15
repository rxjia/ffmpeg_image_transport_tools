/* -*-c++-*--------------------------------------------------------------------
 * 2019 Bernd Pfrommer bernd.pfrommer@gmail.com
 */

#include "ffmpeg_image_transport_tools/split_bag.h"

#include <rosbag/view.h>

#include <boost/range/irange.hpp>
#include <climits>

namespace ffmpeg_image_transport_tools {
  using boost::irange;
  SplitBag::Session::Session(const std::string &topic,
                             const std::string &base,
                             unsigned int idx) :
    topic_(topic) {
    std::string fname = base + std::to_string(idx) + ".h265";
    rawStream_.open(fname, std::ios::out | std::ios::binary);
    if (!rawStream_.is_open()) {
      ROS_ERROR_STREAM("cannot open file: " << fname);
      throw std::runtime_error("cannot open file " + fname);
    }
  }

  void
  SplitBag::Session::process(const FFMPEGPacketConstPtr &msg) {
    if (!rawStream_.write((const char *)(&msg->data[0]), msg->data.size())) {
      ROS_ERROR_STREAM("write failed for topic: " << topic_);
      throw std::runtime_error("write failed!");
    }
  }

  SplitBag::Session::~Session() {
    rawStream_.close();
  }

  SplitBag::SplitBag(const ros::NodeHandle& pnh) :  nh_(pnh) {
  }

  SplitBag::~SplitBag() {
  }


  bool SplitBag::initialize() {
    if (!nh_.getParam("image_topics", imageTopics_)) {
      ROS_ERROR("no image topics found!");
      return (false);
    }
    nh_.param<std::string>("out_file_base",  outFileBase_,  "video_");
    nh_.param<int>("max_num_frames",  maxNumFrames_, INT_MAX);

    std::string bagFile;
    nh_.param<std::string>("bag_file",  bagFile,  "");
    if (!bagFile.empty()) {
      processBag(bagFile);
    } else {
      ROS_ERROR_STREAM("must specify bag_file!");
    }
    ros::shutdown();
    return (true);
  }


  void SplitBag::makeSessions(rosbag::Bag *bag) {
    for (const auto &topic_idx: irange(0ul, imageTopics_.size())) {
      const auto &topic = imageTopics_[topic_idx];
      rosbag::View cv(*bag, rosbag::TopicQuery({topic}));
      if (cv.begin() == cv.end()) {
        ROS_WARN_STREAM("cannot find topic: " << topic);
      } else {
        ROS_INFO_STREAM("using topic: " << topic);
      }
      if (sessions_.count(topic) == 0) {
        sessions_[topic].reset(
          new Session(topic, outFileBase_, topic_idx));
      } else {
        ROS_WARN_STREAM("duplicate topic: " << topic);
      }
    }
  }


  void SplitBag::processBag(const std::string &fname) {
    ROS_INFO_STREAM("playing from file: " << fname);
    rosbag::Bag bag;
    bag.open(fname, rosbag::bagmode::Read);

    makeSessions(&bag);
    
    rosbag::View view(bag, rosbag::TopicQuery(imageTopics_));
    auto t0 = ros::WallTime::now();
    int cnt(0), perfInterval(500);
    for (const rosbag::MessageInstance &m: view) {
      FFMPEGPacketConstPtr msg = m.instantiate<FFMPEGPacket>();
      if (msg) {
        SessionPtr sess = sessions_[m.getTopic()];
        sess->process(msg);
        frameNum_++;
      }
      if (!ros::ok() || (int)frameNum_ > maxNumFrames_) {
        break;
      }
      if (cnt++ > perfInterval) {
        const auto t1 = ros::WallTime::now();
        ROS_INFO_STREAM("wrote frames: " << frameNum_ << " fps: " <<
                        perfInterval / (imageTopics_.size() * (t1-t0).toSec()));
        cnt = 0;
        t0 = t1;
      }
    }
    ROS_INFO_STREAM("total frames: " << frameNum_);
    bag.close();
    sessions_.clear();
  }
  
}  // namespace