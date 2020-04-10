/*
 * SuperBot_Controller
 * ZXC and YYH
 * April, 2020
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <base.h>
#include <gripper.h>
#include <webots/keyboard.h>
#include <webots/robot.h>
#include <webots/motor.h>
#include <webots/touch_sensor.h>
#include <webots/camera.h>
#include <webots/camera_recognition_object.h>
#include <webots/gps.h>
#include <webots/compass.h>
#define TIME_STEP 32

#define max(a, b) (((a) > (b)) ? (a) : (b))
#define min(a, b) (((a) > (b)) ? (b) : (a))
#define abs(a) (((a) < (0)) ? (0) : (a))
#define MAX_WIDTH 0.2f
#define MIN_WIDTH 0.0f
// WbDeviceTag motorL;
// WbDeviceTag motorR;
WbDeviceTag forceL;
WbDeviceTag forceR;
#define MAX_HEIGHT 0.4f
#define MIN_HEIGHT 0.03f
// WbDeviceTag motorM;
#define GRIPPER_MOTOR_MAX_SPEED 0.1
#define PI 3.1415926535f
static WbDeviceTag gripper_motors[3];
static WbDeviceTag camera[2];
WbDeviceTag gps;
WbDeviceTag compass;
double gps_values[2];         //gps值
double compass_angle;         //罗盘角度
double initial_posture[3];    //起点位姿,0为x,1为z,2为角度，每段轨迹只设置一次
double tmp_target_posture[3]; //临时目标位姿，需要不断计算
double fin_target_posture[3]; //最终目标位姿，

//寻找货物定点 右->...-> 上->...->左->...->下
int Travel_Point_Index = 0; //定点编号
double fixed_posture_travelaround[12][3] =
    {
        {1.05, 0.00, PI * 2},      //右
        {1.05, -1.05, PI * 2},     //右上
        {1.05, -1.05, PI / 2},     //右上 转
        {0.00, -1.05, PI / 2},     //上
        {-1.05, -1.05, PI / 2},    //左上
        {-1.05, -1.05, PI},        //左上 转
        {-1.05, 0, PI},            //左
        {-1.05, 1.05, PI},         //左下
        {-1.05, 1.05, 3 * PI / 2}, //左下 转
        {0.00, 1.05, 3 * PI / 2},  //下
        {1.05, 1.05, 3 * PI / 2},  //右下
        {1.05, 1.05, PI * 2}       //右下 转
};
//识别空货架定点 右->上->左->下
int FindEmpty_Point_Index = 0; //定点编号
double fixed_posture_findempty[4][3] =
    {
        {1.05, 0.00, PI},          //右
        {0.00, -1.05, 3 * PI / 2}, //上
        {-1.05, 0, 0},             //左
        {-1.05, 1.05, PI / 2}      //下
};

//机器人状态枚举
enum RobotState
{
  Init_Pose,
  Recognize_Empty,
  Arround_Moving,
  Grab_Item,
  Back_Moving,
  TurnBack_To_LoadItem,
  Item_Loading
};
double width = 0.0;  //爪子0~0.1
double height = 0.0; //爪子-0.05~0.45

static void step();
static void passive_wait(double sec);
static void display_helper_message();
void lift(double position);
void moveFingers(double position);
void init_all();
void caculate_tmp_target(double tmp_posture[], double fin_posture[]);
void set_posture(double posture[], double x, double z, double angle);
void get_gps_values(double v_gps[]);
double vector2_angle(const double v1[], const double v2[]);
void get_compass_angle(double *ret_angle);
int keyboard_control(int c);
bool targetdist_reached(double target_posture[], double dist_threshold);
bool targetpos_reached(double target_posture[], double pos_threshold);

void Find_Empty(WbDeviceTag camera, int goods_class);
void Find_Goods(WbDeviceTag camera, int goods_class);
bool Aim_and_Grasp(int *grasp_state, WbDeviceTag camera, int objectID);
void Return_and_Load(double targetplace);
bool Moveto_CertainPoint(double fin_posture[]);
void Robot_State_Machine(int *main_state, int *grasp_state);

//*?                 main函数      <开始>            ?*//
//主函数

