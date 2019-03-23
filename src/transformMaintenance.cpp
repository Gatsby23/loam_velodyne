// Copyright 2013, Ji Zhang, Carnegie Mellon University
// Further contributions copyright (c) 2016, Southwest Research Institute
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice,
//    this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
// 3. Neither the name of the copyright holder nor the names of its
//    contributors may be used to endorse or promote products derived from this
//    software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
// This is an implementation of the algorithm described in the following paper:
//   J. Zhang and S. Singh. LOAM: Lidar Odometry and Mapping in Real-time.
//     Robotics: Science and Systems Conference (RSS). Berkeley, CA, July 2014.

#include <cmath>

#include <loam_velodyne/common.h>
#include <nav_msgs/Odometry.h>
#include <opencv/cv.h>
#include <pcl_conversions/pcl_conversions.h>
#include <pcl/point_cloud.h>
#include <pcl/point_types.h>
#include <pcl/filters/voxel_grid.h>
#include <pcl/kdtree/kdtree_flann.h>
#include <ros/ros.h>
#include <sensor_msgs/Imu.h>
#include <sensor_msgs/PointCloud2.h>
#include <tf/transform_datatypes.h>
#include <tf/transform_broadcaster.h>

float transformSum[6] = {0};
float transformIncre[6] = {0};
float transformMapped[6] = {0};
float transformBefMapped[6] = {0};
float transformAftMapped[6] = {0};

ros::Publisher *pubLaserOdometry2Pointer = NULL;
tf::TransformBroadcaster *tfBroadcaster2Pointer = NULL;
nav_msgs::Odometry laserOdometry2;
tf::StampedTransform laserOdometryTrans2;

