#include "ros/ros.h"
#include "std_msgs/String.h"
#include "nav_msgs/Path.h"
#include "nav_msgs/Odometry.h"
#include "sensor_msgs/LaserScan.h"
#include "tf/transform_datatypes.h"
#include "roomba_500driver_meiji/RoombaCtrl.h"
#include <cmath>
#include <vector>
#include <limits>
#include <sstream>

double dt;
double dv;
double dyaw;
double max_speed;
double min_speed;
double max_accel;
double max_yawrate;
double max_dyawrate;
double roomba_radius;
double limit_speed;
double limit_yawrate;

double predict_time;
double l_ob_cost_gain;
double speed_cost_gain;
double omega_cost_gain;
double to_g_goal_cost_gain;
double dis_g_goal_cost_gain;

nav_msgs::Path roomba_gpath;
nav_msgs::Odometry roomba_odom;
sensor_msgs::LaserScan roomba_scan;
geometry_msgs::PoseStamped roomba_status;

bool get_pose = false;
bool get_speed = false;
bool get_gpath = false;

struct Speed{
  double v;
  double omega;
};

struct Position{
  double x;
  double y;
  double yaw;
};

struct Status{
  double x;
  double y;
  double yaw;
  double v;
  double omega;
};

struct Dw{
  double min_v;
  double max_v;
  double min_omega;
  double max_omega;
};

void angle_range(double& theta)
{
  if(theta > M_PI){
    theta -= 2*M_PI;
  } else if(theta < -M_PI){
    theta += 2*M_PI;
  }
  return;
}

double atan(const double& x, const double& y)
{
  double theta = 0.0;

  if(!x){
    if(!y){
      theta = 0.0;
    } else if(y < 0) {
      theta = -M_PI / 2;
    } else if(y > 0) {
      theta = M_PI / 2;
    }
  } else {
    theta = std::atan2(y, x);
    angle_range(theta);
  }

  return theta;
}

//全部local
void motion(Status& roomba, const double& v, const double& y)
{
  roomba.yaw += y * dt;
  angle_range(roomba.yaw);
  roomba.x += v * std::cos(roomba.yaw) * dt;
  roomba.y += v * std::sin(roomba.yaw) * dt;
  roomba.v = v;
  roomba.omega = y;
  return;
}

Dw calc_dynamic_window(const Status& roomba)
{
  const Dw Vs= {
   min_speed,
   limit_speed,
   -limit_yawrate,
   limit_yawrate
  };
  Dw Vd = {
    roomba.v - max_accel * dt,
    roomba.v + max_accel * dt,
    roomba.omega - max_dyawrate * dt,
    roomba.omega + max_dyawrate * dt
  };
  Dw dw = {
	  std::max(Vs.min_v, Vd.min_v),
	  std::min(Vs.max_v, Vd.max_v),
	  std::max(Vs.min_omega, Vd.min_omega),
	  std::min(Vs.max_omega, Vd.max_omega)
  };

  return dw;
}

//全部local
void calc_l_traj(std::vector<Status>& traj, const double& v, const double& y)
{
  Status roomba = {0.0, 0.0, 0.0, 0.0, 0.0};

  traj.erase(traj.begin(), traj.end());
  for(double t = 0.0; t < predict_time; t += dt){
    traj.push_back(roomba);
    motion(roomba, v, y);
  }
  traj.push_back(roomba);
  return;
}

double calc_to_g_goal_cost(const std::vector<Status>& l_traj, const Status& g_roomba, const Position& g_goal)
{
  Status l_last = l_traj.back();
  Position g_last = {0.0, 0.0, 0.0};
  Position g_error = {0.0, 0.0, 0.0};
  double l_last_r = 0.0;
  double l_last_theta = 0.0;
  double g_last_theta = 0.0;
  //double g_error_dist = 0.0;
  double g_error_theta = 0.0;
  //double to_g_goal_dist = 0.0;
  double to_g_goal_theta = 0.0;

  l_last_r = std::sqrt(l_last.x*l_last.x + l_last.y*l_last.y);
  l_last_theta = atan(l_last.x, l_last.y);

  g_last.x = g_roomba.x + l_last_r*std::cos(g_roomba.yaw + l_last_theta);
  g_last.y = g_roomba.y + l_last_r*std::sin(g_roomba.yaw + l_last_theta);
  g_last.yaw = g_roomba.yaw + l_last.yaw;
  g_last_theta = atan(g_last.x, g_last.y);

  g_error.x = g_goal.x - g_last.x;
  g_error.y = g_goal.y - g_last.y;
  //g_error_dist = std::sqrt(g_error.x*g_error.x + g_error.y*g_error.y);
  g_error_theta = atan(g_error.x, g_error.y);

  //to_g_goal_dist = g_error_dist;
  to_g_goal_theta = g_error_theta - g_last.yaw;
  angle_range(to_g_goal_theta);
  to_g_goal_theta = std::fabs(to_g_goal_theta);

  return to_g_goal_cost_gain * to_g_goal_theta /*+ dis_g_goal_cost_gain * to_g_goal_dist*/;
}