int main(int argc, char **argv)
{
  init_all();

  printf("Ready to go!\n");
  int main_state = 0;  //机器人运行状态
  int grasp_state = 0; //手爪状态
  while (true)
  {
    step();
    Robot_State_Machine(&main_state, &grasp_state);
    // printf("State:%d\n", main_state);
    keyboard_control(wb_keyboard_get_key());
  }

  wb_robot_cleanup();

  return 0;
}
//*?                 main函数       <结束>            ?*//

//*?                 核心控制函数    <开始>            ?*//
//各模块初始化
void init_all()
{
  // 机器人初始化
  wb_robot_init();
  base_init();
  passive_wait(2.0);

  camera[0] = wb_robot_get_device("camera_top"); //相机初始化
  camera[1] = wb_robot_get_device("camera_front");
  wb_camera_enable(camera[0], TIME_STEP);
  wb_camera_recognition_enable(camera[0], TIME_STEP);
  wb_camera_enable(camera[1], TIME_STEP);
  wb_camera_recognition_enable(camera[1], TIME_STEP);

  //GPS初始化
  gps = wb_robot_get_device("gps_copy");
  wb_gps_enable(gps, TIME_STEP);
  //Compass初始化
  compass = wb_robot_get_device("compass_copy");
  wb_compass_enable(compass, TIME_STEP);
  //底盘全方位移动初始化
  base_goto_init(TIME_STEP);
  //设置初始位姿
  step();
  get_gps_values(gps_values);
  get_compass_angle(&compass_angle);
  set_posture(initial_posture, gps_values[0], gps_values[1], compass_angle);
  //设置第一个定点位姿
  set_posture(fin_target_posture, fixed_posture_findempty[FindEmpty_Point_Index][0], fixed_posture_findempty[FindEmpty_Point_Index][1], fixed_posture_findempty[FindEmpty_Point_Index][2]);
  //计算下一个临时目标;
  caculate_tmp_target(tmp_target_posture, fin_target_posture);
  //设置底盘运动目标
  base_goto_set_target(tmp_target_posture[0], tmp_target_posture[1], tmp_target_posture[2]);

  display_helper_message();
  wb_keyboard_enable(TIME_STEP);

  gripper_motors[0] = wb_robot_get_device("lift motor");
  gripper_motors[1] = wb_robot_get_device("left finger motor");
  gripper_motors[2] = wb_robot_get_device("right finger motor");

  //电机加力反馈
  wb_motor_enable_force_feedback(gripper_motors[1], 1);
  wb_motor_enable_force_feedback(gripper_motors[2], 1);
}

