#include "ros/ros.h"
#include "std_msgs/String.h"
#include "roomba_500driver_meiji/RoombaCtrl.h"
#include "nav_msgs/Odometry.h"
#include <sstream>
#include "sensor_msgs/LaserScan.h"

nav_msgs::Odometry roomba_odom;
sensor_msgs::LaserScan roomba_scan;

void chatterCallback(const nav_msgs::Odometry::ConstPtr& msg)
{
	roomba_odom = *msg;
//	ROS_INFO("p_x = %f",roomba_odom.pose.pose.position.x);
//	ROS_INFO("o_z = %f",roomba_odom.pose.pose.orientation.z);
}

void scan_callback(const sensor_msgs::LaserScan::ConstPtr& scan_msg)
{
        roomba_scan = *scan_msg;
}


int main(int argc, char **argv)
{
  ros::init(argc, argv, "scan_test");
  ros::NodeHandle n;

  ros::Subscriber roomba_odom_sub = n.subscribe("roomba/odometry", 1,chatterCallback);
  ros::Publisher roomba_auto_pub = n.advertise<roomba_500driver_meiji::RoombaCtrl>("/roomba/control", 1);
  ros::Subscriber roomba_scan_sub = n.subscribe("scan",1,scan_callback);

  ros::Rate loop_rate(10);


  bool flag = true;

  while (ros::ok())
  {
    roomba_500driver_meiji::RoombaCtrl roomba_auto;

    roomba_auto.mode = 11; 

    if(roomba_odom.pose.pose.position.x >= 3.0f && flag){
	 roomba_auto.cntl.linear.x = 0.0f;
	 roomba_auto.cntl.angular.z = 0.7f;

	 if(roomba_odom.pose.pose.orientation.z < 0.0f){
	     flag = false;
	 }
    }
    else if(roomba_odom.pose.pose.orientation.z < 0.0f && !flag){

	 roomba_auto.cntl.linear.x = 0.0f;
	 roomba_auto.cntl.angular.z = 0.7f;

    }
    else{
	 roomba_auto.cntl.linear.x = 0.5f;
	 roomba_auto.cntl.angular.z = 0.0f;
    }

    roomba_auto_pub.publish(roomba_auto);

    ROS_INFO("%f", roomba_auto.cntl.linear.x);
    ROS_INFO("%f", roomba_auto.cntl.angular.z);

    int front_scan_pos = roomba_scan.ranges.size()/2;
    ROS_INFO("front_range: %f", roomba_scan.ranges[front_scan_pos]);

    if(roomba_scan.ranges[front_scan_pos] < 0.5f ){
            roomba_auto.mode = 0;
    }

    ros::spinOnce();

    loop_rate.sleep();
    }
  return 0;
}
