 /*
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 *  Description: Core script for foxbot robot.
 *  Author: Matthieu Magnon
 *
 *  Serial communication is enabled with the following command:
 *  rosrun rosserial_python serial_node.py _port:=/dev/ttyACM0 _baud:=115200
 */

#include "foxbot_core_config.h"
#include <Arduino.h>
#include <Servo.h> 

#include <ros.h>
#include <std_msgs/UInt16.h>
#include <std_msgs/Int32.h>
#include <sensor_msgs/JointState.h>
#include <sensor_msgs/Range.h>

//ROS 노드 클래스 변수
sensor_msgs::Range range_msg;
ros::Publisher pub_range("range_data", &range_msg);

float dist; //actual distance measurements of LiDAR
int strength; //signal strength of LiDAR
float temprature;
int check; //save check value
int i;
int uart[9]; //save data measured by LiDAR
const int HEADER=0x59; //frame header of data package
float distance;


//서보 클래스 변수
Servo robot_servos[2];
char servo_pins[2] = {6, 5}; // PWM Pins on Arduino Uno
int mid_positions[2] = {90, 90};
int SERVO_CURRENT_POSITIONS[2];
char frameid[] = "/link3";
float TARGET_JOINT_POSITIONS[2] = {0,0};


// Real Time debug GPIO with oscilloscop
#define RT_PIN0 8
#define RT_PIN1 9

void writeServos() {
  for (int j = 0; j < 2; j++) {
    int target_angle;
    if (j == 1) {
      // Due to difference in mounting directions
      target_angle = - TARGET_JOINT_POSITIONS[j]*(180/3.14) + mid_positions[j];
    } else {
      target_angle = TARGET_JOINT_POSITIONS[j]*(180/3.14) + mid_positions[j];
    }
    robot_servos[j].write(target_angle);
    SERVO_CURRENT_POSITIONS[j] = target_angle;
    //range_msg.range=distance/10;
    //range_msg.header.stamp = nh.now();
    //pub_range.publish(&range_msg);
  }
  nh.spinOnce();
}

void servoControlSubscriberCallbackJointState(const sensor_msgs::JointState& msg) {
  TARGET_JOINT_POSITIONS[0] = msg.position[0];
  TARGET_JOINT_POSITIONS[1] = msg.position[1];
  // Call the method to write the joint positions to the servo motors
  writeServos();

}
ros::Subscriber<sensor_msgs::JointState> servo_control_subscriber_joint_state("joint_states", &servoControlSubscriberCallbackJointState);


void setup()
{
	// serial
	nh.getHardware()->setBaud(115200);
	analogWriteResolution(12);

	// ROS node initialization
	nh.initNode();

	// Subscriber
	nh.subscribe(cmd_vel_sub);

	// Builtin LED
	pinMode(LED_BUILTIN, OUTPUT);

	// blink LED
	digitalWrite(LED_BUILTIN, LOW);

	// define pin mode motor Right
	pinMode(motorRightEncoderPinA, INPUT_PULLUP);
	attachInterrupt(digitalPinToInterrupt(motorRightEncoderPinA),
					motorRightIsrCounterDirection, RISING);
	pinMode(motorRightEncoderPinB, INPUT_PULLUP);
	pinMode(enMotorRight, OUTPUT);
	pinMode(in1MotorRight, OUTPUT);
	pinMode(in2MotorRight, OUTPUT);

	// define pin mode motor Left
	pinMode(motorLeftEncoderPinA, INPUT_PULLUP);
	attachInterrupt(digitalPinToInterrupt(motorLeftEncoderPinA),
					motorLeftIsrCounterDirection, RISING);
	pinMode(motorLeftEncoderPinB, INPUT_PULLUP);
	pinMode(enMotorLeft, OUTPUT);
	pinMode(in1MotorLeft, OUTPUT);
	pinMode(in2MotorLeft, OUTPUT);

	// define tasks frequency
	_frequency_rate.start((unsigned long) millis());
	_frequency_odometry.start((unsigned long) millis());
	_frequency_controller.start((unsigned long) millis());

	nh.advertise(odom_publisher);

	// DEBUG
	nh.advertise(debug_publisher1);
	nh.advertise(debug_publisher2);
  tf_broadcaster.init(nh);
	// RT
	pinMode(RT_PIN0, OUTPUT);
	pinMode(RT_PIN1, OUTPUT);

  for (unsigned int i = 0; i < 2; i++) {
    robot_servos[i].attach(servo_pins[i]);
    robot_servos[i].write(mid_positions[i]);
    SERVO_CURRENT_POSITIONS[i] = mid_positions[i];
  }

  nh.subscribe(servo_control_subscriber_joint_state);
  nh.advertise(pub_range);
  
  range_msg.radiation_type = sensor_msgs::Range::INFRARED;
  range_msg.header.frame_id =  frameid;
  range_msg.field_of_view = 0.01;
  range_msg.min_range = -12;
  range_msg.max_range = -0.1;
  Serial1.begin(115200);

}