//机器人状态机
void Robot_State_Machine(int *main_state, int *grasp_state)
{
  switch (*main_state)
  {
  //初始工作状态，站在四个定点之一，准备识别空货架
  case Init_Pose:
  {
    if (Moveto_CertainPoint(fin_target_posture))
    {
      *main_state = Recognize_Empty;
      printf("Ready for scanningscanning!/n");
    }
    break;
  }
  //识别空货架
  case Recognize_Empty:
  {
    int Empty_Flag = 1;
    //TODO ...这里写识别空货架
    Empty_Flag = 0;
    printf("Ready for scanning!/n");
    Find_Empty(camera[1], 43);

    if (Empty_Flag) //这里写识别结束标志位
    {
      *main_state = Arround_Moving;
      set_posture(initial_posture, gps_values[0], gps_values[1], compass_angle);
      set_posture(fin_target_posture, fixed_posture_travelaround[Travel_Point_Index][0], fixed_posture_travelaround[Travel_Point_Index][1], fixed_posture_travelaround[Travel_Point_Index][2]);
    }
    break;
  }
  //做环绕运动
  case Arround_Moving:
  {
    int Found_Item_Flag = 0;
    //TODO ...这里写识别待抓取物体 比如正好面对时物品

    if (Found_Item_Flag)
    {
      *main_state = Grab_Item;
    }
    else
    {
      if (Moveto_CertainPoint(fin_target_posture))
      {
        *main_state = Recognize_Empty;
        set_posture(initial_posture, gps_values[0], gps_values[1], compass_angle);
        Travel_Point_Index += 1;
        Travel_Point_Index %= 12;
        set_posture(fin_target_posture, fixed_posture_travelaround[Travel_Point_Index][0], fixed_posture_travelaround[Travel_Point_Index][1], fixed_posture_travelaround[Travel_Point_Index][2]);
      }
    }
    break;
  }
  //抓物品
  case Grab_Item:
  {
    if (Aim_and_Grasp(grasp_state, camera[1], 43))
    {
      *main_state = Back_Moving;
    }
    break;
  }
  //取货回程
  case Back_Moving:
  {
    int Shelf_Arriving_Flag = 0;
    //TODO ...这里写到达指定目标货架
    if (Shelf_Arriving_Flag)
    {
      *main_state = TurnBack_To_LoadItem;
      set_posture(initial_posture, gps_values[0], gps_values[1], compass_angle);
      set_posture(fin_target_posture, fixed_posture_findempty[FindEmpty_Point_Index][0], fixed_posture_findempty[FindEmpty_Point_Index][1], fixed_posture_findempty[FindEmpty_Point_Index][2]);
    }
    else
    {
      //TODO 这里还要处理往哪边近绕圈的问题
      if (Moveto_CertainPoint(fin_target_posture))
      {
        set_posture(initial_posture, gps_values[0], gps_values[1], compass_angle);
        Travel_Point_Index += 1;
        Travel_Point_Index %= 12;
        set_posture(fin_target_posture, fixed_posture_travelaround[Travel_Point_Index][0], fixed_posture_travelaround[Travel_Point_Index][1], fixed_posture_travelaround[Travel_Point_Index][2]);
      }
    }
  }
  //转身准备上货动作
  case TurnBack_To_LoadItem:
  {
    if (Moveto_CertainPoint(fin_target_posture))
    {
      *main_state = Item_Loading;
    }
  }
  //上货
  case Item_Loading:
  {
    int Item_Load_Finished = 0;

    //TODO 这里写上货

    if (Item_Load_Finished)
    {
      *main_state = Init_Pose;
      set_posture(initial_posture, gps_values[0], gps_values[1], compass_angle);
      set_posture(fin_target_posture, fixed_posture_findempty[FindEmpty_Point_Index][0], fixed_posture_findempty[FindEmpty_Point_Index][1], fixed_posture_findempty[FindEmpty_Point_Index][2]);
    }
  }
  //ERROR
  default:
  {
    printf("Error form State Machine !!!/n");
    break;
  }
  }
}