double calc_to_g_path_cost(const std::vector<Status>& l_traj, const Status& g_roomba, const nav_msgs::Path& g_path)
{
  Position g_goal = {0.0, 0.0, 0.0};
  Position g_error = {0.0, 0.0, 0.0};
  int next_g_path_p_i = 0;
  int n_g_path_p_d_i = 0;//nearest gpath point distance i
  double n_g_path_p_d = 10000.0;//nearest gpath point distance
  double g_error_dist = 0.0;
  double to_g_path_cost = 0.0;

  for(int i = 0; i < g_path.poses.size(); i++){
    g_error.x = g_path.poses[i].pose.position.x - g_roomba.x;
    g_error.y = g_path.poses[i].pose.position.y - g_roomba.y;
    g_error_dist = std::sqrt(g_error.x*g_error.x + g_error.y*g_error.y);

    if(n_g_path_p_d > g_error_dist){
      n_g_path_p_d = g_error_dist;
      n_g_path_p_d_i = i;
    }
  }

  //gpathが環状なら
  next_g_path_p_i = (n_g_path_p_d_i + (int)predict_time) % g_path.poses.size();
  g_goal.x = g_path.poses[next_g_path_p_i].pose.position.x;
  g_goal.y = g_path.poses[next_g_path_p_i].pose.position.y;
  g_goal.yaw = tf::getYaw(g_path.poses[next_g_path_p_i].pose.orientation);

  to_g_path_cost = calc_to_g_goal_cost(l_traj, g_roomba, g_goal);
  return to_g_path_cost;
}

//全部local
double calc_speed_cost(const std::vector<Status>& traj)
{
  Status last = traj.back();
  double error_speed = max_speed - last.v;
  double error_omega = std::fabs(last.omega); 

  return speed_cost_gain*error_speed + omega_cost_gain*error_omega;
}

//全部local
double calc_l_ob_cost(const std::vector<Status>& traj, const std::vector<float>& obstacle)
{
  int skip_i = 1;
  int skip_j = 10;
  double x = 0.0;
  double xx = 0.0;
  double y = 0.0;
  double yy = 0.0;
  double r = 0.0;
  double rr = 0.0;
  double ob = 0.0;
  double past_ob = 0.0;
  double obob = 0.0;
  double rob = 0.0;
  double theta = 0.0;
  double ob_theta = 0.0;
  double c = 0.0;
  double dist = 0.0;
  double inf = std::numeric_limits<double>::infinity();
  double min_dist = inf;
  double left_rod_max = 1.10;
  double left_rod_min = 0.90;
  double right_rod_max = -0.90;
  double right_rod_min = -1.10;

  for(int i = 0; i < traj.size(); i += skip_i){
    x = traj[i].x;
    y = traj[i].y;
    xx = x*x;
    yy = y*y;
    r = std::sqrt(xx + yy);
    theta = atan(x, y);
    ob_theta = roomba_scan.angle_min;
    past_ob = obstacle[0];

    for(int j = 0; j < obstacle.size(); j += skip_j){
      //ROS_INFO("ob[j] = %f\nob_theta = %lf\n", obstabcle[j], ob_theta);
      if(( left_rod_min < ob_theta && ob_theta < left_rod_max)
      || ( right_rod_min < ob_theta && ob_theta < right_rod_max)){
        //ob = past_ob;
        ob_theta += skip_j * roomba_scan.angle_increment;
        continue;
      } //else {
        if(obstacle[j] < 60.0f){
          ob = obstacle[j];
          past_ob = ob;
        } else {
          ob = 60.0;
          past_ob = ob;
        }
      //}

      //棒の場所判定用
      //if(obstacle[j] < 5.0 * roomba_radius){
      //  ROS_INFO("ob[j] = %f\nob_theta = %lf\n", obstacle[j], ob_theta);
      //  ob_theta += skip_j * roomba_scan.angle_increment;
      //  continue;
      //}

      //極座標での２点間の距離を調べる
      rr = r*r;
      obob = ob*ob;
      rob = r*ob;
      c = std::cos(theta - ob_theta);
      dist = std::sqrt(rr + obob - 2*rob*c);

      if(dist <= roomba_radius){
		return inf;
      }

      if(min_dist >= dist){
        min_dist = dist;
      }

      ob_theta += skip_j * roomba_scan.angle_increment;
    }
  }

  return l_ob_cost_gain / min_dist;
}