void transformAssociateToMap()
{
  // do exactly the same thing as the transformAssociateToMap() in Mapping
  // compute the latest Twc of the system based on the most recent mapping info
  float x1 = cos(transformSum[1]) * (transformBefMapped[3] - transformSum[3]) -
             sin(transformSum[1]) * (transformBefMapped[5] - transformSum[5]);
  float y1 = transformBefMapped[4] - transformSum[4];
  float z1 = sin(transformSum[1]) * (transformBefMapped[3] - transformSum[3]) +
             cos(transformSum[1]) * (transformBefMapped[5] - transformSum[5]);

  float x2 = x1;
  float y2 = cos(transformSum[0]) * y1 + sin(transformSum[0]) * z1;
  float z2 = -sin(transformSum[0]) * y1 + cos(transformSum[0]) * z1;

  transformIncre[3] = cos(transformSum[2]) * x2 + sin(transformSum[2]) * y2;
  transformIncre[4] = -sin(transformSum[2]) * x2 + cos(transformSum[2]) * y2;
  transformIncre[5] = z2;

  //@@ the original implementation is wrong
  //  float sbcx = sin(transformSum[0]);
  //  float cbcx = cos(transformSum[0]);
  //  float sbcy = sin(transformSum[1]);
  //  float cbcy = cos(transformSum[1]);
  //  float sbcz = sin(transformSum[2]);
  //  float cbcz = cos(transformSum[2]);

  //  float sblx = sin(transformBefMapped[0]);
  //  float cblx = cos(transformBefMapped[0]);
  //  float sbly = sin(transformBefMapped[1]);
  //  float cbly = cos(transformBefMapped[1]);
  //  float sblz = sin(transformBefMapped[2]);
  //  float cblz = cos(transformBefMapped[2]);

  //  float salx = sin(transformAftMapped[0]);
  //  float calx = cos(transformAftMapped[0]);
  //  float saly = sin(transformAftMapped[1]);
  //  float caly = cos(transformAftMapped[1]);
  //  float salz = sin(transformAftMapped[2]);
  //  float calz = cos(transformAftMapped[2]);

  //@@ should do it like this
  float sbcx = sin(transformAftMapped[0]);
  float cbcx = cos(transformAftMapped[0]);
  float sbcy = sin(transformAftMapped[1]);
  float cbcy = cos(transformAftMapped[1]);
  float sbcz = sin(transformAftMapped[2]);
  float cbcz = cos(transformAftMapped[2]);

  float sblx = sin(transformBefMapped[0]);
  float cblx = cos(transformBefMapped[0]);
  float sbly = sin(transformBefMapped[1]);
  float cbly = cos(transformBefMapped[1]);
  float sblz = sin(transformBefMapped[2]);
  float cblz = cos(transformBefMapped[2]);

  float salx = sin(transformSum[0]);
  float calx = cos(transformSum[0]);
  float saly = sin(transformSum[1]);
  float caly = cos(transformSum[1]);
  float salz = sin(transformSum[2]);
  float calz = cos(transformSum[2]);

  float srx = -sbcx * (salx * sblx + calx * caly * cblx * cbly +
                       calx * cblx * saly * sbly) -
              cbcx * cbcz * (calx * saly * (cbly * sblz - cblz * sblx * sbly) -
                             calx * caly * (sbly * sblz + cbly * cblz * sblx) +
                             cblx * cblz * salx) -
              cbcx * sbcz * (calx * caly * (cblz * sbly - cbly * sblx * sblz) -
                             calx * saly * (cbly * cblz + sblx * sbly * sblz) +
                             cblx * salx * sblz);
  transformMapped[0] = -asin(srx);

  float srycrx = (cbcy * sbcz - cbcz * sbcx * sbcy) *
                     (calx * saly * (cbly * sblz - cblz * sblx * sbly) -
                      calx * caly * (sbly * sblz + cbly * cblz * sblx) +
                      cblx * cblz * salx) -
                 (cbcy * cbcz + sbcx * sbcy * sbcz) *
                     (calx * caly * (cblz * sbly - cbly * sblx * sblz) -
                      calx * saly * (cbly * cblz + sblx * sbly * sblz) +
                      cblx * salx * sblz) +
                 cbcx * sbcy * (salx * sblx + calx * caly * cblx * cbly +
                                calx * cblx * saly * sbly);
  float crycrx = (cbcz * sbcy - cbcy * sbcx * sbcz) *
                     (calx * caly * (cblz * sbly - cbly * sblx * sblz) -
                      calx * saly * (cbly * cblz + sblx * sbly * sblz) +
                      cblx * salx * sblz) -
                 (sbcy * sbcz + cbcy * cbcz * sbcx) *
                     (calx * saly * (cbly * sblz - cblz * sblx * sbly) -
                      calx * caly * (sbly * sblz + cbly * cblz * sblx) +
                      cblx * cblz * salx) +
                 cbcx * cbcy * (salx * sblx + calx * caly * cblx * cbly +
                                calx * cblx * saly * sbly);
  transformMapped[1] =
      atan2(srycrx / cos(transformMapped[0]), crycrx / cos(transformMapped[0]));

  float srzcrx = sbcx * (cblx * cbly * (calz * saly - caly * salx * salz) -
                         cblx * sbly * (caly * calz + salx * saly * salz) +
                         calx * salz * sblx) -
                 cbcx * cbcz * ((caly * calz + salx * saly * salz) *
                                    (cbly * sblz - cblz * sblx * sbly) +
                                (calz * saly - caly * salx * salz) *
                                    (sbly * sblz + cbly * cblz * sblx) -
                                calx * cblx * cblz * salz) +
                 cbcx * sbcz * ((caly * calz + salx * saly * salz) *
                                    (cbly * cblz + sblx * sbly * sblz) +
                                (calz * saly - caly * salx * salz) *
                                    (cblz * sbly - cbly * sblx * sblz) +
                                calx * cblx * salz * sblz);
  float crzcrx = sbcx * (cblx * sbly * (caly * salz - calz * salx * saly) -
                         cblx * cbly * (saly * salz + caly * calz * salx) +
                         calx * calz * sblx) +
                 cbcx * cbcz * ((saly * salz + caly * calz * salx) *
                                    (sbly * sblz + cbly * cblz * sblx) +
                                (caly * salz - calz * salx * saly) *
                                    (cbly * sblz - cblz * sblx * sbly) +
                                calx * calz * cblx * cblz) -
                 cbcx * sbcz * ((saly * salz + caly * calz * salx) *
                                    (cblz * sbly - cbly * sblx * sblz) +
                                (caly * salz - calz * salx * saly) *
                                    (cbly * cblz + sblx * sbly * sblz) -
                                calx * calz * cblx * sblz);
  transformMapped[2] =
      atan2(srzcrx / cos(transformMapped[0]), crzcrx / cos(transformMapped[0]));

  x1 = cos(transformMapped[2]) * transformIncre[3] -
       sin(transformMapped[2]) * transformIncre[4];
  y1 = sin(transformMapped[2]) * transformIncre[3] +
       cos(transformMapped[2]) * transformIncre[4];
  z1 = transformIncre[5];

  x2 = x1;
  y2 = cos(transformMapped[0]) * y1 - sin(transformMapped[0]) * z1;
  z2 = sin(transformMapped[0]) * y1 + cos(transformMapped[0]) * z1;

  transformMapped[3] = transformAftMapped[3] - (cos(transformMapped[1]) * x2 +
                                                sin(transformMapped[1]) * z2);
  transformMapped[4] = transformAftMapped[4] - y2;
  transformMapped[5] = transformAftMapped[5] - (-sin(transformMapped[1]) * x2 +
                                                cos(transformMapped[1]) * z2);
}