//键盘控制基本运动
int keyboard_control(int c)
{
  if ((c >= 0))
  { //&& c != pc) {//不要求键值变化
    switch (c)
    {
    case 'G':
    {
      get_gps_values(gps_values);
      printf("GPS device: %.3f %.3f\n", gps_values[0], gps_values[1]);
      get_compass_angle(&compass_angle);
      printf("Compass device: %.3f\n", compass_angle);
      break;
    }
    case WB_KEYBOARD_UP:
      printf("Go forwards\n");
      base_forwards();
      break;
    case WB_KEYBOARD_DOWN:
      printf("Go backwards\n");
      base_backwards();
      break;
    case WB_KEYBOARD_LEFT:
      printf("Strafe left\n");
      base_strafe_left();
      break;
    case WB_KEYBOARD_RIGHT:
      printf("Strafe right\n");
      base_strafe_right();
      break;
    case WB_KEYBOARD_PAGEUP:
      printf("Turn left\n");
      base_turn_left();
      break;
    case WB_KEYBOARD_PAGEDOWN:
      printf("Turn right\n");
      base_turn_right();
      break;
    case WB_KEYBOARD_END:
    case ' ':
      printf("Reset\n");
      base_reset();
      // arm_reset();
      break;
    case '+':
    case 388:
    case 65585:
      printf("Grip\n");
      //  gripper_grip();
      break;
    case '-':
    case 390:
      printf("Ungrip\n");
      //  gripper_release();
      break;
    case 332:
    case WB_KEYBOARD_UP | WB_KEYBOARD_SHIFT:
      //UpDownControll(Target_Height+=0.02);
      lift(height += 0.005);
      printf("Increase arm height to %.3f\n", height);
      break;
    case 326:
    case WB_KEYBOARD_DOWN | WB_KEYBOARD_SHIFT:
      //UpDownControll(Target_Height-=0.02);
      lift(height -= 0.005);
      printf("Decrease arm height to %.3f\n", height);
      // arm_decrease_height();
      break;
    case 330:
    case WB_KEYBOARD_RIGHT | WB_KEYBOARD_SHIFT:
      //ClawControll(Target_Width-=0.01);
      moveFingers(width -= 0.001);
      printf("Close the Claws to %.3f\n", width);
      break;
    case 328:
    case WB_KEYBOARD_LEFT | WB_KEYBOARD_SHIFT:
      //ClawControll(Target_Width+=0.01);
      moveFingers(width += 0.001);
      printf("Open the Claws to %.3f\n", width);
      break;
    default:
      fprintf(stderr, "Wrong keyboard input\n");
      break;
    }
  }
  return 0;
}

//GPS运动到指定位姿，返回bool值反馈是否到达
bool Moveto_CertainPoint(double fin_posture[])
{
  if (targetdist_reached(fin_posture, 0.05) && targetpos_reached(fin_posture, 0.05))
  {
    printf("到达目标位置！\n");
    base_reset();
    return true;
  }
  else
  {
    caculate_tmp_target(tmp_target_posture, fin_posture);
    base_goto_set_target(tmp_target_posture[0], tmp_target_posture[1], tmp_target_posture[2]);
    // printf("Target:%s\n", point_name[point_index]);

    // printf("initial target： %.3f  %.3f  %.3f\n", initial_posture[0], initial_posture[1], initial_posture[2]);
    // printf("tmp target： %.3f  %.3f  %.3f\n", tmp_target_posture[0], tmp_target_posture[1], tmp_target_posture[2]);
    // printf("final target： %.3f  %.3f  %.3f\n\n", fin_posture[0], fin_posture[1], fin_posture[2]);
   
    base_goto_run();
    return false;
  }
}

