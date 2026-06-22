import rclpy
from rclpy.node import Node
from nav_msgs.msg import OccupancyGrid, Odometry, Path
from geometry_msgs.msg import PoseWithCovarianceStamped, PoseStamped, Twist
from sensor_msgs.msg import LaserScan
from rcl_interfaces.msg import Log
from action_msgs.msg import GoalStatusArray
import socket
import json
import math
import threading

from rclpy.qos import QoSProfile, DurabilityPolicy

class TakshakBridge(Node):
    def __init__(self):
        super().__init__('takshak_bridge')
        
        self.latest_sim_time = None

        # Publishers (from Takshak to ROS 2)
        self.goal_pub = self.create_publisher(PoseStamped, '/goal_pose', 10)
        self.initialpose_pub = self.create_publisher(PoseWithCovarianceStamped, '/initialpose', 10)
        
        # TakshakDB Publisher Socket
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        import os
        self.host = os.environ.get("TAKSHAK_HOST", "127.0.0.1")
        try:
            self.sock.connect((self.host, 6379))
            self.get_logger().info(f"Connected to TakshakDB Publisher at {self.host}:6379")
        except Exception as e:
            self.get_logger().error(f"Failed to connect to TakshakDB Publisher: {e}")
            
        # ROS 2 Subscribers (from ROS 2 to Takshak)
        map_qos = QoSProfile(depth=1, durability=DurabilityPolicy.TRANSIENT_LOCAL)
        self.map_sub = self.create_subscription(OccupancyGrid, '/map', self.map_callback, map_qos)
        self.odom_sub = self.create_subscription(Odometry, '/odom', self.odom_callback, 10)
        self.amcl_sub = self.create_subscription(PoseWithCovarianceStamped, '/amcl_pose', self.amcl_callback, 10)
        self.cmd_vel_sub = self.create_subscription(Twist, '/cmd_vel', self.cmd_vel_callback, 10)
        self.rosout_sub = self.create_subscription(Log, '/rosout', self.rosout_callback, 100)
        self.path_sub = self.create_subscription(Path, '/plan', self.path_callback, 10)
        self.scan_sub = self.create_subscription(LaserScan, '/scan', self.scan_callback, 10)

        self.map_payload = None
        self.map_timer = self.create_timer(5.0, self.publish_map_periodic)
        
        # Start TakshakDB Subscriber Thread
        self.sub_thread = threading.Thread(target=self.takshak_subscriber_loop, daemon=True)
        self.sub_thread.start()

    def takshak_subscriber_loop(self):
        sub_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        try:
            sub_sock.connect((self.host, 6379))
            self.get_logger().info(f"Connected to TakshakDB Subscriber at {self.host}:6379")
            sub_sock.sendall(b"SUBSCRIBE amr_goal\r\nSUBSCRIBE amr_initialpose\r\n")
            
            buffer = ""
            while True:
                data = sub_sock.recv(4096)
                if not data:
                    break
                buffer += data.decode('utf-8')
                
                while True:
                    start_idx = buffer.find('*3\r\n$7\r\nmessage\r\n')
                    if start_idx == -1:
                        if len(buffer) > 10000:
                            buffer = buffer[-5000:]
                        break
                        
                    lines = buffer[start_idx:].split('\r\n')
                    if len(lines) >= 8:
                        channel = lines[4]
                        payload_str = lines[6]
                        
                        try:
                            # Verify payload is completely received by checking RESP length
                            expected_len = int(lines[5][1:])
                            if len(payload_str) >= expected_len:
                                payload = json.loads(payload_str)
                                if channel == 'amr_goal':
                                    self.publish_goal_to_ros(payload)
                                elif channel == 'amr_initialpose':
                                    self.publish_initialpose_to_ros(payload)
                                
                                # Advance buffer past this message (7 lines)
                                pos = start_idx
                                for _ in range(7):
                                    pos = buffer.find('\r\n', pos) + 2
                                buffer = buffer[pos:]
                            else:
                                break # Wait for more data
                        except Exception as e:
                            self.get_logger().error(f"Error parsing Takshak payload: {e}")
                            pos = start_idx
                            for _ in range(7):
                                pos = buffer.find('\r\n', pos) + 2
                            buffer = buffer[pos:] if pos > 1 else ""
                    else:
                        break
        except Exception as e:
            self.get_logger().error(f"Takshak subscriber thread failed: {e}")

    def get_quaternion_from_euler(self, roll, pitch, yaw):
        qx = math.sin(roll/2) * math.cos(pitch/2) * math.cos(yaw/2) - math.cos(roll/2) * math.sin(pitch/2) * math.sin(yaw/2)
        qy = math.cos(roll/2) * math.sin(pitch/2) * math.cos(yaw/2) + math.sin(roll/2) * math.cos(pitch/2) * math.sin(yaw/2)
        qz = math.cos(roll/2) * math.cos(pitch/2) * math.sin(yaw/2) - math.sin(roll/2) * math.sin(pitch/2) * math.cos(yaw/2)
        qw = math.cos(roll/2) * math.cos(pitch/2) * math.cos(yaw/2) + math.sin(roll/2) * math.sin(pitch/2) * math.sin(yaw/2)
        return qx, qy, qz, qw

    def publish_goal_to_ros(self, payload):
        msg = PoseStamped()
        if self.latest_sim_time:
            msg.header.stamp = self.latest_sim_time
        else:
            msg.header.stamp = self.get_clock().now().to_msg()
        msg.header.frame_id = 'map'
        msg.pose.position.x = float(payload['x'])
        msg.pose.position.y = float(payload['y'])
        qx, qy, qz, qw = self.get_quaternion_from_euler(0, 0, float(payload['theta']))
        msg.pose.orientation.x = qx
        msg.pose.orientation.y = qy
        msg.pose.orientation.z = qz
        msg.pose.orientation.w = qw
        self.goal_pub.publish(msg)
        self.get_logger().info(f"Published /goal_pose: x={payload['x']}, y={payload['y']}")

    def publish_initialpose_to_ros(self, payload):
        msg = PoseWithCovarianceStamped()
        if self.latest_sim_time:
            msg.header.stamp = self.latest_sim_time
        else:
            msg.header.stamp = self.get_clock().now().to_msg()
        msg.header.frame_id = 'map'
        msg.pose.pose.position.x = float(payload['x'])
        msg.pose.pose.position.y = float(payload['y'])
        qx, qy, qz, qw = self.get_quaternion_from_euler(0, 0, float(payload['theta']))
        msg.pose.pose.orientation.x = qx
        msg.pose.pose.orientation.y = qy
        msg.pose.pose.orientation.z = qz
        msg.pose.pose.orientation.w = qw
        # Default AMCL covariance - fully initialized list to prevent array assignment errors
        cov = [0.0] * 36
        cov[0] = 0.25
        cov[7] = 0.25
        cov[35] = 0.0685
        msg.pose.covariance = cov
        self.initialpose_pub.publish(msg)
        self.get_logger().info(f"Published /initialpose: x={payload['x']}, y={payload['y']}")

    def publish_to_takshak(self, channel, payload_dict):
        try:
            payload_str = json.dumps(payload_dict, separators=(',', ':'))
            cmd = f"PUBLISH {channel} {payload_str}\r\n"
            self.sock.sendall(cmd.encode())
        except Exception as e:
            pass # Socket errors are common, fail silently to avoid spam

    def euler_from_quaternion(self, q):
        siny_cosp = 2 * (q.w * q.z + q.x * q.y)
        cosy_cosp = 1 - 2 * (q.y * q.y + q.z * q.z)
        yaw = math.atan2(siny_cosp, cosy_cosp)
        return yaw

    def map_callback(self, msg):
        if self.map_payload is not None:
            return 
        self.get_logger().info("Received /map, RLE compressing...")
        data = list(msg.data)
        rle = []
        if len(data) > 0:
            current_val = data[0]
            count = 1
            for val in data[1:]:
                if val == current_val:
                    count += 1
                else:
                    rle.extend([current_val, count])
                    current_val = val
                    count = 1
            rle.extend([current_val, count])

        self.map_payload = {
            "width": msg.info.width,
            "height": msg.info.height,
            "resolution": msg.info.resolution,
            "origin_x": msg.info.origin.position.x,
            "origin_y": msg.info.origin.position.y,
            "data_rle": rle
        }
        self.get_logger().info("Map compressed. Broadcasting continuously...")
        self.publish_map_periodic()

    def publish_map_periodic(self):
        if self.map_payload:
            self.publish_to_takshak("amr_map", self.map_payload)

    def odom_callback(self, msg):
        self.latest_sim_time = msg.header.stamp
        x = msg.pose.pose.position.x
        y = msg.pose.pose.position.y
        yaw = self.euler_from_quaternion(msg.pose.pose.orientation)
        payload = {"source": "odom", "x": x, "y": y, "theta": yaw}
        self.publish_to_takshak("amr_odom", payload)

    def amcl_callback(self, msg):
        x = msg.pose.pose.position.x
        y = msg.pose.pose.position.y
        yaw = self.euler_from_quaternion(msg.pose.pose.orientation)
        payload = {"source": "amcl", "x": x, "y": y, "theta": yaw}
        self.publish_to_takshak("amr_odom", payload)
        
    def cmd_vel_callback(self, msg):
        payload = {
            "linear": msg.linear.x,
            "angular": msg.angular.z
        }
        self.publish_to_takshak("amr_cmd_vel", payload)

    def path_callback(self, msg):
        path_data = []
        for pose in msg.poses:
            path_data.extend([pose.pose.position.x, pose.pose.position.y])
        self.publish_to_takshak("amr_path", {"path": path_data})

    def scan_callback(self, msg):
        payload = {
            "angle_min": msg.angle_min,
            "angle_max": msg.angle_max,
            "angle_increment": msg.angle_increment,
            "range_min": msg.range_min,
            "range_max": msg.range_max,
            "ranges": [r if not math.isinf(r) and not math.isnan(r) else -1.0 for r in msg.ranges]
        }
        self.publish_to_takshak("amr_scan", payload)

    def rosout_callback(self, msg):
        name = msg.name
        msg_str = msg.msg
        
        # Filter for Nav2 specific logs that represent major events
        if name == 'bt_navigator' and 'Goal succeeded' in msg_str:
            self.publish_to_takshak("amr_nav_status", {"event": "GOAL_REACHED", "msg": "Target_destination_reached_successfully", "level": "success"})
        elif name == 'controller_server' and 'Aborting handle' in msg_str:
            self.publish_to_takshak("amr_nav_status", {"event": "ABORTED", "msg": "Failed_to_make_progress._Path_aborted.", "level": "error"})
        elif name == 'behavior_server' and 'Running spin' in msg_str:
            self.publish_to_takshak("amr_nav_status", {"event": "RECOVERY_SPIN", "msg": "Executing_recovery_spin_behavior", "level": "warning"})
        elif name == 'behavior_server' and 'spin completed' in msg_str:
            self.publish_to_takshak("amr_nav_status", {"event": "RECOVERY_SUCCESS", "msg": "Recovery_spin_completed", "level": "info"})
        elif name == 'controller_server' and 'Received a goal' in msg_str:
            self.publish_to_takshak("amr_nav_status", {"event": "PLANNING", "msg": "Computing_control_effort_for_new_goal", "level": "info"})

def main(args=None):
    rclpy.init(args=args)
    bridge = TakshakBridge()
    rclpy.spin(bridge)
    bridge.destroy_node()
    rclpy.shutdown()

if __name__ == '__main__':
    main()