void loop() {
  updateTFPrefix(nh.connected());
	// rate computation
 if(_frequency_laser.delay(millis())) {
  if(Serial1.available()) { //check if serial port has data input
    if(Serial1.read() == HEADER) { //assess data package frame header 0x59
    uart[0]=HEADER;
    //Serial.println(uart[0]);
    //Serial.println(Serial1.read());
    //if (Serial1.read() == -1) { //assess data package frame header 0x59
    //uart[1] = -1;
    //Serial.println("sdf");
    for (i = 2; i < 9; i++) { //save data in array
      uart[i] = Serial1.read();
    }
    check = uart[0] + uart[1] + uart[2] + uart[3] + uart[4] + uart[5] + uart[6] + uart[7];
    //Serial.println(check);
    //Serial.println(uart[8]);
    //if (uart[8] == (check & 0xff)){ //verify the received data as per protocol
    dist = uart[2] + uart[3] * 256; //calculate distance value
    strength = uart[4] + uart[5] * 256; //calculate signal strength value
    temprature = uart[6] + uart[7] *256;//calculate chip temprature
    temprature = temprature/8 - 256;
    
    if((dist>0)&&(dist<1200))
      distance = dist;       // 거리값을 cm단위로 불러옵니다.
    range_msg.range=distance/-100;
    range_msg.header.stamp = nh.now();
    pub_range.publish(&range_msg);    
    }
  }
 }
 
	if(_frequency_rate.delay(millis())) {
		digitalWrite(RT_PIN0, HIGH);

		float dt;

		// MOTOR RIGHT
		// direction
		motor_right_direction_median_filter.in(motor_right_direction);
		motor_right_filtered_direction = motor_right_direction_median_filter.out();

		// filter increment per second
		dt = (millis() - motor_right_prev_time);
		motor_right_prev_time = millis();
		motor_right_filtered_inc_per_second = runningAverage(motor_right_filtered_inc_per_second,
				(float)motor_right_inc / dt * 1000.0f, RATE_AVERAGE_FILTER_SIZE);

		// estimated rate
		motor_right_rate_est = (float)motor_right_filtered_direction
							 * motor_right_filtered_inc_per_second * RATE_CONV;
		motor_right_inc = 0;

		if (abs(motor_right_rate_est) < 0.1f)
			motor_right_rate_est = 0.0f;

		motor_right_check_dir = 1;
		motor_right_direction = 0;

		// MOTOR LEFT
		// direction
		motor_left_direction_median_filter.in(motor_left_direction);
		motor_left_filtered_direction = motor_left_direction_median_filter.out();

		// filter increment per second
		dt = (millis() - motor_left_prev_time);
		motor_left_prev_time = millis();
		motor_left_filtered_inc_per_second = runningAverage(motor_left_filtered_inc_per_second,
				(float)motor_left_inc / dt * 1000.0f, RATE_AVERAGE_FILTER_SIZE);


		// estimated rate
		motor_left_rate_est = (float)motor_left_filtered_direction
							* motor_left_filtered_inc_per_second * RATE_CONV;
		motor_left_inc = 0;

		if (abs(motor_left_rate_est) < 0.1f)
			motor_left_rate_est = 0.0f;
		motor_left_check_dir = 1;
		motor_left_direction = 0;

		// MIXER
		motor_right_rate_ref = (linear_velocity_ref + BASE_LENGTH / 2.f * angular_velocity_ref)
							 / (WHEEL_RADIUS);
		motor_left_rate_ref  = (linear_velocity_ref - BASE_LENGTH / 2.f * angular_velocity_ref)
							 / (WHEEL_RADIUS);

		digitalWrite(RT_PIN0, LOW);
	}

	// rate controler
	if(_frequency_controller.delay(millis())) {
		digitalWrite(RT_PIN1, HIGH);

		// MOTOR RIGHT
		rateControler(motor_right_rate_ref, motor_right_rate_est, motor_right_pwm_rate,
					  controler_motor_right_prev_time, controler_motor_right_prev_epsilon,
					  controler_motor_right_int);
		pwmMotorRight = pwmMotorRight + motor_right_pwm_rate;
		pwmMotorRight = constrain(pwmMotorRight, 0, PWM_MAX);
		setMotorRateAndDirection(pwmMotorRight, motor_right_rate_ref, enMotorRight,
								 in1MotorRight, in2MotorRight);

		// MOTOR LEFT
		rateControler(motor_left_rate_ref, motor_left_rate_est, motor_left_pwm_rate,
					  controler_motor_left_prev_time, controler_motor_left_prev_epsilon,
					  controler_motor_left_int);
		pwmMotorLeft = pwmMotorLeft + motor_left_pwm_rate;
		pwmMotorLeft = constrain(pwmMotorLeft, 0, PWM_MAX);
		setMotorRateAndDirection(pwmMotorLeft, motor_left_rate_ref, enMotorLeft,
								 in1MotorLeft, in2MotorLeft);

		// DEBUG
		debug_left.x = motor_left_rate_ref;
		debug_left.y = motor_left_rate_est;
    debug_left.z = pwmMotorLeft;
		debug_publisher1.publish(&debug_left);

		// DEBUG
		debug_right.x = motor_right_rate_ref;
		debug_right.y = motor_right_rate_est;
		debug_right.z = pwmMotorRight;
		debug_publisher2.publish(&debug_right);
		digitalWrite(RT_PIN1, LOW);
	}

	// update odometry
	if(_frequency_odometry.delay(millis())) {
		float dt, dx, dy;
		float qw, qx, qy, qz;

		dt = (float)(millis() - odom_prev_time) * 0.001f;
		odom_prev_time = millis();

		// compute linear and angular estimated velocity
		linear_velocity_est = WHEEL_RADIUS * (motor_right_rate_est + motor_left_rate_est) / 2.0f;
		angular_velocity_est = (WHEEL_RADIUS / BASE_LENGTH)
							 * (motor_right_rate_est - motor_left_rate_est);

		// compute translation and rotation
		yaw_est += angular_velocity_est * dt;
		dx = cos(yaw_est) * linear_velocity_est * dt;
		dy = sin(yaw_est) * linear_velocity_est * dt;

		// DEBUG
		//debug_left.y = yaw_est * 57.2958;
		//debug_left.z = angular_velocity_est * dt;
		debug_publisher1.publish(&debug_left);

		// compute quaternion
		qw = cos(abs(yaw_est) / 2.0f);
		qx = 0.0f;
		qy = 0.0f;
		qz = sign(yaw_est) * sin(abs(yaw_est) / 2.0f);

		// feed odom message
		odom.header.stamp = nh.now();
		odom.header.frame_id = odom_header_frame_id;
		odom.child_frame_id = odom_child_frame_id;
		odom.pose.pose.position.x += dx;
		odom.pose.pose.position.y += dy;
		odom.pose.pose.position.z = 0.0;
		odom.pose.pose.orientation.w = qw;
		odom.pose.pose.orientation.x = qx;
		odom.pose.pose.orientation.y = qy;
		odom.pose.pose.orientation.z = qz;
		// Velocity expressed in base_link frame
		odom.twist.twist.linear.x = linear_velocity_est;
		odom.twist.twist.linear.y = 0.0f;
		odom.twist.twist.angular.z = angular_velocity_est;

    //odom_publisher.publish(&odom);
    updateTF(odom_tf);
    odom_tf.header.stamp = nh.now();
    tf_broadcaster.sendTransform(odom_tf);
	}

	// update subscribers values
	if(_frequency_rospinonce.delay(millis())) {
		nh.spinOnce();
	}
}