void laserOdometryHandler(const nav_msgs::Odometry::ConstPtr &laserOdometry)
{
  double roll, pitch, yaw;
  geometry_msgs::Quaternion geoQuat = laserOdometry->pose.pose.orientation;
  tf::Matrix3x3(tf::Quaternion(geoQuat.z, -geoQuat.x, -geoQuat.y, geoQuat.w))
      .getRPY(roll, pitch, yaw);

  // the latest Twc from odometry
  transformSum[0] = -pitch;
  transformSum[1] = -yaw;   
  transformSum[2] = roll;

  transformSum[3] = laserOdometry->pose.pose.position.x;
  transformSum[4] = laserOdometry->pose.pose.position.y;
  transformSum[5] = laserOdometry->pose.pose.position.z;

  // compute T_wc = T_wl_map * inv(T_wl_odo) * T_wc_odo,
  transformAssociateToMap();

  // publish Twc
  geoQuat = tf::createQuaternionMsgFromRollPitchYaw(
      transformMapped[2], -transformMapped[0], -transformMapped[1]);

  laserOdometry2.header.stamp = laserOdometry->header.stamp;
  laserOdometry2.pose.pose.orientation.x = -geoQuat.y;
  laserOdometry2.pose.pose.orientation.y = -geoQuat.z;
  laserOdometry2.pose.pose.orientation.z = geoQuat.x;
  laserOdometry2.pose.pose.orientation.w = geoQuat.w;
  laserOdometry2.pose.pose.position.x = transformMapped[3];
  laserOdometry2.pose.pose.position.y = transformMapped[4];
  laserOdometry2.pose.pose.position.z = transformMapped[5];
  pubLaserOdometry2Pointer->publish(laserOdometry2);

  laserOdometryTrans2.stamp_ = laserOdometry->header.stamp;
  laserOdometryTrans2.setRotation(
      tf::Quaternion(-geoQuat.y, -geoQuat.z, geoQuat.x, geoQuat.w));
  laserOdometryTrans2.setOrigin(
      tf::Vector3(transformMapped[3], transformMapped[4], transformMapped[5]));
  tfBroadcaster2Pointer->sendTransform(laserOdometryTrans2);
}

void odomAftMappedHandler(const nav_msgs::Odometry::ConstPtr &odomAftMapped)
{
  double roll, pitch, yaw;
  geometry_msgs::Quaternion geoQuat = odomAftMapped->pose.pose.orientation;
  tf::Matrix3x3(tf::Quaternion(geoQuat.z, -geoQuat.x, -geoQuat.y, geoQuat.w))
      .getRPY(roll, pitch, yaw);

  // most recent map-corrected T_wc_odo
  transformAftMapped[0] = -pitch;
  transformAftMapped[1] = -yaw;
  transformAftMapped[2] = roll;

  transformAftMapped[3] = odomAftMapped->pose.pose.position.x;
  transformAftMapped[4] = odomAftMapped->pose.pose.position.y;
  transformAftMapped[5] = odomAftMapped->pose.pose.position.z;

  // most recent T_wc_odo that has been corrected by mapping
  transformBefMapped[0] = odomAftMapped->twist.twist.angular.x;
  transformBefMapped[1] = odomAftMapped->twist.twist.angular.y;
  transformBefMapped[2] = odomAftMapped->twist.twist.angular.z;

  transformBefMapped[3] = odomAftMapped->twist.twist.linear.x;
  transformBefMapped[4] = odomAftMapped->twist.twist.linear.y;
  transformBefMapped[5] = odomAftMapped->twist.twist.linear.z;
}

int main(int argc, char **argv)
{
  ros::init(argc, argv, "transformMaintenance");
  ros::NodeHandle nh;

  // subscribe to odometry
  ros::Subscriber subLaserOdometry = nh.subscribe<nav_msgs::Odometry>(
      "/laser_odom_to_init", 5, laserOdometryHandler);

  // subscribe to map-corrected odometry
  ros::Subscriber subOdomAftMapped = nh.subscribe<nav_msgs::Odometry>(
      "/aft_mapped_to_init", 5, odomAftMappedHandler);

  // publish integrated odometry
  ros::Publisher pubLaserOdometry2 =
      nh.advertise<nav_msgs::Odometry>("/integrated_to_init", 5);
  pubLaserOdometry2Pointer = &pubLaserOdometry2;
  laserOdometry2.header.frame_id = "/camera_init";
  laserOdometry2.child_frame_id = "/camera";

  tf::TransformBroadcaster tfBroadcaster2;
  tfBroadcaster2Pointer = &tfBroadcaster2;
  laserOdometryTrans2.frame_id_ = "/camera_init";
  laserOdometryTrans2.child_frame_id_ = "/camera";

  ros::spin();

  return 0;
}