//前部摄像头校准并抓取
bool Aim_and_Grasp(int *grasp_state, WbDeviceTag camera, int objectID)
{
  //饼干盒ID43 水瓶ID56
  int number_of_objects = wb_camera_recognition_get_number_of_objects(camera);
  const WbCameraRecognitionObject *objects = wb_camera_recognition_get_objects(camera);
  for (int i = 0; i < number_of_objects; ++i)
  {
    if (1 || objects[i].id == objectID) //找到画面中第一个ID物体
    {
      if (*grasp_state == 0) //调整位置
      {
        lift(0.0);
        moveFingers(width = 0.1);
        printf("ID %d 的物体 %s 在 %lf %lf\n", objects[i].id, objects[i].model, objects[i].position[0], objects[i].position[2]);
        get_gps_values(gps_values);
        get_compass_angle(&compass_angle);
        double grasp_target_posture[3];
        double grasp_dis_set = -0.16;

        //相对偏移 同时纵向位移稍微削弱一下
        grasp_target_posture[0] = gps_values[0] - sin(compass_angle) * objects[i].position[0] + cos(compass_angle) * (objects[i].position[2] - grasp_dis_set) * 0.6;
        grasp_target_posture[1] = gps_values[1] - cos(compass_angle) * objects[i].position[0] - sin(compass_angle) * (objects[i].position[2] - grasp_dis_set) * 0.6;
        grasp_target_posture[2] = compass_angle;

        set_posture(fin_target_posture, grasp_target_posture[0], grasp_target_posture[1], grasp_target_posture[2]);
        Moveto_CertainPoint(fin_target_posture);

        double grasp_threshold = 0.005;
        if (fabs(objects[i].position[0]) < grasp_threshold && fabs(objects[i].position[2] - grasp_dis_set) < grasp_threshold)
        {
          *grasp_state += 1;
          printf("对准了\n");
          base_reset();
          // 用视觉先来个抓手基本值
          // printf("物体大小: %lf %lf\n", objects[i].size[0], objects[i].size[1]);
          moveFingers(width = objects[i].size[0] / 2.0);
          wb_robot_step(30000 / TIME_STEP);
        }
      }
      else if (*grasp_state == 1) //抓
      {
        printf("当前电机力反馈：%.3f\n", wb_motor_get_force_feedback(gripper_motors[1]));
        if (wb_motor_get_force_feedback(gripper_motors[1]) > -8)
          moveFingers(width -= 0.0004); //步进
        else
        {
          printf("抓紧了\n");
          wb_robot_step(50000 / TIME_STEP); //等他抓稳定
          if (wb_motor_get_force_feedback(gripper_motors[1]) < -8)
          {
            *grasp_state += 1;
            lift(height = 0.3);
            printf("举起了\n");
          }
        }
      }
      else if (*grasp_state == 2) //举
      {
        return true;
      }
      break;
    }
  }
  return false;
}

//寻找空货架 给四个定点GPS 摄像头看四面墙 返回货架位置和一个商品种类
void Find_Empty(WbDeviceTag camera, int goods_class)
{
  // 下面是demo 看起来一个摄像头就够了
  int number_of_objects = wb_camera_recognition_get_number_of_objects(camera);
  printf("\n识别到 %d 个物体.\n", number_of_objects);
  const WbCameraRecognitionObject *objects = wb_camera_recognition_get_objects(camera);
  for (int i = 0; i < number_of_objects; ++i)
  {
    printf("物体 %d 的类型: %s\n", i, objects[i].model);
    printf("物体 %d 的ID: %d\n", i, objects[i].id);
    printf("物体 %d 的相对位置: %lf %lf %lf\n", i, objects[i].position[0], objects[i].position[1],
           objects[i].position[2]);
    // printf("物体 %d 的相对姿态: %lf %lf %lf %lf\n", i, objects[i].orientation[0], objects[i].orientation[1],
    //        objects[i].orientation[2], objects[i].orientation[3]);
    // printf("物体的大小 %d: %lf %lf\n", i, objects[i].size[0], objects[i].size[1]);
    printf("物体 %d 在图像中的坐标: %d %d\n", i, objects[i].position_on_image[0],
           objects[i].position_on_image[1]);
    // printf("物体 %d 在图像中的大小: %d %d\n", i, objects[i].size_on_image[0], objects[i].size_on_image[1]);
    // for (int j = 0; j < objects[i].number_of_colors; ++j)
    //   printf("颜色 %d/%d: %lf %lf %lf\n", j + 1, objects[i].number_of_colors, objects[i].colors[3 * j],
    //          objects[i].colors[3 * j + 1], objects[i].colors[3 * j + 2]);
  }
}

//给一个固定的巡逻轨迹 前部摄像头寻找指定商品 靠近直到顶部摄像头能捕捉
void Find_Goods(WbDeviceTag camera, int goods_class)
{

}

//返回货架放置货物 手动插补一下 最多插一次就够了
void Return_and_Load(double targetplace)
{
}
//*?                 核心控制函数    <结束>               ?*//

//*?                 功能函数        <开始>               ?*//
//仿真前进 1 step
static void step()
{
  if (wb_robot_step(TIME_STEP) == -1)
  {
    wb_robot_cleanup();
    exit(EXIT_SUCCESS);
  }
}