//Speed dwa_control(const Status& g_roomba, const Position& g_goal, const std::vector<float>& l_ob)
Speed dwa_control(const Status& g_roomba, const nav_msgs::Path& g_path, const std::vector<float>& l_ob)
{
  Dw dw = {0.0, 0.0, 0.0, 0.0};
  Speed best_output = {0.0, 0.0};
  double min_cost = 1000.0;
  double l_ob_cost = 0.0;
  double final_cost = 0.0;
  double speed_cost = 0.0;
  double to_g_goal_cost = 0.0;
  double to_g_path_cost = 0.0;
  double exception_omega = max_yawrate * 0.10;
  std::vector<Status> l_traj;
  double min_to_goal_cost = to_g_goal_cost;
  double min_to_gpath_cost = to_g_path_cost;
  double min_speed_cost = speed_cost;
  double min_ob_cost = l_ob_cost;

  dw = calc_dynamic_window(g_roomba);
  for(double v = dw.min_v; v <= dw.max_v; v += dv){
	//ROS_INFO("\ndw.max_omega = %lf\n", dw.max_omega);
    for(double y = dw.min_omega; y <= dw.max_omega; y += dyaw){
      if((-exception_omega < y && y < 0.0) || (0.0 < y && y < exception_omega))
		  continue;
     // ROS_INFO("\n--------------------------------------------------------------------------------------------\nv = %lf\ny = %lf\n", v, y);

      calc_l_traj(l_traj, v, y);

      speed_cost = calc_speed_cost(l_traj);
      l_ob_cost = calc_l_ob_cost(l_traj, l_ob);
      //to_g_goal_cost = calc_to_g_goal_cost(l_traj, g_roomba, g_goal);
      to_g_path_cost = calc_to_g_path_cost(l_traj, g_roomba, g_path);
      //final_cost = to_g_goal_cost + speed_cost + l_ob_cost;
      final_cost = to_g_path_cost + speed_cost + l_ob_cost;

      ////ROS_INFO("\nto_goal_cost = %lf\n\n", to_g_goal_cost);
      //ROS_INFO("\nto_gpath_cost = %lf\n\n", to_g_path_cost);
      //ROS_INFO("\nspeed_cost = %lf\n\n", speed_cost);
      //ROS_INFO("\nob_cost = %lf\n\n", l_ob_cost);
      //ROS_INFO("\nfinal_cost = %lf\n", final_cost);
      //ROS_INFO("\n---------------------------------------------------------------------\nnow min_cost = %lf\n", min_cost);
      //ROS_INFO("\nnow min_speed_cost = %lf\n", min_speed_cost);
      //ROS_INFO("\nnow min_ob_cost = %lf\n", min_ob_cost);
      ////ROS_INFO("\nnow min_to_goal_cost = %lf\n", min_to_goal_cost);
      //ROS_INFO("\nnow min_to_gpath_cost = %lf\n", min_to_gpath_cost);
      //ROS_INFO("\nnow best_output.v = %lf\n", best_output.v);
      //ROS_INFO("\nnow best_output.omega = %lf\n-----------------------------------------------------------------\n", best_output.omega);

      if(min_cost > final_cost) {
        //min_to_goal_cost = to_g_goal_cost;
        min_to_gpath_cost = to_g_path_cost;
        min_speed_cost = speed_cost;
        min_ob_cost = l_ob_cost;
        min_cost = final_cost;
        best_output.v = v;
        best_output.omega = y;
      }
    }
  }

  if(min_cost > 999.0) {
    best_output.v = 0.0;
    best_output.omega = 0.2;
  }

  return best_output;
}

//全部global
int is_goal(const Status& roomba, const Position& goal)
{
  Position error = {0.0, 0.0, 0.0};
  error.x = goal.x - roomba.x;
  error.y = goal.y - roomba.y;
  error.yaw = goal.yaw - roomba.yaw;
  double error_dist = std::sqrt(error.x*error.x + error.y*error.y);
  double yaw_tolerance = M_PI / 90;

  if(error_dist <= roomba_radius && error.yaw < yaw_tolerance){
    //ROS_INFO("\n\nGoal!!!\n\n");
    return 0;
  } else {
    return 11;
  }
}

bool is_normalized(const geometry_msgs::Quaternion& msg)
{
  const double quaternion_tolerance = 0.1;
  tf::Quaternion bt = tf::Quaternion(msg.x, msg.y, msg.z, msg.w);

  if(fabs(bt.length2() - 1.0) > quaternion_tolerance){
    std::cout << "quaternion unnormalized" << std::endl;
    return false;
  } else {
    return true;
  }
}

void odom_callback(const nav_msgs::Odometry::ConstPtr& msg)
{
  roomba_odom = *msg;
  get_speed = true;
}

