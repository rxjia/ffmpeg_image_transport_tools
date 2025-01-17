/* -*-c++-*--------------------------------------------------------------------
 * 2019 Bernd Pfrommer bernd.pfrommer@gmail.com
 */

#include "ffmpeg_image_transport_tools/extract_ffmpeg.h"

#include <rosbag/view.h>

#include <boost/range/irange.hpp>
#include <climits>
#include <bits/stdc++.h>
#include <sstream>

namespace ffmpeg_image_transport_tools
{
using boost::irange;

std::string subreplace(const std::string& resource_str, const std::string& sub_str, const std::string& new_str)
{
  std::string dst_str        = resource_str;
  std::string::size_type pos = 0;
  while ((pos = dst_str.find(sub_str)) != std::string::npos)  //替换所有指定子串
  {
    dst_str.replace(pos, sub_str.length(), new_str);
  }
  return dst_str;
}

std::string get_fname_topic(const std::string& topic_name)
{
  std::string dst_str = subreplace(topic_name, "/", "_");
  dst_str             = subreplace(dst_str, "_ffmpeg", "");
  return dst_str;
}

SplitBag::Session::Session(const std::string& topic, const std::string& base, bool writeTimeStamp, unsigned int idx)
  : topic_(topic)
{
  std::string fname_topic = get_fname_topic(topic);
  std::string fname       = base + fname_topic + ".h265";
  std::string tsname      = base + fname_topic + "_ts.txt";

  if (writeTimeStamp)
  {
    ts_.open(tsname, std::ios::out);
    if (!ts_.is_open())
    {
      ROS_ERROR_STREAM("cannot open file: " << tsname);
      throw std::runtime_error("cannot open file " + tsname);
    }
  }
  rawStream_.open(fname, std::ios::out | std::ios::binary);
  if (!rawStream_.is_open())
  {
    ROS_ERROR_STREAM("cannot open file: " << fname);
    throw std::runtime_error("cannot open file " + fname);
  }
}

void SplitBag::Session::process(const FFMPEGPacketConstPtr& msg)
{
  if (msg->flags == 0 && frameCnt_ == 0)
  {
    // wait for first keyframe...
    return;
  }
  if (!rawStream_.write((const char*)(&msg->data[0]), msg->data.size()))
  {
    ROS_ERROR_STREAM("write failed for topic: " << topic_);
    throw std::runtime_error("write failed!");
  }
  if (ts_.is_open())
  {
    ts_ << frameCnt_ << " " << msg->header.stamp << std::endl;
  }
  frameCnt_++;
}

SplitBag::Session::~Session()
{
  rawStream_.close();
  if (ts_.is_open())
  {
    ts_.close();
  }
}

SplitBag::SplitBag(const ros::NodeHandle& pnh) : nh_(pnh)
{
}

SplitBag::~SplitBag()
{
}

bool SplitBag::initialize()
{
  imageTypes_.push_back(std::string("ffmpeg_image_transport_msgs/FFMPEGPacket"));
  // if (!nh_.getParam("image_topics", imageTopics_)) {
  //    ROS_ERROR("no image topics found!");
  //    //return (false);
  //}
  nh_.param<std::string>("out_file_base", outFileBase_, "video_");
  nh_.param<int>("max_num_frames", maxNumFrames_, INT_MAX);
  nh_.param<bool>("write_time_stamps", writeTimeStamps_, false);
  nh_.param<int>("video_rate", videoRate_, 40);
  double start_time, duration;
  nh_.param<double>("start_time", start_time, 0);
  nh_.param<double>("duration", duration, -1.0);
  std::string bagFile;
  nh_.param<std::string>("bag_file", bagFile, "");

  outFileBase_ = bagFile.substr(0, bagFile.size() - 4);
  if (!bagFile.empty())
  {
    processBag(bagFile, start_time, duration);
    bool convert;
    nh_.param<bool>("convert_to_mp4", convert, false);
    if (convert)
    {
      convertToMP4();
    }
  }
  else
  {
    ROS_ERROR_STREAM("must specify bag_file!");
  }
  ros::shutdown();
  return (true);
}

void SplitBag::convertToMP4() const
{
  for (const auto& idx : irange(0ul, imageTopics_.size()))
  {
    std::string fname_topic = get_fname_topic(imageTopics_[idx]);

    std::string iname = outFileBase_ + fname_topic + ".h265";
    std::string oname = outFileBase_ + fname_topic + ".mp4";
    std::stringstream ss;
    ss << "ffmpeg -y -r " << videoRate_ << " -i " << iname << " -c:v copy " << oname;
    std::cout << "issuing string: " << ss.str() << std::endl;
    int rc = std::system(ss.str().c_str());
    if (rc == -1)
    {
      std::cerr << "error running ffmpeg: " << ss.str() << std::endl;
    }
    rc = std::system(("rm " + iname).c_str());
    if (rc == -1)
    {
      std::cerr << "error removing file: " << iname << std::endl;
    }
  }
}

void SplitBag::makeSessions(rosbag::Bag* bag, const ros::Time& t_start, const ros::Time& t_end)
{
  for (const auto& topic_idx : irange(0ul, imageTopics_.size()))
  {
    const auto& topic = imageTopics_[topic_idx];
    rosbag::View cv(*bag, rosbag::TopicQuery({ topic }), t_start, t_end);
    if (cv.begin() == cv.end())
    {
      ROS_WARN_STREAM("cannot find topic: " << topic);
    }
    else
    {
      ROS_INFO_STREAM("using topic: " << topic);
    }
    if (sessions_.count(topic) == 0)
    {
      sessions_[topic].reset(new Session(topic, outFileBase_, writeTimeStamps_, topic_idx));
    }
    else
    {
      ROS_WARN_STREAM("duplicate topic: " << topic);
    }
  }
}

void SplitBag::processBag(const std::string& fname, double start_time, double duration)
{
  ROS_INFO_STREAM("processing file: " << fname);
  rosbag::Bag bag;
  bag.open(fname, rosbag::bagmode::Read);

  ros::Time t_start = rosbag::View(bag).getBeginTime() + ros::Duration(start_time);
  ros::Time t_end   = duration >= 0 ? t_start + ros::Duration(duration) : ros::TIME_MAX;

  rosbag::View view(bag, rosbag::TypeQuery(imageTypes_), t_start, t_end);

  std::set<std::string> topics;
  std::vector<const rosbag::ConnectionInfo*> connection_infos = view.getConnections();
  BOOST_FOREACH (const rosbag::ConnectionInfo* info, connection_infos)
  {
    topics.insert(info->topic);
  }
  imageTopics_.assign(topics.begin(), topics.end());

  makeSessions(&bag, t_start, t_end);

  auto t0 = ros::WallTime::now();
  int cnt(0), perfInterval(500);
  for (const rosbag::MessageInstance& m : view)
  {
    FFMPEGPacketConstPtr msg = m.instantiate<FFMPEGPacket>();
    if (msg)
    {
      SessionPtr sess = sessions_[m.getTopic()];
      sess->process(msg);
      frameNum_++;
    }
    if (!ros::ok() || (int)frameNum_ > maxNumFrames_)
    {
      break;
    }
    if (cnt++ > perfInterval)
    {
      const auto t1 = ros::WallTime::now();
      ROS_INFO_STREAM("wrote frames: " << frameNum_
                                       << " fps: " << perfInterval / (imageTopics_.size() * (t1 - t0).toSec()));
      cnt = 0;
      t0  = t1;
    }
  }
  ROS_INFO_STREAM("total frames: " << frameNum_);
  bag.close();
  sessions_.clear();
}

}  // namespace ffmpeg_image_transport_tools