//软件仿真延时
static void passive_wait(double sec)
{
  double start_time = wb_robot_get_time();
  do
  {
    step();
  } while (start_time + sec > wb_robot_get_time());
}

//打印帮助
static void display_helper_message()
{
  printf("Control commands:\n");
  printf(" Arrows:       Move the robot\n");
  printf(" Page Up/Down: Rotate the robot\n");
  printf(" +/-:          (Un)grip\n");
  printf(" Shift + arrows:   Handle the arm\n");
  printf(" Space: Reset\n");
}

//设置机械臂上升高度
void lift(double position)
{
  wb_motor_set_velocity(gripper_motors[0], GRIPPER_MOTOR_MAX_SPEED);
  wb_motor_set_position(gripper_motors[0], position);
}

//设置手爪开合大小
void moveFingers(double position)
{
  wb_motor_set_velocity(gripper_motors[1], GRIPPER_MOTOR_MAX_SPEED);
  wb_motor_set_velocity(gripper_motors[2], GRIPPER_MOTOR_MAX_SPEED);
  wb_motor_set_position(gripper_motors[1], position);
  wb_motor_set_position(gripper_motors[2], position);
}

//细分目标位姿
double SUB = 2.0; //细分目标份数
void caculate_tmp_target(double tmp_posture[], double fin_posture[])
{
  get_gps_values(gps_values);
  get_compass_angle(&compass_angle);
  tmp_posture[0] = gps_values[0] + (fin_posture[0] - gps_values[0]) / SUB;
  tmp_posture[1] = gps_values[1] + (fin_posture[1] - gps_values[1]) / SUB;
  //选择所需旋转角度最小的的方向进行旋转
  if (fabs(fin_posture[2] - compass_angle) > PI)
  {
    tmp_posture[2] = compass_angle + (compass_angle - fin_posture[2]) / (SUB * 5);
  }
  else
    tmp_posture[2] = compass_angle + (fin_posture[2] - compass_angle) / (SUB * 5);
}

//设置位姿
void set_posture(double posture[], double x, double z, double angle)
{
  posture[0] = x;
  posture[1] = z;
  posture[2] = angle;
}

//bool函数 返回是否到达指定位置
bool targetdist_reached(double target_posture[], double dist_threshold)
{
  get_gps_values(gps_values);
  double dis = sqrt((gps_values[0] - target_posture[0]) * (gps_values[0] - target_posture[0]) + (gps_values[1] - target_posture[1]) * (gps_values[1] - target_posture[1]));

  // double angle = compass_angle - target_posture[2];
  if (dis <= dist_threshold)
  {
    return true;
  }
  else
  {
    // printf("距离目标位置：%.3f  m\n", dis);
    return false;
  }
}

//bool函数 返回是否到达指定姿态
bool targetpos_reached(double target_posture[], double pos_threshold)
{
  get_compass_angle(&compass_angle);
  double angle = target_posture[2] - compass_angle;
  if (fabs(angle) <= pos_threshold || fabs(angle) >= 2 * PI - pos_threshold)
    return true;
  return false;
}

//获取GPS的值
void get_gps_values(double v_gps[])
{
  const double *gps_raw_values = wb_gps_get_values(gps);
  v_gps[0] = gps_raw_values[0];
  v_gps[1] = gps_raw_values[2];
}

//数学函数，返回arctan值
double vector2_angle(const double v1[], const double v2[])
{
  return atan2(v2[1], v2[0]) - atan2(v1[1], v1[0]);
}

//计算罗盘角度
void get_compass_angle(double *ret_angle)
{
  const double *compass_raw_values = wb_compass_get_values(compass);
  const double v_front[2] = {compass_raw_values[0], compass_raw_values[1]};
  const double v_north[2] = {1.0, 0.0};
  *ret_angle = vector2_angle(v_front, v_north) + PI; // angle E(0, 2*PI)
  // printf("当前姿态：%.3f  rad\n", *ret_angle);
}

//*?                 功能函数        <结束>               ?*//