void commandVelocityCallback(const geometry_msgs::Twist& cmd_vel_msg) {
	linear_velocity_ref  = cmd_vel_msg.linear.x;
	angular_velocity_ref = cmd_vel_msg.angular.z;
}

void motorRightIsrCounterDirection() {
	motor_right_inc ++;
	if ( motor_right_check_dir == 1) {
		if (digitalRead(motorRightEncoderPinB) && digitalRead(motorRightEncoderPinA)){
			motor_right_direction = 1;;
		} else {
			motor_right_direction = -1;
		}
		motor_right_check_dir = 1;
	}
}

void motorLeftIsrCounterDirection() {
	motor_left_inc ++;
	if ( motor_left_check_dir == 1) {
		if ( digitalRead(motorLeftEncoderPinB) && digitalRead(motorLeftEncoderPinA)){
			motor_left_direction = 1;
		} else {
			motor_left_direction = -1;
		}
		motor_left_check_dir = 0;
	}
}

void rateControler(const float rate_ref, const float rate_est, int & pwm_rate,
				   unsigned long & prev_time, float & previous_epsilon, float & i_epsilon) {

	float epsilon = abs(rate_ref) - abs(rate_est);
	float d_epsilon = (epsilon - previous_epsilon) / (prev_time - millis());

	// reset and clamp integral (todo : add anti windup)
	if (rate_ref == 0.0) {
		i_epsilon = 0.0;
	} else {
		i_epsilon += epsilon * (prev_time - millis()) * RATE_CONTROLLER_KI;
	}
	i_epsilon = constrain(i_epsilon, -RATE_INTEGRAL_FREEZE, RATE_INTEGRAL_FREEZE);

	prev_time = millis();
	previous_epsilon = epsilon;

	debug_left.z = i_epsilon * RATE_CONTROLLER_KI;

	pwm_rate = epsilon * RATE_CONTROLLER_KP
			 + d_epsilon * RATE_CONTROLLER_KD
			 + i_epsilon * RATE_CONTROLLER_KI;

	// saturate output
	pwm_rate = constrain(pwm_rate, RATE_CONTROLLER_MIN_PWM, RATE_CONTROLLER_MAX_PWM);
}