void scan_callback(const sensor_msgs::LaserScan::ConstPtr& msg)
{
  roomba_scan = *msg;
}

void gpath_callback(const nav_msgs::Path::ConstPtr& msg)
{
  roomba_gpath = *msg;
  get_gpath = true;
}

void amcl_callback(const geometry_msgs::PoseStamped::ConstPtr& msg)
{
  roomba_status = *msg;
  get_pose = true;
  std::cout << "get pose from amcl" << std::endl;
}

int main(int argc, char **argv)
{
  ros::init(argc, argv, "dwa");
  ros::NodeHandle n;
  ros::NodeHandle nh("~");
  ros::Publisher roomba_ctrl_pub = n.advertise<roomba_500driver_meiji::RoombaCtrl>("roomba/control", 1);
  ros::Subscriber roomba_odom_sub = n.subscribe("roomba/odometry", 1, odom_callback);
  ros::Subscriber roomba_scan_sub = n.subscribe("scan",1, scan_callback);
  ros::Subscriber roomba_gpath_sub = n.subscribe("gpath", 1, gpath_callback);
  ros::Subscriber roomba_status_sub = n.subscribe("amcl_pose", 1, amcl_callback);
  ros::Rate loop_rate(10.0);

  nh.param("dt", dt, 0.0);
  nh.param("dv", dv, 0.0);
  nh.param("dyaw", dyaw, 0.0);
  nh.param("max_speed", max_speed, 0.0);
  nh.param("min_speed", min_speed, 0.0);
  nh.param("max_accel", max_accel, 0.0);
  nh.param("max_yawrate", max_yawrate, 0.0);
  nh.param("max_dyawrate", max_dyawrate, 0.0);
  nh.param("roomba_radius", roomba_radius, 0.0);
  nh.param("limit_speed", limit_speed, 0.0);
  nh.param("limit_yawrate", limit_yawrate, 0.0);
  nh.param("predict_time", predict_time, 0.0);
  nh.param("l_ob_cost_gain", l_ob_cost_gain, 0.0);
  nh.param("speed_cost_gain", speed_cost_gain, 0.0);
  nh.param("omega_cost_gain", omega_cost_gain, 0.0);
  nh.param("to_g_goal_cost_gain", to_g_goal_cost_gain, 0.0);
  nh.param("dis_g_goal_cost_gain", dis_g_goal_cost_gain, 0.0);

  roomba_500driver_meiji::RoombaCtrl roomba_ctrl;

  Speed output = {0.0, 0.0};
  Position g_goal = {3.0, 3.0, 0.0};
  Status g_roomba = {0.0, 0.0, 0.0, 0.0, 0.0};

  //dwaのみ試したいときにtrue
  //dwa_control, output, to_g_goal_cost, final_costのコメントも変えること
  const bool dev = false;
  bool normalized = false;
  //ROS_INFO("start");

  while (ros::ok()){
    if(dev){
      normalized = is_normalized(roomba_odom.pose.pose.orientation);
    } else {
      normalized = is_normalized(roomba_status.pose.orientation);
    }
    if(roomba_scan.ranges.size() && normalized && get_pose && get_speed && get_gpath){
      if(dev) {
        g_roomba.x = roomba_odom.pose.pose.position.x;
        g_roomba.y = roomba_odom.pose.pose.position.y;
        g_roomba.yaw = tf::getYaw(roomba_odom.pose.pose.orientation);
        roomba_ctrl.mode = is_goal(g_roomba, g_goal);//ゴール判別
        //roomba_ctrl.mode = 0;
      } else {
        g_roomba.x = roomba_status.pose.position.x;
        g_roomba.y = roomba_status.pose.position.y;
        g_roomba.yaw = tf::getYaw(roomba_status.pose.orientation);
        roomba_ctrl.mode = 11;
      }
      g_roomba.v = max_speed * roomba_odom.twist.twist.linear.x;
      g_roomba.omega = max_yawrate * roomba_odom.twist.twist.angular.z;

      //output = dwa_control(g_roomba, g_goal, roomba_scan.ranges);
      output = dwa_control(g_roomba, roomba_gpath, roomba_scan.ranges);
      roomba_ctrl.cntl.linear.x = output.v / max_speed;
      roomba_ctrl.cntl.angular.z = output.omega / max_yawrate;
	  //ROS_INFO("\nroomba_ctrl...x = %lf\nroomba_ctrl...z = %lf\n", roomba_ctrl.cntl.linear.x, roomba_ctrl.cntl.angular.z);
      roomba_ctrl_pub.publish(roomba_ctrl);
    }
    ros::spinOnce();
    loop_rate.sleep();
  }

  return 0;
}