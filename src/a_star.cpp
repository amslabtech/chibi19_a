#include "ros/ros.h"
//#include "tf/transform_datatypes.h"
#include "nav_msgs/Path.h"
#include "nav_msgs/OccupancyGrid.h"
#include "geometry_msgs/PoseStamped.h"
#include <sstream>
#include <vector>
#include <algorithm>
#include <cmath>

geometry_msgs::PoseStamped roomba_status;
nav_msgs::OccupancyGrid map;
bool map_flag = false;

struct Open{
  int f;
  int g;
  int h;
  int x;
  int y;
};

bool A_star(
  //std::vector<std::vector<char> >& policy,
  nav_msgs::Path roomba_gpath,
  const std::vector<std::vector<char> >& grid,
  const std::vector<std::vector<int> >& heuristic,
  const std::vector<int>& init,
  const std::vector<int>& goal
){
  bool found = false;
  bool resign = false;
  int row = map.info.height;
  int col = map.info.width;
  int x = init[0];
  int y = init[1];
  int g = 0;
  int h = heuristic[x][y];
  int f = g + h;
  int x2 = 0;
  int y2 = 0;
  int g2 = 0;
  int h2 = 0;
  int f2 = 0;
  int cost = 1;
  float res = map.info.resolution;
  double origin_x = map.info.origin.position.x;
  double origin_y = map.info.origin.position.y;
  geometry_msgs::PoseStamped gpath_point;

  std::vector<std::vector<char> > delta = {
    {-1,  0},
    //{-1, -1},
    { 0, -1},
    //{ 1, -1},
    { 1,  0},
    //{ 1,  1},
    { 0,  1}
    //{-1,  1}
  };
  //std::vector<double> delta_name = {'^','>','v','<'};

  std::vector<std::vector<bool> > closed(row, std::vector<bool>(col, false));
  closed[init[0]][init[1]] = true;
  std::vector<std::vector<char> > action(row, std::vector<char>(col, -1));

  Open open_init = {f, g, h, x, y};
  std::vector<Open> open;
  open.push_back(open_init);

  Open next = {0, 0, 0, 0, 0};
  Open new_open = {0, 0, 0, 0, 0};

  int roop_count = 0;
  //ROS_INFO("\nopen.size() = %ld\n", open.size());
  while(!found && !resign){
    roop_count++;
    if(!open.size()){
      resign = true;
      ROS_INFO("\nfail\nfound = %d\nresign = %d\nroop_count = %d\n", found, resign, roop_count);
    } else {
      std::sort(open.begin(), open.end(),
          [](const Open& x, const Open& y) {
            return x.f > y.f;
          });
      next = open.back();
      open.pop_back();
      x = next.x;
      y = next.y;
      g = next.g;

      if(x == goal[0] && y == goal[1]){
        found = true;
      } else {
        for(char i = 0; i < delta.size(); i++){
          x2 = x + delta[i][0];
          y2 = y + delta[i][1];
          if(x2 >= 0 && x2 < row && y2 >= 0 && y2 < col){
            if(!closed[x2][y2] && -1 < grid[x2][y2] && grid[x2][y2] < 100){
              g2 = g + cost;
              h2 = heuristic[x2][y2];
              f2 = g2 + h2;

              new_open.f = f2;
              new_open.g = g2;
              new_open.h = h2;
              new_open.x = x2;
              new_open.y = y2;
              open.push_back(new_open);

              closed[x2][y2] = true;
              action[x2][y2] = i;

              gpath_point.pose.position.x = x2/res + origin_x;
              gpath_point.pose.position.y = y2/res + origin_y;
              roomba_gpath.poses.push_back(gpath_point);
            }
          }
        }
      }
    }
  }


//  x = goal[0];
//  y = goal[1];
//  policy[x][y] = '*';
//  while(x != init[0] && y != init[1]){
//    x2 = x - delta[action[x][y]][0];
//    y2 = y - delta[action[x][y]][1];
//    policy[x2][y2] = delta_name[action[x][y]];
//    x = x2;
//    y = y2;
//  }

//  for(int i = 0; i < row; i++){
//    ROS_INFO("\n");
//    for(int j = 0; j < col; j++){
//      ROS_INFO("%3c ", policy[i][j]);
//    }
//  }
}

void map_callback(const nav_msgs::OccupancyGrid::ConstPtr& msg){
  map = *msg;
  map_flag = true;
}

void amcl_callback(const geometry_msgs::PoseStamped::ConstPtr& msg){
  roomba_status = *msg;
}

int main(int argc, char **argv)
{
  ros::init(argc, argv, "a_star");
  ros::NodeHandle n;

  ros::Publisher roomba_gpath_pub = n.advertise<nav_msgs::Path>("gpath", 1);
  ros::Subscriber map_sub = n.subscribe("map", 1, map_callback);
  ros::Subscriber roomba_status_sub = n.subscribe("amcl_pose", 1, amcl_callback);
  
  ros::Rate loop_rate(10);

  nav_msgs::Path roomba_gpath;

//ここのmap.info.heightとかもmap_flagがtrueになってからじゃないとだめ？
//おそらくそうじゃ
  while(!map_flag){
    ros::spinOnce();
    ROS_INFO("\nmap unready\n");
  }

  unsigned int map_row = map.info.height;
  unsigned int map_col = map.info.width;
  bool find_init = false;

  std::vector<int> init(2, 0);
  std::vector<int> goal(2, 0);
  std::vector<std::vector<char> > grid(map_row, std::vector<char>(map_col, 0));
  std::vector<std::vector<int> > heuristic(map_row, std::vector<int>(map_col, 0));
  for(int row = 0; row < map_row; row++){
    for(int col = 0; col < map_col; col++){
      grid[row][col] = map.data[ row*map_col + col];
      heuristic[row][col] = map_row + map_col - row - col - 2;
      //デバッグのための初期化
      //もう少し工夫が必要
      if(!grid[row][col]){
        if(!find_init){
          init[0] = row;
          init[1] = col;
          find_init = true;
        }
        if(goal[0] <= row && goal[1] <= col){
          goal[0] = row;
          goal[1] = col;
        }
      }    
    }    
  }
  ROS_INFO("\ninit[0] = %d\n", init[0]);
  ROS_INFO("\ninit[1] = %d\n", init[1]);
  ROS_INFO("\ngrid[init[0]+0][init[1]+0] = %d\n", grid[init[0]][init[1]]);//==0
  ROS_INFO("\ngrid[init[0]+0][init[1]+1] = %d\n", grid[init[0]][init[1]+1]);//==-1
  ROS_INFO("\ngrid[init[0]+1][init[1]+0] = %d\n", grid[init[0]+1][init[1]]);//==100
  ROS_INFO("\ngrid[init[0]+1][init[1]+1] = %d\n", grid[init[0]+1][init[1]+1]);//==-1
  ROS_INFO("\ngoal[0] = %d\n", goal[0]);
  ROS_INFO("\ngoal[1] = %d\n", goal[1]);

  //std::vector<std::vector<char> > policy(map_row, std::vector<char>(map_col, ' '));

  while(ros::ok())
  {

    //ROS_INFO("\nmap_row = %d\n", map_row);
    //ROS_INFO("\nmap_col = %d\n", map_col);
//    roomba_gpath.erase(roomba_gpath.begin(), roomba_gpath.end());

    if(A_star(roomba_gpath, grid, heuristic, init, goal) && map_flag){
      roomba_gpath_pub.publish(roomba_gpath);
    }

    ros::spinOnce();

    loop_rate.sleep();
  }

  return 0;
}