float runningAverage(float prev_avg, const float val, const int n) {

	return (prev_avg * (n - 1) + val) / n;
}

void setMotorRateAndDirection(int pwm_ref, const float rate_ref,
							  const byte enMotor, const byte in1Motor, const byte in2Motor) {

		// avoid noisy pwm range
		if (abs(rate_ref) < 0.1)
			pwm_ref = 0;

		// write direction
		if (rate_ref > 0) {
			digitalWrite(in1Motor, LOW);
			digitalWrite(in2Motor, HIGH);
		}
		else if (rate_ref < 0) {
			digitalWrite(in1Motor, HIGH);
			digitalWrite(in2Motor, LOW);
		}

		// write pwm
		analogWrite(enMotor, pwm_ref);
}
void updateTFPrefix(bool isConnected)
{
  static bool isChecked = false;
  char log_msg[50];

  if (isConnected)
  {
    if (isChecked == false)
    {
      nh.getParam("~tf_prefix", &get_tf_prefix);

      if (!strcmp(get_tf_prefix, ""))
      {
        sprintf(odom_header_frame_id, "odom");
        sprintf(odom_child_frame_id, "base_footprint");  
      }
      else
      {
        strcpy(odom_header_frame_id, get_tf_prefix);
        strcpy(odom_child_frame_id, get_tf_prefix);

        strcat(odom_header_frame_id, "/odom");
        strcat(odom_child_frame_id, "/base_footprint");
      }
      isChecked = true;
    }
  }
  else
  {
    isChecked = false;
  }
}
void updateTF(geometry_msgs::TransformStamped& odom_tf)
{
  odom_tf.header = odom.header;
  odom_tf.child_frame_id = odom.child_frame_id;
  odom_tf.transform.translation.x = odom.pose.pose.position.x;
  odom_tf.transform.translation.y = odom.pose.pose.position.y;
  odom_tf.transform.translation.z = odom.pose.pose.position.z;
  odom_tf.transform.rotation      = odom.pose.pose.orientation;
}
